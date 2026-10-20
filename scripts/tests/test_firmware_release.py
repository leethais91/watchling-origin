"""Focused tests for the firmware release packager and local build script."""

import hashlib
import importlib.util
import json
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

BUILD_SCRIPT = Path(__file__).parents[1] / "build-firmware-release.sh"


SCRIPT = Path(__file__).parents[1] / "package-firmware-release.py"
SPEC = importlib.util.spec_from_file_location("package_firmware_release", SCRIPT)
PACKAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGER)


class FirmwareReleaseFixture:
    """Minimal real-file fixture matching the ESP-IDF release inputs."""

    def __init__(self, base):
        self.base = Path(base)
        self.root = self.base / "source"
        self.build = self.base / "build"
        self.web = self.base / "web"
        self.output = self.base / "output"
        self.idf = self.base / "idf"
        self.parts = {
            "0x0": ("bootloader/bootloader.bin", b"bootloader"),
            "0x8000": ("partition_table/partition-table.bin", b"partitions"),
            "0x9000": ("nvs.bin", b"nvs-settings"),
            "0x10000": ("watchling.bin", b"application"),
            "0x21000": ("extra/ota_data_initial.bin", b"ota-state"),
        }
        self._create()

    def _create(self):
        for directory in (self.root, self.build, self.web, self.idf):
            directory.mkdir(parents=True)
        (self.root / "LICENSE").write_text("project license\n", encoding="utf-8")
        (self.root / "THIRD_PARTY_NOTICES.md").write_text("notices\n", encoding="utf-8")
        font_dir = self.root / "echoear-firmware/main/assets"
        font_dir.mkdir(parents=True)
        (font_dir / "display-OFL.txt").write_text("font license\n", encoding="utf-8")
        managed = self.base / "managed_components/component"
        managed.mkdir(parents=True)
        (managed / "LICENSE").write_text("component license\n", encoding="utf-8")
        (self.idf / "COPYING").write_text("idf license\n", encoding="utf-8")

        for name in ("index.html", "style.css", "app.js", "start.command", "serve-installer.mjs"):
            (self.web / name).write_text(f"fixture {name}\n", encoding="utf-8")
        for directory in (self.web / "assets", self.web / "licenses"):
            directory.mkdir()
        (self.web / "assets/installer.js").write_text("export {};\n", encoding="utf-8")
        (self.web / "licenses/dependency.txt").write_text("dependency license\n", encoding="utf-8")
        (self.web / "source-map.js.map").write_text("private build metadata\n", encoding="utf-8")
        (self.web / "private-config.txt").write_text("fixture only\n", encoding="utf-8")
        (self.root / "unrelated-source.c").write_text("int private_value;\n", encoding="utf-8")

        for relative, content in self.parts.values():
            target = self.build / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
        self.write_metadata()

    def write_metadata(self, *, chip="esp32s3", version="v1.2.3", flash_files=None):
        flasher = {
            "extra_esptool_args": {"chip": chip},
            "flash_files": flash_files or {
                offset: relative for offset, (relative, _) in self.parts.items()
            },
        }
        (self.build / "flasher_args.json").write_text(json.dumps(flasher), encoding="utf-8")
        description = {"project_version": version, "idf_ver": "v5.5"}
        (self.build / "project_description.json").write_text(
            json.dumps(description), encoding="utf-8"
        )

    def package(self, *, version="v1.2.3", commit="a" * 40):
        return PACKAGER.package_release(
            self.build, self.web, self.output, version, commit, self.root, self.idf
        )


class FirmwareReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.fixture = FirmwareReleaseFixture(self.temporary.name)

    def test_rejects_unsafe_or_outside_flash_paths(self):
        outside = Path(self.temporary.name) / "outside.bin"
        outside.write_bytes(b"outside")
        link = self.fixture.build / "linked.bin"
        link.symlink_to(outside)
        unsafe_paths = ("../outside.bin", str(outside), "linked.bin")

        for unsafe in unsafe_paths:
            with self.subTest(path=unsafe):
                files = {
                    offset: relative for offset, (relative, _) in self.fixture.parts.items()
                }
                files["0x9000"] = unsafe
                self.fixture.write_metadata(flash_files=files)
                with self.assertRaisesRegex(ValueError, "Unsafe|out-of-build"):
                    PACKAGER.flash_parts(self.fixture.build)

    def test_rejects_missing_mandatory_flash_parts(self):
        required = ("0x0", "0x8000", "0x10000")
        for missing in required:
            with self.subTest(missing=missing):
                files = {
                    offset: relative
                    for offset, (relative, _) in self.fixture.parts.items()
                    if offset != missing
                }
                self.fixture.write_metadata(flash_files=files)
                with self.assertRaisesRegex(ValueError, "Missing bootloader"):
                    PACKAGER.flash_parts(self.fixture.build)

    def test_rejects_wrong_chip_and_built_version(self):
        self.fixture.write_metadata(chip="esp32")
        with self.assertRaisesRegex(ValueError, "ESP32-S3"):
            self.fixture.package()

        self.fixture.write_metadata(version="v9.9.9")
        with self.assertRaisesRegex(ValueError, "differs from the release tag"):
            self.fixture.package()

    def test_rejects_overlapping_parts(self):
        oversized = self.fixture.build / "bootloader/bootloader.bin"
        oversized.write_bytes(b"x" * 0x9000)
        with self.assertRaisesRegex(ValueError, "Overlapping flash parts"):
            PACKAGER.flash_parts(self.fixture.build)

    def test_rejects_invalid_release_tag_and_commit(self):
        invalid_tags = ("1.2.3", "v1.2", "v1.2.3+build", "v1.2.3/beta")
        for tag in invalid_tags:
            with self.subTest(tag=tag):
                with self.assertRaisesRegex(ValueError, "Version must be a release tag"):
                    self.fixture.package(version=tag)
        with self.assertRaisesRegex(ValueError, "full Git SHA"):
            self.fixture.package(commit="abc123")

    def test_preserves_every_flash_part_without_gap_filler(self):
        archive = self.fixture.package()
        bundle = archive.with_suffix("")
        manifest = json.loads((bundle / "manifest.json").read_text(encoding="utf-8"))
        actual_parts = manifest["builds"][0]["parts"]
        expected_offsets = [int(offset, 0) for offset in self.fixture.parts]
        self.assertEqual(expected_offsets, [part["offset"] for part in actual_parts])
        self.assertEqual(len(self.fixture.parts), len(actual_parts))

        for part, (_, expected_content) in zip(actual_parts, self.fixture.parts.values()):
            packaged = bundle / part["path"]
            self.assertEqual(expected_content, packaged.read_bytes())
        nvs_part = next(part for part in actual_parts if part["offset"] == 0x9000)
        self.assertEqual(b"nvs-settings", (bundle / nvs_part["path"]).read_bytes())
        self.assertFalse(any("merged" in part["path"] for part in actual_parts))

    def test_archive_hashes_launcher_mode_and_source_allowlist(self):
        archive = self.fixture.package()
        bundle = archive.with_suffix("")

        sidecar = archive.with_suffix(".zip.sha256").read_text(encoding="utf-8").strip()
        expected_archive_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
        self.assertEqual(f"{expected_archive_hash}  {archive.name}", sidecar)

        inventory = (bundle / "SHA256SUMS").read_text(encoding="utf-8").splitlines()
        for line in inventory:
            digest, relative = line.split("  ", 1)
            self.assertEqual(digest, hashlib.sha256((bundle / relative).read_bytes()).hexdigest())

        with zipfile.ZipFile(archive) as zipped:
            names = zipped.namelist()
            prefix = f"{bundle.name}/"
            launcher = zipped.getinfo(f"{prefix}start.command")
            self.assertTrue((launcher.external_attr >> 16) & 0o111)
            self.assertFalse(any(name.endswith("private-config.txt") for name in names))
            self.assertFalse(any(name.endswith("source-map.js.map") for name in names))
            self.assertFalse(any(name.endswith("unrelated-source.c") for name in names))
            self.assertFalse(any("node_modules" in name for name in names))


    def test_local_build_script_cli_validation(self):
        help_run = subprocess.run(
            ["bash", str(BUILD_SCRIPT), "--help"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(0, help_run.returncode)
        self.assertIn("Usage:", help_run.stdout)

        invalid_ver = subprocess.run(
            ["bash", str(BUILD_SCRIPT), "1.2.3", "--allow-dirty"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(0, invalid_ver.returncode)
        self.assertIn("Version must be a semantic tag", invalid_ver.stderr)

        unknown_opt = subprocess.run(
            ["bash", str(BUILD_SCRIPT), "--not-a-flag"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(0, unknown_opt.returncode)
        self.assertIn("Unknown option", unknown_opt.stderr)

if __name__ == "__main__":
    unittest.main()
