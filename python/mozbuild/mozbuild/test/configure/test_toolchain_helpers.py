# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import copy
import re
import unittest
from fnmatch import fnmatch
from textwrap import dedent

from mozpack import path as mozpath
from mozunit import MockedOpen, main
from six import StringIO

from mozbuild.preprocessor import Preprocessor
from mozbuild.util import ReadOnlyNamespace


class CompilerPreprocessor(Preprocessor):
    # The C preprocessor only expands macros when they are not in C strings.
    # For now, we don't look very hard for C strings because they don't matter
    # that much for our unit tests, but we at least avoid expanding in the
    # simple "FOO" case.
    VARSUBST = re.compile(r'(?<!")(?P<VAR>\w+)(?!")', re.U)
    NON_WHITESPACE = re.compile(r"\S")
    HAS_FEATURE_OR_BUILTIN = re.compile(
        r'(__has_(?:feature|builtin|attribute|warning))\("?([^"\)]*)"?\)'
    )

    def __init__(self, *args, **kwargs):
        Preprocessor.__init__(self, *args, **kwargs)
        self.do_filter("c_substitution")
        self.setMarker(r"#\s*")

    def do_if(self, expression, **kwargs):
        # The C preprocessor handles numbers following C rules, which is a
        # different handling than what our Preprocessor does out of the box.
        # Hack around it enough that the configure tests work properly.
        context = self.context

        def normalize_numbers(value):
            if isinstance(value, str):
                if value[-1:] == "L" and value[:-1].isdigit():
                    value = int(value[:-1])
            return value

        # Our Preprocessor doesn't handle macros with parameters, so we hack
        # around that for __has_feature()-like things.

        def normalize_has_feature_or_builtin(expr):
            return (
                self.HAS_FEATURE_OR_BUILTIN.sub(r"\1\2", expr)
                .replace("-", "_")
                .replace("+", "_")
            )

        self.context = self.Context(
            (normalize_has_feature_or_builtin(k), normalize_numbers(v))
            for k, v in context.items()
        )
        try:
            return Preprocessor.do_if(
                self, normalize_has_feature_or_builtin(expression), **kwargs
            )
        finally:
            self.context = context

    class Context(dict):
        def __missing__(self, key):
            return None

    def filter_c_substitution(self, line):
        def repl(matchobj):
            varname = matchobj.group("VAR")
            if varname in self.context:
                result = str(self.context[varname])
                # The C preprocessor inserts whitespaces around expanded
                # symbols.
                start, end = matchobj.span("VAR")
                if self.NON_WHITESPACE.match(line[start - 1 : start]):
                    result = " " + result
                if self.NON_WHITESPACE.match(line[end : end + 1]):
                    result = result + " "
                return result
            return matchobj.group(0)

        return self.VARSUBST.sub(repl, line)


class TestCompilerPreprocessor(unittest.TestCase):
    def test_expansion(self):
        pp = CompilerPreprocessor({"A": 1, "B": "2", "C": "c", "D": "d"})
        pp.out = StringIO()
        input = StringIO('A.B.C "D"')
        input.name = "foo"
        pp.do_include(input)

        self.assertEqual(pp.out.getvalue(), '1 . 2 . c "D"')

    def test_normalization(self):
        pp = CompilerPreprocessor(
            {"__has_attribute(bar)": 1, '__has_warning("-Wc++98-foo")': 1}
        )
        pp.out = StringIO()
        input = StringIO(
            dedent(
                """\
        #if __has_warning("-Wbar")
        WBAR
        #endif
        #if __has_warning("-Wc++98-foo")
        WFOO
        #endif
        #if !__has_warning("-Wc++98-foo")
        NO_WFOO
        #endif
        #if __has_attribute(bar)
        BAR
        #else
        NO_BAR
        #endif
        #if !__has_attribute(foo)
        NO_FOO
        #endif
        """
            )
        )

        input.name = "foo"
        pp.do_include(input)

        self.assertEqual(pp.out.getvalue(), "WFOO\nBAR\nNO_FOO\n")

    def test_condition(self):
        pp = CompilerPreprocessor({"A": 1, "B": "2", "C": "0L"})
        pp.out = StringIO()
        input = StringIO(
            dedent(
                """\
            #ifdef A
            IFDEF_A
            #endif
            #if A
            IF_A
            #endif
            #  if B
            IF_B
            #  else
            IF_NOT_B
            #  endif
            #if !C
            IF_NOT_C
            #else
            IF_C
            #endif
        """
            )
        )
        input.name = "foo"
        pp.do_include(input)

        self.assertEqual("IFDEF_A\nIF_A\nIF_NOT_B\nIF_NOT_C\n", pp.out.getvalue())


