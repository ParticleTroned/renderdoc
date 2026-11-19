# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 ParticleTroned

"""Exercise fork identities and release decisions against real temporary histories."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

import fork_version as version


class ForkVersionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.git("init", "-b", "dev")
        self.git("config", "user.name", "Version Test")
        self.git("config", "user.email", "version-test@example.invalid")
        self.header = self.root / "renderdoc/api/replay/version.h"
        self.header.parent.mkdir(parents=True)
        self.header.write_text("#define RENDERDOC_VERSION_MAJOR 1\n#define RENDERDOC_VERSION_MINOR 47\n")
        self.commit("chore: initial upstream baseline")

    def git(self, *arguments):
        return version.git(self.root, *arguments)

    def commit(self, message):
        self.git("add", "--all")
        self.git("commit", "--allow-empty", "-m", message)

    def tag(self, value="1.2.3"):
        self.git("tag", "pt-v" + value)

    def test_tagged_release_preserves_both_versions(self):
        self.tag()
        result = version.metadata(self.root, expected_tag="pt-v1.2.3")
        self.assertEqual(result["version"], "1.47+pt.1.2.3")
        self.assertEqual(result["source"], self.git("rev-parse", "HEAD"))

    def test_development_and_dirty_identity(self):
        self.tag()
        self.commit("fix: next correction")
        expected = "1.47+pt.1.2.3.dev.1." + self.git("rev-parse", "HEAD")[:12]
        self.assertEqual(version.metadata(self.root)["version"], expected)
        self.header.write_text(self.header.read_text() + "\n")
        self.assertEqual(version.metadata(self.root)["version"], expected + ".dirty")

    def test_annotated_fork_tags_ignore_other_release_namespaces(self):
        self.git("tag", "-a", "pt-v1.2.3", "-m", "Fork release")
        self.commit("fix: later change")
        self.git("tag", "v1.47")
        self.git("tag", "v1.47-alandtse.99.0.0")
        self.assertEqual(version.metadata(self.root)["fork"], "1.2.3")
        self.assertEqual(version.next_release(self.root)["tag"], "pt-v1.2.4")

    def test_upstream_version_does_not_reset_fork(self):
        self.tag()
        self.header.write_text(self.header.read_text().replace("MINOR 47", "MINOR 48"))
        self.commit("chore(upstream-sync): merge upstream")
        result = version.metadata(self.root)
        self.assertTrue(result["version"].startswith("1.48+pt.1.2.3.dev.1."))
        self.assertFalse(version.next_release(self.root)["released"])

    def test_tags_on_merged_side_branches_do_not_replace_fork_identity(self):
        self.tag()
        self.git("checkout", "-b", "upstream")
        self.commit("fix: side branch change")
        self.tag("9.0.0")
        self.git("checkout", "dev")
        self.commit("docs: retain first-parent history")
        self.git("merge", "--no-ff", "upstream", "-m", "chore(upstream-sync): merge upstream")
        self.assertEqual(version.metadata(self.root)["fork"], "1.2.3")

    def test_bootstrap_requires_explicit_unreleased_opt_in(self):
        with self.assertRaisesRegex(ValueError, "No reachable"):
            version.metadata(self.root)
        self.assertEqual(version.metadata(self.root, True)["version"],
                         "1.47+pt.0.0.0.dev." + self.git("rev-parse", "HEAD")[:12])

    def test_unreachable_fork_tags_cannot_be_hidden_by_bootstrap(self):
        self.git("checkout", "-b", "side")
        self.commit("fix: side release")
        self.tag()
        self.git("checkout", "dev")
        with self.assertRaisesRegex(ValueError, "No reachable"):
            version.metadata(self.root, True)

    def test_malformed_tags_and_upstream_versions_fail(self):
        self.git("tag", "pt-v1.2.3-beta")
        with self.assertRaisesRegex(ValueError, "Invalid fork release tag"):
            version.metadata(self.root)
        self.header.write_text(self.header.read_text() + "#define RENDERDOC_VERSION_MAJOR 2\n")
        with self.assertRaisesRegex(ValueError, "Expected one upstream"):
            version.upstream_version(self.root)

    def test_release_requires_exact_clean_tag(self):
        self.tag()
        with self.assertRaisesRegex(ValueError, "exact clean fork tag"):
            version.metadata(self.root, expected_tag="pt-v9.0.0")
        self.header.write_text(self.header.read_text() + "\n")
        with self.assertRaisesRegex(ValueError, "exact clean fork tag"):
            version.metadata(self.root, expected_tag="pt-v1.2.3")
        self.commit("fix: later change")
        with self.assertRaisesRegex(ValueError, "exact clean fork tag"):
            version.metadata(self.root, expected_tag="pt-v1.2.3")

    def test_initial_merge_release_only_analyzes_new_changes(self):
        self.git("checkout", "-b", "feature")
        self.commit("fix(d3d11): restore state")
        self.commit("build(release): add independent versioning")
        self.git("checkout", "dev")
        self.git("merge", "--no-ff", "feature", "-m", "Merge pull request #1")
        result = version.next_release(self.root)
        self.assertEqual(result["tag"], "pt-v1.0.0")
        self.assertNotIn("chore: initial upstream baseline", result["changes"])

    def test_semantic_bumps(self):
        for message, expected in (("fix: correction", "1.2.4"),
                                  ("feat: capability", "1.3.0"),
                                  ("feat!: replace ABI", "2.0.0"),
                                  ("fix: ABI\n\nBREAKING CHANGE: incompatible", "2.0.0")):
            with self.subTest(message=message):
                self.tag()
                self.commit(message)
                self.assertEqual(version.next_release(self.root)["fork"], expected)
                self.git("tag", "-d", "pt-v1.2.3")

    def test_first_release_survives_a_merge_burst_ending_in_build_changes(self):
        self.commit("fix(d3d11): restore state (#1)")
        self.commit("build(d3d11): export compatibility (#2)")
        self.assertEqual(version.next_release(self.root)["tag"], "pt-v1.0.0")

    def test_build_only_release_is_explicit_and_cannot_lower_required_bump(self):
        self.tag()
        self.assertFalse(version.next_release(self.root, "patch")["released"])
        self.commit("build(d3d11): export compatibility")
        self.assertFalse(version.next_release(self.root)["released"])
        self.assertEqual(version.next_release(self.root, "patch")["tag"], "pt-v1.2.4")
        self.commit("feat: new capability")
        self.assertEqual(version.next_release(self.root, "patch")["tag"], "pt-v1.3.0")

    def test_release_does_not_reuse_a_tag_from_another_history(self):
        self.tag()
        self.git("checkout", "-b", "side")
        self.commit("fix: other release")
        self.tag("1.2.4")
        self.git("checkout", "dev")
        self.commit("fix: this release")
        with self.assertRaisesRegex(ValueError, "already exists"):
            version.next_release(self.root)

    def test_msbuild_metadata_reaches_compiler_and_resources(self):
        self.tag()
        identity = version.metadata(self.root)
        path = self.root / "Directory.Build.props"
        version.write_props(path, identity)
        stamp = path.stat().st_mtime_ns
        root = ET.parse(path).getroot()
        namespace = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
        for tool in ("ClCompile", "ResourceCompile"):
            definitions = root.find("m:ItemDefinitionGroup/m:" + tool + "/m:PreprocessorDefinitions", namespace).text
            quote = r'\"' if tool == "ResourceCompile" else '"'
            self.assertIn('DISTRIBUTION_VERSION=' + quote + '1.47+pt.1.2.3' + quote, definitions)
            self.assertIn('DISTRIBUTION_NAME=' + quote + 'ParticleTroned/renderdoc' + quote, definitions)
            self.assertIn("%(PreprocessorDefinitions)", definitions)
        version.write_props(path, identity)
        self.assertEqual(path.stat().st_mtime_ns, stamp)

    def test_preserves_unmanaged_props_and_rejects_distribution_injection(self):
        path = self.root / "Directory.Build.props"
        path.write_text("user configuration")
        with self.assertRaisesRegex(ValueError, "unmanaged"):
            version.write_props(path, version.metadata(self.root, True))
        self.assertEqual(path.read_text(), "user configuration")
        with self.assertRaisesRegex(ValueError, "owner/repository"):
            version.metadata(self.root, True, distribution='owner/repo";INJECT=1')

    def test_cli_outputs_exact_runtime_and_source_without_publishing(self):
        self.commit("fix: initial release")
        output = self.root / "github output.txt"
        notes = self.root / "release notes.md"
        result = subprocess.run([sys.executable, str(Path(version.__file__).resolve()), "release",
                                 "--repository", str(self.root), "--github-output", str(output),
                                 "--notes", str(notes)], capture_output=True, text=True, check=True)
        self.assertEqual(json.loads(result.stdout)["tag"], "pt-v1.0.0")
        self.assertIn("version=1.47+pt.1.0.0\n", output.read_text())
        self.assertIn("source=" + self.git("rev-parse", "HEAD"), output.read_text())
        self.assertIn("Runtime version: `1.47+pt.1.0.0`.", notes.read_text())
        self.assertEqual(self.git("tag", "--list"), "")


if __name__ == "__main__":
    unittest.main()
