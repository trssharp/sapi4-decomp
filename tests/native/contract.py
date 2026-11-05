"""No-device ctypes fragment contract. SPDX-License-Identifier: GPL-2.0-or-later"""
import ctypes as C
import hashlib
import re

PCM = C.CFUNCTYPE(C.c_int, C.POINTER(C.c_int16), C.c_size_t, C.c_void_p)
MARK = C.CFUNCTYPE(C.c_int, C.c_uint32, C.c_uint64, C.c_void_p)
class Fragment(C.Structure):
    _fields_ = [('kind', C.c_uint32), ('index', C.c_uint32), ('text', C.c_char_p)]
class Mode(C.Structure):
    _fields_ = [('name', C.c_char*64), ('speaker', C.c_char*64), ('gender', C.c_uint),
                ('age', C.c_uint), ('features', C.c_uint), ('sample_rate', C.c_int)]

class Engine:
    def __init__(self, dll, resources):
        self.lib = C.CDLL(str(dll)); self.e = None; self.data = str(resources).encode('utf8')
        def bind(n, args, result):
            f = getattr(self.lib, 's4_'+n); f.argtypes = args; f.restype = result; setattr(self, n, f)
        bind('init', [C.c_char_p], C.c_int)
        bind('list_modes', [C.c_char_p, C.POINTER(Mode), C.c_int], C.c_int)
        bind('open', [C.c_char_p, C.c_char_p, C.c_char_p, C.c_size_t], C.c_void_p)
        bind('close', [C.c_void_p], None)
        bind('shutdown', [], C.c_int)
        bind('stop', [C.c_void_p], None)
        bind('speak', [C.c_void_p, C.c_char_p, C.c_uint, PCM, C.c_void_p], C.c_int)
        bind('speak_fragments', [C.c_void_p, C.POINTER(Fragment), C.c_uint32, MARK, C.c_void_p, PCM, C.c_void_p], C.c_int)
        for name in ('speed', 'pitch', 'volume'):
            bind('set_'+name, [C.c_void_p, C.c_uint], C.c_int)
        assert self.init(self.data+b'/msttssyn.dll') == 0
    def voice(self, name):
        if self.e: self.close(self.e)
        err = C.create_string_buffer(512)
        self.e = self.open(self.data, name.encode('cp1252'), err, len(err)); assert self.e, err.value
    def render(self, parts):
        a = (Fragment*len(parts))(*(Fragment(0, 0, x.encode('cp1252')) if isinstance(x, str) else Fragment(1, x, None) for x in parts))
        chunks = []; events = []; frames = 0
        @PCM
        def pcm(ptr, n, u):
            nonlocal frames
            chunks.append(C.string_at(ptr, n*2)); frames += n; return 0
        @MARK
        def marker(i, f, u):
            events.append([i, f, frames]); return 0
        result = self.speak_fragments(self.e, a, len(a), marker, None, pcm, None)
        data = b''.join(chunks)
        assert all(f == observed for i, f, observed in events)
        return dict(result=result, frames=frames, events=[[i, f] for i, f, _ in events], pcm_sha256=hashlib.sha256(data).hexdigest())
    def finish(self):
        if self.e: self.close(self.e); self.e = None
        assert self.shutdown() == 0
        k = C.WinDLL('kernel32'); k.FreeLibrary.argtypes = [C.c_void_p]; k.FreeLibrary.restype = C.c_int
        assert k.FreeLibrary(self.lib._handle)

def decode(text):
    parts = []; literal = ''; i = 0
    while i < len(text):
        if text[i] != '\\': literal += text[i]; i += 1; continue
        if text[i:i+2] == '\\\\': literal += '\\'; i += 2; continue
        m = re.match(r'\\[Mm][Rr][Kk]=(\d+)\\', text[i:])
        if not m: raise ValueError(text[i:])
        if literal: parts.append(literal); literal = ''
        parts.append(int(m[1])); i += len(m[0])
    if literal: parts.append(literal)
    return parts
