# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import unittest

from mozpack import path as mozpath
from mozunit import main

from mozbuild.frontend.context import (
    FUNCTIONS,
    SPECIAL_VARIABLES,
    SUBCONTEXTS,
    VARIABLES,
    AbsolutePath,
    Context,
    ContextDerivedTypedHierarchicalStringList,
    ContextDerivedTypedList,
    ContextDerivedTypedListWithItems,
    ContextDerivedTypedRecord,
    Files,
    ObjDirPath,
    Path,
    SourcePath,
)
from mozbuild.util import StrictOrderingOnAppendListWithFlagsFactory


class TestContext(unittest.TestCase):
    def test_defaults(self):
        test = Context({
            "foo": (int, int, ""),
            "bar": (bool, bool, ""),
            "baz": (dict, dict, ""),
        })

        self.assertEqual(list(test), [])

        self.assertEqual(test["foo"], 0)

        self.assertEqual(set(test.keys()), {"foo"})

        self.assertEqual(test["bar"], False)

        self.assertEqual(set(test.keys()), {"foo", "bar"})

        self.assertEqual(test["baz"], {})

        self.assertEqual(set(test.keys()), {"foo", "bar", "baz"})

        with self.assertRaises(KeyError):
            test["qux"]

        self.assertEqual(set(test.keys()), {"foo", "bar", "baz"})

    def test_type_check(self):
        test = Context({
            "foo": (int, int, ""),
            "baz": (dict, list, ""),
        })

        test["foo"] = 5

        self.assertEqual(test["foo"], 5)

        with self.assertRaises(ValueError):
            test["foo"] = {}

        self.assertEqual(test["foo"], 5)

        with self.assertRaises(KeyError):
            test["bar"] = True

        test["baz"] = [("a", 1), ("b", 2)]

        self.assertEqual(test["baz"], {"a": 1, "b": 2})

    def test_update(self):
        test = Context({
            "foo": (int, int, ""),
            "bar": (bool, bool, ""),
            "baz": (dict, list, ""),
        })

        self.assertEqual(list(test), [])

        with self.assertRaises(ValueError):
            test.update(bar=True, foo={})

        self.assertEqual(list(test), [])

        test.update(bar=True, foo=1)

        self.assertEqual(set(test.keys()), {"foo", "bar"})
        self.assertEqual(test["foo"], 1)
        self.assertEqual(test["bar"], True)

        test.update([("bar", False), ("foo", 2)])
        self.assertEqual(test["foo"], 2)
        self.assertEqual(test["bar"], False)

        test.update([("foo", 0), ("baz", {"a": 1, "b": 2})])
        self.assertEqual(test["foo"], 0)
        self.assertEqual(test["baz"], {"a": 1, "b": 2})

        test.update([("foo", 42), ("baz", [("c", 3), ("d", 4)])])
        self.assertEqual(test["foo"], 42)
        self.assertEqual(test["baz"], {"c": 3, "d": 4})

    def test_context_paths(self):
        test = Context()

        # Newly created context has no paths.
        self.assertIsNone(test.main_path)
        self.assertIsNone(test.current_path)
        self.assertEqual(test.all_paths, set())
        self.assertEqual(test.source_stack, [])

        foo = os.path.abspath("foo")
        test.add_source(foo)

        # Adding the first source makes it the main and current path.
        self.assertEqual(test.main_path, foo)
        self.assertEqual(test.current_path, foo)
        self.assertEqual(test.all_paths, set([foo]))
        self.assertEqual(test.source_stack, [foo])

        bar = os.path.abspath("bar")
        test.add_source(bar)

        # Adding the second source makes leaves main and current paths alone.
        self.assertEqual(test.main_path, foo)
        self.assertEqual(test.current_path, foo)
        self.assertEqual(test.all_paths, set([bar, foo]))
        self.assertEqual(test.source_stack, [foo])

        qux = os.path.abspath("qux")
        test.push_source(qux)

        # Pushing a source makes it the current path
        self.assertEqual(test.main_path, foo)
        self.assertEqual(test.current_path, qux)
        self.assertEqual(test.all_paths, set([bar, foo, qux]))
        self.assertEqual(test.source_stack, [foo, qux])

        hoge = os.path.abspath("hoge")
        test.push_source(hoge)
        self.assertEqual(test.main_path, foo)
        self.assertEqual(test.current_path, hoge)
        self.assertEqual(test.all_paths, set([bar, foo, hoge, qux]))
        self.assertEqual(test.source_stack, [foo, qux, hoge])

        fuga = os.path.abspath("fuga")

        # Adding a source after pushing doesn't change the source stack
        test.add_source(fuga)
        self.assertEqual(test.main_path, foo)
        self.assertEqual(test.current_path, hoge)
        self.assertEqual(test.all_paths, set([bar, foo, fuga, hoge, qux]))
        self.assertEqual(test.source_stack, [foo, qux, hoge])

        # Adding a source twice doesn't change anything
        test.add_source(qux)
        self.assertEqual(test.main_path, foo)
        self.assertEqual(test.current_path, hoge)
        self.assertEqual(test.all_paths, set([bar, foo, fuga, hoge, qux]))
        self.assertEqual(test.source_stack, [foo, qux, hoge])

        last = test.pop_source()

        # Popping a source returns the last pushed one, not the last added one.
        self.assertEqual(last, hoge)
        self.assertEqual(test.main_path, foo)
        self.assertEqual(test.current_path, qux)
        self.assertEqual(test.all_paths, set([bar, foo, fuga, hoge, qux]))
        self.assertEqual(test.source_stack, [foo, qux])

        last = test.pop_source()
        self.assertEqual(last, qux)
        self.assertEqual(test.main_path, foo)
        self.assertEqual(test.current_path, foo)
        self.assertEqual(test.all_paths, set([bar, foo, fuga, hoge, qux]))
        self.assertEqual(test.source_stack, [foo])

        # Popping the main path is allowed.
        last = test.pop_source()
        self.assertEqual(last, foo)
        self.assertEqual(test.main_path, foo)
        self.assertIsNone(test.current_path)
        self.assertEqual(test.all_paths, set([bar, foo, fuga, hoge, qux]))
        self.assertEqual(test.source_stack, [])

        # Popping past the main path asserts.
        with self.assertRaises(AssertionError):
            test.pop_source()

        # Pushing after the main path was popped asserts.
        with self.assertRaises(AssertionError):
            test.push_source(foo)

        test = Context()
        test.push_source(foo)
        test.push_source(bar)

        # Pushing the same file twice is allowed.
        test.push_source(bar)
        test.push_source(foo)
        self.assertEqual(last, foo)
        self.assertEqual(test.main_path, foo)
        self.assertEqual(test.current_path, foo)
        self.assertEqual(test.all_paths, set([bar, foo]))
        self.assertEqual(test.source_stack, [foo, bar, bar, foo])

    def test_context_dirs(self):
        class Config:
            pass

        config = Config()
        config.topsrcdir = mozpath.abspath(os.curdir)
        config.topobjdir = mozpath.abspath("obj")
        test = Context(config=config)
        foo = mozpath.abspath("foo")
        test.push_source(foo)

        self.assertEqual(test.srcdir, config.topsrcdir)
        self.assertEqual(test.relsrcdir, "")
        self.assertEqual(test.objdir, config.topobjdir)
        self.assertEqual(test.relobjdir, "")

        foobar = os.path.abspath("foo/bar")
        test.push_source(foobar)
        self.assertEqual(test.srcdir, mozpath.join(config.topsrcdir, "foo"))
        self.assertEqual(test.relsrcdir, "foo")
        self.assertEqual(test.objdir, config.topobjdir)
        self.assertEqual(test.relobjdir, "")


