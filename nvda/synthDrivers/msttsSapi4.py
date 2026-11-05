"""Private experimental direct-x64 Microsoft SAPI4 reconstruction driver.

No COM registration, helper, original-DLL execution or automatic language switching.
All ordinary native operations and PCM callbacks belong to one serialized worker.
Only the documented thread-safe native stop crosses from the control thread.
"""
from collections import OrderedDict, deque
import ctypes
from pathlib import Path
import threading

import config
import nvwave
import queueHandler
from logHandler import log
from synthDriverHandler import SynthDriver as BaseSynthDriver, VoiceInfo
from synthDriverHandler import synthIndexReached, synthDoneSpeaking
from speech.commands import IndexCommand, BreakCommand, PitchCommand, RateCommand, VolumeCommand
from . import _msttsSapi4Native as bridge
import time


def clamp(value):
    return max(0, min(100, int(value)))


def native_value(percent, low, default, high):
    # Slider centre is each original voice's exact native default, not a guessed
    # generic pitch/rate. Piecewise mapping preserves both limits and baseline.
    percent = clamp(percent)
    if percent <= 50:
        return round(low + (default - low) * percent / 50)
    return round(default + (high - default) * (percent - 50) / 50)


class SynthDriver(BaseSynthDriver):
    name = 'msttsSapi4'
    description = 'Microsoft SAPI4 native private preview05 stock streaming'
    supportedSettings = (BaseSynthDriver.VoiceSetting(), BaseSynthDriver.RateSetting(),
                         BaseSynthDriver.PitchSetting(), BaseSynthDriver.VolumeSetting())
    supportedCommands = {IndexCommand, BreakCommand, PitchCommand, RateCommand, VolumeCommand}
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}

    @classmethod
    def check(cls):
        return ctypes.sizeof(ctypes.c_void_p) == 8 and (Path(__file__).parent / 'MSTTSSapi4.dll').is_file()

    def __init__(self):
        bridge.claim()
        try:
            super().__init__()
        except BaseException:
            bridge.release()
            raise
        self._condition = threading.Condition(threading.RLock())
        self._control = threading.RLock()
        self._generation = 0
        self._closing = False
        self._terminated = False
        self._busy = False
        self._pending = deque()
        self._native = None
        self._handle = None
        self._nativeVoice = None
        self._players = {}
        self._defaults = {}
        self._voices = OrderedDict()
        self._rate = self._pitch = 50
        self._volume = 100
        self._voice = None
        self._failure = None
        self._ready = threading.Event()
        self._closedCleanly = False
        self._thread = threading.Thread(target=self._worker, name='MSTTSSapi4-owner', daemon=True)
        self._thread.start()
        if not self._ready.wait(15):
            self._closing = True
            bridge.quarantine(self)
            raise RuntimeError('Native initialization timed out; runtime retained for safety')
        if self._failure:
            self._thread.join(5)
            if self._closedCleanly and not bridge._quarantine:
                bridge.release()
            else:
                bridge.quarantine(self)
            raise RuntimeError('MSTTS SAPI4 initialization failed: ' + self._failure)

    def _getAvailableVoices(self):
        return self._voices.copy()

    def _get_voice(self): return self._voice
    def _get_rate(self): return self._rate
    def _get_pitch(self): return self._pitch
    def _get_volume(self): return self._volume

    def _set_voice(self, value):
        value = str(value)
        with self._condition:
            if value not in self._voices:
                raise ValueError('Unknown native SAPI4 voice: ' + value)
            self._voice = value
            self._rate = self._pitch = 50
            # Original native volume baseline is 100.
            self._volume = 100

    def _set_rate(self, value):
        with self._condition: self._rate = clamp(value)
    def _set_pitch(self, value):
        with self._condition: self._pitch = clamp(value)
    def _set_volume(self, value):
        with self._condition: self._volume = clamp(value)

    def _get_language(self):
        return self._voices[self._voice].language

    def _parse(self, sequence):
        # Snapshot all public state once; queued utterances cannot inherit mutable
        # settings or a previous utterance's speech-command prosody.
        state = {'rate': self._rate, 'pitch': self._pitch, 'volume': self._volume}
        base = dict(state)
        operations = []
        text = []
        def flush():
            if text:
                encoded = ''.join(text).replace('\x00', ' ').encode('cp1252', 'replace')
                # Native ABI maximum. Split at a word boundary where possible.
                while encoded:
                    end = min(len(encoded), 65535)
                    if end < len(encoded):
                        space = encoded.rfind(b' ', 0, end)
                        if space > 0: end = space + 1
                    operations.append(('text', encoded[:end], dict(state)))
                    encoded = encoded[end:]
                text.clear()
        for item in sequence:
            if isinstance(item, str): text.append(item)
            elif isinstance(item, IndexCommand):
                flush(); operations.append(('index', item.index))
            elif isinstance(item, BreakCommand):
                flush(); operations.append(('break', max(0, min(60000, int(item.time)))))
            elif isinstance(item, (PitchCommand, RateCommand, VolumeCommand)):
                flush()
                name = {PitchCommand: 'pitch', RateCommand: 'rate', VolumeCommand: 'volume'}[type(item)]
                # NVDA's authoritative absolute command value also handles a
                # configured default of zero. Harness models use offset/multiplier.
                value = getattr(item, 'newValue', None)
                if value is None:
                    offset = getattr(item, 'offset', 0)
                    value = base[name] + offset if offset else base[name] * item.multiplier
                state[name] = clamp(value)
        flush()
        return self._voice, operations

    def speak(self, speechSequence):
        with self._condition:
            if self._closing or self._failure:
                raise RuntimeError('MSTTS SAPI4 worker unavailable')
            voice, operations = self._parse(speechSequence)
            # FIFO is necessary for NVDA's successive chunks. Cancellation
            # atomically discards every older-generation pending request: the
            # latest generation wins without silently dropping same-generation speech.
            self._pending.append((self._generation, voice, operations))
            self._condition.notify()

    def _valid(self, generation):
        with self._condition:
            return not self._closing and generation == self._generation

    def _notify(self, generation, notification, **kwargs):
        def deliver():
            with self._condition:
                if not self._closing and generation == self._generation:
                    notification.notify(synth=self, **kwargs)
        queueHandler.queueFunction(queueHandler.eventQueue, deliver)

    def cancel(self):
        with self._condition:
            self._generation += 1
            self._pending.clear()
            self._condition.notify_all()
        with self._control:
            if self._native is not None and self._handle is not None:
                self._native.stop(self._handle)
            players = list(self._players.values())
        for player in players:
            player.stop()

    def pause(self, switch):
        with self._control:
            players = list(self._players.values())
        for player in players: player.pause(switch)

    def _initialize(self):
        self._native = bridge.Native(Path(__file__).parent)
        for mode in self._native.modes:
            name = bytes(mode.name).decode('cp1252')
            # ABI's authentic inventory is English US. IDs are native mode names,
            # not enumeration positions, and remain stable across restarts.
            self._voices[name] = VoiceInfo(name, name, 'en_US')
            handle = self._native.open_voice(name)
            try:
                limits = bridge.Limits()
                self._native.get_limits(handle, ctypes.byref(limits))
                self._defaults[name] = {field: getattr(limits, field) for field, _ in limits._fields_}
            finally:
                self._native.close(handle)
        self._voice = 'Sam' if 'Sam' in self._voices else next(iter(self._voices))
        for rate in sorted({m.sample_rate for m in self._native.modes}):
            self._players[rate] = nvwave.WavePlayer(channels=1, samplesPerSec=rate,
                bitsPerSample=16, outputDevice=config.conf['audio']['outputDevice'])

    def _select(self, voice):
        with self._control:
            if voice != self._nativeVoice:
                if self._handle is not None:
                    self._native.close(self._handle)
                    self._handle = None
                self._handle = self._native.open_voice(voice)
                self._nativeVoice = voice
        return self._players[self._native.sample_rate(self._handle)]

    def _runs(self, operations):
        # Preview04 parsing remains authoritative for snapshots and commands.
        # Discard parser-only flushes: equal prosody on adjacent text is one run.
        run, state, size = [], None, 0
        for op in operations:
            if op[0] == 'break':
                if run: yield ('run', run, state)
                run, state, size = [], None, 0
                yield op
                continue
            cost = (len(op[1]) + op[1].count(b'\\')) if op[0] == 'text' else len(str(op[1])) + 6
            if op[0] == 'index' and not 0 <= op[1] <= 2147483647:
                raise ValueError('Index outside signed native bookmark range')
            if cost > 65535:
                # Escaped expansion can exceed the underlying parser byte cap.
                # Split literal pieces without changing text or moving indexes.
                if run: yield ('run', run, state)
                run, state, size = [], None, 0
                data = op[1]
                while data:
                    part, data = data[:32767], data[32767:]
                    yield ('run', [('text', part, op[2])], op[2])
                continue
            changed = op[0] == 'text' and state is not None and op[2] != state
            if run and (changed or size + cost > 65535 or len(run) == 4096):
                yield ('run', run, state)
                run, state, size = [], None, 0
            run.append(op); size += cost
            if op[0] == 'text': state = op[2]
        if run: yield ('run', run, state)

    def _run(self, generation, voice, operations):
        if not self._valid(generation): return
        player = self._select(voice)
        sampleRate = self._native.sample_rate(self._handle)
        limits = self._defaults[voice]
        deliveredFrames = 0
        errors = []
        # Callback objects and borrowed arrays are held by this stack until the
        # synchronous native call returns. WavePlayer owns copied bytes only.
        def feed(data, index=None):
            if not self._valid(generation): return 1
            # WavePlayer.feed itself blocks for native WASAPI buffer space.
            # onDone dispatch runs INSIDE feed/sync; waiting for application
            # credits before feed starves that dispatcher. Keep only this chunk
            # (at most 2048 frames), and request callbacks for indexes only.
            def played():
                self._notify(generation, synthIndexReached, index=index)
            # No sequence/control lock across blocking player/native work.
            # cancel can reach native atomic stop and player.stop's wake event.
            player.feed(data, onDone=played if index is not None else None)
            if not self._valid(generation):
                player.stop(); return 1
            return 0
        @bridge.PCM
        def pcm(pointer, count, user):
            nonlocal deliveredFrames
            try:
                for offset in range(0, count, 2048):
                    size = min(2048, count - offset)
                    data = ctypes.string_at(ctypes.addressof(pointer.contents) + offset * 2, size * 2)
                    if feed(data): return 1
                    deliveredFrames += size
                return 0
            except BaseException as error:
                errors.append(error); return 1
        @bridge.MARK
        def marker(index, frame, user):
            try:
                if not self._valid(generation): return 1
                if frame != deliveredFrames:
                    raise RuntimeError('Native marker not at delivered PCM boundary')
                return feed(b'', index)
            except BaseException as error:
                errors.append(error); return 1
        for operation in self._runs(operations):
            if not self._valid(generation): return
            if operation[0] == 'break':
                remaining = round(sampleRate * operation[1] / 1000)
                while remaining:
                    count = min(remaining, 2048)
                    if feed(b'\0\0' * count): return
                    remaining -= count
                continue
            run, state = operation[1:]
            if state is not None:
                for name, nativeName in (('pitch', 'pitch'), ('rate', 'speed')):
                    value = native_value(state[name], limits[nativeName + '_min'],
                        limits[nativeName + '_default'], limits[nativeName + '_max'])
                    if getattr(self._native, 'set_' + nativeName)(self._handle, value) != 0:
                        raise RuntimeError('Native prosody rejected')
                if self._native.set_volume(self._handle, state['volume']) != 0:
                    raise RuntimeError('Native volume rejected')
            parts = (bridge.Fragment * len(run))(*(bridge.Fragment(0, 0, op[1])
                if op[0] == 'text' else bridge.Fragment(1, op[1], None) for op in run))
            deliveredFrames = 0
            # No driver lock across synthesis. Native wrapper serializes ordinary
            # calls; documented stop alone can cross from the control thread.
            result = self._native.speak_fragments(self._handle, parts, len(parts), marker, None, pcm, None)
            if errors: raise RuntimeError('Streaming callback failed') from errors[0]
            if result == -1: raise RuntimeError('Native fragment request rejected')
            if result == 1 or not self._valid(generation): return
        # Includes silent/index-only requests: marker must precede idle. Do not
        # equate the native final bookmark with playback drain or add PAU=1.
        if feed(b''): return
        player.idle()
        if self._valid(generation): self._notify(generation, synthDoneSpeaking)

    def _worker(self):
        try:
            self._initialize()
            self._ready.set()
            while True:
                with self._condition:
                    self._condition.wait_for(lambda: self._closing or self._pending)
                    if self._closing: break
                    generation, voice, operations = self._pending.popleft()
                    self._busy = True
                try:
                    self._run(generation, voice, operations)
                finally:
                    with self._condition:
                        self._busy = False
        except BaseException as error:
            with self._condition:
                self._failure = repr(error)
                self._closing = True
                self._generation += 1
                self._pending.clear()
            log.error('MSTTS SAPI4 owner failed: %s', error, exc_info=True)
        finally:
            try:
                # Owner exits native calls/callbacks first. No native or player
                # is touched by terminate after a timed-out join.
                with self._control:
                    if self._native is not None:
                        if self._handle is not None:
                            self._native.close(self._handle)
                            self._handle = None
                        for player in self._players.values(): player.close()
                        if self._native.shutdown() != 0:
                            raise RuntimeError('Native shared shutdown rejected; DLL retained')
                        self._native.unload()
                        self._native = None
                    else:
                        for player in self._players.values(): player.close()
                self._closedCleanly = True
            except BaseException as error:
                self._failure = repr(error)
                bridge.quarantine(self)
                log.error('MSTTS SAPI4 teardown retained runtime: %s', error)
            self._ready.set()

    def terminate(self):
        if self._terminated: return
        self._terminated = True
        with self._condition:
            self._closing = True
            self._condition.notify_all()
        self.cancel()
        self._thread.join(5)
        if self._thread.is_alive() or not self._closedCleanly:
            bridge.quarantine(self)
            log.error('MSTTS SAPI4 worker did not safely close; native runtime retained')
            return
        bridge.release()
        super().terminate()
