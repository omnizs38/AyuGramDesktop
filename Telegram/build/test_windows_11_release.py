import pathlib
import unittest
import xml.etree.ElementTree as ElementTree


REPOSITORY = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = REPOSITORY / "Telegram/Resources/winrc/Telegram.manifest"
WINDOWS_ACTION = REPOSITORY / ".github/actions/windows-compile/action.yml"
WINDOWS_WORKFLOW = REPOSITORY / ".github/workflows/windows-release.yml"


class Windows11ManifestTests(unittest.TestCase):
    def test_manifest_enables_modern_windows_features(self):
        root = ElementTree.parse(MANIFEST).getroot()
        dpi = root.find(
            ".//{http://schemas.microsoft.com/SMI/2016/WindowsSettings}dpiAwareness"
        )
        long_paths = root.find(
            ".//{http://schemas.microsoft.com/SMI/2016/WindowsSettings}longPathAware"
        )
        code_page = root.find(
            ".//{http://schemas.microsoft.com/SMI/2019/WindowsSettings}activeCodePage"
        )

        self.assertIsNotNone(dpi)
        self.assertEqual(dpi.text, "PerMonitorV2,PerMonitor")
        self.assertIsNotNone(long_paths)
        self.assertEqual(long_paths.text, "true")
        self.assertIsNotNone(code_page)
        self.assertEqual(code_page.text, "UTF-8")

    def test_manifest_is_utf8_without_bom(self):
        self.assertFalse(MANIFEST.read_bytes().startswith(b"\xef\xbb\xbf"))


class Windows11ReleaseContractTests(unittest.TestCase):
    def test_compile_action_pins_windows_11_sdk(self):
        action = WINDOWS_ACTION.read_text(encoding="utf-8")
        self.assertIn("default: '10.0.26100.0'", action)
        self.assertIn("-D CMAKE_SYSTEM_VERSION=%WINDOWS_SDK%", action)
        self.assertIn("$cache['CMAKE_SYSTEM_VERSION']", action)

    def test_release_has_setup_and_portable_packages(self):
        workflow = WINDOWS_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("windows-x64-setup.exe", workflow)
        self.assertIn("windows-x64-portable.zip", workflow)
        self.assertIn("TelegramForcePortable", workflow)
        self.assertIn("SHA256SUMS.txt", workflow)
        self.assertIn("gh release create", workflow)


if __name__ == "__main__":
    unittest.main()
