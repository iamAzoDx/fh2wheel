import os
import sys
import tempfile
import unittest
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import make_release  # noqa: E402


class ReleaseTest(unittest.TestCase):
    def test_markdown_becomes_text(self):
        self.assertEqual(make_release.archive_name("public/README.md"), "README.txt")
        self.assertEqual(make_release.archive_name("build/fh2wheel.dll"), "fh2wheel.dll")

    def test_rejects_name_clash(self):
        with self.assertRaises(SystemExit):
            make_release.release_entries(["a/README.md", "b/README.txt"])

    def test_archive_contents(self):
        with tempfile.TemporaryDirectory() as folder:
            source = os.path.join(folder, "NOTES.md")
            with open(source, "w", encoding="utf-8") as f:
                f.write("notes")
            archive = os.path.join(folder, "out.zip")
            make_release.main(["make_release.py", archive, source])
            with zipfile.ZipFile(archive) as opened:
                self.assertEqual(opened.namelist(), ["NOTES.txt"])


if __name__ == "__main__":
    unittest.main()
