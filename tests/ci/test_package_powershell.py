"""Exercise the PowerShell packager with a disposable, non-executable fixture."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from verify_packages import verify


@unittest.skipUnless(shutil.which("pwsh"), "PowerShell is not installed")
class PowerShellPackageTests(unittest.TestCase):
    def test_prerelease_and_stable_multi_config_packages(self):
        with tempfile.TemporaryDirectory(prefix="crbot-package-") as temporary:
            root = Path(temporary)
            (root / "tools").mkdir()
            shutil.copy2(ROOT / "tools/package.ps1", root / "tools/package.ps1")
            shutil.copytree(ROOT / "assets", root / "assets")
            shutil.copytree(ROOT / "docs", root / "docs")
            for name in ["LICENSE", "THIRD_PARTY_NOTICES.md", "README.md", "CHANGELOG.md"]:
                shutil.copy2(ROOT / name, root / name)
            build = root / "build"
            (build / "Release").mkdir(parents=True)
            # Package layout test only; this fixture is never executed.
            (build / "Release/CRbot.exe").write_bytes(b"package test fixture, not an executable")
            command = ["pwsh", "-NoProfile", "-File", str(root / "tools/package.ps1"),
                       "-SkipBuild", "-BuildDir", str(build), "-Config", "Release"]
            for channel, version, number, display in [
                ("prerelease", "1.1.0", 3, "1.1.0(3)"),
                ("stable", "1.1.1", 4, "1.1.1")
            ]:
                metadata = {"channel": channel, "version": version, "build_number": number,
                            "display_version": display, "configuration": "Release", "source_revision": "a" * 40}
                (build / "build-info.json").write_text(json.dumps(metadata), encoding="utf-8")
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                verify(root / "dist", display, number, "win64")
            metadata["configuration"] = "Debug"
            (build / "build-info.json").write_text(json.dumps(metadata), encoding="utf-8")
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