class FakeCompiler(dict):
    """Defines a fake compiler for use in toolchain tests below.

    The definitions given when creating an instance can have one of two
    forms:
    - a dict giving preprocessor symbols and their respective value, e.g.
        { '__GNUC__': 4, '__STDC__': 1 }
    - a dict associating flags to preprocessor symbols. An entry for `None`
      is required in this case. Those are the baseline preprocessor symbols.
      Additional entries describe additional flags to set or existing flags
      to unset (with a value of `False`).
        {
          None: { '__GNUC__': 4, '__STDC__': 1, '__STRICT_ANSI__': 1 },
          '-std=gnu99': { '__STDC_VERSION__': '199901L',
                          '__STRICT_ANSI__': False },
        }
      With the dict above, invoking the preprocessor with no additional flags
      would define __GNUC__, __STDC__ and __STRICT_ANSI__, and with -std=gnu99,
      __GNUC__, __STDC__, and __STDC_VERSION__ (__STRICT_ANSI__ would be
      unset).
      It is also possible to have different symbols depending on the source
      file extension. In this case, the key is '*.ext'. e.g.
        {
          '*.c': { '__STDC__': 1 },
          '*.cpp': { '__cplusplus': '199711L' },
        }

    All the given definitions are merged together.

    A FakeCompiler instance itself can be used as a definition to create
    another FakeCompiler.

    For convenience, FakeCompiler instances can be added (+) to one another.
    """

    def __init__(self, *definitions):
        for definition in definitions:
            if all(not isinstance(d, dict) for d in definition.values()):
                definition = {None: definition}
            for key, value in definition.items():
                self.setdefault(key, {}).update(value)

    def __call__(self, stdin, args):
        files = []
        flags = []
        args = iter(args)
        while True:
            arg = next(args, None)
            if arg is None:
                break
            if arg.startswith("-"):
                # Ignore -isysroot/--sysroot and the argument that follows it.
                if arg in ("-isysroot", "--sysroot"):
                    next(args, None)
                else:
                    flags.append(arg)
            else:
                files.append(arg)

        if "-E" in flags:
            assert len(files) == 1
            file = files[0]
            pp = CompilerPreprocessor(self[None])

            def apply_defn(defn):
                for k, v in defn.items():
                    if v is False:
                        if k in pp.context:
                            del pp.context[k]
                    else:
                        pp.context[k] = v

            for glob, defn in self.items():
                if glob and not glob.startswith("-") and fnmatch(file, glob):
                    apply_defn(defn)

            for flag in flags:
                apply_defn(self.get(flag, {}))

            pp.out = StringIO()
            pp.do_include(file)
            return 0, pp.out.getvalue(), ""
        elif "-c" in flags:
            if "-funknown-flag" in flags:
                return 1, "", ""
            return 0, "", ""

        return 1, "", ""

    def __add__(self, other):
        return FakeCompiler(self, other)


