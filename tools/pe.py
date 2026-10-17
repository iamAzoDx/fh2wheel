#!/usr/bin/env python3
import re
import struct
import subprocess
import sys
from collections import namedtuple

from pe_format import *

USAGE = """\
  pe.py info    FILE                 sections, image base, entry point
  pe.py imports FILE                 IAT slot VA -> DLL!name
  pe.py exports FILE
  pe.py str     FILE VA [--w]        string at VA (--w: UTF-16LE)
  pe.py find    FILE TEXT [--w]      VAs where TEXT occurs
  pe.py xrefs   FILE VA              instructions referencing VA (via objdump, rip-relative)
  pe.py func    FILE VA [N]          objdump -d of N bytes (default 0x200) from VA, IAT calls annotated"""

Section = namedtuple("Section", "name rva virtual_size raw_offset raw_size")


class PE:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        pe_header, = struct.unpack_from("<I", self.data, DOS_PE_OFFSET_FIELD)
        if self.data[pe_header:pe_header + len(PE_SIGNATURE)] != PE_SIGNATURE:
            raise ValueError("not a PE")
        section_count, = struct.unpack_from("<H", self.data, pe_header + FILE_HEADER_SECTION_COUNT)
        optional_size, = struct.unpack_from("<H", self.data, pe_header + FILE_HEADER_OPTIONAL_SIZE)
        optional = pe_header + OPTIONAL_HEADER_OFFSET
        if struct.unpack_from("<H", self.data, optional)[0] != PE32_PLUS_MAGIC:
            raise ValueError("PE32+ only")
        self.image_base, = struct.unpack_from("<Q", self.data, optional + OPTIONAL_IMAGE_BASE)
        entry_rva, = struct.unpack_from("<I", self.data, optional + OPTIONAL_ENTRY_POINT)
        self.entry_point = self.image_base + entry_rva
        self.directories = [
            struct.unpack_from("<II", self.data, optional + OPTIONAL_DATA_DIRECTORIES + DATA_DIRECTORY_SIZE * i)
            for i in range(DATA_DIRECTORY_COUNT)
        ]
        self.sections = []
        for i in range(section_count):
            header = optional + optional_size + SECTION_HEADER_SIZE * i
            name = self.data[header:header + SECTION_NAME_SIZE].rstrip(b"\0").decode()
            virtual_size, rva, raw_size, raw_offset = struct.unpack_from(
                "<IIII", self.data, header + SECTION_SIZES_OFFSET)
            self.sections.append(Section(name, rva, max(virtual_size, raw_size), raw_offset, raw_size))

    def rva_to_offset(self, rva):
        for s in self.sections:
            if s.rva <= rva < s.rva + s.virtual_size:
                return s.raw_offset + rva - s.rva if rva - s.rva < s.raw_size else None
        return None

    def offset_to_va(self, offset):
        for s in self.sections:
            if s.raw_offset <= offset < s.raw_offset + s.raw_size:
                return self.image_base + s.rva + offset - s.raw_offset
        return None

    def read_string(self, rva, wide=False):
        offset = self.rva_to_offset(rva)
        if wide:
            chunk = self.data[offset:offset + WIDE_STRING_READ_LIMIT]
            end = next((i for i in range(0, len(chunk) - 1, 2) if chunk[i:i + 2] == WIDE_NUL), len(chunk))
            return chunk[:end].decode("utf-16le", "replace")
        return self.data[offset:self.data.index(b"\0", offset)].decode("latin-1")

    def imports(self):
        slots = {}
        descriptor = self.directories[DIRECTORY_IMPORT][0]
        while descriptor:
            lookup, _, _, dll_name, iat = struct.unpack_from(
                "<IIIII", self.data, self.rva_to_offset(descriptor))
            if not dll_name:
                break
            dll = self.read_string(dll_name)
            table = lookup or iat
            index = 0
            while True:
                thunk, = struct.unpack_from("<Q", self.data, self.rva_to_offset(table + THUNK_SIZE * index))
                if not thunk:
                    break
                if thunk & IMPORT_BY_ORDINAL_FLAG:
                    function = f"#{thunk & ORDINAL_MASK}"
                else:
                    function = self.read_string((thunk & HINT_NAME_RVA_MASK) + HINT_SIZE)
                slots[self.image_base + iat + THUNK_SIZE * index] = f"{dll}!{function}"
                index += 1
            descriptor += IMPORT_DESCRIPTOR_SIZE
        return slots

    def exports(self):
        directory, _ = self.directories[DIRECTORY_EXPORT]
        if not directory:
            return []
        (_, _, _, _, _, ordinal_base, _, name_count,
         functions, names, ordinals) = struct.unpack_from("<IIHHIIIIIII", self.data, self.rva_to_offset(directory))
        result = []
        for i in range(name_count):
            name_rva, = struct.unpack_from("<I", self.data, self.rva_to_offset(names + 4 * i))
            index, = struct.unpack_from("<H", self.data, self.rva_to_offset(ordinals + 2 * i))
            function_rva, = struct.unpack_from("<I", self.data, self.rva_to_offset(functions + 4 * index))
            result.append((index + ordinal_base, self.read_string(name_rva), self.image_base + function_rva))
        return result

    def find(self, text, wide=False):
        pattern = text.encode("utf-16le" if wide else "latin-1")
        found = []
        offset = self.data.find(pattern)
        while offset >= 0:
            found.append(self.offset_to_va(offset))
            offset = self.data.find(pattern, offset + 1)
        return found

    def disasm(self, start=None, stop=None):
        command = ["objdump", "-d", "-M", "intel", "--no-show-raw-insn", self.path]
        if start:
            command[2:2] = [f"--start-address={start:#x}", f"--stop-address={stop:#x}"]
        return subprocess.run(command, capture_output=True, text=True).stdout


def annotate(listing, imports):
    def with_import_name(match):
        va = int(match.group(1), 16)
        return match.group(0) + (f"  <{imports[va]}>" if va in imports else "")
    return re.sub(r"# (0x[0-9a-f]+)", with_import_name, listing)


def main(args):
    command, image = args[0], PE(args[1])
    wide = "--w" in args
    if command == "info":
        print(f"base {image.image_base:#x} entry {image.entry_point:#x}")
        for s in image.sections:
            print(f"  {s.name:8} va {image.image_base + s.rva:#x} size {s.virtual_size:#x}")
    elif command == "imports":
        for va, name in image.imports().items():
            print(f"{va:#x} {name}")
    elif command == "exports":
        for ordinal, name, va in image.exports():
            print(f"#{ordinal} {name} {va:#x}")
    elif command == "str":
        print(repr(image.read_string(int(args[2], 16) - image.image_base, wide)))
    elif command == "find":
        for va in image.find(args[2], wide):
            print(f"{va:#x}")
    elif command == "xrefs":
        target = f"# {int(args[2], 16):#x}"
        imports = image.imports()
        for line in image.disasm().splitlines():
            if line.rstrip().endswith(target):
                print(annotate(line, imports))
    elif command == "func":
        start = int(args[2], 16)
        length = int(args[3], 16) if len(args) > 3 else DEFAULT_DISASSEMBLY_LENGTH
        print(annotate(image.disasm(start, start + length), image.imports()))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(USAGE)
    main(sys.argv[1:])
