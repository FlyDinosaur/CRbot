#!/usr/bin/env python3
"""Verify deployable ZIPs without running the app or contacting a device."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import zipfile


def verify(directory, version, build_number, platform):
    name = f"CRBot-{version}-{platform}"
    archive = Path(directory) / f"{name}.zip"
    checksum = Path(str(archive) + ".sha256")
    expected = hashlib.sha256(archive.read_bytes()).hexdigest() + "  " + archive.name
    if checksum.read_text(encoding="utf-8").strip() != expected:
        raise ValueError("SHA256 mismatch")
    with zipfile.ZipFile(archive) as package:
        if package.testzip() is not None:
            raise ValueError("Corrupt ZIP")
        manifest = json.loads(package.read(f"{name}/build-info.json"))
        if manifest["display_version"] != version or manifest["build_number"] != int(build_number):
            raise ValueError("Package version does not match the build")
        expected_version = manifest["version"]
        if manifest["channel"] == "prerelease":
            expected_version += f"({build_number})"
        elif manifest["channel"] != "stable":
            raise ValueError("Unknown release channel")
        if version != expected_version:
            raise ValueError("Invalid display version")
        if package.read(f"{name}/VERSION").decode().strip() != manifest["version"]:
            raise ValueError("VERSION differs from the build")
        required = ["LICENSE", "THIRD_PARTY_NOTICES.md", "README.md", "CHANGELOG.md"]
        templates = ["btn_battle.png", "btn_battle2.png", "btn_ok.png", "btn_mode_1v1.png"]
        required.extend(f"assets/templates/{item}" for item in templates)
        if platform == "win64":
            required.append("CRbot.exe")
        else:
            required.extend(["CRbot", "CRBot.app/Contents/MacOS/CRBot"])
            required.extend(f"CRBot.app/Contents/Resources/assets/templates/{item}" for item in templates)
            plist = plistlib.loads(package.read(f"{name}/CRBot.app/Contents/Info.plist"))
            if plist["CFBundleVersion"] != str(build_number) or plist["CFBundleShortVersionString"] != manifest["version"]:
                raise ValueError("App bundle version differs from the package")
            if json.loads(package.read(f"{name}/CRBot.app/Contents/Resources/build-info.json")) != manifest:
                raise ValueError("App bundle metadata differs from the package")
            for binary in ["CRbot", "CRBot.app/Contents/MacOS/CRBot"]:
                if not (package.getinfo(f"{name}/{binary}").external_attr >> 16) & 0o111:
                    raise ValueError(f"Executable permission lost: {binary}")
        for item in required:
            if package.getinfo(f"{name}/{item}").file_size == 0:
                raise ValueError(f"Empty resource: {item}")
    return archive.name


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory")
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-number", type=int, required=True)
    parser.add_argument("--platform", choices=["win64", "macos-arm64", "macos-x86_64"], required=True)
    args = parser.parse_args()
    print("PASS:", verify(args.directory, args.version, args.build_number, args.platform))
