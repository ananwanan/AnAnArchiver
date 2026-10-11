"""Offline policy and real-Git checkout regression tests for dependency resolution."""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import urllib.error


SCRIPT = Path(__file__).resolve().parents[2] / "scripts/resolve_dependencies.py"
SPEC = importlib.util.spec_from_file_location("dependency_resolver", SCRIPT)
resolver = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(resolver)
NOW = "2026-01-01T00:00:00Z"
A, B, C = "a" * 40, "b" * 40, "c" * 40


def release(tag: str, **values) -> dict:
    return {"tag_name": tag, "published_at": NOW, "draft": False, "prerelease": False, **values}


class PolicyTests(unittest.TestCase):
    def select(self, name="zlib", tags=None, releases=None):
        return resolver.select_version(name, "https://github.com/example/project.git",
                                       tags or {"v1.9.0": A, "v1.10.0": B}, "refs/heads/main", C,
                                       releases if releases is not None else [], NOW)

    def test_semantic_order_and_prerelease_filter(self):
        selected = self.select(tags={"v1.9.0": A, "v1.10.0": B, "v90.0.0": C, "v99.0.0": C},
                               releases=[release("v1.9.0"), release("v1.10.0"),
                                         release("v90.0.0", draft=True), release("v99.0.0", prerelease=True)])
        self.assertEqual(selected["tag"], "v1.10.0")
        self.assertEqual(selected["selection"], "release")

    def test_fork_ignores_inherited_tags(self):
        selected = self.select(name="libarchive")
        self.assertEqual(selected["revision"], C)
        self.assertEqual(selected["selection"], "default-branch")
        self.assertIsNone(selected["tag"])

    def test_fork_prefers_independent_release(self):
        selected = self.select(name="libarchive", releases=[release("v1.9.0")])
        self.assertEqual(selected["revision"], A)

    def test_official_tag_formats_reject_preview_suffixes(self):
        for name, tag in (("qt", "v6.12.1"), ("expat", "R_2_8_1"), ("bzip2", "bzip2-1.0.9"),
                          ("mbedtls", "mbedtls-4.2.0"), ("vulkan-headers", "v1.4.999")):
            with self.subTest(name=name):
                self.assertIsNotNone(resolver.version_key(name, tag))
                self.assertIsNone(resolver.version_key(name, tag + "-rc1"))
        self.assertIsNone(resolver.version_key("qt", "v5.99.0"))

    def test_release_without_git_ref_fails(self):
        with self.assertRaisesRegex(resolver.ResolutionError, "missing"):
            self.select(releases=[release("v2.0.0")])

    def test_api_failure_never_falls_back_or_prints_token(self):
        error = urllib.error.HTTPError("https://api.github.com", 403, "limited", {}, None)
        with patch.dict(os.environ, {"GITHUB_TOKEN": "test-private-token"}), \
                patch.object(resolver.urllib.request, "urlopen", side_effect=error):
            with self.assertRaises(resolver.ResolutionError) as caught:
                resolver.github_releases("https://github.com/example/project.git")
        self.assertIn("HTTP 403", str(caught.exception))
        self.assertNotIn("test-private-token", str(caught.exception))

    def test_api_pagination_and_authentication_do_not_put_tokens_in_urls(self):
        first = io.BytesIO(json.dumps([release("v1.9.0")]).encode())
        first.headers = {"Link": '<https://api.github.com/repos/example/project/releases?page=2>; rel="next"'}
        second = io.BytesIO(json.dumps([release("v1.10.0")]).encode())
        second.headers = {}
        with patch.dict(os.environ, {"GITHUB_TOKEN": "test-private-token"}), \
                patch.object(resolver.urllib.request, "urlopen", side_effect=[first, second]) as request:
            releases = resolver.github_releases("https://github.com/example/project.git")
        self.assertEqual(len(releases), 2)
        for call in request.call_args_list:
            self.assertEqual(call.args[0].get_header("Authorization"), "Bearer test-private-token")
            self.assertNotIn("test-private-token", call.args[0].full_url)
        self.assertEqual(request.call_args_list[0].args[0].full_url,
                         "https://api.github.com/repos/example/project/releases?per_page=100")

    def test_malformed_api_payloads_fail_explicitly(self):
        for payload in ({}, [None], ["release"], [{"tag_name": None}], [release("v1.0.0", prerelease="false")]):
            with self.subTest(payload=payload):
                response = io.BytesIO(json.dumps(payload).encode())
                response.headers = {}
                with patch.object(resolver.urllib.request, "urlopen", return_value=response):
                    with self.assertRaisesRegex(resolver.ResolutionError, "Invalid release response"):
                        resolver.github_releases("https://github.com/example/project.git")

    def test_malformed_manifest_roots_are_reported_without_traceback(self):
        with tempfile.TemporaryDirectory(prefix="anan-manifest-test-") as temporary:
            path = Path(temporary) / "manifest.json"
            for payload in ([], [1], None, {"schema_version": True},
                            {"schema_version": 1, "strategy": "latest-stable", "resolved_at": 42, "dependencies": [{}]}):
                with self.subTest(payload=payload):
                    path.write_text(json.dumps(payload), encoding="utf-8")
                    diagnostics = io.StringIO()
                    with contextlib.redirect_stderr(diagnostics):
                        result = resolver.main(["--manifest", str(path), "--verify"])
                    self.assertEqual(result, 1)
                    self.assertIn("Dependency resolution failed:", diagnostics.getvalue())
                    self.assertNotIn("Traceback", diagnostics.getvalue())

    def test_optional_top_level_identity_is_checked_in_core_mode(self):
        with tempfile.TemporaryDirectory(prefix="anan-manifest-test-") as temporary:
            root = Path(temporary)
            source = "https://github.com/example/vulkan.git"
            (root / ".gitmodules").write_text('[submodule "vulkan"]\n path = third_party/vulkan-headers\n url = ' + source + "\n")
            forged = {"name": "fake-core", "path": "third_party/vulkan-headers", "source": source}
            with patch.object(resolver, "CORE_DEPENDENCIES", ()), self.assertRaisesRegex(resolver.ResolutionError, "mismatched"):
                resolver.validate_top_sources(root, {"dependencies": [forged]}, False)

    def test_annotated_tag_is_peeled(self):
        remote = f"ref: refs/heads/main\tHEAD\n{C}\tHEAD\n{A}\trefs/tags/v1.0.0\n{B}\trefs/tags/v1.0.0^{{}}"
        with patch.object(resolver, "git", return_value=remote):
            tags, branch, head = resolver.remote_refs("source")
        self.assertEqual((tags["v1.0.0"], branch, head), (B, "refs/heads/main", C))

    def test_relative_submodule_url(self):
        self.assertEqual(resolver.relative_source("https://github.com/qt/qt5.git", "../qtbase.git"),
                         "https://github.com/qt/qtbase.git")

    def test_unsafe_manifest_paths_rejected(self):
        for path in ("../outside", "/outside", "C:/outside", "third_party/../outside", "a\\b"):
            with self.subTest(path=path), self.assertRaises(resolver.ResolutionError):
                resolver.validate_path(path)


class CheckoutTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="anan-dependency-test-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.upstream = self.repository("upstream")
        self.commit_file(self.upstream, "payload.txt", "first")
        resolver.git("tag", "v1.9.0", cwd=self.upstream)
        self.commit_file(self.upstream, "payload.txt", "latest")
        resolver.git("tag", "-a", "v1.10.0", "-m", "stable", cwd=self.upstream)
        self.latest = resolver.git("rev-parse", "HEAD", cwd=self.upstream)
        self.commit_file(self.upstream, "payload.txt", "preview")
        resolver.git("tag", "v99.0.0-rc1", cwd=self.upstream)
        self.root = self.repository("application")
        self.add_module(self.root, "third_party/zlib", self.upstream.as_posix(), A)
        self.manifest = self.root / "out/dependency-resolution.json"

    def repository(self, name: str) -> Path:
        path = self.base / name
        resolver.git("init", "--quiet", str(path))
        resolver.git("config", "user.email", "fixture@example.invalid", cwd=path)
        resolver.git("config", "user.name", "Fixture", cwd=path)
        resolver.git("config", "core.autocrlf", "false", cwd=path)
        return path

    def commit_file(self, path: Path, relative: str, contents: str) -> None:
        target = path / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(contents, encoding="utf-8")
        resolver.git("add", "--", relative, cwd=path)
        resolver.git("commit", "--quiet", "-m", "fixture", cwd=path)

    def add_module(self, parent: Path, relative: str, source: str, revision: str) -> None:
        with (parent / ".gitmodules").open("a", encoding="utf-8") as output:
            output.write(f'[submodule "{relative}"]\n path = {relative}\n url = {source}\n')
        resolver.git("add", ".gitmodules", cwd=parent)
        resolver.git("update-index", "--add", "--cacheinfo", f"160000,{revision},{relative}", cwd=parent)
        resolver.git("commit", "--quiet", "-m", "fixture submodule", cwd=parent)

    def run_resolver(self, *arguments, root=None) -> int:
        diagnostics = io.StringIO()
        with patch.object(resolver, "CORE_DEPENDENCIES", ("zlib",)), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(diagnostics):
            result = resolver.main(["--root", str(root or self.root), "--manifest", str(self.manifest), *arguments])
        self.last_diagnostics = diagnostics.getvalue()
        return result

    def test_fresh_init_bypasses_unfetchable_gitlink(self):
        self.assertEqual(self.run_resolver(), 0)
        entry = json.loads(self.manifest.read_text(encoding="utf-8"))["dependencies"][0]
        self.assertEqual(entry["revision"], self.latest)
        self.assertEqual(resolver.git("rev-parse", "HEAD", cwd=self.root / "third_party/zlib"), self.latest)
        self.assertTrue((self.root / "third_party/zlib/.git").is_file())
        self.assertEqual(self.run_resolver("--verify"), 0)

    def test_restore_freezes_snapshot_when_upstream_advances(self):
        self.assertEqual(self.run_resolver(), 0)
        self.commit_file(self.upstream, "payload.txt", "new release")
        resolver.git("tag", "v1.11.0", cwd=self.upstream)
        fresh = self.base / "restored"
        resolver.git("clone", "--quiet", str(self.root), str(fresh))
        self.assertEqual(self.run_resolver("--restore", root=fresh), 0)
        self.assertEqual(resolver.git("rev-parse", "HEAD", cwd=fresh / "third_party/zlib"), self.latest)
        self.assertEqual(self.run_resolver(), 0)
        self.assertEqual(json.loads(self.manifest.read_text())["dependencies"][0]["version"], "1.11.0")

    def test_dirty_checkout_and_unpublished_commit_preserved(self):
        self.assertEqual(self.run_resolver(), 0)
        source = self.root / "third_party/zlib"
        (source / "payload.txt").write_text("local modification", encoding="utf-8")
        self.assertEqual(self.run_resolver(), 1)
        self.assertEqual((source / "payload.txt").read_text(), "local modification")
        resolver.git("config", "user.email", "fixture@example.invalid", cwd=source)
        resolver.git("config", "user.name", "Fixture", cwd=source)
        resolver.git("add", "payload.txt", cwd=source)
        resolver.git("commit", "--quiet", "-m", "unpublished", cwd=source)
        local_revision = resolver.git("rev-parse", "HEAD", cwd=source)
        resolver.git("tag", "v100.0.0", cwd=source)
        self.assertEqual(self.run_resolver(), 1)
        self.assertEqual(resolver.git("rev-parse", "HEAD", cwd=source), local_revision)

    def test_verify_is_offline_and_rejects_mismatches(self):
        self.assertEqual(self.run_resolver(), 0)
        with patch.object(resolver, "github_releases", side_effect=AssertionError("network")), \
                patch.object(resolver, "remote_refs", side_effect=AssertionError("network")):
            self.assertEqual(self.run_resolver("--verify"), 0)
            data = json.loads(self.manifest.read_text())
            data["dependencies"][0]["revision"] = A
            resolver.write_manifest(self.manifest, data)
            self.assertEqual(self.run_resolver("--verify"), 1)

    def test_verify_rejects_forged_version(self):
        self.assertEqual(self.run_resolver(), 0)
        data = json.loads(self.manifest.read_text())
        data["dependencies"][0]["version"] = "999.0.0"
        resolver.write_manifest(self.manifest, data)
        self.assertEqual(self.run_resolver("--verify"), 1)

    def test_verify_rejects_unrelated_manifest_entries(self):
        self.assertEqual(self.run_resolver(), 0)
        data = json.loads(self.manifest.read_text())
        unrelated = data["dependencies"][0].copy()
        unrelated.update(name="unrelated", path="third_party/unrelated")
        data["dependencies"].append(unrelated)
        resolver.write_manifest(self.manifest, data)
        self.assertEqual(self.run_resolver("--verify"), 1)

    def test_failed_run_can_resume_without_publishing_partial_manifest(self):
        with patch.object(resolver, "verify", side_effect=resolver.ResolutionError("fixture verification failure")):
            self.assertEqual(self.run_resolver(), 1)
        self.assertFalse(self.manifest.exists())
        self.assertEqual(self.run_resolver(), 0)

    def test_parent_gitlink_children_are_restored_recursively(self):
        framework = self.repository("framework")
        self.commit_file(framework, "framework.txt", "matching framework")
        framework_revision = resolver.git("rev-parse", "HEAD", cwd=framework)
        self.add_module(self.upstream, "framework", "../framework", framework_revision)
        resolver.git("tag", "v1.11.0", cwd=self.upstream)
        self.assertEqual(self.run_resolver(), 0)
        data = json.loads(self.manifest.read_text())
        self.assertEqual(len(data["dependencies"]), 2)
        child = data["dependencies"][1]
        self.assertEqual((child["revision"], child["parent"], child["selection"]),
                         (framework_revision, "zlib", "parent-gitlink"))
        self.assertEqual(self.run_resolver("--verify"), 0)
        fresh = self.base / "restored-recursive"
        resolver.git("clone", "--quiet", str(self.root), str(fresh))
        self.assertEqual(self.run_resolver("--restore", root=fresh), 0)
        self.assertEqual((fresh / "third_party/zlib/framework/framework.txt").read_text(), "matching framework")
        data["dependencies"].pop()
        resolver.write_manifest(self.manifest, data)
        self.assertEqual(self.run_resolver("--verify"), 1)

    def test_interrupted_parent_update_can_resume_but_child_changes_are_protected(self):
        framework = self.repository("framework")
        self.commit_file(framework, "framework.txt", "original framework")
        first = resolver.git("rev-parse", "HEAD", cwd=framework)
        self.add_module(self.upstream, "framework", "../framework", first)
        resolver.git("tag", "v1.11.0", cwd=self.upstream)
        self.assertEqual(self.run_resolver(), 0)
        old_manifest = self.manifest.read_text()
        self.commit_file(framework, "framework.txt", "updated framework")
        updated = resolver.git("rev-parse", "HEAD", cwd=framework)
        resolver.git("update-index", "--cacheinfo", f"160000,{updated},framework", cwd=self.upstream)
        resolver.git("commit", "--quiet", "-m", "new paired framework", cwd=self.upstream)
        resolver.git("tag", "v1.12.0", cwd=self.upstream)
        original_checkout = resolver.checkout

        def interrupt_child(root, entry, parent, relative):
            if entry["selection"] == "parent-gitlink":
                raise resolver.ResolutionError("fixture child download interrupted")
            return original_checkout(root, entry, parent, relative)

        with patch.object(resolver, "checkout", side_effect=interrupt_child):
            self.assertEqual(self.run_resolver(), 1)
        self.assertEqual(self.manifest.read_text(), old_manifest)
        self.assertEqual(self.run_resolver(), 0)
        child = self.root / "third_party/zlib/framework/framework.txt"
        self.assertEqual(child.read_text(), "updated framework")
        child.write_text("local change", encoding="utf-8")
        self.assertEqual(self.run_resolver(), 1)
        self.assertEqual(child.read_text(), "local change")

    def test_source_versions_are_read_for_default_branch_and_psa(self):
        source = self.root / "third_party/libarchive/build/version"
        source.parent.mkdir(parents=True)
        source.write_text("3012004b\n", encoding="utf-8")
        archive = {"name": "libarchive", "path": "third_party/libarchive", "selection": "default-branch"}
        resolver.actual_default_version(self.root, archive)
        self.assertEqual(archive["version"], "3.12.4b")
        source = self.root / "third_party/mbedtls/tf-psa-crypto/include/tf-psa-crypto/build_info.h"
        source.parent.mkdir(parents=True)
        source.write_text('#define TF_PSA_CRYPTO_VERSION_STRING "1.9.0"\n', encoding="utf-8")
        crypto = {"name": "mbedtls/tf-psa-crypto", "path": "third_party/mbedtls/tf-psa-crypto"}
        resolver.actual_default_version(self.root, crypto)
        self.assertEqual(crypto["version"], "1.9.0")


if __name__ == "__main__":
    unittest.main()
