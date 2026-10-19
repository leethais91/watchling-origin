#!/usr/bin/env python3
"""Package ESP-IDF's exact flash parts with an offline local web installer."""
import argparse
import hashlib
import json
import re
import shutil
import zipfile
from pathlib import Path


def write_json(filename, value):
    filename.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def flash_parts(build):
    args = json.loads((build / "flasher_args.json").read_text())
    if args.get("extra_esptool_args", {}).get("chip") != "esp32s3":
        raise ValueError("Release requires an ESP32-S3 build")
    files = args.get("flash_files", {})
    parts, end = [], 0
    for address, relative in sorted(files.items(), key=lambda item: int(item[0], 0)):
        offset = int(address, 0)
        source = Path(relative)
        if source.is_absolute() or ".." in source.parts:
            raise ValueError("Unsafe flash file path")
        source = (build / source).resolve()
        if not source.is_relative_to(build.resolve()) or not source.is_file():
            raise ValueError("Missing or out-of-build flash file")
        size = source.stat().st_size
        if not size or source.suffix != ".bin" or offset % 4096:
            raise ValueError("Flash parts must be nonempty .bin files at sector boundaries")
        if offset < end or offset + size > 16 * 1024 * 1024:
            raise ValueError("Overlapping flash parts or image exceeds 16 MB")
        end = offset + size
        parts.append((offset, source))
    if not {0, 0x8000, 0x10000}.issubset({offset for offset, _ in parts}):
        raise ValueError("Missing bootloader, partition table, or application")
    return parts


def collect_notices(root, build, idf, destination):
    destination.mkdir()
    for name in ["LICENSE", "THIRD_PARTY_NOTICES.md"]:
        shutil.copyfile(root / name, destination / name)
    groups = [("fonts", root / "echoear-firmware/main/assets"),
              ("managed", build.parent / "managed_components"), ("esp-idf", idf)]
    for group, directory in groups:
        if not directory.is_dir():
            raise ValueError(f"Missing license source: {group}")
        # Include license notices only, never sources or SDK configuration.
        found = False
        for source in directory.rglob("*"):
            if not source.is_file() or source.is_symlink():
                continue
            if not re.match(r"^(licen[sc]e|copying|notice)(\.|$)|.*-OFL\.txt$", source.name, re.I):
                continue
            relative = source.relative_to(directory)
            if any(part.startswith(".") for part in relative.parts):
                continue
            target = destination / group / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            found = True
        if not found:
            raise ValueError(f"No license notices found: {group}")


def package_release(build, web, output, version, commit, root, idf):
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?", version):
        raise ValueError("Version must be a release tag such as v0.1.0")
    if not re.fullmatch(r"[a-f0-9]{40}", commit):
        raise ValueError("Commit must be a full Git SHA")
    description = json.loads((build / "project_description.json").read_text())
    if description.get("project_version") != version:
        raise ValueError("Built firmware version differs from the release tag; build with -DPROJECT_VER")
    parts = flash_parts(build)
    bundle = output / f"watchling-{version}-esp-vocat-v1.2"
    if bundle.exists():
        raise ValueError("Output bundle already exists; use a fresh output directory")
    # Copy only installer outputs. No node_modules, repo files, logs or credentials.
    if not (web / "assets/installer.js").is_file():
        raise ValueError("Build web-installer first with npm ci && npm run build")
    allowed = {"index.html", "style.css", "app.js", "start.command", "serve-installer.mjs"}
    output.mkdir(parents=True, exist_ok=True)
    bundle.mkdir()
    for name in allowed:
        shutil.copyfile(web / name, bundle / name)
    for name in ["assets", "licenses"]:
        shutil.copytree(web / name, bundle / name)
    (bundle / "start.command").chmod(0o755)
    (bundle / "firmware").mkdir()
    manifest_parts, hashes = [], {}
    for offset, source in parts:
        relative = f"firmware/{offset:08x}-{source.name}"
        shutil.copyfile(source, bundle / relative)
        hashes[relative] = hashlib.sha256(source.read_bytes()).hexdigest()
        manifest_parts.append({"path": relative, "offset": offset})
    write_json(bundle / "manifest.json", {
        "name": "Watchling", "version": version, "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,
        "builds": [{"chipFamily": "ESP32-S3", "parts": manifest_parts}],
    })
    write_json(bundle / "release.json", {
        "version": version, "commit": commit, "board": "ESP-VoCat V1.2",
        "esp_idf": description.get("idf_ver", "unknown"), "sha256": hashes,
    })
    collect_notices(root, build, idf, bundle / "notices")
    entries = []
    for source in sorted(bundle.rglob("*")):
        if source.is_file():
            relative = source.relative_to(bundle).as_posix()
            if not re.fullmatch(r"[A-Za-z0-9_./-]+", relative):
                raise ValueError("Unsupported filename in release")
            entries.append(f"{hashlib.sha256(source.read_bytes()).hexdigest()}  {relative}\n")
    (bundle / "SHA256SUMS").write_text("".join(entries), encoding="utf-8")
    archive = output / f"{bundle.name}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
        for source in sorted(bundle.rglob("*")):
            if source.is_file():
                zipped.write(source, source.relative_to(output))
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".zip.sha256").write_text(f"{checksum}  {archive.name}\n", encoding="utf-8")
    return archive


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["build-dir", "web-dir", "output-dir", "source-root", "idf-path"]:
        parser.add_argument(f"--{name}", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    try:
        print(package_release(args.build_dir, args.web_dir, args.output_dir, args.version,
                              args.commit, args.source_root, args.idf_path))
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")
