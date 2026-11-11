from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from driver.contract import (
    Capability,
    PortManifest,
    _patch_targets_from_text,
    load_manifest,
    resolve_capabilities,
    validate_port,
)
from driver.patching import (
    PatchFormatError,
    normalize_unified_diff,
    parse_hunks,
    verify_hunks_applied,
)


REPO_ROOT = Path(__file__).resolve().parents[1]


class PortManifestTests(unittest.TestCase):
    def test_repository_manifest_matches_driver_and_patches(self) -> None:
        report = validate_port(REPO_ROOT)
        self.assertEqual([], report.errors)
        self.assertGreater(report.patch_count, 100)

    def test_manifest_represents_a_single_chromium_branch(self) -> None:
        manifest = load_manifest(REPO_ROOT)
        # 每个分支对应一个 Chromium 版本：里程碑必须和 chromium_version 的主版本一致，不写死某个数字
        self.assertEqual(int(manifest.chromium_version.split(".")[0]), manifest.chromium_milestone)
        self.assertEqual(1, manifest.core_api)
        self.assertEqual([], sorted(p.name for p in (REPO_ROOT / "port").glob("chromium-*")))

    def test_dependency_resolution_is_topological(self) -> None:
        manifest = load_manifest(REPO_ROOT)
        selected = resolve_capabilities(manifest, ["launch-cookies"])
        self.assertEqual(
            ["eventbus-input-sync", "account-import", "launch-cookies"],
            [item.id for item in selected],
        )

    def test_unknown_capability_fails(self) -> None:
        manifest = load_manifest(REPO_ROOT)
        with self.assertRaisesRegex(ValueError, "unknown capability"):
            resolve_capabilities(manifest, ["does-not-exist"])

    def test_dependency_cycle_fails(self) -> None:
        manifest = PortManifest(
            schema_version=1,
            chromium_version="144.0.0.0",
            chromium_milestone=144,
            core_api=1,
            capabilities=(
                Capability("a", "a", "port", ("b",)),
                Capability("b", "b", "port", ("a",)),
            ),
        )
        with self.assertRaisesRegex(ValueError, "dependency cycle"):
            resolve_capabilities(manifest, ["a"])


class PatchSafetyTests(unittest.TestCase):
    def test_normalizes_crlf_and_blank_hunk_context(self) -> None:
        raw = (
            "diff --git a/a.cc b/a.cc\r\n"
            "--- a/a.cc\r\n"
            "+++ b/a.cc\r\n"
            "@@ -1,2 +1,2 @@\r\n"
            " old\r\n"
            "\r\n"
        )
        normalized = normalize_unified_diff(raw)
        self.assertEqual(
            "diff --git a/a.cc b/a.cc\n"
            "--- a/a.cc\n"
            "+++ b/a.cc\n"
            "@@ -1,2 +1,2 @@\n"
            " old\n"
            " \n",
            normalized,
        )

    def test_rejects_hunk_longer_than_its_header(self) -> None:
        # GNU patch skips the text after the declared length and any later hunk.
        raw = (
            "diff --git a/a.cc b/a.cc\n--- a/a.cc\n+++ b/a.cc\n"
            "@@ -1,1 +1,2 @@\n one\n+two\n three\n"
            "@@ -9,1 +10,2 @@\n nine\n+ten\n"
        )
        with self.assertRaises(PatchFormatError):
            parse_hunks(raw)

    def test_rejects_truncated_hunk(self) -> None:
        raw = "--- a/a.cc\n+++ b/a.cc\n@@ -1,3 +1,3 @@\n one\n"
        with self.assertRaises(PatchFormatError):
            parse_hunks(raw)

    def test_parses_every_hunk(self) -> None:
        raw = (
            "diff --git a/a.cc b/a.cc\n--- a/a.cc\n+++ b/a.cc\n"
            "@@ -1,1 +1,2 @@\n one\n+two\n"
            "@@ -9,1 +10,2 @@\n nine\n+ten\n"
        )
        self.assertEqual(["a.cc", "a.cc"], [h.target for h in parse_hunks(raw)])

    def test_verify_reports_missing_hunk(self) -> None:
        raw = (
            "--- a/a.cc\n+++ b/a.cc\n"
            "@@ -1,1 +1,2 @@\n one\n+two\n"
            "@@ -9,1 +10,2 @@\n nine\n+ten\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "a.cc").write_text("one\ntwo\nnine\n", encoding="utf-8")
            problems = verify_hunks_applied(root, parse_hunks(raw))
            self.assertEqual(1, len(problems))
            self.assertIn("'ten'", problems[0])
            (root / "a.cc").write_text("one\ntwo\nnine\nten\n", encoding="utf-8")
            self.assertEqual([], verify_hunks_applied(root, parse_hunks(raw)))

    def test_verify_checks_pure_deletion(self) -> None:
        raw = "--- a/a.cc\n+++ b/a.cc\n@@ -1,3 +1,2 @@\n one\n-two\n three\n"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "a.cc").write_text("one\ntwo\nthree\n", encoding="utf-8")
            self.assertEqual(1, len(verify_hunks_applied(root, parse_hunks(raw))))
            (root / "a.cc").write_text("one\nthree\n", encoding="utf-8")
            self.assertEqual([], verify_hunks_applied(root, parse_hunks(raw)))

    def test_rejects_parent_traversal(self) -> None:
        _, errors = _patch_targets_from_text(
            "diff --git a/../outside.cc b/../outside.cc\n", "unsafe.patch"
        )
        self.assertTrue(any("unsafe patch target" in item for item in errors))

    def test_collects_valid_target(self) -> None:
        targets, errors = _patch_targets_from_text(
            "diff --git a/chrome/browser/example.cc "
            "b/chrome/browser/example.cc\n",
            "valid.patch",
        )
        self.assertEqual(["chrome/browser/example.cc"], targets)
        self.assertEqual([], errors)


if __name__ == "__main__":
    unittest.main()
