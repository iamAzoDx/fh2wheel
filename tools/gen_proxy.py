#!/usr/bin/env python3
"""Generate the forwarders of a system DLL proxy from the list of expected exports.

  gen_proxy.py LIBRARY EXPORTS_TXT OUT_C OUT_DEF

EXPORTS_TXT holds one "ordinal name" line per export. Each name becomes an indirect
jump through a pointer resolved at run time, which keeps the arguments without knowing
the signatures; the original ordinal is kept.
"""
import re
import sys

IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
THUNK_PREFIX = "fhw_thunk_"
TARGET_ARRAY = "fhw_proxy_targets"
POINTER_SIZE = 8


def read_exports(path):
    exports = []
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            fields = line.split()
            if not fields:
                continue
            if len(fields) != 2 or not fields[0].isdigit() or not IDENTIFIER.match(fields[1]):
                raise SystemExit(f"invalid line {number}: {line.rstrip()}")
            exports.append((int(fields[0]), fields[1]))
    names = [name for _, name in exports]
    ordinals = [ordinal for ordinal, _ in exports]
    if len(names) != len(set(names)) or len(ordinals) != len(set(ordinals)):
        raise SystemExit("duplicates in the export list")
    return exports


def generate_c(exports):
    lines = [
        "#include <windows.h>",
        "",
        f"#define FHW_PROXY_EXPORT_COUNT {len(exports)}",
        "",
        f"void *{TARGET_ARRAY}[FHW_PROXY_EXPORT_COUNT];",
        "",
        "static HRESULT WINAPI fhw_proxy_unavailable(void)",
        "{",
        "    return E_NOTIMPL;",
        "}",
        "",
        "static const char *const fhw_proxy_names[FHW_PROXY_EXPORT_COUNT] = {",
    ]
    lines += [f'    "{name}",' for _, name in exports]
    lines += [
        "};",
        "",
        "void fhw_proxy_bind(HMODULE real)",
        "{",
        "    int index;",
        "    for (index = 0; index < FHW_PROXY_EXPORT_COUNT; index++) {",
        f"        {TARGET_ARRAY}[index] = real != NULL",
        "            ? (void *)GetProcAddress(real, fhw_proxy_names[index])",
        "            : NULL;",
        f"        if ({TARGET_ARRAY}[index] == NULL)",
        f"            {TARGET_ARRAY}[index] = (void *)fhw_proxy_unavailable;",
        "    }",
        "}",
        "",
        "asm(",
        '    ".text\\n"',
    ]
    for index, (_, name) in enumerate(exports):
        offset = index * POINTER_SIZE
        lines.append(f'    ".globl {THUNK_PREFIX}{name}\\n"')
        lines.append(f'    "{THUNK_PREFIX}{name}:\\n"')
        lines.append(f'    "    jmp *{TARGET_ARRAY}+{offset}(%rip)\\n"')
    lines.append(");")
    return "\n".join(lines) + "\n"


def generate_def(library, exports):
    lines = [f"LIBRARY {library}", "EXPORTS"]
    lines += [f"    {name} = {THUNK_PREFIX}{name} @{ordinal}" for ordinal, name in exports]
    return "\n".join(lines) + "\n"


def main(argv):
    if len(argv) != 5:
        raise SystemExit(__doc__)
    exports = read_exports(argv[2])
    with open(argv[3], "w", encoding="utf-8", newline="\n") as f:
        f.write(generate_c(exports))
    with open(argv[4], "w", encoding="utf-8", newline="\n") as f:
        f.write(generate_def(argv[1], exports))


if __name__ == "__main__":
    main(sys.argv)
