"""Deterministic dependency-policy and real Git replay tests; no network access."""
from __future__ import annotations

import contextlib
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("dependency_resolver", ROOT / "scripts/resolve-dependencies.py")
resolver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(resolver)
A, B, C = "a" * 40, "b" * 40, "c" * 40
NOW = "2026-10-09T00:00:00+00:00"


class FakeGitHub:
    def __init__(self, *, fork=False, releases=None, contents=None, links=None):
        self.fork = fork
        self.release_rows = [{"published_at": NOW, **row} for row in (releases or [])]
        self.contents = contents or {}
        self.links = links or {}

    def repo(self, url):
        return resolver.canonical_url(url).removeprefix("https://github.com/")

    def metadata(self, repo):
        return {"fork": self.fork, "default_branch": "main"}

    def releases(self, repo):
        return self.release_rows

    def content(self, repo, revision, path, missing_ok=False):
        return self.contents.get((repo, revision, path))

    def gitlinks(self, repo, revision, modules):
        return self.links[(repo, revision)]


class PolicyTests(unittest.TestCase):
    def test_stable_filters_and_numeric_order(self):
        for tag in ("v6.13.0-rc1", "6.13.0-beta", "v6.13.0alpha", "nightly", "v7.0.0", "v5.15.99"):
            self.assertIsNone(resolver.stable_version("qt", tag))
        self.assertGreater(resolver.stable_version("qt", "v6.12.0"), resolver.stable_version("qt", "v6.9.3"))
        self.assertEqual((1, 0, 8), resolver.stable_version("bzip2", "bzip2-1.0.8"))
        self.assertEqual((2, 7, 3), resolver.stable_version("expat", "R_2_7_3"))
        self.assertEqual((4, 2, 0), resolver.stable_version("mbedtls", "mbedtls-4.2.0"))
        self.assertEqual((1, 4, 328, 1), resolver.stable_version("vulkan-headers", "vulkan-sdk-1.4.328.1"))

    def test_release_metadata_excludes_drafts_and_prereleases(self):
        rows = [{"tag_name": "v1.10.0"}, {"tag_name": "v1.11.0", "prerelease": True},
                {"tag_name": "v1.12.0", "draft": True}, {"tag_name": "v1.20.0-rc1"}, {"tag_name": "v1.9.0"}]
        rows = [{"published_at": NOW, **row} for row in rows]
        self.assertEqual("v1.10.0", resolver.choose_release("zlib", rows)["tag_name"])

    def test_independent_fork_release_is_used_and_tag_peeled(self):
        api = FakeGitHub(fork=True, releases=[{"tag_name": "v3.9.1", "html_url": "https://github.com/fork/releases/3.9.1"}],
                         contents={("ananwanan/libarchive", B, "libarchive/archive.h"):
                                   '#define ARCHIVE_VERSION_ONLY_STRING "3.9.1"'})
        with patch.object(resolver, "remote_refs", return_value={"refs/tags/v3.9.1": A, "refs/tags/v3.9.1^{}": B}):
            row = resolver.select_source("libarchive", "https://github.com/ananwanan/libarchive.git", api)
        self.assertEqual(B, row["revision"])
        self.assertEqual("release", row["selection"])

    def test_fork_without_own_releases_ignores_inherited_tags(self):
        api = FakeGitHub(fork=True, contents={("ananwanan/libarchive", B, "libarchive/archive.h"):
                                             '#define ARCHIVE_VERSION_ONLY_STRING "3.9.0dev"'})
        with patch.object(resolver, "remote_refs", return_value={"refs/tags/v99.0.0": A, "refs/heads/main": B}):
            row = resolver.select_source("libarchive", "https://github.com/ananwanan/libarchive.git", api)
        self.assertEqual(B, row["revision"])
        self.assertEqual("3.9.0dev", row["version"])
        self.assertEqual("default-branch", row["selection"])
        self.assertEqual("libarchive/archive.h:ARCHIVE_VERSION_ONLY_STRING", row["version_source"])

    def test_release_selection_does_not_jump_to_unreleased_future_tag(self):
        api = FakeGitHub(releases=[{"tag_name": "v1.2.3", "html_url": "https://github.com/project/releases/1.2.3"}])
        with patch.object(resolver, "remote_refs", return_value={"refs/tags/v1.2.3": A, "refs/tags/v2.0.0": B}):
            self.assertEqual(A, resolver.select_source("zlib", "https://github.com/madler/zlib.git", api)["revision"])

    def test_official_tag_fallback_and_no_release_branch_fallback(self):
        api = FakeGitHub()
        with patch.object(resolver, "remote_refs", return_value={"refs/tags/bzip2-1.0.8": A, "refs/heads/main": B}):
            self.assertEqual("stable-tag", resolver.select_source("bzip2", "https://github.com/libarchive/bzip2.git", api)["selection"])
        with patch.object(resolver, "remote_refs", return_value={"refs/heads/main": B}):
            row = resolver.select_source("zlib", "https://github.com/madler/zlib.git", api)
        self.assertEqual("git-" + B[:12], row["version"])

    def test_coupled_sources_are_recursive_and_never_latest_independent(self):
        parent = {"name": "mbedtls", "path": "third_party/mbedtls", "url": "https://github.com/Mbed-TLS/mbedtls.git", "revision": A}
        content = '[submodule "tf-psa-crypto"]\npath=tf-psa-crypto\nurl=../TF-PSA-Crypto.git\n'
        nested = '[submodule "framework"]\npath=framework\nurl=../mbedtls-framework.git\n'
        api = FakeGitHub(contents={("Mbed-TLS/mbedtls", A, ".gitmodules"): content,
                                    ("Mbed-TLS/TF-PSA-Crypto", B, ".gitmodules"): nested,
                                    ("Mbed-TLS/TF-PSA-Crypto", B, "include/tf-psa-crypto/build_info.h"):
                                    '#define TF_PSA_CRYPTO_VERSION_STRING "1.2.0"'},
                          links={("Mbed-TLS/mbedtls", A): {"tf-psa-crypto": B},
                                 ("Mbed-TLS/TF-PSA-Crypto", B): {"framework": C}})
        rows = resolver.resolve_coupled(parent, api)
        self.assertEqual([B, C], [row["revision"] for row in rows])
        self.assertEqual("mbedtls/tf-psa-crypto", rows[1]["coupled_to"])
        self.assertEqual("1.2.0", rows[0]["version"])
        self.assertEqual("https://github.com/Mbed-TLS/mbedtls-framework.git", rows[1]["url"])
        self.assertTrue(all(row["selection"] == "coupled-gitlink" for row in rows))

    def test_qt_resolves_only_requested_module_set(self):
        parent = {"name": "qt", "path": "third_party/qt", "url": "https://github.com/qt/qt5.git", "revision": A}
        content = "".join(f'[submodule "{name}"]\npath={name}\nurl=../{name}.git\n' for name in (*resolver.QT_MODULES, "qtwebengine"))
        api = FakeGitHub(contents={("qt/qt5", A, ".gitmodules"): content},
                          links={("qt/qt5", A): {name: B for name in (*resolver.QT_MODULES, "qtwebengine")}})
        rows = resolver.resolve_coupled(parent, api, resolver.QT_MODULES)
        self.assertEqual(set(resolver.QT_MODULES), {row["name"].split("/")[-1] for row in rows})

    def test_api_failure_cannot_silently_fallback(self):
        api = FakeGitHub()
        api.releases = lambda repo: (_ for _ in ()).throw(resolver.ResolutionError("rate limit"))
        with patch.object(resolver, "remote_refs") as remote, self.assertRaisesRegex(resolver.ResolutionError, "rate limit"):
            resolver.select_source("zlib", "https://github.com/madler/zlib.git", api)
        remote.assert_not_called()

    def test_unsafe_module_paths_rejected(self):
        for path in ("../outside", "/absolute", "C:/drive", "a\\b", "foo/../outside", "foo//bar"):
            with self.assertRaises(resolver.ResolutionError):
                resolver.parse_gitmodules(f'[submodule "source"]\npath={path}\nurl=https://github.com/source/source\n')

    def test_libarchive_import_with_inherited_tags_still_follows_default_branch(self):
        api = FakeGitHub(fork=False, contents={("ananwanan/libarchive", B, "libarchive/archive.h"):
                                              '#define ARCHIVE_VERSION_ONLY_STRING "3.9.0dev"'})
        with patch.object(resolver, "remote_refs", return_value={"refs/tags/v99.0.0": A, "refs/heads/main": B}):
            row = resolver.select_source("libarchive", "https://github.com/ananwanan/libarchive.git", api)
        self.assertEqual((B, "default-branch"), (row["revision"], row["selection"]))

    def test_unpublished_release_metadata_is_not_formal_release(self):
        self.assertIsNone(resolver.choose_release("zlib", [{"tag_name": "v1.2.3", "published_at": None}]))

    def test_tag_fallback_does_not_select_an_explicit_prerelease(self):
        api = FakeGitHub(releases=[{"tag_name": "v2.0.0", "prerelease": True}])
        with patch.object(resolver, "remote_refs", return_value={"refs/tags/v1.0.0": A, "refs/tags/v2.0.0": B}):
            row = resolver.select_source("zlib", "https://github.com/madler/zlib.git", api)
        self.assertEqual((A, "stable-tag"), (row["revision"], row["selection"]))

    def test_malformed_api_shapes_fail_explicitly(self):
        api = resolver.GitHub()
        for rows in ({}, [None], ["release"], [{"tag_name": None}],
                     [{"tag_name": "v1.2.3", "prerelease": "false"}]):
            with self.subTest(rows=rows), patch.object(api, "request", return_value=(rows, "")):
                with self.assertRaisesRegex(resolver.ResolutionError, "Invalid GitHub release response"):
                    api.releases("owner/repository")
        for invalid in ([], None, {"encoding": "base64", "content": 42}):
            with self.subTest(content=invalid), patch.object(api, "request", return_value=(invalid, "")):
                if invalid is None:
                    continue
                with self.assertRaises(resolver.ResolutionError):
                    api.content("owner/repository", A, ".gitmodules")
        with patch.object(api, "request", return_value=({"tree": [None]}, "")), self.assertRaises(resolver.ResolutionError):
            api.gitlinks("owner/repository", A, [])

    def test_pagination_never_sends_authentication_to_another_host(self):
        api = resolver.GitHub()
        response = io.BytesIO(b"[]")
        response.headers = {"Link": '<https://other.example/releases?page=2>; rel="next"'}
        with patch.object(resolver.urllib.request, "urlopen", return_value=response) as request:
            with self.assertRaisesRegex(resolver.ResolutionError, "pagination host"):
                api.releases("owner/repository")
        self.assertEqual(1, request.call_count)

    def test_token_uses_header_and_api_failures_close_response(self):
        response = io.BytesIO(b"[]")
        response.headers = {}
        with patch.dict(os.environ, {"GITHUB_TOKEN": "test-private-token"}), \
                patch.object(resolver.urllib.request, "urlopen", return_value=response) as request:
            resolver.GitHub().releases("owner/repository")
        self.assertEqual("Bearer test-private-token", request.call_args.args[0].get_header("Authorization"))
        self.assertNotIn("test-private-token", request.call_args.args[0].full_url)
        error = urllib.error.HTTPError("https://api.github.com/example", 403, "limited", {}, io.BytesIO())
        with patch.object(resolver.urllib.request, "urlopen", side_effect=error):
            with self.assertRaises(resolver.ResolutionError) as caught:
                resolver.GitHub().releases("owner/repository")
        self.assertNotIn("test-private-token", str(caught.exception))
        self.assertTrue(error.closed)


class ReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.baseline = tempfile.TemporaryDirectory(prefix="ananarchiver-resolver-baseline-")
        fixture = cls(methodName="test_offline_replay_no_network_and_exact_revisions")
        fixture.root = Path(cls.baseline.name).resolve()
        fixture.create_fixture()
        cls.baseline_rows = fixture.rows

    @classmethod
    def tearDownClass(cls):
        cls.baseline.cleanup()

    def create_fixture(self):
        resolver.git("init", str(self.root))
        self.rows = []
        for name in resolver.CORE:
            path = self.root / "third_party" / name
            revision = self.make_repo(path)
            url = "https://github.com/" + resolver.UPSTREAMS[name] + ".git"
            resolver.git("remote", "add", "origin", url, cwd=path)
            self.rows.append({"name": name, "path": "third_party/" + name, "url": url,
                              "version": "1.2.3", "ref": "refs/tags/v1.2.3", "revision": revision,
                              "selection": "release", "metadata_source": "https://github.com/releases/v1.2.3"})
        self.write_modules(self.root, self.rows)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ananarchiver-resolver-")
        self.root = Path(self.temp.name).resolve()
        shutil.copytree(self.baseline.name, self.root, dirs_exist_ok=True)
        self.rows = copy.deepcopy(self.baseline_rows)
        self.manifest = {"schema_version": 1, "resolved_at": "2026-10-09T00:00:00+00:00", "includes_qt": False,
                         "dependencies": self.rows}
        self.stdout = contextlib.redirect_stdout(io.StringIO())
        self.stdout.__enter__()

    def tearDown(self):
        self.stdout.__exit__(None, None, None)
        self.temp.cleanup()

    def make_repo(self, path):
        resolver.git("init", str(path))
        (path / "source.txt").write_text("original\n", encoding="utf-8")
        if path.name == "libarchive":
            header = path / "libarchive/archive.h"
            header.parent.mkdir()
            header.write_text('#define ARCHIVE_VERSION_ONLY_STRING "1.2.3"\n', encoding="utf-8")
            resolver.git("add", "libarchive/archive.h", cwd=path)
        resolver.git("add", "source.txt", cwd=path)
        resolver.git("-c", "user.name=Resolver Tests", "-c", "user.email=resolver@example.invalid", "commit", "-m", "fixture", cwd=path)
        return resolver.git("rev-parse", "HEAD", cwd=path)

    def write_modules(self, parent, rows):
        text = ""
        for row in rows:
            relative = (self.root / row["path"]).relative_to(parent).as_posix()
            text += f'[submodule "{relative}"]\npath={relative}\nurl={row["url"]}\n'
            resolver.git("update-index", "--add", "--cacheinfo", "160000," + row["revision"] + "," + relative, cwd=parent)
        (parent / ".gitmodules").write_text(text, encoding="utf-8")
        resolver.git("add", ".gitmodules", cwd=parent)
        resolver.git("-c", "user.name=Resolver Tests", "-c", "user.email=resolver@example.invalid", "commit", "-m", "modules", cwd=parent)

    def add_framework(self):
        parent = self.root / "third_party/mbedtls"
        revision = self.make_repo(parent / "framework")
        url = "https://github.com/Mbed-TLS/mbedtls-framework.git"
        resolver.git("remote", "add", "origin", url, cwd=parent / "framework")
        row = {"name": "mbedtls/framework", "path": "third_party/mbedtls/framework", "url": url,
               "version": "git-" + revision[:12], "ref": revision, "revision": revision,
               "selection": "coupled-gitlink", "metadata_source": "https://github.com/parent/gitlink", "coupled_to": "mbedtls"}
        self.write_modules(parent, [row])
        self.rows[-1]["revision"] = resolver.git("rev-parse", "HEAD", cwd=parent)
        self.rows.append(row)
        return parent / "framework"

    def test_offline_replay_no_network_and_exact_revisions(self):
        with patch.object(resolver.GitHub, "request", side_effect=AssertionError("Network forbidden")):
            resolver.apply_manifest(self.manifest, self.root, offline=True)
        self.assertEqual([row["revision"] for row in self.rows],
                         [resolver.git("rev-parse", "HEAD", cwd=self.root / row["path"]) for row in self.rows])

    def test_tracked_dirty_refusal_before_any_checkout(self):
        (self.root / "third_party/zstd/source.txt").write_text("user change", encoding="utf-8")
        with patch.object(resolver, "initialize_checkout") as initialize, self.assertRaisesRegex(resolver.ResolutionError, "tracked changes"):
            resolver.apply_manifest(self.manifest, self.root, offline=True)
        initialize.assert_not_called()
        self.assertEqual("user change", (self.root / "third_party/zstd/source.txt").read_text())

    def test_untracked_refusal_even_when_not_checkout_conflict(self):
        (self.root / "third_party/lz4/personal-notes.txt").write_text("preserve", encoding="utf-8")
        with self.assertRaisesRegex(resolver.ResolutionError, "untracked files"):
            resolver.apply_manifest(self.manifest, self.root, offline=True)
        self.assertEqual("preserve", (self.root / "third_party/lz4/personal-notes.txt").read_text())

    def test_nested_dirty_checkout_refusal_before_parent_moves(self):
        nested = self.add_framework()
        (nested / "source.txt").write_text("nested user change", encoding="utf-8")
        before = resolver.git("rev-parse", "HEAD", cwd=nested.parent)
        with self.assertRaisesRegex(resolver.ResolutionError, "tracked changes"):
            resolver.apply_manifest(self.manifest, self.root, offline=True)
        self.assertEqual(before, resolver.git("rev-parse", "HEAD", cwd=nested.parent))

    def test_nested_offline_replay_and_tampered_revision_rejected(self):
        self.add_framework()
        resolver.apply_manifest(self.manifest, self.root, offline=True)
        invalid = copy.deepcopy(self.manifest)
        invalid["dependencies"][-1]["revision"] = self.rows[0]["revision"]
        with self.assertRaises(resolver.ResolutionError):
            resolver.apply_manifest(invalid, self.root, offline=True)

    def test_omitted_coupled_sources_rejected(self):
        self.add_framework()
        invalid = copy.deepcopy(self.manifest)
        invalid["dependencies"].pop()
        with self.assertRaisesRegex(resolver.ResolutionError, "omits or invents"):
            resolver.apply_manifest(invalid, self.root, offline=True)

    def test_missing_offline_object_fails_before_mutations(self):
        invalid = copy.deepcopy(self.manifest)
        invalid["dependencies"][-1]["revision"] = A
        with patch.object(resolver, "initialize_checkout") as initialize, self.assertRaisesRegex(resolver.ResolutionError, "locally available"):
            resolver.apply_manifest(invalid, self.root, offline=True)
        initialize.assert_not_called()

    def test_invalid_provenance_duplicate_and_path_escape(self):
        for field, value in (("path", "../outside"), ("url", "https://github.com/other/source.git"), ("revision", "bad")):
            invalid = copy.deepcopy(self.manifest)
            invalid["dependencies"][0][field] = value
            with self.assertRaises(resolver.ResolutionError):
                resolver.validate_manifest(invalid, self.root)
        invalid = copy.deepcopy(self.manifest)
        invalid["dependencies"].append(invalid["dependencies"][0])
        with self.assertRaisesRegex(resolver.ResolutionError, "Duplicate"):
            resolver.validate_manifest(invalid, self.root)

    def test_malformed_manifest_and_parent_rejected_cleanly(self):
        for invalid in ([], "bad", None, {"schema_version": 1}):
            with self.assertRaises(resolver.ResolutionError):
                resolver.validate_manifest(invalid, self.root)
        self.add_framework()
        invalid = copy.deepcopy(self.manifest)
        invalid["dependencies"][-1]["coupled_to"] = []
        with self.assertRaisesRegex(resolver.ResolutionError, "Invalid coupled source parent"):
            resolver.validate_manifest(invalid, self.root)

    def test_atomic_manifest_and_failed_replace_preserve_previous_record(self):
        output = self.root / "records/resolved.json"
        resolver.atomic_write_manifest(output, self.manifest)
        original = output.read_bytes()
        with patch.object(resolver.os, "replace", side_effect=OSError("simulated disk failure")), self.assertRaises(OSError):
            resolver.atomic_write_manifest(output, {"schema_version": 999})
        self.assertEqual(original, output.read_bytes())
        self.assertEqual([], list(output.parent.glob("*.tmp")))

    def test_cli_offline_replay_preserves_resolution_record(self):
        input_path, output_path = self.root / "input.json", self.root / "output.json"
        resolver.atomic_write_manifest(input_path, self.manifest)
        self.assertEqual(0, resolver.main(["--root", str(self.root), "--offline", "--replay-manifest", str(input_path), "--manifest", str(output_path)]))
        self.assertEqual(self.manifest, json.loads(output_path.read_text(encoding="utf-8")))

    def test_reinitialize_deactivated_submodule_from_existing_git_objects(self):
        path = self.root / "third_party/zlib"
        resolver.git("submodule", "absorbgitdirs", "--", "third_party/zlib", cwd=self.root)
        resolver.git("submodule", "deinit", "--", "third_party/zlib", cwd=self.root)
        self.assertFalse(resolver.repository_present(path))
        original_git = resolver.git
        def without_network(*args, **kwargs):
            self.assertNotEqual("fetch", args[0], "Existing objects must avoid fetch")
            return original_git(*args, **kwargs)
        with patch.object(resolver, "git", side_effect=without_network):
            resolver.apply_manifest(self.manifest, self.root)
        self.assertEqual(self.rows[1]["revision"], resolver.git("rev-parse", "HEAD", cwd=path))
        self.assertTrue((path / ".git").is_file())
        self.assertEqual("original\n", (path / "source.txt").read_text(encoding="utf-8"))

    def test_core_only_replay_preserves_complete_qt_manifest(self):
        qt = {"name": "qt", "path": "third_party/qt", "url": "https://github.com/qt/qt5.git",
              "version": "6.12.0", "ref": "refs/tags/v6.12.0", "revision": A,
              "selection": "stable-tag", "metadata_source": "https://github.com/qt/qt5/tags"}
        vulkan = {**qt, "name": "vulkan-headers", "path": "third_party/vulkan-headers",
                  "url": "https://github.com/KhronosGroup/Vulkan-Headers.git", "version": "1.4.1", "revision": C}
        self.rows.append(qt)
        for name in resolver.QT_MODULES:
            self.rows.append({**qt, "name": "qt/" + name, "path": "third_party/qt/" + name,
                              "url": "https://github.com/qt/" + name + ".git", "revision": B,
                              "ref": B, "version": "git-" + B[:12],
                              "selection": "coupled-gitlink", "coupled_to": "qt"})
        self.rows.append(vulkan)
        self.write_modules(self.root, [row for row in self.rows if not row.get("coupled_to")])
        self.manifest["includes_qt"] = True
        source, output = self.root / "full.json", self.root / "replayed.json"
        resolver.atomic_write_manifest(source, self.manifest)
        self.assertEqual(0, resolver.main(["--root", str(self.root), "--offline", "--core-only",
                                          "--replay-manifest", str(source), "--manifest", str(output)]))
        self.assertEqual(self.manifest, json.loads(output.read_text(encoding="utf-8")))
        self.assertFalse((self.root / "third_party/qt/.git").exists())

    def test_verify_is_read_only_offline_and_never_writes_output(self):
        source, output = self.root / "full.json", self.root / "must-not-exist.json"
        resolver.atomic_write_manifest(source, self.manifest)
        original_git = resolver.git

        def reads_only(*args, **kwargs):
            self.assertIn(args[0], ("rev-parse", "remote", "status", "show", "ls-tree"))
            if args[0] == "remote":
                self.assertEqual("get-url", args[1])
            return original_git(*args, **kwargs)

        with patch.object(resolver.GitHub, "request", side_effect=AssertionError("Network forbidden")), \
                patch.object(resolver, "git", side_effect=reads_only), \
                patch.object(resolver, "atomic_write_manifest", side_effect=AssertionError("Write forbidden")):
            self.assertEqual(0, resolver.main(["--root", str(self.root), "--verify", "--core-only",
                                              "--replay-manifest", str(source), "--manifest", str(output)]))
        self.assertFalse(output.exists())

    def test_verify_rejects_forged_versions_and_dirty_sources(self):
        invalid = copy.deepcopy(self.manifest)
        invalid["dependencies"][0]["version"] = "999.0.0"
        with self.assertRaisesRegex(resolver.ResolutionError, "version differs"):
            resolver.verify_manifest(invalid, self.root)
        self.add_framework()
        resolver.verify_manifest(self.manifest, self.root)
        invalid = copy.deepcopy(self.manifest)
        invalid["dependencies"][-1]["version"] = "git-" + A[:12]
        with self.assertRaisesRegex(resolver.ResolutionError, "version differs"):
            resolver.verify_manifest(invalid, self.root)
        (self.root / "third_party/mbedtls/framework/source.txt").write_text("local dirty\n")
        with self.assertRaisesRegex(resolver.ResolutionError, "tracked changes"):
            resolver.verify_manifest(self.manifest, self.root)

    def test_unpublished_commit_is_protected_even_with_local_tag(self):
        source = self.root / "third_party/zlib"
        (source / "source.txt").write_text("unpublished\n")
        resolver.git("add", "source.txt", cwd=source)
        resolver.git("-c", "user.name=Resolver Tests", "-c", "user.email=resolver@example.invalid", "commit", "-m", "unpublished", cwd=source)
        current = resolver.git("rev-parse", "HEAD", cwd=source)
        resolver.git("tag", "v100.0.0", cwd=source)
        with patch.object(resolver, "initialize_checkout") as initialize, self.assertRaisesRegex(resolver.ResolutionError, "unrecorded local commit"):
            resolver.apply_manifest(self.manifest, self.root)
        initialize.assert_not_called()
        self.assertEqual(current, resolver.git("rev-parse", "HEAD", cwd=source))

    def test_interrupted_parent_child_update_can_resume(self):
        child = self.add_framework()
        resolver.apply_manifest(self.manifest, self.root)
        old = resolver.git("rev-parse", "HEAD", cwd=child)
        (child / "source.txt").write_text("new paired framework\n")
        resolver.git("add", "source.txt", cwd=child)
        resolver.git("-c", "user.name=Resolver Tests", "-c", "user.email=resolver@example.invalid", "commit", "-m", "new framework", cwd=child)
        new = resolver.git("rev-parse", "HEAD", cwd=child)
        resolver.git("checkout", "--detach", old, cwd=child)
        parent = child.parent
        resolver.git("update-index", "--cacheinfo", "160000," + new + ",framework", cwd=parent)
        resolver.git("-c", "user.name=Resolver Tests", "-c", "user.email=resolver@example.invalid", "commit", "-m", "new parent pairing", cwd=parent)
        self.rows[-2]["revision"] = resolver.git("rev-parse", "HEAD", cwd=parent)
        self.rows[-1].update(revision=new, ref=new, version="git-" + new[:12])
        original_initialize = resolver.initialize_checkout

        def interrupt_child(root, row, rows):
            if row.get("coupled_to"):
                raise resolver.ResolutionError("fixture child fetch interrupted")
            return original_initialize(root, row, rows)

        with patch.object(resolver, "initialize_checkout", side_effect=interrupt_child):
            with self.assertRaisesRegex(resolver.ResolutionError, "interrupted"):
                resolver.apply_manifest(self.manifest, self.root)
        resolver.apply_manifest(self.manifest, self.root)
        resolver.verify_manifest(self.manifest, self.root)
        self.assertEqual(new, resolver.git("rev-parse", "HEAD", cwd=child))

    def test_fresh_init_bypasses_unfetchable_parent_gitlink(self):
        source = self.root / "third_party/zlib"
        mirror = self.root / "out/zlib-upstream.git"
        resolver.git("clone", "--bare", str(source), str(mirror))
        self.assertTrue(source.resolve().is_relative_to(self.root))
        previous = self.root / "out/zlib-previous-checkout"
        self.assertTrue(previous.resolve().is_relative_to(self.root))
        source.rename(previous)
        resolver.git("update-index", "--cacheinfo", "160000," + A + ",third_party/zlib", cwd=self.root)
        resolver.git("-c", "user.name=Resolver Tests", "-c", "user.email=resolver@example.invalid", "commit", "-m", "unavailable old gitlink", cwd=self.root)
        original_git = resolver.git
        fetched = []

        def local_upstream(*args, **kwargs):
            if args[0] == "fetch":
                fetched.append(args[-1])
                args = tuple(str(mirror) if value == self.rows[1]["url"] else value for value in args)
            return original_git(*args, **kwargs)

        with patch.object(resolver, "git", side_effect=local_upstream):
            resolver.apply_manifest(self.manifest, self.root)
        self.assertEqual([self.rows[1]["revision"]], fetched)
        self.assertTrue((source / ".git").is_file())
        resolver.verify_manifest(self.manifest, self.root)

    def test_invalid_manifest_shapes_and_timestamps_are_reported_without_traceback(self):
        source = self.root / "invalid.json"
        invalid_time = {**self.manifest, "resolved_at": "yesterday"}
        invalid_bool = {**self.manifest, "schema_version": True}
        for invalid in ([], None, invalid_time, invalid_bool):
            source.write_text(json.dumps(invalid), encoding="utf-8")
            diagnostics = io.StringIO()
            with contextlib.redirect_stderr(diagnostics):
                self.assertEqual(1, resolver.main(["--root", str(self.root), "--verify", "--replay-manifest", str(source)]))
            self.assertIn("Dependency resolution failed:", diagnostics.getvalue())
            self.assertNotIn("Traceback", diagnostics.getvalue())


