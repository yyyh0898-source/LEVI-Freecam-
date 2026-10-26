#!/usr/bin/env python3
"""Assemble and verify the Freecam .levipack for LeviLauncher.

Build the package from a compiled mod library:

    package_levipack.py --so build/android/out/arm64-v8a/libfreecam.so \\
                        --config-gen build/host/freecam_config_gen \\
                        --out dist

Verify any existing pack against the launcher's import rules:

    package_levipack.py --verify dist/freecam.levipack

The checks mirror what LeviLaunchroid's FileHandler (import) and the preloader's
ModManifest (load) require: manifest.json present, exactly one .so, the manifest
`entry` / `icon` pointing at real files, `type` being "preload-native". On top of
that it flags `overwrite_files` entries that name no file in the pack, because the
launcher would skip them silently.
"""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MOD_DIR = ROOT / "mod"

EM_AARCH64 = 183
SHT_DYNAMIC = 6
DT_NEEDED = 1
PRELOADER_LIBRARY = "libpreloader.so"
FIXED_ZIP_TIME = (1980, 1, 1, 0, 0, 0)


class PackageError(Exception):
    pass


# ---------------------------------------------------------------------------
# ELF inspection (stdlib only)
# ---------------------------------------------------------------------------


def inspect_elf(path: Path) -> tuple[int, list[str] | None]:
    """Returns (e_machine, DT_NEEDED names or None when not determinable)."""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise PackageError(f"{path.name} is not an ELF file")
    if data[4] != 2 or data[5] != 1:
        raise PackageError(f"{path.name} is not a 64-bit little-endian ELF file")

    machine = struct.unpack_from("<H", data, 18)[0]
    section_offset = struct.unpack_from("<Q", data, 40)[0]
    section_size, section_count = struct.unpack_from("<HH", data, 58)
    if section_offset == 0 or section_count == 0:
        return machine, None

    def section(index: int) -> dict[str, int]:
        base = section_offset + index * section_size
        (_name, kind, _flags, _addr, offset, size, link, _info, _align,
         _entsize) = struct.unpack_from("<IIQQQQIIQQ", data, base)
        return {"type": kind, "offset": offset, "size": size, "link": link}

    for index in range(section_count):
        dynamic = section(index)
        if dynamic["type"] != SHT_DYNAMIC:
            continue
        strings = section(dynamic["link"])
        needed: list[str] = []
        for position in range(dynamic["offset"], dynamic["offset"] + dynamic["size"], 16):
            tag, value = struct.unpack_from("<QQ", data, position)
            if tag == 0:
                break
            if tag == DT_NEEDED:
                start = strings["offset"] + value
                needed.append(data[start:data.index(b"\0", start)].decode("utf-8"))
        return machine, needed
    return machine, None


# ---------------------------------------------------------------------------
# Launcher import rules (port of FileHandler / ModManifest checks)
# ---------------------------------------------------------------------------


def normalize_zip_name(name: str) -> str:
    """FileHandler.normalizeZipEntryName"""
    normalized = name.strip().replace("\\", "/")
    if normalized.startswith("./"):
        normalized = normalized[2:]
    normalized = normalized.lstrip("/")
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    return normalized


def normalize_entry_path(path: object) -> str | None:
    """FileHandler.normalizeEntryPath: a safe relative path, or None."""
    if not isinstance(path, str):
        return None
    normalized = path.strip().replace("\\", "/")
    if not normalized or normalized.startswith("/"):
        return None
    if any(part in ("", ".", "..") for part in normalized.split("/")):
        return None
    return normalized


