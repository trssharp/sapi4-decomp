"""Minimal helpers over pefile for msttssyn.dll: section lookup, reads by virtual address, relocations,
imports. Addresses are the DLL's own (preferred base 0x63670000) throughout the tools."""
import struct
import pefile


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(path)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.data = self.pe.get_memory_mapped_image()
        self.sections = []
        for s in self.pe.sections:
            name = s.Name.rstrip(b"\0").decode()
            va = self.base + s.VirtualAddress
            self.sections.append((name, va, va + max(s.Misc_VirtualSize, s.SizeOfRawData)))
        t = self.section(".text")
        self.text_lo, self.text_hi = t[1], t[1] + self.pe.sections[0].Misc_VirtualSize
        # import address table: IAT slot va -> "dll!name"
        self.iat = {}
        for d in self.pe.DIRECTORY_ENTRY_IMPORT:
            for i in d.imports:
                nm = i.name.decode() if i.name else "#%d" % i.ordinal
                self.iat[i.address] = "%s!%s" % (d.dll.decode().split(".")[0].upper(), nm)
        self.exports = {}
        if hasattr(self.pe, "DIRECTORY_ENTRY_EXPORT"):
            for e in self.pe.DIRECTORY_ENTRY_EXPORT.symbols:
                self.exports[self.base + e.address] = e.name.decode() if e.name else "#%d" % e.ordinal
        # base relocations: set of va of each 32-bit absolute address stored in the image
        self.relocs = set()
        for blk in getattr(self.pe, "DIRECTORY_ENTRY_BASERELOC", []):
            for r in blk.entries:
                if r.type == 3:
                    self.relocs.add(self.base + r.rva)

    def section(self, name):
        for s in self.sections:
            if s[0] == name:
                return s
        return None

    def section_of(self, va):
        for s in self.sections:
            if s[1] <= va < s[2]:
                return s[0]
        return None

    def read(self, va, n):
        o = va - self.base
        return self.data[o:o + n]

    def u32(self, va):
        return struct.unpack_from("<I", self.data, va - self.base)[0]

    def in_text(self, va):
        return self.text_lo <= va < self.text_hi