class FreshReplayTests(unittest.TestCase):
    def test_entirely_fresh_three_nested_levels_replay_and_read_only_verify(self):
        with tempfile.TemporaryDirectory(prefix="ananarchiver-fresh-nested-") as temporary:
            base = Path(temporary).resolve()
            root = base / "application"
            rows, upstreams = [], {}
            # Build upward from the leaf: Mbed TLS -> PSA -> framework -> vendor.
            definitions = [
                ("mbedtls/tf-psa-crypto/framework/vendor", "https://github.com/example/vendor.git", None),
                ("mbedtls/tf-psa-crypto/framework", "https://github.com/Mbed-TLS/mbedtls-framework.git", "vendor"),
                ("mbedtls/tf-psa-crypto", "https://github.com/Mbed-TLS/TF-PSA-Crypto.git", "framework"),
                ("mbedtls", "https://github.com/Mbed-TLS/mbedtls.git", "tf-psa-crypto"),
            ]
            for name, url, child_path in definitions:
                checkout = base / "upstreams" / name.replace("/", "-")
                resolver.git("init", str(checkout))
                (checkout / "payload.txt").write_text(name + "\n", encoding="utf-8")
                resolver.git("add", "payload.txt", cwd=checkout)
                if name == "mbedtls/tf-psa-crypto":
                    header = checkout / "include/tf-psa-crypto/build_info.h"
                    header.parent.mkdir(parents=True)
                    header.write_text('#define TF_PSA_CRYPTO_VERSION_STRING "1.2.0"\n', encoding="utf-8")
                    resolver.git("add", "include/tf-psa-crypto/build_info.h", cwd=checkout)
                if child_path:
                    child = rows[-1]
                    (checkout / ".gitmodules").write_text(
                        f'[submodule "{child_path}"]\npath={child_path}\nurl={child["url"]}\n', encoding="utf-8")
                    resolver.git("add", ".gitmodules", cwd=checkout)
                    resolver.git("update-index", "--add", "--cacheinfo", f"160000,{child['revision']},{child_path}", cwd=checkout)
                resolver.git("-c", "user.name=Resolver Tests", "-c", "user.email=resolver@example.invalid", "commit", "-m", "matched source fixture", cwd=checkout)
                revision = resolver.git("rev-parse", "HEAD", cwd=checkout)
                row = {"name": name, "path": "third_party/" + name, "url": url,
                       "version": "git-" + revision[:12], "ref": revision, "revision": revision,
                       "selection": "coupled-gitlink", "metadata_source": "https://github.com/fixture/gitlink"}
                if name == "mbedtls":
                    row.update(version="4.2.0", ref="refs/tags/mbedtls-4.2.0", selection="release")
                else:
                    row["coupled_to"] = name.rsplit("/", 1)[0]
                if name == "mbedtls/tf-psa-crypto":
                    row["version"] = "1.2.0"
                rows.append(row)
                upstreams[url] = checkout
            rows.reverse()
            resolver.git("init", str(root))
            (root / ".gitmodules").write_text(
                '[submodule "mbedtls"]\npath=third_party/mbedtls\nurl=' + rows[0]["url"] + "\n", encoding="utf-8")
            resolver.git("add", ".gitmodules", cwd=root)
            # The application's historical gitlink is deliberately unavailable.
            resolver.git("update-index", "--add", "--cacheinfo", f"160000,{A},third_party/mbedtls", cwd=root)
            resolver.git("-c", "user.name=Resolver Tests", "-c", "user.email=resolver@example.invalid", "commit", "-m", "historical unavailable gitlink", cwd=root)
            manifest = {"schema_version": 1, "resolved_at": NOW, "includes_qt": False, "dependencies": rows}
            source, output = base / "input.json", root / "out/resolved.json"
            resolver.atomic_write_manifest(source, manifest)
            for row in rows:
                self.assertFalse(resolver.repository_present(root / row["path"]))
            self.assertFalse((root / "third_party/mbedtls/tf-psa-crypto").exists())
            original_git, fetched = resolver.git, []

            def local_upstreams(*args, **kwargs):
                if args[0] == "fetch":
                    fetched.append(args[-1])
                    args = tuple(str(upstreams[value]) if value in upstreams else value for value in args)
                return original_git(*args, **kwargs)

            diagnostics = io.StringIO()
            with patch.object(resolver, "CORE", ("mbedtls",)), \
                    patch.object(resolver, "git", side_effect=local_upstreams), \
                    patch.object(resolver.GitHub, "request", side_effect=AssertionError("API forbidden during replay")), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(diagnostics):
                result = resolver.main(["--root", str(root), "--replay-manifest", str(source), "--manifest", str(output)])
                self.assertEqual(0, result, diagnostics.getvalue())
                original_record = output.read_bytes()
                with patch.object(resolver, "atomic_write_manifest", side_effect=AssertionError("Verify must not write")):
                    result = resolver.main(["--root", str(root), "--core-only", "--verify", "--replay-manifest", str(output)])
                self.assertEqual(0, result, diagnostics.getvalue())
                self.assertEqual(original_record, output.read_bytes())
            self.assertEqual([row["revision"] for row in rows], fetched)
            self.assertNotIn(A, fetched)
            for row in rows:
                self.assertEqual(row["revision"], resolver.git("rev-parse", "HEAD", cwd=root / row["path"]))
                self.assertTrue((root / row["path"] / ".git").is_file())


if __name__ == "__main__":
    unittest.main()
