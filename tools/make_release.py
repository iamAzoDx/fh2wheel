#!/usr/bin/env python3
"""Pack the release zip.

  make_release.py OUT_ZIP FILE...

Every file is stored at the root of the archive under its own name. Markdown files are
stored with a .txt extension so they open with a double click on any Windows machine.
"""
import os
import sys
import zipfile

MARKDOWN_SUFFIX = ".md"
TEXT_SUFFIX = ".txt"


def archive_name(path):
    name = os.path.basename(path)
    if name.endswith(MARKDOWN_SUFFIX):
        return name[: -len(MARKDOWN_SUFFIX)] + TEXT_SUFFIX
    return name


def release_entries(paths):
    entries = [(path, archive_name(path)) for path in paths]
    names = [name for _, name in entries]
    if len(names) != len(set(names)):
        raise SystemExit("two files would share the same name in the archive")
    return entries


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    missing = [path for path in argv[2:] if not os.path.isfile(path)]
    if missing:
        raise SystemExit(f"missing files: {missing}")
    with zipfile.ZipFile(argv[1], "w", zipfile.ZIP_DEFLATED) as archive:
        for path, name in release_entries(argv[2:]):
            archive.write(path, name)
    print(f"release: {argv[1]}")


if __name__ == "__main__":
    main(sys.argv)
