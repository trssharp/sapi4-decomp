"""Exclusive native x64 preview03 boundary. Original Microsoft DLL is data only."""
import ctypes as C
import hashlib
from pathlib import Path
import threading

_lock = threading.Lock()
_claimed = False
# Failed teardown must retain Python callbacks, worker and DLL for process lifetime.
_quarantine = []
DLL_SHA256 = '0c58dcf30aa362419ce5b78de8db5e26e0d153e6e7ed1a4d10e98ce91c6cddd7'

class Mode(C.Structure):
    _fields_ = [('name', C.c_char * 64), ('speaker', C.c_char * 64),
                ('gender', C.c_int), ('age', C.c_int), ('features', C.c_uint32),
                ('sample_rate', C.c_int)]

class Limits(C.Structure):
    _fields_ = [(n, C.c_uint) for n in ('pitch_default', 'pitch_min', 'pitch_max',
                                      'speed_default', 'speed_min', 'speed_max')]

PCM = C.CFUNCTYPE(C.c_int, C.POINTER(C.c_int16), C.c_size_t, C.c_void_p)

def claim():
    global _claimed
    with _lock:
        if _claimed:
            raise RuntimeError('MSTTS SAPI4 already owns its singleton native runtime')
        _claimed = True

def release():
    global _claimed
    with _lock:
        _claimed = False

def quarantine(owner):
    _quarantine.append(owner)

class Fragment(C.Structure):
    _fields_ = [('kind', C.c_uint32), ('index', C.c_uint32), ('text', C.c_char_p)]
MARK = C.CFUNCTYPE(C.c_int, C.c_uint32, C.c_uint64, C.c_void_p)

class Native:
    def __init__(self, directory):
        self.lib = None
        if C.sizeof(C.c_void_p) != 8:
            raise RuntimeError('MSTTS SAPI4 preview04 requires native 64-bit NVDA')
        path = Path(directory) / 'MSTTSSapi4.dll'
        if hashlib.sha256(path.read_bytes()).hexdigest() != DLL_SHA256:
            raise RuntimeError('Reconstructed native DLL hash does not match reviewed preview03')
        # Absolute path; never load msttssyn.dll, which s4_init reads as data.
        self.lib = C.CDLL(str(path.resolve()))
        try:
            def bind(name, args, result):
                f = getattr(self.lib, 's4_' + name)
                f.argtypes = args
                f.restype = result
                setattr(self, name, f)
            bind('abi_version', [], C.c_uint)
            bind('init', [C.c_char_p], C.c_int)
            bind('shutdown', [], C.c_int)
            bind('list_modes', [C.c_char_p, C.POINTER(Mode), C.c_int], C.c_int)
            bind('open', [C.c_char_p, C.c_char_p, C.c_char_p, C.c_size_t], C.c_void_p)
            bind('close', [C.c_void_p], None)
            bind('sample_rate', [C.c_void_p], C.c_int)
            bind('get_limits', [C.c_void_p, C.POINTER(Limits)], None)
            for name in ('pitch', 'speed', 'volume'):
                bind('get_' + name, [C.c_void_p], C.c_uint)
                bind('set_' + name, [C.c_void_p, C.c_uint], C.c_int)
            bind('speak', [C.c_void_p, C.c_char_p, C.c_uint, PCM, C.c_void_p], C.c_int)
            bind('speak_fragments', [C.c_void_p, C.POINTER(Fragment), C.c_uint32, MARK, C.c_void_p, PCM, C.c_void_p], C.c_int)
            bind('stop', [C.c_void_p], None)
            if self.abi_version() != 0x10001:
                raise RuntimeError('Unexpected native ABI')
            self.data = str((Path(directory) / 'msttsSapi4Data').resolve()).encode('utf-8')
            if self.init(self.data + b'/msttssyn.dll') != 0:
                raise RuntimeError('Native initialization rejected private resources/path. All 11 pinned files are required; Unicode paths require an existing ASCII short alias, drive-local path under 239 bytes.')
            modes = (Mode * 64)()
            count = self.list_modes(self.data, modes, 64)
            if count != 22:
                raise RuntimeError('Native resource enumeration failed (expected 22 modes); verify filesystem short aliases and resource hashes')
            self.modes = [Mode.from_buffer_copy(modes[i]) for i in range(count)]
        except BaseException:
            if hasattr(self, 'shutdown') and self.shutdown() == 0:
                self.unload()
            else:
                quarantine(self)
            raise

    def open_voice(self, name):
        error = C.create_string_buffer(512)
        handle = self.open(self.data, name.encode('cp1252'), error, len(error))
        if not handle:
            raise RuntimeError('Native voice open failed: ' + error.value.decode('utf-8', 'replace'))
        return handle

    def unload(self):
        # Caller has excluded new calls, ended borrowed access, successfully shut down.
        if self.lib is not None:
            kernel = C.WinDLL('kernel32', use_last_error=True)
            kernel.FreeLibrary.argtypes = [C.c_void_p]
            kernel.FreeLibrary.restype = C.c_int
            if not kernel.FreeLibrary(self.lib._handle):
                raise C.WinError(C.get_last_error())
            self.lib = None
