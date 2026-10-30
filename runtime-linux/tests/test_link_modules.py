"""Module resolution tests for link_modules.py."""
import tempfile
import unittest
from pathlib import Path
from paths import ROOT
import link_modules


class LinkModulesTests(unittest.TestCase):
    def test_default_modules_without_dlcldr(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            (game / "sce_module").mkdir(parents=True, exist_ok=True)
            self.assertEqual(link_modules.modules(game), list(link_modules.DEFAULT_MODULES))

    def test_modules_links_dlcldr_when_present(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            (game / "sce_module").mkdir(parents=True, exist_ok=True)
            (game / "dlcldr.prx").write_bytes(b"dummy_dlcldr")
            expected = list(link_modules.DEFAULT_MODULES) + ["../dlcldr.prx"]
            self.assertEqual(link_modules.modules(game), expected)

    def test_custom_modules_override(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            custom = ["custom1.prx", "custom2.prx"]
            self.assertEqual(link_modules.modules(game, custom), custom)


if __name__ == "__main__":
    unittest.main()
