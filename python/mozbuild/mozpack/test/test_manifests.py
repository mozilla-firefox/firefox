# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

import mozunit

from mozpack.copier import FileCopier, FileRegistry
from mozpack.manifests import InstallManifest, UnreadableInstallManifest
from mozpack.test.test_files import TestWithTmpDir


class TestInstallManifest(TestWithTmpDir):
    def test_construct(self):
        m = InstallManifest()
        self.assertEqual(len(m), 0)

    def test_malformed(self):
        f = self.tmppath("manifest")
        open(f, "w").write("junk\n")
        with self.assertRaises(UnreadableInstallManifest):
            InstallManifest(f)

    def test_adds(self):
        m = InstallManifest()
        m.add_link("s_source", "s_dest")
        m.add_copy("c_source", "c_dest")
        m.add_required_exists("e_dest")
        m.add_optional_exists("o_dest")
        m.add_pattern_link("ps_base", "ps/*", "ps_dest")
        m.add_pattern_copy("pc_base", "pc/**", "pc_dest")
        m.add_preprocess("p_source", "p_dest", "p_source.pp")
        m.add_content("content", "content")

        self.assertEqual(len(m), 8)
        self.assertIn("s_dest", m)
        self.assertIn("c_dest", m)
        self.assertIn("p_dest", m)
        self.assertIn("e_dest", m)
        self.assertIn("o_dest", m)
        self.assertIn("content", m)

        with self.assertRaises(ValueError):
            m.add_link("s_other", "s_dest")

        with self.assertRaises(ValueError):
            m.add_copy("c_other", "c_dest")

        with self.assertRaises(ValueError):
            m.add_preprocess("p_other", "p_dest", "p_other.pp")

        with self.assertRaises(ValueError):
            m.add_required_exists("e_dest")

        with self.assertRaises(ValueError):
            m.add_optional_exists("o_dest")

        with self.assertRaises(ValueError):
            m.add_pattern_link("ps_base", "ps/*", "ps_dest")

        with self.assertRaises(ValueError):
            m.add_pattern_copy("pc_base", "pc/**", "pc_dest")

        with self.assertRaises(ValueError):
            m.add_content("content", "content")

    def _get_test_manifest(self):
        m = InstallManifest()
        m.add_link(self.tmppath("s_source"), "s_dest")
        m.add_copy(self.tmppath("c_source"), "c_dest")
        m.add_preprocess(
            self.tmppath("p_source"),
            "p_dest",
            self.tmppath("p_source.pp"),
            "#",
            {"FOO": "BAR", "BAZ": "QUX"},
        )
        m.add_required_exists("e_dest")
        m.add_optional_exists("o_dest")
        m.add_pattern_link("ps_base", "*", "ps_dest")
        m.add_pattern_copy("pc_base", "**", "pc_dest")
        m.add_content("the content\non\nmultiple lines", "content")

        return m

    def test_serialization(self):
        m = self._get_test_manifest()

        p = self.tmppath("m")
        m.write(path=p)
        self.assertTrue(os.path.isfile(p))

        with open(p) as fh:
            c = fh.read()

        self.assertEqual(c.count("\n"), 9)

        lines = c.splitlines()
        self.assertEqual(len(lines), 9)

        self.assertEqual(lines[0], "5")

        m2 = InstallManifest(path=p)
        self.assertEqual(m, m2)
        p2 = self.tmppath("m2")
        m2.write(path=p2)

        with open(p2) as fh:
            c2 = fh.read()

        self.assertEqual(c, c2)

    def test_write_leaves_an_unchanged_file_alone(self):
        m = self._get_test_manifest()

        p = self.tmppath("m")
        m.write(path=p)
        first = os.stat(p).st_mtime_ns

        m.write(path=p)

        self.assertEqual(os.stat(p).st_mtime_ns, first)

    def test_write_updates_a_changed_file(self):
        m = self._get_test_manifest()

        p = self.tmppath("m")
        m.write(path=p)
        with open(p) as fh:
            before = fh.read()

        m.add_link(self.tmppath("s_extra"), "dest_extra")
        m.write(path=p)

        with open(p) as fh:
            after = fh.read()

        self.assertNotEqual(before, after)
        self.assertEqual(InstallManifest(path=p), m)

    def test_populate_registry(self):
        m = self._get_test_manifest()
        r = FileRegistry()
        m.populate_registry(r)

        self.assertEqual(len(r), 6)
        self.assertEqual(
            r.paths(), ["c_dest", "content", "e_dest", "o_dest", "p_dest", "s_dest"]
        )

    def test_pattern_expansion(self):
        source = self.tmppath("source")
        os.mkdir(source)
        os.mkdir("%s/base" % source)
        os.mkdir("%s/base/foo" % source)

        with open("%s/base/foo/file1" % source, "a"):
            pass

        with open("%s/base/foo/file2" % source, "a"):
            pass

        m = InstallManifest()
        m.add_pattern_link("%s/base" % source, "**", "dest")

        c = FileCopier()
        m.populate_registry(c)
        self.assertEqual(c.paths(), ["dest/foo/file1", "dest/foo/file2"])

    def test_write_expand_pattern(self):
        source = self.tmppath("source")
        os.mkdir(source)
        os.mkdir("%s/base" % source)
        os.mkdir("%s/base/foo" % source)

        with open("%s/base/foo/file1" % source, "a"):
            pass

        with open("%s/base/foo/file2" % source, "a"):
            pass

        m = InstallManifest()
        m.add_pattern_link("%s/base" % source, "**", "dest")

        track = self.tmppath("track")
        m.write(path=track, expand_pattern=True)

        m = InstallManifest(path=track)
        self.assertEqual(
            sorted(dest for dest in m._dests), ["dest/foo/file1", "dest/foo/file2"]
        )

    def test_write_expand_pattern_is_deterministic(self):
        source = self.tmppath("source")
        os.makedirs(f"{source}/base/foo")
        for name in ("file2", "file1", "file3"):
            with open(f"{source}/base/foo/{name}", "a"):
                pass

        m = InstallManifest()
        m.add_pattern_link(f"{source}/base", "**", "dest")

        first = self.tmppath("track1")
        second = self.tmppath("track2")
        m.write(path=first, expand_pattern=True)
        m.write(path=second, expand_pattern=True)

        with open(first) as fh:
            with open(second) as fh2:
                self.assertEqual(fh.read(), fh2.read())

    def test_write_expand_pattern_leaves_an_unchanged_file_list_alone(self):
        source = self.tmppath("source")
        os.makedirs(f"{source}/base/foo")
        with open(f"{source}/base/foo/file1", "a"):
            pass

        m = InstallManifest()
        m.add_pattern_link(f"{source}/base", "**", "dest")

        track = self.tmppath("track")
        m.write(path=track, expand_pattern=True)
        unchanged = os.stat(track).st_mtime_ns

        m.write(path=track, expand_pattern=True)
        self.assertEqual(os.stat(track).st_mtime_ns, unchanged)

        with open(f"{source}/base/foo/file2", "a"):
            pass
        m.write(path=track, expand_pattern=True)

        added = InstallManifest(path=track)
        self.assertEqual(sorted(added._dests), ["dest/foo/file1", "dest/foo/file2"])

        os.remove(f"{source}/base/foo/file2")
        m.write(path=track, expand_pattern=True)

        removed = InstallManifest(path=track)
        self.assertEqual(sorted(removed._dests), ["dest/foo/file1"])

    def test_or(self):
        m1 = self._get_test_manifest()
        orig_length = len(m1)
        m2 = InstallManifest()
        m2.add_link("s_source2", "s_dest2")
        m2.add_copy("c_source2", "c_dest2")

        m1 |= m2

        self.assertEqual(len(m2), 2)
        self.assertEqual(len(m1), orig_length + 2)

        self.assertIn("s_dest2", m1)
        self.assertIn("c_dest2", m1)

    def test_copier_application(self):
        dest = self.tmppath("dest")
        os.mkdir(dest)

        to_delete = self.tmppath("dest/to_delete")
        with open(to_delete, "a"):
            pass

        with open(self.tmppath("s_source"), "w") as fh:
            fh.write("symlink!")

        with open(self.tmppath("c_source"), "w") as fh:
            fh.write("copy!")

        with open(self.tmppath("p_source"), "w") as fh:
            fh.write("#define FOO 1\npreprocess!")

        with open(self.tmppath("dest/e_dest"), "a"):
            pass

        with open(self.tmppath("dest/o_dest"), "a"):
            pass

        m = self._get_test_manifest()
        c = FileCopier()
        m.populate_registry(c)
        result = c.copy(dest)

        self.assertTrue(os.path.exists(self.tmppath("dest/s_dest")))
        self.assertTrue(os.path.exists(self.tmppath("dest/c_dest")))
        self.assertTrue(os.path.exists(self.tmppath("dest/p_dest")))
        self.assertTrue(os.path.exists(self.tmppath("dest/e_dest")))
        self.assertTrue(os.path.exists(self.tmppath("dest/o_dest")))
        self.assertTrue(os.path.exists(self.tmppath("dest/content")))
        self.assertFalse(os.path.exists(to_delete))

        with open(self.tmppath("dest/s_dest")) as fh:
            self.assertEqual(fh.read(), "symlink!")

        with open(self.tmppath("dest/c_dest")) as fh:
            self.assertEqual(fh.read(), "copy!")

        with open(self.tmppath("dest/p_dest")) as fh:
            self.assertEqual(fh.read(), "preprocess!")

        self.assertEqual(
            result.updated_files,
            set(
                self.tmppath(p)
                for p in ("dest/s_dest", "dest/c_dest", "dest/p_dest", "dest/content")
            ),
        )
        self.assertEqual(
            result.existing_files,
            set([self.tmppath("dest/e_dest"), self.tmppath("dest/o_dest")]),
        )
        self.assertEqual(result.removed_files, {to_delete})
        self.assertEqual(result.removed_directories, set())

    def test_preprocessor(self):
        manifest = self.tmppath("m")
        deps = self.tmppath("m.pp")
        dest = self.tmppath("dest")
        include = self.tmppath("p_incl")

        with open(include, "w") as fh:
            fh.write("#define INCL\n")
        time = os.path.getmtime(include) - 3
        os.utime(include, (time, time))

        with open(self.tmppath("p_source"), "w") as fh:
            fh.write("#ifdef FOO\n#if BAZ == QUX\nPASS1\n#endif\n#endif\n")
            fh.write("#ifdef DEPTEST\nPASS2\n#endif\n")
            fh.write("#include p_incl\n#ifdef INCLTEST\nPASS3\n#endif\n")
        time = os.path.getmtime(self.tmppath("p_source")) - 3
        os.utime(self.tmppath("p_source"), (time, time))

        # Create and write a manifest with the preprocessed file, then apply it.
        # This should write out our preprocessed file.
        m = InstallManifest()
        m.add_preprocess(
            self.tmppath("p_source"), "p_dest", deps, "#", {"FOO": "BAR", "BAZ": "QUX"}
        )
        m.write(path=manifest)

        m = InstallManifest(path=manifest)
        c = FileCopier()
        m.populate_registry(c)
        c.copy(dest)

        self.assertTrue(os.path.exists(self.tmppath("dest/p_dest")))

        with open(self.tmppath("dest/p_dest")) as fh:
            self.assertEqual(fh.read(), "PASS1\n")

        # Create a second manifest with the preprocessed file, then apply it.
        # Since this manifest does not exist on the disk, there should not be a
        # dependency on it, and the preprocessed file should not be modified.
        m2 = InstallManifest()
        m2.add_preprocess(
            self.tmppath("p_source"), "p_dest", deps, "#", {"DEPTEST": True}
        )
        c = FileCopier()
        m2.populate_registry(c)
        result = c.copy(dest)

        self.assertFalse(self.tmppath("dest/p_dest") in result.updated_files)
        self.assertTrue(self.tmppath("dest/p_dest") in result.existing_files)

        # Write out the second manifest, then load it back in from the disk.
        # This should add the dependency on the manifest file, so our
        # preprocessed file should be regenerated with the new defines.
        # We also set the mtime on the destination file back, so it will be
        # older than the manifest file.
        m2.write(path=manifest)
        time = os.path.getmtime(manifest) - 1
        os.utime(self.tmppath("dest/p_dest"), (time, time))
        m2 = InstallManifest(path=manifest)
        c = FileCopier()
        m2.populate_registry(c)
        self.assertTrue(c.copy(dest))

        with open(self.tmppath("dest/p_dest")) as fh:
            self.assertEqual(fh.read(), "PASS2\n")

        # Set the time on the manifest back, so it won't be picked up as
        # modified in the next test
        time = os.path.getmtime(manifest) - 1
        os.utime(manifest, (time, time))

        # Update the contents of a file included by the source file. This should
        # cause the destination to be regenerated.
        with open(include, "w") as fh:
            fh.write("#define INCLTEST\n")

        time = os.path.getmtime(include) - 1
        os.utime(self.tmppath("dest/p_dest"), (time, time))
        c = FileCopier()
        m2.populate_registry(c)
        self.assertTrue(c.copy(dest))

        with open(self.tmppath("dest/p_dest")) as fh:
            self.assertEqual(fh.read(), "PASS2\nPASS3\n")

    def test_preprocessor_dependencies(self):
        manifest = self.tmppath("m")
        deps = self.tmppath("m.pp")
        dest = self.tmppath("dest")
        source = self.tmppath("p_source")
        destfile = self.tmppath("dest/p_dest")
        include = self.tmppath("p_incl")
        os.mkdir(dest)

        with open(source, "w") as fh:
            fh.write("#define SRC\nSOURCE\n")
        time = os.path.getmtime(source) - 3
        os.utime(source, (time, time))

        with open(include, "w") as fh:
            fh.write("INCLUDE\n")
        time = os.path.getmtime(source) - 3
        os.utime(include, (time, time))

        # Create and write a manifest with the preprocessed file.
        m = InstallManifest()
        m.add_preprocess(source, "p_dest", deps, "#", {"FOO": "BAR", "BAZ": "QUX"})
        m.write(path=manifest)

        time = os.path.getmtime(source) - 5
        os.utime(manifest, (time, time))

        # Now read the manifest back in, and apply it. This should write out
        # our preprocessed file.
        m = InstallManifest(path=manifest)
        c = FileCopier()
        m.populate_registry(c)
        self.assertTrue(c.copy(dest))

        with open(destfile) as fh:
            self.assertEqual(fh.read(), "SOURCE\n")

        # Next, modify the source to #INCLUDE another file.
        with open(source, "w") as fh:
            fh.write("SOURCE\n#include p_incl\n")
        time = os.path.getmtime(source) - 1
        os.utime(destfile, (time, time))

        # Apply the manifest, and confirm that it also reads the newly included
        # file.
        m = InstallManifest(path=manifest)
        c = FileCopier()
        m.populate_registry(c)
        c.copy(dest)

        with open(destfile) as fh:
            self.assertEqual(fh.read(), "SOURCE\nINCLUDE\n")

        # Set the time on the source file back, so it won't be picked up as
        # modified in the next test.
        time = os.path.getmtime(source) - 1
        os.utime(source, (time, time))

        # Now, modify the include file (but not the original source).
        with open(include, "w") as fh:
            fh.write("INCLUDE MODIFIED\n")
        time = os.path.getmtime(include) - 1
        os.utime(destfile, (time, time))

        # Apply the manifest, and confirm that the change to the include file
        # is detected. That should cause the preprocessor to run again.
        m = InstallManifest(path=manifest)
        c = FileCopier()
        m.populate_registry(c)
        c.copy(dest)

        with open(destfile) as fh:
            self.assertEqual(fh.read(), "SOURCE\nINCLUDE MODIFIED\n")

        # ORing an InstallManifest should copy file dependencies
        m = InstallManifest()
        m |= InstallManifest(path=manifest)
        c = FileCopier()
        m.populate_registry(c)
        e = c._files["p_dest"]
        self.assertEqual(e.extra_depends, [manifest])

    def test_add_entries_from(self):
        source = self.tmppath("source")
        os.mkdir(source)
        os.mkdir("%s/base" % source)
        os.mkdir("%s/base/foo" % source)

        with open("%s/base/foo/file1" % source, "a"):
            pass

        with open("%s/base/foo/file2" % source, "a"):
            pass

        m = InstallManifest()
        m.add_pattern_link("%s/base" % source, "**", "dest")

        p = InstallManifest()
        p.add_entries_from(m)
        self.assertEqual(len(p), 1)

        c = FileCopier()
        p.populate_registry(c)
        self.assertEqual(c.paths(), ["dest/foo/file1", "dest/foo/file2"])

        q = InstallManifest()
        q.add_entries_from(m, base="target")
        self.assertEqual(len(q), 1)

        d = FileCopier()
        q.populate_registry(d)
        self.assertEqual(d.paths(), ["target/dest/foo/file1", "target/dest/foo/file2"])

        # Some of the values in an InstallManifest include destination
        # information that is present in the keys.  Verify that we can
        # round-trip serialization.
        r = InstallManifest()
        r.add_entries_from(m)
        r.add_entries_from(m, base="target")
        self.assertEqual(len(r), 2)

        temp_path = self.tmppath("temp_path")
        r.write(path=temp_path)

        s = InstallManifest(path=temp_path)
        e = FileCopier()
        s.populate_registry(e)

        self.assertEqual(
            e.paths(),
            [
                "dest/foo/file1",
                "dest/foo/file2",
                "target/dest/foo/file1",
                "target/dest/foo/file2",
            ],
        )


if __name__ == "__main__":
    mozunit.main()
