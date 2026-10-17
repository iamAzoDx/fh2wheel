import os
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gen_proxy  # noqa: E402
import pe  # noqa: E402

EXPORTS = os.path.join(ROOT, "proxy", "d3dcompiler_47_exports.txt")
SYSTEM_CANDIDATES = [
    "/mnt/c/Windows/System32/D3DCompiler_47.dll",
    r"C:\Windows\System32\D3DCompiler_47.dll",
]


def system_library():
    return next((path for path in SYSTEM_CANDIDATES if os.path.exists(path)), None)


class GeneratorTest(unittest.TestCase):
    def test_def_keeps_ordinals(self):
        exports = gen_proxy.read_exports(EXPORTS)
        text = gen_proxy.generate_def("D3DCOMPILER_47.dll", exports)
        self.assertIn("LIBRARY D3DCOMPILER_47.dll", text)
        self.assertIn("D3DCompile = fhw_thunk_D3DCompile @3", text)
        self.assertIn("DebugSetMute = fhw_thunk_DebugSetMute @2", text)

    def test_c_has_one_thunk_per_export(self):
        exports = gen_proxy.read_exports(EXPORTS)
        text = gen_proxy.generate_c(exports)
        self.assertEqual(text.count("jmp *fhw_proxy_targets+"), len(exports))
        self.assertIn(f"#define FHW_PROXY_EXPORT_COUNT {len(exports)}", text)

    def test_rejects_duplicates(self):
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
            f.write("1 A\n2 A\n")
        try:
            with self.assertRaises(SystemExit):
                gen_proxy.read_exports(f.name)
        finally:
            os.unlink(f.name)


@unittest.skipIf(system_library() is None, "system D3DCompiler_47.dll not found")
class SystemExportsTest(unittest.TestCase):
    def test_list_matches_system_library(self):
        expected = {(ordinal, name) for ordinal, name, _ in pe.PE(system_library()).exports()}
        self.assertEqual(set(gen_proxy.read_exports(EXPORTS)), expected)


if __name__ == "__main__":
    unittest.main()
