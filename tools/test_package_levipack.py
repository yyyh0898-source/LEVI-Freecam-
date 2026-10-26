#!/usr/bin/env python3
"""Tests for tools/package_levipack.py and the files it packages.

    python3 tools/test_package_levipack.py

Standard library only. The library inside the test packs is a synthetic ELF, so
no Android toolchain is needed.
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import stat
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
ROOT = TOOLS_DIR.parent

_spec = importlib.util.spec_from_file_location("package_levipack", TOOLS_DIR / "package_levipack.py")
pkg = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = pkg
_spec.loader.exec_module(pkg)

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def make_elf(machine: int = pkg.EM_AARCH64, needed: list[str] | None = None,
             *, with_dynamic: bool = True, elf_class: int = 2) -> bytes:
    """Smallest ELF64 image that inspect_elf() can read: header, .dynamic, .dynstr."""
    needed = needed or []
    dynstr = b"\0" + b"".join(name.encode() + b"\0" for name in needed)
    offsets, cursor = [], 1
    for name in needed:
        offsets.append(cursor)
        cursor += len(name) + 1
    dynamic = b"".join(struct.pack("<QQ", pkg.DT_NEEDED, off) for off in offsets) + struct.pack("<QQ", 0, 0)

    header_size, shentsize = 64, 64
    dynamic_offset = header_size
    dynstr_offset = dynamic_offset + len(dynamic)
    shoff = dynstr_offset + len(dynstr)
    shoff += -shoff % 8

    def shdr(kind: int, offset: int, size: int, link: int) -> bytes:
        return struct.pack("<IIQQQQIIQQ", 0, kind, 0, 0, offset, size, link, 0, 8, 0)

    sections = [shdr(0, 0, 0, 0),
                shdr(pkg.SHT_DYNAMIC, dynamic_offset, len(dynamic), 2),
                shdr(3, dynstr_offset, len(dynstr), 0)]
    e_shnum = len(sections) if with_dynamic else 0
    e_shoff = shoff if with_dynamic else 0

    ident = b"\x7fELF" + bytes([elf_class, 1, 1]) + b"\0" * 9
    header = ident + struct.pack("<HHIQQQIHHHHHH", 3, machine, 1, 0, 0, e_shoff, 0,
                                 header_size, 0, 0, shentsize, e_shnum, 0)
    assert len(header) == header_size
    body = header + dynamic + dynstr
    body += b"\0" * (shoff - len(body))
    return body + b"".join(sections)


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


FAKE_GENERATOR = """#!/usr/bin/env python3
import json, pathlib, sys
config = pathlib.Path(sys.argv[1]) / "config"
config.mkdir(parents=True, exist_ok=True)
(config / "config.json").write_text(json.dumps({"version": 1}))
(config / "config.schema.json").write_text(json.dumps({"title": "Freecam"}))
"""


def make_good_pack_files() -> dict[str, bytes]:
    manifest = (ROOT / "mod" / "manifest.json").read_bytes()
    return {
        "manifest.json": manifest,
        "libfreecam.so": make_elf(needed=["libpreloader.so"]),
        "icon.png": (ROOT / "mod" / "assets" / "icon.png").read_bytes(),
        "data/signatures.json": (ROOT / "mod" / "data" / "signatures.json").read_bytes(),
        "config/config.json": b'{"version": 1}',
        "config/config.schema.json": b"{}",
    }


def write_zip(path: Path, files: dict[str, bytes]) -> Path:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        for name, data in files.items():
            zf.writestr(name, data)
    return path


class ElfInspectionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="freecam-elf-"))
        self.addCleanup(lambda: __import__("shutil").rmtree(self.tmp, ignore_errors=True))

    def write(self, data: bytes) -> Path:
        path = self.tmp / "lib.so"
        path.write_bytes(data)
        return path

    def test_reads_machine_and_needed(self):
        path = self.write(make_elf(needed=["liblog.so", "libpreloader.so", "libc.so"]))
        self.assertEqual(pkg.inspect_elf(path),
                         (pkg.EM_AARCH64, ["liblog.so", "libpreloader.so", "libc.so"]))

    def test_other_architecture_is_reported(self):
        machine, needed = pkg.inspect_elf(self.write(make_elf(machine=62)))
        self.assertEqual((machine, needed), (62, []))

    def test_no_section_table_means_unknown_needed(self):
        machine, needed = pkg.inspect_elf(self.write(make_elf(with_dynamic=False)))
        self.assertEqual(machine, pkg.EM_AARCH64)
        self.assertIsNone(needed)

    def test_rejects_non_elf_and_32_bit(self):
        with self.assertRaises(pkg.PackageError):
            pkg.inspect_elf(self.write(b"MZ not an elf"))
        with self.assertRaises(pkg.PackageError):
            pkg.inspect_elf(self.write(make_elf(elf_class=1)))


class VerifyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="freecam-verify-"))
        self.addCleanup(lambda: __import__("shutil").rmtree(self.tmp, ignore_errors=True))

    def verify(self, files: dict[str, bytes]) -> dict:
        return pkg.verify_levipack(write_zip(self.tmp / "t.levipack", files))

    def with_manifest(self, **changes) -> dict[str, bytes]:
        files = make_good_pack_files()
        manifest = json.loads(files["manifest.json"])
        for key, value in changes.items():
            if value is None:
                manifest.pop(key, None)
            else:
                manifest[key] = value
        files["manifest.json"] = json.dumps(manifest).encode()
        return files

    def test_accepts_the_good_pack(self):
        report = self.verify(make_good_pack_files())
        self.assertEqual(report["manifest"]["entry"], "libfreecam.so")

    def test_launcher_style_name_normalization(self):
        files = {("./" + name if name == "manifest.json" else name.replace("/", "\\")): data
                 for name, data in make_good_pack_files().items()}
        self.verify(files)

    def test_rejects_broken_packs(self):
        good = make_good_pack_files()

        def drop(*names):
            return {k: v for k, v in good.items() if k not in names}

        cases = {
            "no manifest": drop("manifest.json"),
            "no .so": drop("libfreecam.so"),
            "two .so files": {**good, "extra/libother.so": make_elf()},
            "manifest in a sub folder": {f"Freecam/{k}": v for k, v in good.items()},
            "manifest is not json": {**good, "manifest.json": b"{"},
            "manifest is not an object": {**good, "manifest.json": b"[]"},
            "wrong type": self.with_manifest(type="script"),
            "type missing": self.with_manifest(type=None),
            "entry missing from archive": self.with_manifest(entry="libnope.so"),
            "entry escapes the pack": self.with_manifest(entry="../libfreecam.so"),
            "entry is absolute": self.with_manifest(entry="/libfreecam.so"),
            "entry is not a .so": self.with_manifest(entry="manifest.json"),
            "icon missing from archive": self.with_manifest(icon="nope.png"),
            "icon is not a png": {**good, "icon.png": b"GIF89a"},
            "versions is a string": self.with_manifest(minecraft_versions="1.26.52"),
            "versions has a number": self.with_manifest(minecraft_versions=["1.26.52", 5]),
            "overwrite_files is a string": self.with_manifest(overwrite_files="icon.png"),
            "overwrite_files names a missing file": self.with_manifest(overwrite_files=["data/nope.json"]),
            "overwrite_files escapes the pack": self.with_manifest(overwrite_files=["../icon.png"]),
            "no config.json": drop("config/config.json"),
            "no config schema": drop("config/config.schema.json"),
            "config is not json": {**good, "config/config.json": b"nope"},
        }
        for label, files in cases.items():
            with self.subTest(label):
                with self.assertRaises(pkg.PackageError):
                    self.verify(files)

    def test_rejects_non_zip(self):
        path = self.tmp / "garbage.levipack"
        path.write_bytes(b"not a zip")
        with self.assertRaises(pkg.PackageError):
            pkg.verify_levipack(path)


class PathRuleTests(unittest.TestCase):
    def test_normalize_zip_name(self):
        self.assertEqual(pkg.normalize_zip_name("./a\\b//c.json"), "a/b/c.json")
        self.assertEqual(pkg.normalize_zip_name("/abs/x"), "abs/x")

    def test_normalize_entry_path(self):
        self.assertEqual(pkg.normalize_entry_path(" lib/x.so "), "lib/x.so")
        for bad in ("", "/x", "../x", "a/../b", "a//b", "./x", None, 5):
            with self.subTest(bad):
                self.assertIsNone(pkg.normalize_entry_path(bad))


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="freecam-pack-"))
        self.addCleanup(lambda: __import__("shutil").rmtree(self.tmp, ignore_errors=True))
        self.generator = self.tmp / "fake_config_gen"
        self.generator.write_text(FAKE_GENERATOR)
        self.generator.chmod(self.generator.stat().st_mode | stat.S_IXUSR)
        self.so = self.tmp / "libfreecam.so"
        self.so.write_bytes(make_elf(needed=["liblog.so", "libpreloader.so"]))

    def run_main(self, *extra: str, so: Path | None = None, out: str = "dist"):
        argv = ["--so", str(so or self.so), "--config-gen", str(self.generator),
                "--out", str(self.tmp / out), *extra]
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = pkg.main(argv)
        return code, stdout.getvalue(), stderr.getvalue()

    def test_builds_a_pack_the_verifier_accepts(self):
        code, out, err = self.run_main()
        self.assertEqual(code, 0, err)
        self.assertIn("OK:", out)

        archive = self.tmp / "dist" / "freecam.levipack"
        with zipfile.ZipFile(archive) as zf:
            names = zf.namelist()
            self.assertEqual(names[0], "manifest.json")
            self.assertEqual(sorted(names), sorted([
                "manifest.json", "libfreecam.so", "icon.png", "data/signatures.json",
                "config/config.json", "config/config.schema.json"]))
            self.assertTrue(all("\\" not in name and not name.startswith("/") for name in names))
            self.assertEqual(zf.read("libfreecam.so"), self.so.read_bytes())
            manifest = json.loads(zf.read("manifest.json"))
        self.assertEqual(manifest["name"], "Freecam")
        self.assertEqual(manifest["type"], "preload-native")
        self.assertIn("1.26.52", manifest["minecraft_versions"])

    def test_output_is_deterministic(self):
        self.assertEqual(self.run_main(out="a")[0], 0)
        self.assertEqual(self.run_main(out="b")[0], 0)
        self.assertEqual((self.tmp / "a" / "freecam.levipack").read_bytes(),
                         (self.tmp / "b" / "freecam.levipack").read_bytes())

    def test_mod_version_override_and_archive_name(self):
        code, _, err = self.run_main("--mod-version", "2.3.4", "--name", "freecam-test")
        self.assertEqual(code, 0, err)
        with zipfile.ZipFile(self.tmp / "dist" / "freecam-test.levipack") as zf:
            self.assertEqual(json.loads(zf.read("manifest.json"))["version"], "2.3.4")

    def test_rejects_x86_library_unless_allowed(self):
        x86 = self.tmp / "x86.so"
        x86.write_bytes(make_elf(machine=62, needed=["libpreloader.so"]))
        code, _, err = self.run_main(so=x86)
        self.assertEqual(code, 1)
        self.assertIn("arm64", err)
        self.assertFalse((self.tmp / "dist" / "freecam.levipack").exists())

        code, _, err = self.run_main("--allow-non-arm64", so=x86)
        self.assertEqual(code, 0, err)

    def test_warns_when_preloader_is_not_linked(self):
        bare = self.tmp / "bare.so"
        bare.write_bytes(make_elf(needed=["liblog.so"]))
        code, _, err = self.run_main(so=bare)
        self.assertEqual(code, 0)
        self.assertIn("libpreloader.so", err)

    def test_reports_missing_inputs(self):
        code, _, err = self.run_main(so=self.tmp / "missing.so")
        self.assertEqual(code, 1)
        self.assertIn("not found", err)

        self.generator.unlink()
        code, _, err = self.run_main()
        self.assertEqual(code, 1)
        self.assertIn("generator", err)

    def test_generator_failure_is_reported(self):
        self.generator.write_text("#!/bin/sh\necho boom >&2\nexit 3\n")
        code, _, err = self.run_main()
        self.assertEqual(code, 1)
        self.assertIn("boom", err)

    def test_verify_mode_on_a_built_pack(self):
        self.assertEqual(self.run_main()[0], 0)
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = pkg.main(["--verify", str(self.tmp / "dist" / "freecam.levipack")])
        self.assertEqual(code, 0, stderr.getvalue())


class ProjectFileTests(unittest.TestCase):
    """The files the pack is made from have to stay consistent with each other."""

    def test_manifest_targets_the_requested_game_version(self):
        manifest = read_json(ROOT / "mod" / "manifest.json")
        self.assertEqual(manifest["type"], "preload-native")
        self.assertEqual(manifest["name"], "Freecam")
        self.assertEqual(manifest["entry"], "libfreecam.so")
        self.assertEqual(manifest["icon"], "icon.png")
        self.assertEqual(manifest["minecraft_versions"], ["1.26.52", "1.26.52.*"])

    def test_reimport_refreshes_shipped_data_but_never_user_settings(self):
        # The launcher keeps every installed file that is not listed here (apart from
        # manifest.json and the entry library), so a re-import would otherwise leave
        # an old signatures.json or icon behind. config/config.json holds the user's
        # settings and must never be overwritten.
        manifest = read_json(ROOT / "mod" / "manifest.json")
        overwrite = manifest["overwrite_files"]
        for path in ("icon.png", "data/signatures.json", "config/config.schema.json"):
            self.assertIn(path, overwrite)
        self.assertNotIn("config/config.json", overwrite)
        self.assertNotIn("overwrite_folders", manifest)

    def test_icon_is_a_square_png(self):
        data = (ROOT / "mod" / "assets" / "icon.png").read_bytes()
        self.assertTrue(data.startswith(PNG_MAGIC))
        width, height = struct.unpack(">II", data[16:24])
        self.assertEqual((width, height), (128, 128))

    def test_embedded_icon_matches_the_png(self):
        header = (ROOT / "mod" / "src" / "IconData.hpp").read_text(encoding="utf-8")
        png = (ROOT / "mod" / "assets" / "icon.png").read_bytes()
        values = [int(token, 16) for token in
                  __import__("re").findall(r"0x([0-9a-fA-F]{2})", header)]
        self.assertEqual(bytes(values), png, "run tools/embed_icon.py after changing icon.png")

    def test_signature_file_is_well_formed(self):
        data = read_json(ROOT / "mod" / "data" / "signatures.json")
        self.assertEqual(data["schemaVersion"], 1)
        self.assertEqual(data["module"], "libminecraftpe.so")
        self.assertIsInstance(data["rules"], list)
        for rule in data["rules"]:
            for key in ("cameraPose", "moveInput", "nameTag"):
                self.assertTrue(rule.get(key), f"rule {rule.get('label')!r} lacks {key}")

    def test_icon_credit_is_shipped(self):
        notice = (ROOT / "mod" / "assets" / "ICON-LICENSE.txt").read_text(encoding="utf-8")
        self.assertIn("MIT", notice)


if __name__ == "__main__":
    os.chdir(ROOT)
    unittest.main(verbosity=2)