def verify_levipack(archive: Path) -> dict:
    """Raises PackageError unless the launcher would accept and load the pack."""
    try:
        zf = zipfile.ZipFile(archive)
    except zipfile.BadZipFile as exc:
        raise PackageError(f"{archive.name} is not a zip archive: {exc}") from exc

    with zf:
        bad = zf.testzip()
        if bad is not None:
            raise PackageError(f"corrupt zip entry: {bad}")

        files: dict[str, bytes] = {}
        for info in zf.infolist():
            name = normalize_zip_name(info.filename)
            if not name or name.startswith("__MACOSX") or info.is_dir():
                continue
            files[name] = zf.read(info)

    manifest_roots = {
        name.rsplit("/", 1)[0] if "/" in name else ""
        for name in files
        if name == "manifest.json" or name.endswith("/manifest.json")
    }
    so_entries = [name for name in files if name.lower().endswith(".so")]
    if not manifest_roots:
        raise PackageError("no manifest.json in the archive")
    if len(so_entries) != 1:
        raise PackageError(
            f"exactly one .so is required, found {len(so_entries)}: {so_entries}")
    if "" not in manifest_roots:
        raise PackageError(
            "manifest.json must sit at the archive root (the launcher would "
            "otherwise need to guess the mod folder)")

    try:
        manifest = json.loads(files["manifest.json"].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise PackageError(f"manifest.json is not valid JSON: {exc}") from exc
    if not isinstance(manifest, dict):
        raise PackageError("manifest.json must be a JSON object")

    if manifest.get("type") != "preload-native":
        raise PackageError('manifest "type" must be "preload-native"')

    entry = normalize_entry_path(manifest.get("entry"))
    if entry is None or entry not in files:
        raise PackageError(f'manifest "entry" ({manifest.get("entry")!r}) is not in the archive')
    if not entry.lower().endswith(".so"):
        raise PackageError(f'manifest "entry" ({entry}) is not a .so file')

    icon = manifest.get("icon")
    if icon:
        icon_path = normalize_entry_path(icon)
        if icon_path is None or icon_path not in files:
            raise PackageError(f'manifest "icon" ({icon!r}) is not in the archive')
        if not files[icon_path].startswith(b"\x89PNG\r\n\x1a\n"):
            raise PackageError(f'manifest "icon" ({icon_path}) is not a PNG')

    versions = manifest.get("minecraft_versions", [])
    if not isinstance(versions, list) or not all(isinstance(v, str) for v in versions):
        raise PackageError('manifest "minecraft_versions" must be a list of strings')

    overwrite = manifest.get("overwrite_files", [])
    if not isinstance(overwrite, list) or not all(isinstance(v, str) for v in overwrite):
        raise PackageError('manifest "overwrite_files" must be a list of strings')
    for listed in overwrite:
        listed_path = normalize_entry_path(listed)
        if listed_path is None or listed_path not in files:
            # The launcher ignores such an entry silently, which would leave an
            # outdated file in place when the pack is imported over an old install.
            raise PackageError(f'manifest "overwrite_files" entry {listed!r} is not in the archive')

    for required in ("config/config.json", "config/config.schema.json"):
        if required not in files:
            raise PackageError(f"{required} is missing (run the config generator)")
        try:
            json.loads(files[required].decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise PackageError(f"{required} is not valid JSON: {exc}") from exc

    return {"manifest": manifest, "files": sorted(files), "sizes": {k: len(v) for k, v in files.items()}}


# ---------------------------------------------------------------------------
# Packaging
# ---------------------------------------------------------------------------


def stage_package(args: argparse.Namespace, stage: Path) -> None:
    so_path = Path(args.so)
    if not so_path.is_file():
        raise PackageError(f"library not found: {so_path}")

    machine, needed = inspect_elf(so_path)
    if machine != EM_AARCH64 and not args.allow_non_arm64:
        raise PackageError(
            f"{so_path.name} is not an arm64 (AArch64) library (e_machine={machine}); "
            "LeviLauncher only loads arm64-v8a native mods")
    if needed is None:
        print(f"note: could not read DT_NEEDED from {so_path.name}")
    elif PRELOADER_LIBRARY not in needed:
        print(f"WARNING: {so_path.name} does not link {PRELOADER_LIBRARY} "
              f"(NEEDED: {', '.join(needed) or 'none'}). Was it built with "
              "-DFREECAM_LINK_PRELOADER=OFF? Preloader symbols may not resolve at load time.",
              file=sys.stderr)

    manifest_path = MOD_DIR / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if args.mod_version:
        manifest["version"] = args.mod_version

    if stage.exists():
        shutil.rmtree(stage)
    (stage / "data").mkdir(parents=True)

    (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    shutil.copyfile(so_path, stage / manifest["entry"])
    shutil.copyfile(MOD_DIR / "assets" / "icon.png", stage / manifest["icon"])
    shutil.copyfile(MOD_DIR / "data" / "signatures.json", stage / "data" / "signatures.json")

    generator = Path(args.config_gen)
    if not generator.is_file():
        raise PackageError(f"config generator not found: {generator}")
    result = subprocess.run([str(generator), str(stage)], capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise PackageError(f"config generator failed ({result.returncode}): {result.stderr.strip()}")


def write_archive(stage: Path, archive: Path) -> None:
    files = sorted(p for p in stage.rglob("*") if p.is_file())
    files.sort(key=lambda p: (p.relative_to(stage).as_posix() != "manifest.json",
                              p.relative_to(stage).as_posix()))

    archive.parent.mkdir(parents=True, exist_ok=True)
    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for path in files:
            info = zipfile.ZipInfo(path.relative_to(stage).as_posix(), date_time=FIXED_ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zf.writestr(info, path.read_bytes())


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--so", help="compiled libfreecam.so (arm64-v8a)")
    parser.add_argument("--config-gen", help="host build of freecam_config_gen")
    parser.add_argument("--out", default="dist", help="output directory (default: dist)")
    parser.add_argument("--name", default="freecam", help="archive base name (default: freecam)")
    parser.add_argument("--mod-version", help="override manifest version")
    parser.add_argument("--allow-non-arm64", action="store_true",
                        help="testing only: accept a non-AArch64 library")
    parser.add_argument("--verify", metavar="LEVIPACK", help="only verify an existing pack")
    args = parser.parse_args(argv)

    try:
        if args.verify:
            archive = Path(args.verify)
        else:
            if not args.so or not args.config_gen:
                parser.error("--so and --config-gen are required (or use --verify)")
            out = Path(args.out)
            stage = out / args.name
            archive = out / f"{args.name}.levipack"
            stage_package(args, stage)
            write_archive(stage, archive)

        report = verify_levipack(archive)
    except PackageError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    manifest = report["manifest"]
    print(f"{archive}  ({archive.stat().st_size} bytes)")
    print(f'  name={manifest.get("name")!r} version={manifest.get("version")!r} '
          f'minecraft_versions={manifest.get("minecraft_versions")}')
    for name in report["files"]:
        print(f"  {report['sizes'][name]:>9}  {name}")
    print("OK: the launcher's import rules are satisfied.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