class TestSymbols(unittest.TestCase):
    def _verify_doc(self, doc):
        # Documentation should be of the format:
        # """SUMMARY LINE
        #
        # EXTRA PARAGRAPHS
        # """

        self.assertNotIn("\r", doc)

        lines = doc.split("\n")

        # No trailing whitespace.
        for line in lines[0:-1]:
            self.assertEqual(line, line.rstrip())

        self.assertGreater(len(lines), 0)
        self.assertGreater(len(lines[0].strip()), 0)

        # Last line should be empty.
        self.assertEqual(lines[-1].strip(), "")

    def test_documentation_formatting(self):
        for typ, inp, doc in VARIABLES.values():
            self._verify_doc(doc)

        for attr, args, doc in FUNCTIONS.values():
            self._verify_doc(doc)

        for func, typ, doc in SPECIAL_VARIABLES.values():
            self._verify_doc(doc)

        for name, cls in SUBCONTEXTS.items():
            self._verify_doc(cls.__doc__)

            for name, v in cls.VARIABLES.items():
                self._verify_doc(v[2])


class TestPaths(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        class Config:
            pass

        cls.config = config = Config()
        config.topsrcdir = mozpath.abspath(os.curdir)
        config.topobjdir = mozpath.abspath("obj")

    def test_path(self):
        config = self.config
        ctxt1 = Context(config=config)
        ctxt1.push_source(mozpath.join(config.topsrcdir, "foo", "moz.build"))
        ctxt2 = Context(config=config)
        ctxt2.push_source(mozpath.join(config.topsrcdir, "bar", "moz.build"))

        path1 = Path(ctxt1, "qux")
        self.assertIsInstance(path1, SourcePath)
        self.assertEqual(path1, "qux")
        self.assertEqual(path1.full_path, mozpath.join(config.topsrcdir, "foo", "qux"))

        path2 = Path(ctxt2, "../foo/qux")
        self.assertIsInstance(path2, SourcePath)
        self.assertEqual(path2, "../foo/qux")
        self.assertEqual(path2.full_path, mozpath.join(config.topsrcdir, "foo", "qux"))

        self.assertEqual(path1, path2)

        self.assertEqual(
            path1.join("../../bar/qux").full_path,
            mozpath.join(config.topsrcdir, "bar", "qux"),
        )

        path1 = Path(ctxt1, "/qux/qux")
        self.assertIsInstance(path1, SourcePath)
        self.assertEqual(path1, "/qux/qux")
        self.assertEqual(path1.full_path, mozpath.join(config.topsrcdir, "qux", "qux"))

        path2 = Path(ctxt2, "/qux/qux")
        self.assertIsInstance(path2, SourcePath)
        self.assertEqual(path2, "/qux/qux")
        self.assertEqual(path2.full_path, mozpath.join(config.topsrcdir, "qux", "qux"))

        self.assertEqual(path1, path2)

        path1 = Path(ctxt1, "!qux")
        self.assertIsInstance(path1, ObjDirPath)
        self.assertEqual(path1, "!qux")
        self.assertEqual(path1.full_path, mozpath.join(config.topobjdir, "foo", "qux"))

        path2 = Path(ctxt2, "!../foo/qux")
        self.assertIsInstance(path2, ObjDirPath)
        self.assertEqual(path2, "!../foo/qux")
        self.assertEqual(path2.full_path, mozpath.join(config.topobjdir, "foo", "qux"))

        self.assertEqual(path1, path2)

        path1 = Path(ctxt1, "!/qux/qux")
        self.assertIsInstance(path1, ObjDirPath)
        self.assertEqual(path1, "!/qux/qux")
        self.assertEqual(path1.full_path, mozpath.join(config.topobjdir, "qux", "qux"))

        path2 = Path(ctxt2, "!/qux/qux")
        self.assertIsInstance(path2, ObjDirPath)
        self.assertEqual(path2, "!/qux/qux")
        self.assertEqual(path2.full_path, mozpath.join(config.topobjdir, "qux", "qux"))

        self.assertEqual(path1, path2)

        path1 = Path(ctxt1, path1)
        self.assertIsInstance(path1, ObjDirPath)
        self.assertEqual(path1, "!/qux/qux")
        self.assertEqual(path1.full_path, mozpath.join(config.topobjdir, "qux", "qux"))

        path2 = Path(ctxt2, path2)
        self.assertIsInstance(path2, ObjDirPath)
        self.assertEqual(path2, "!/qux/qux")
        self.assertEqual(path2.full_path, mozpath.join(config.topobjdir, "qux", "qux"))

        self.assertEqual(path1, path2)

        path1 = Path(path1)
        self.assertIsInstance(path1, ObjDirPath)
        self.assertEqual(path1, "!/qux/qux")
        self.assertEqual(path1.full_path, mozpath.join(config.topobjdir, "qux", "qux"))

        self.assertEqual(path1, path2)

        path2 = Path(path2)
        self.assertIsInstance(path2, ObjDirPath)
        self.assertEqual(path2, "!/qux/qux")
        self.assertEqual(path2.full_path, mozpath.join(config.topobjdir, "qux", "qux"))

        self.assertEqual(path1, path2)

    def test_source_path(self):
        config = self.config
        ctxt = Context(config=config)
        ctxt.push_source(mozpath.join(config.topsrcdir, "foo", "moz.build"))

        path = SourcePath(ctxt, "qux")
        self.assertEqual(path, "qux")
        self.assertEqual(path.full_path, mozpath.join(config.topsrcdir, "foo", "qux"))
        self.assertEqual(path.translated, mozpath.join(config.topobjdir, "foo", "qux"))

        path = SourcePath(ctxt, "../bar/qux")
        self.assertEqual(path, "../bar/qux")
        self.assertEqual(path.full_path, mozpath.join(config.topsrcdir, "bar", "qux"))
        self.assertEqual(path.translated, mozpath.join(config.topobjdir, "bar", "qux"))

        path = SourcePath(ctxt, "/qux/qux")
        self.assertEqual(path, "/qux/qux")
        self.assertEqual(path.full_path, mozpath.join(config.topsrcdir, "qux", "qux"))
        self.assertEqual(path.translated, mozpath.join(config.topobjdir, "qux", "qux"))

        with self.assertRaises(ValueError):
            SourcePath(ctxt, "!../bar/qux")

        with self.assertRaises(ValueError):
            SourcePath(ctxt, "!/qux/qux")

        path = SourcePath(path)
        self.assertIsInstance(path, SourcePath)
        self.assertEqual(path, "/qux/qux")
        self.assertEqual(path.full_path, mozpath.join(config.topsrcdir, "qux", "qux"))
        self.assertEqual(path.translated, mozpath.join(config.topobjdir, "qux", "qux"))

        path = Path(path)
        self.assertIsInstance(path, SourcePath)

    def test_objdir_path(self):
        config = self.config
        ctxt = Context(config=config)
        ctxt.push_source(mozpath.join(config.topsrcdir, "foo", "moz.build"))

        path = ObjDirPath(ctxt, "!qux")
        self.assertEqual(path, "!qux")
        self.assertEqual(path.full_path, mozpath.join(config.topobjdir, "foo", "qux"))

        path = ObjDirPath(ctxt, "!../bar/qux")
        self.assertEqual(path, "!../bar/qux")
        self.assertEqual(path.full_path, mozpath.join(config.topobjdir, "bar", "qux"))

        path = ObjDirPath(ctxt, "!/qux/qux")
        self.assertEqual(path, "!/qux/qux")
        self.assertEqual(path.full_path, mozpath.join(config.topobjdir, "qux", "qux"))

        with self.assertRaises(ValueError):
            path = ObjDirPath(ctxt, "../bar/qux")

        with self.assertRaises(ValueError):
            path = ObjDirPath(ctxt, "/qux/qux")

        path = ObjDirPath(path)
        self.assertIsInstance(path, ObjDirPath)
        self.assertEqual(path, "!/qux/qux")
        self.assertEqual(path.full_path, mozpath.join(config.topobjdir, "qux", "qux"))

        path = Path(path)
        self.assertIsInstance(path, ObjDirPath)

    def test_absolute_path(self):
        config = self.config
        ctxt = Context(config=config)
        ctxt.push_source(mozpath.join(config.topsrcdir, "foo", "moz.build"))

        abspath = mozpath.join(config.topobjdir, "qux")
        path = AbsolutePath(ctxt, f"%{abspath}")
        self.assertEqual(path, f"%{abspath}")
        self.assertEqual(path.full_path, abspath)

        with self.assertRaises(ValueError):
            path = AbsolutePath(ctxt, "%qux")

    def test_path_with_mixed_contexts(self):
        config = self.config
        ctxt1 = Context(config=config)
        ctxt1.push_source(mozpath.join(config.topsrcdir, "foo", "moz.build"))
        ctxt2 = Context(config=config)
        ctxt2.push_source(mozpath.join(config.topsrcdir, "bar", "moz.build"))

        path1 = Path(ctxt1, "qux")
        path2 = Path(ctxt2, path1)
        self.assertEqual(path2, path1)
        self.assertEqual(path2, "qux")
        self.assertEqual(path2.context, ctxt1)
        self.assertEqual(path2.full_path, mozpath.join(config.topsrcdir, "foo", "qux"))

        path1 = Path(ctxt1, "../bar/qux")
        path2 = Path(ctxt2, path1)
        self.assertEqual(path2, path1)
        self.assertEqual(path2, "../bar/qux")
        self.assertEqual(path2.context, ctxt1)
        self.assertEqual(path2.full_path, mozpath.join(config.topsrcdir, "bar", "qux"))

        path1 = Path(ctxt1, "/qux/qux")
        path2 = Path(ctxt2, path1)
        self.assertEqual(path2, path1)
        self.assertEqual(path2, "/qux/qux")
        self.assertEqual(path2.context, ctxt1)
        self.assertEqual(path2.full_path, mozpath.join(config.topsrcdir, "qux", "qux"))

        path1 = Path(ctxt1, "!qux")
        path2 = Path(ctxt2, path1)
        self.assertEqual(path2, path1)
        self.assertEqual(path2, "!qux")
        self.assertEqual(path2.context, ctxt1)
        self.assertEqual(path2.full_path, mozpath.join(config.topobjdir, "foo", "qux"))

        path1 = Path(ctxt1, "!../bar/qux")
        path2 = Path(ctxt2, path1)
        self.assertEqual(path2, path1)
        self.assertEqual(path2, "!../bar/qux")
        self.assertEqual(path2.context, ctxt1)
        self.assertEqual(path2.full_path, mozpath.join(config.topobjdir, "bar", "qux"))

        path1 = Path(ctxt1, "!/qux/qux")
        path2 = Path(ctxt2, path1)
        self.assertEqual(path2, path1)
        self.assertEqual(path2, "!/qux/qux")
        self.assertEqual(path2.context, ctxt1)
        self.assertEqual(path2.full_path, mozpath.join(config.topobjdir, "qux", "qux"))

    def test_path_typed_list(self):
        config = self.config
        ctxt1 = Context(config=config)
        ctxt1.push_source(mozpath.join(config.topsrcdir, "foo", "moz.build"))
        ctxt2 = Context(config=config)
        ctxt2.push_source(mozpath.join(config.topsrcdir, "bar", "moz.build"))

        paths = [
            "!../bar/qux",
            "!/qux/qux",
            "!qux",
            "../bar/qux",
            "/qux/qux",
            "qux",
        ]

        MyList = ContextDerivedTypedList(Path)
        l = MyList(ctxt1)
        l += paths

        for p_str, p_path in zip(paths, l):
            self.assertEqual(p_str, p_path)
            self.assertEqual(p_path, Path(ctxt1, p_str))
            self.assertEqual(
                p_path.join("foo"), Path(ctxt1, mozpath.join(p_str, "foo"))
            )

        l2 = MyList(ctxt2)
        l2 += paths

        for p_str, p_path in zip(paths, l2):
            self.assertEqual(p_str, p_path)
            self.assertEqual(p_path, Path(ctxt2, p_str))

        # Assigning with Paths from another context doesn't rebase them
        l2 = MyList(ctxt2)
        l2 += l

        for p_str, p_path in zip(paths, l2):
            self.assertEqual(p_str, p_path)
            self.assertEqual(p_path, Path(ctxt1, p_str))

        MyListWithFlags = ContextDerivedTypedListWithItems(
            Path,
            StrictOrderingOnAppendListWithFlagsFactory({
                "foo": bool,
            }),
        )
        l = MyListWithFlags(ctxt1)
        l += paths

        for p in paths:
            l[p].foo = True

        for p_str, p_path in zip(paths, l):
            self.assertEqual(p_str, p_path)
            self.assertEqual(p_path, Path(ctxt1, p_str))
            self.assertEqual(l[p_str].foo, True)
            self.assertEqual(l[p_path].foo, True)

    def test_path_typed_hierarchy_list(self):
        config = self.config
        ctxt1 = Context(config=config)
        ctxt1.push_source(mozpath.join(config.topsrcdir, "foo", "moz.build"))
        ctxt2 = Context(config=config)
        ctxt2.push_source(mozpath.join(config.topsrcdir, "bar", "moz.build"))

        paths = [
            "!../bar/qux",
            "!/qux/qux",
            "!qux",
            "../bar/qux",
            "/qux/qux",
            "qux",
        ]

        MyList = ContextDerivedTypedHierarchicalStringList(Path)
        l = MyList(ctxt1)
        l += paths
        l.subdir += paths

        for _, files in l.walk():
            for p_str, p_path in zip(paths, files):
                self.assertEqual(p_str, p_path)
                self.assertEqual(p_path, Path(ctxt1, p_str))
                self.assertEqual(
                    p_path.join("foo"), Path(ctxt1, mozpath.join(p_str, "foo"))
                )

        l2 = MyList(ctxt2)
        l2 += paths
        l2.subdir += paths

        for _, files in l2.walk():
            for p_str, p_path in zip(paths, files):
                self.assertEqual(p_str, p_path)
                self.assertEqual(p_path, Path(ctxt2, p_str))

        # Assigning with Paths from another context doesn't rebase them
        l2 = MyList(ctxt2)
        l2 += l

        for _, files in l2.walk():
            for p_str, p_path in zip(paths, files):
                self.assertEqual(p_str, p_path)
                self.assertEqual(p_path, Path(ctxt1, p_str))


class TestTypedRecord(unittest.TestCase):
    def test_fields(self):
        T = ContextDerivedTypedRecord(("field1", str), ("field2", list))
        inst = T(None)
        self.assertEqual(inst.field1, "")
        self.assertEqual(inst.field2, [])

        inst.field1 = "foo"
        inst.field2 += ["bar"]

        self.assertEqual(inst.field1, "foo")
        self.assertEqual(inst.field2, ["bar"])

        with self.assertRaises(AttributeError):
            inst.field3 = []

    def test_coercion(self):
        T = ContextDerivedTypedRecord(("field1", str), ("field2", list))
        inst = T(None)
        inst.field1 = 3
        inst.field2 += ("bar",)
        self.assertEqual(inst.field1, "3")
        self.assertEqual(inst.field2, ["bar"])

        with self.assertRaises(TypeError):
            inst.field2 = object()


class TestFiles(unittest.TestCase):
    def test_aggregate_empty(self):
        c = Context({})

        files = {"moz.build": Files(c, "**")}

        self.assertEqual(
            Files.aggregate(files),
            {
                "bug_component_counts": [],
                "recommended_bug_component": None,
            },
        )

    def test_single_bug_component(self):
        c = Context({})
        f = Files(c, "**")
        f["BUG_COMPONENT"] = ("Product1", "Component1")

        files = {"moz.build": f}
        self.assertEqual(
            Files.aggregate(files),
            {
                "bug_component_counts": [(("Product1", "Component1"), 1)],
                "recommended_bug_component": ("Product1", "Component1"),
            },
        )

    def test_multiple_bug_components(self):
        c = Context({})
        f1 = Files(c, "**")
        f1["BUG_COMPONENT"] = ("Product1", "Component1")

        f2 = Files(c, "**")
        f2["BUG_COMPONENT"] = ("Product2", "Component2")

        files = {"a": f1, "b": f2, "c": f1}
        self.assertEqual(
            Files.aggregate(files),
            {
                "bug_component_counts": [
                    (("Product1", "Component1"), 2),
                    (("Product2", "Component2"), 1),
                ],
                "recommended_bug_component": ("Product1", "Component1"),
            },
        )

    def test_no_recommended_bug_component(self):
        """If there is no clear count winner, we don't recommend a bug component."""
        c = Context({})
        f1 = Files(c, "**")
        f1["BUG_COMPONENT"] = ("Product1", "Component1")

        f2 = Files(c, "**")
        f2["BUG_COMPONENT"] = ("Product2", "Component2")

        files = {"a": f1, "b": f2}
        self.assertEqual(
            Files.aggregate(files),
            {
                "bug_component_counts": [
                    (("Product1", "Component1"), 1),
                    (("Product2", "Component2"), 1),
                ],
                "recommended_bug_component": None,
            },
        )

    def test_multiple_patterns(self):
        c = Context({})
        f1 = Files(c, "a/**")
        f1["BUG_COMPONENT"] = ("Product1", "Component1")
        f2 = Files(c, "b/**", "a/bar")
        f2["BUG_COMPONENT"] = ("Product2", "Component2")

        files = {"a/foo": f1, "a/bar": f2, "b/foo": f2}
        self.assertEqual(
            Files.aggregate(files),
            {
                "bug_component_counts": [
                    (("Product2", "Component2"), 2),
                    (("Product1", "Component1"), 1),
                ],
                "recommended_bug_component": ("Product2", "Component2"),
            },
        )


if __name__ == "__main__":
    main()