class TestFakeCompiler(unittest.TestCase):
    def test_fake_compiler(self):
        with MockedOpen({"file": "A B C", "file.c": "A B C"}):
            compiler = FakeCompiler({"A": "1", "B": "2"})
            self.assertEqual(compiler(None, ["-E", "file"]), (0, "1 2 C", ""))

            compiler = FakeCompiler(
                {
                    None: {"A": "1", "B": "2"},
                    "-foo": {"C": "foo"},
                    "-bar": {"B": "bar", "C": "bar"},
                    "-qux": {"B": False},
                    "*.c": {"B": "42"},
                }
            )
            self.assertEqual(compiler(None, ["-E", "file"]), (0, "1 2 C", ""))
            self.assertEqual(compiler(None, ["-E", "-foo", "file"]), (0, "1 2 foo", ""))
            self.assertEqual(
                compiler(None, ["-E", "-bar", "file"]), (0, "1 bar bar", "")
            )
            self.assertEqual(compiler(None, ["-E", "-qux", "file"]), (0, "1 B C", ""))
            self.assertEqual(
                compiler(None, ["-E", "-foo", "-bar", "file"]), (0, "1 bar bar", "")
            )
            self.assertEqual(
                compiler(None, ["-E", "-bar", "-foo", "file"]), (0, "1 bar foo", "")
            )
            self.assertEqual(
                compiler(None, ["-E", "-bar", "-qux", "file"]), (0, "1 B bar", "")
            )
            self.assertEqual(
                compiler(None, ["-E", "-qux", "-bar", "file"]), (0, "1 bar bar", "")
            )
            self.assertEqual(compiler(None, ["-E", "file.c"]), (0, "1 42 C", ""))
            self.assertEqual(
                compiler(None, ["-E", "-bar", "file.c"]), (0, "1 bar bar", "")
            )

    def test_multiple_definitions(self):
        compiler = FakeCompiler({"A": 1, "B": 2}, {"C": 3})

        self.assertEqual(compiler, {None: {"A": 1, "B": 2, "C": 3}})
        compiler = FakeCompiler({"A": 1, "B": 2}, {"B": 4, "C": 3})

        self.assertEqual(compiler, {None: {"A": 1, "B": 4, "C": 3}})
        compiler = FakeCompiler(
            {"A": 1, "B": 2}, {None: {"B": 4, "C": 3}, "-foo": {"D": 5}}
        )

        self.assertEqual(compiler, {None: {"A": 1, "B": 4, "C": 3}, "-foo": {"D": 5}})

        compiler = FakeCompiler(
            {None: {"A": 1, "B": 2}, "-foo": {"D": 5}},
            {"-foo": {"D": 5}, "-bar": {"E": 6}},
        )

        self.assertEqual(
            compiler, {None: {"A": 1, "B": 2}, "-foo": {"D": 5}, "-bar": {"E": 6}}
        )


class PrependFlags(list):
    """Wrapper to allow to Prepend to flags instead of appending, in
    CompilerResult.
    """


class CompilerResult(ReadOnlyNamespace):
    """Helper of convenience to manipulate toolchain results in unit tests

    When adding a dict, the result is a new CompilerResult with the values
    from the dict replacing those from the CompilerResult, except for `flags`,
    where the value from the dict extends the `flags` in `self`.
    """

    def __init__(
        self, wrapper=None, compiler="", version="", type="", language="", flags=None
    ):
        if flags is None:
            flags = []
        if wrapper is None:
            wrapper = []
        super(CompilerResult, self).__init__(
            flags=flags,
            version=version,
            type=type,
            compiler=mozpath.abspath(compiler),
            wrapper=wrapper,
            language=language,
        )

    def __add__(self, other):
        assert isinstance(other, dict)
        result = copy.deepcopy(self.__dict__)
        for k, v in other.items():
            if k == "flags":
                flags = result.setdefault(k, [])
                if isinstance(v, PrependFlags):
                    flags[:0] = v
                else:
                    flags.extend(v)
            else:
                result[k] = v
        return CompilerResult(**result)


class TestCompilerResult(unittest.TestCase):
    def test_compiler_result(self):
        result = CompilerResult()
        self.assertEqual(
            result.__dict__,
            {
                "wrapper": [],
                "compiler": mozpath.abspath(""),
                "version": "",
                "type": "",
                "language": "",
                "flags": [],
            },
        )

        result = CompilerResult(
            compiler="/usr/bin/gcc",
            version="4.2.1",
            type="gcc",
            language="C",
            flags=["-std=gnu99"],
        )
        self.assertEqual(
            result.__dict__,
            {
                "wrapper": [],
                "compiler": mozpath.abspath("/usr/bin/gcc"),
                "version": "4.2.1",
                "type": "gcc",
                "language": "C",
                "flags": ["-std=gnu99"],
            },
        )

        result2 = result + {"flags": ["-m32"]}
        self.assertEqual(
            result2.__dict__,
            {
                "wrapper": [],
                "compiler": mozpath.abspath("/usr/bin/gcc"),
                "version": "4.2.1",
                "type": "gcc",
                "language": "C",
                "flags": ["-std=gnu99", "-m32"],
            },
        )
        # Original flags are untouched.
        self.assertEqual(result.flags, ["-std=gnu99"])

        result3 = result + {
            "compiler": "/usr/bin/gcc-4.7",
            "version": "4.7.3",
            "flags": ["-m32"],
        }
        self.assertEqual(
            result3.__dict__,
            {
                "wrapper": [],
                "compiler": mozpath.abspath("/usr/bin/gcc-4.7"),
                "version": "4.7.3",
                "type": "gcc",
                "language": "C",
                "flags": ["-std=gnu99", "-m32"],
            },
        )


if __name__ == "__main__":
    main()
