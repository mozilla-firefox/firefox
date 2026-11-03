# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
r"""
This is a very primitive line based preprocessor, for times when using
a C preprocessor isn't an option.

It currently supports the following grammar for expressions, whitespace is
ignored:

expression :
  and_cond ( '||' expression ) ? ;
and_cond:
  test ( '&&' and_cond ) ? ;
test:
  unary ( ( '==' | '!=' ) unary ) ? ;
unary :
  '!'? value ;
value :
  [0-9]+ # integer
  | 'defined(' \w+ ')'
  | \w+  # string identifier or value;
"""

import io
import os
import re
import sys
from optparse import OptionParser

from mozpack.path import normsep

from mozbuild.makeutil import Makefile

# hack around win32 mangling our line endings
# http://aspn.activestate.com/ASPN/Cookbook/Python/Recipe/65443
if sys.platform == "win32":
    import msvcrt

    msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)
    os.linesep = "\n"


__all__ = ["Context", "Expression", "Preprocessor", "preprocess"]


def _to_text(a):
    # We end up converting a lot of different types (text_type, binary_type,
    # int, etc.) to Unicode in this script. This function handles all of those
    # possibilities.
    if isinstance(a, bytes):
        return a.decode()
    if isinstance(a, str):
        return a
    return str(a)


def path_starts_with(path, prefix):
    if os.altsep:
        prefix = prefix.replace(os.altsep, os.sep)
        path = path.replace(os.altsep, os.sep)
    prefix = [os.path.normcase(p) for p in prefix.split(os.sep)]
    path = [os.path.normcase(p) for p in path.split(os.sep)]
    return path[: len(prefix)] == prefix


class Expression:
    def __init__(self, expression_string):
        """
        Create a new expression with this string.
        The expression will already be parsed into an Abstract Syntax Tree.
        """
        self.content = expression_string
        self.offset = 0
        self.__ignore_whitespace()
        self.e = self.__get_logical_or()
        if self.content:
            raise Expression.ParseError(self)

    def __get_logical_or(self):
        """
        Production: and_cond ( '||' expression ) ?
        """
        if not len(self.content):
            return None
        rv = Expression.__AST("logical_op")
        # test
        rv.append(self.__get_logical_and())
        self.__ignore_whitespace()
        if self.content[:2] != "||":
            # no logical op needed, short cut to our prime element
            return rv[0]
        # append operator
        rv.append(Expression.__ASTLeaf("op", self.content[:2]))
        self.__strip(2)
        self.__ignore_whitespace()
        rv.append(self.__get_logical_or())
        self.__ignore_whitespace()
        return rv

    def __get_logical_and(self):
        """
        Production: test ( '&&' and_cond ) ?
        """
        if not len(self.content):
            return None
        rv = Expression.__AST("logical_op")
        # test
        rv.append(self.__get_equality())
        self.__ignore_whitespace()
        if self.content[:2] != "&&":
            # no logical op needed, short cut to our prime element
            return rv[0]
        # append operator
        rv.append(Expression.__ASTLeaf("op", self.content[:2]))
        self.__strip(2)
        self.__ignore_whitespace()
        rv.append(self.__get_logical_and())
        self.__ignore_whitespace()
        return rv

    def __get_equality(self):
        """
        Production: unary ( ( '==' | '!=' ) unary ) ?
        """
        if not len(self.content):
            return None
        rv = Expression.__AST("equality")
        # unary
        rv.append(self.__get_unary())
        self.__ignore_whitespace()
        if not re.match("[=!]=", self.content):
            # no equality needed, short cut to our prime unary
            return rv[0]
        # append operator
        rv.append(Expression.__ASTLeaf("op", self.content[:2]))
        self.__strip(2)
        self.__ignore_whitespace()
        rv.append(self.__get_unary())
        self.__ignore_whitespace()
        return rv

    def __get_unary(self):
        """
        Production: '!'? value
        """
        # eat whitespace right away, too
        not_ws = re.match(r"!\s*", self.content)
        if not not_ws:
            return self.__get_value()
        rv = Expression.__AST("not")
        self.__strip(not_ws.end())
        rv.append(self.__get_value())
        self.__ignore_whitespace()
        return rv

    def __get_value(self):
        r"""
        Production: ( [0-9]+ | 'defined(' \w+ ')' | \w+ )
        Note that the order is important, and the expression is kind-of
        ambiguous as \w includes 0-9. One could make it unambiguous by
        removing 0-9 from the first char of a string literal.
        """
        rv = None
        m = re.match(r"defined\s*\(\s*(\w+)\s*\)", self.content)
        if m:
            word_len = m.end()
            rv = Expression.__ASTLeaf("defined", m.group(1))
        else:
            word_len = re.match("[0-9]*", self.content).end()
            if word_len:
                value = int(self.content[:word_len])
                rv = Expression.__ASTLeaf("int", value)
            else:
                word_len = re.match(r"\w*", self.content).end()
                if word_len:
                    rv = Expression.__ASTLeaf("string", self.content[:word_len])
                else:
                    raise Expression.ParseError(self)
        self.__strip(word_len)
        self.__ignore_whitespace()
        return rv

    def __ignore_whitespace(self):
        ws_len = re.match(r"\s*", self.content).end()
        self.__strip(ws_len)

    def __strip(self, length):
        """
        Remove a given amount of chars from the input and update
        the offset.
        """
        self.content = self.content[length:]
        self.offset += length

    def evaluate(self, context):
        """
        Evaluate the expression with the given context
        """

        # Helper function to evaluate __get_equality results
        def eval_equality(tok):
            left = opmap[tok[0].type](tok[0])
            right = opmap[tok[2].type](tok[2])
            rv = left == right
            if tok[1].value == "!=":
                rv = not rv
            return rv

        # Helper function to evaluate __get_logical_and and __get_logical_or results
        def eval_logical_op(tok):
            left = opmap[tok[0].type](tok[0])
            right = opmap[tok[2].type](tok[2])
            if tok[1].value == "&&":
                return left and right
            elif tok[1].value == "||":
                return left or right
            raise Expression.ParseError(self)

        # Mapping from token types to evaluator functions
        # Apart from (non-)equality, all these can be simple lambda forms.
        opmap = {
            "logical_op": eval_logical_op,
            "equality": eval_equality,
            "not": lambda tok: not opmap[tok[0].type](tok[0]),
            "string": lambda tok: context[tok.value],
            "defined": lambda tok: tok.value in context,
            "int": lambda tok: tok.value,
        }

        return opmap[self.e.type](self.e)

    class __AST(list):
        """
        Internal class implementing Abstract Syntax Tree nodes
        """

        def __init__(self, type):
            self.type = type
            super(self.__class__, self).__init__(self)

    class __ASTLeaf:
        """
        Internal class implementing Abstract Syntax Tree leafs
        """

        def __init__(self, type, value):
            self.value = value
            self.type = type

        def __str__(self):
            return self.value.__str__()

        def __repr__(self):
            return self.value.__repr__()

    class ParseError(Exception):
        """
        Error raised when parsing fails.
        It has two members, offset and content, which give the offset of the
        error and the offending content.
        """

        def __init__(self, expression):
            self.offset = expression.offset
            self.content = expression.content[:3]

        def __str__(self):
            return f'Unexpected content at offset {self.offset}, "{self.content}"'


class Context(dict):
    """
    This class holds variable values by subclassing dict, and while it
    truthfully reports True and False on

    name in context

    it returns the variable name itself on

    context["name"]

    to reflect the ambiguity between string literals and preprocessor
    variables.
    """

    def __getitem__(self, key):
        if key in self:
            return super(self.__class__, self).__getitem__(key)
        return key


class Preprocessor:
    """
    Class for preprocessing text files.
    """

    class Error(RuntimeError):
        def __init__(self, cpp, MSG, context):
            self.file = cpp.context["FILE"]
            self.line = cpp.context["LINE"]
            self.key = MSG
            RuntimeError.__init__(self, (self.file, self.line, self.key, context))

    def __init__(self, defines=None, marker="#"):
        self.context = Context()
        self.context.update({"FILE": "", "LINE": 0, "DIRECTORY": os.path.abspath(".")})
        try:
            # Can import globally because of bootstrapping issues.
            from buildconfig import topobjdir, topsrcdir
        except ImportError:
            # Allow this script to still work independently of a configured objdir.
            topsrcdir = topobjdir = None
        self.topsrcdir = topsrcdir
        self.topobjdir = topobjdir
        self.curdir = "."
        self.actionLevel = 0
        self.disableLevel = 0
        # ifStates can be
        #  0: hadTrue
        #  1: wantsTrue
        #  2: #else found
        self.ifStates = []
        self.checkLineNumbers = False

        # A list of (filter_name, filter_function) pairs.
        self.filters = []

        self.cmds = {}
        for cmd, level in (
            ("define", 0),
            ("undef", 0),
            ("if", sys.maxsize),
            ("ifdef", sys.maxsize),
            ("ifndef", sys.maxsize),
            ("else", 1),
            ("elif", 1),
            ("elifdef", 1),
            ("elifndef", 1),
            ("endif", sys.maxsize),
            ("expand", 0),
            ("literal", 0),
            ("filter", 0),
            ("unfilter", 0),
            ("include", 0),
            ("includesubst", 0),
            ("error", 0),
        ):
            self.cmds[cmd] = (level, getattr(self, "do_" + cmd))
        self.out = sys.stdout
        self.setMarker(marker)
        self.varsubst = re.compile(r"@(?P<VAR>\w+)@", re.U)
        self.includes = set()
        self.silenceMissingDirectiveWarnings = False
        if defines:
            self.context.update(defines)

    def failUnused(self, file):
        msg = None
        if self.actionLevel == 0 and not self.silenceMissingDirectiveWarnings:
            msg = "no preprocessor directives found"
        elif self.actionLevel == 1:
            msg = "no useful preprocessor directives found"
        if msg:

            class Fake:
                pass

            fake = Fake()
            fake.context = {
                "FILE": file,
                "LINE": None,
            }
            raise Preprocessor.Error(fake, msg, None)

    def setMarker(self, aMarker):
        """
        Set the marker to be used for processing directives.
        Used for handling CSS files, with pp.setMarker('%'), for example.
        The given marker may be None, in which case no markers are processed.
        """
        self.marker = aMarker
        if aMarker:
            instruction_prefix = r"\s*{0}"
            instruction_cmd = r"(?P<cmd>[a-z]+)(?:\s+(?P<args>.*?))?\s*$"
            instruction_fmt = instruction_prefix + instruction_cmd
            ambiguous_fmt = instruction_prefix + r"\s+" + instruction_cmd

            self.instruction = re.compile(instruction_fmt.format(aMarker))
            self.comment = re.compile(aMarker, re.U)
            self.ambiguous_comment = re.compile(ambiguous_fmt.format(aMarker))
        else:

            class NoMatch:
                def match(self, *args):
                    return False

            self.instruction = self.comment = NoMatch()

    def setSilenceDirectiveWarnings(self, value):
        """
        Sets whether missing directive warnings are silenced, according to
        ``value``.  The default behavior of the preprocessor is to emit
        such warnings.
        """
        self.silenceMissingDirectiveWarnings = value

    def addDefines(self, defines):
        """
        Adds the specified defines to the preprocessor.
        ``defines`` may be a dictionary object or an iterable of key/value pairs
        (as tuples or other iterables of length two)
        """
        self.context.update(defines)

    def clone(self):
        """
        Create a clone of the current processor, including line ending
        settings, marker, variable definitions, output stream.
        """
        rv = Preprocessor()
        rv.context.update(self.context)
        rv.setMarker(self.marker)
        rv.out = self.out
        return rv

    def processFile(self, input, output, depfile=None):
        """
        Preprocesses the contents of the ``input`` stream and writes the result
        to the ``output`` stream. If ``depfile`` is set,  the dependencies of
        ``output`` file are written to ``depfile`` in Makefile format.
        """
        self.out = output

        self.do_include(input, False)
        self.failUnused(input.name)

        if depfile:
            mk = Makefile()
            mk.create_rule([output.name]).add_dependencies(self.includes)
            mk.dump(depfile)

    def computeDependencies(self, input):
        """
        Reads the ``input`` stream, and computes the dependencies for that input.
        """
        try:
            old_out = self.out
            self.out = None
            self.do_include(input, False)

            return self.includes
        finally:
            self.out = old_out

    def applyFilters(self, aLine):
        for f in self.filters:
            aLine = f[1](aLine)
        return aLine

    def noteLineInfo(self):
        # Record the current line and file. Called once before transitioning
        # into or out of an included file and after writing each line.
        self.line_info = self.context["FILE"], self.context["LINE"]

    def write(self, aLine):
        """
        Internal method for handling output.
        """
        if not self.out:
            return

        next_line, next_file = self.context["LINE"], self.context["FILE"]
        if self.checkLineNumbers:
            expected_file, expected_line = self.line_info
            expected_line += 1
            if (
                expected_line != next_line
                or expected_file
                and expected_file != next_file
            ):
                self.out.write(f'//@line {next_line} "{next_file}"\n')
        self.noteLineInfo()

        filteredLine = self.applyFilters(aLine)
        if filteredLine != aLine:
            self.actionLevel = 2
        self.out.write(filteredLine)

    def handleCommandLine(self, args, defaultToStdin=False):
        """
        Parse a commandline into this parser.
        Uses OptionParser internally, no args mean sys.argv[1:].
        """

        def get_output_file(path, encoding=None):
            if encoding is None:
                encoding = "utf-8"
            dir = os.path.dirname(path)
            if dir:
                os.makedirs(dir, exist_ok=True)

            return open(path, "w", encoding=encoding, newline="\n")

        p = self.getCommandLineParser()
        options, args = p.parse_args(args=args)
        out = self.out
        encoded_out = None
        depfile = None

        if options.output:
            out = get_output_file(options.output, options.output_encoding)
        elif options.output_encoding:
            if isinstance(out, io.TextIOBase):
                raise Preprocessor.Error(
                    self,
                    "--output-encoding without --output requires a binary output stream",
                    None,
                )
            # No --output to open ourselves, so buffer the result and encode it
            # into the caller-provided stream, which must accept bytes.
            encoded_out = out
            out = io.StringIO()
        if defaultToStdin and len(args) == 0:
            args = [sys.stdin]
            if options.depend:
                raise Preprocessor.Error(self, "--depend doesn't work with stdin", None)
        if options.depend:
            if not options.output:
                raise Preprocessor.Error(
                    self, "--depend doesn't work with stdout", None
                )
            depfile = get_output_file(options.depend)

        if args:
            for f in args:
                if not isinstance(f, io.TextIOBase):
                    f = open(f, encoding="utf-8")
                with f as input_:
                    self.processFile(input=input_, output=out)
            if depfile:
                mk = Makefile()
                mk.create_rule([options.output]).add_dependencies(self.includes)
                mk.dump(depfile)
                depfile.close()

        if options.output:
            out.close()
        elif encoded_out is not None:
            encoded_out.write(out.getvalue().encode(options.output_encoding))

    def getCommandLineParser(self):
        numberValue = re.compile(r"\d+$")

        def handleD(option, opt, value, parser):
            vals = value.split("=", 1)
            if len(vals) == 1:
                vals.append(1)
            elif numberValue.match(vals[1]):
                vals[1] = int(vals[1])
            self.context[vals[0]] = vals[1]

        def handleU(option, opt, value, parser):
            del self.context[value]

        def handleF(option, opt, value, parser):
            self.do_filter(value)

        def handleMarker(option, opt, value, parser):
            self.setMarker(value)

        def handleSilenceDirectiveWarnings(option, opt, value, parse):
            self.setSilenceDirectiveWarnings(True)

        p = OptionParser()
        p.add_option(
            "-D",
            action="callback",
            callback=handleD,
            type="string",
            metavar="VAR[=VAL]",
            help="Define a variable",
        )
        p.add_option(
            "-U",
            action="callback",
            callback=handleU,
            type="string",
            metavar="VAR",
            help="Undefine a variable",
        )
        p.add_option(
            "-F",
            action="callback",
            callback=handleF,
            type="string",
            metavar="FILTER",
            help="Enable the specified filter",
        )
        p.add_option(
            "-o",
            "--output",
            type="string",
            default=None,
            metavar="FILENAME",
            help="Output to the specified file instead of stdout",
        )
        p.add_option(
            "--depend",
            type="string",
            default=None,
            metavar="FILENAME",
            help="Generate dependencies in the given file",
        )
        p.add_option(
            "--marker",
            action="callback",
            callback=handleMarker,
            type="string",
            help="Use the specified marker instead of #",
        )
        p.add_option(
            "--silence-missing-directive-warnings",
            action="callback",
            callback=handleSilenceDirectiveWarnings,
            help="Don't emit warnings about missing directives",
        )
        p.add_option(
            "--output-encoding",
            type="string",
            default=None,
            metavar="ENCODING",
            help="Encoding to use for the output",
        )
        return p

    def handleLine(self, aLine):
        """
        Handle a single line of input (internal).
        """
        if self.actionLevel == 0 and self.comment.match(aLine):
            self.actionLevel = 1
        m = self.instruction.match(aLine)
        if m:
            args = None
            cmd = m.group("cmd")
            try:
                args = m.group("args")
            except IndexError:
                pass
            if cmd not in self.cmds:
                raise Preprocessor.Error(self, "INVALID_CMD", aLine)
            level, cmd = self.cmds[cmd]
            if level >= self.disableLevel:
                cmd(args)
            if cmd != "literal":
                self.actionLevel = 2
        elif self.disableLevel == 0:
            if self.comment.match(aLine):
                # make sure the comment is not ambiguous with a command
                m = self.ambiguous_comment.match(aLine)
                if m:
                    cmd = m.group("cmd")
                    if cmd in self.cmds:
                        raise Preprocessor.Error(self, "AMBIGUOUS_COMMENT", aLine)
            else:
                self.write(aLine)

    # Instruction handlers
    # These are named do_'instruction name' and take one argument

    # Variables
    def do_define(self, args):
        m = re.match(r"(?P<name>\w+)(?:\s(?P<value>.*))?", args, re.U)
        if not m:
            raise Preprocessor.Error(self, "SYNTAX_DEF", args)
        val = ""
        if m.group("value"):
            val = self.applyFilters(m.group("value"))
            try:
                val = int(val)
            except Exception:
                pass
        self.context[m.group("name")] = val

    def do_undef(self, args):
        m = re.match(r"(?P<name>\w+)$", args, re.U)
        if not m:
            raise Preprocessor.Error(self, "SYNTAX_DEF", args)
        if args in self.context:
            del self.context[args]

    # Logic
    def ensure_not_else(self):
        if len(self.ifStates) == 0 or self.ifStates[-1] == 2:
            sys.stderr.write(
                "WARNING: bad nesting of #else in %s\n" % self.context["FILE"]
            )

    def do_if(self, args, replace=False):
        if self.disableLevel and not replace:
            self.disableLevel += 1
            return
        val = None
        try:
            e = Expression(args)
            val = e.evaluate(self.context)
        except Exception:
            # XXX do real error reporting
            raise Preprocessor.Error(self, "SYNTAX_ERR", args)
        if isinstance(val, (str, bytes)):
            # we're looking for a number value, strings are false
            val = False
        if not val:
            self.disableLevel = 1
        if replace:
            if val:
                self.disableLevel = 0
            self.ifStates[-1] = self.disableLevel
        else:
            self.ifStates.append(self.disableLevel)

    def do_ifdef(self, args, replace=False):
        if self.disableLevel and not replace:
            self.disableLevel += 1
            return
        if re.search(r"\W", args, re.U):
            raise Preprocessor.Error(self, "INVALID_VAR", args)
        if args not in self.context:
            self.disableLevel = 1
        if replace:
            if args in self.context:
                self.disableLevel = 0
            self.ifStates[-1] = self.disableLevel
        else:
            self.ifStates.append(self.disableLevel)

    def do_ifndef(self, args, replace=False):
        if self.disableLevel and not replace:
            self.disableLevel += 1
            return
        if re.search(r"\W", args, re.U):
            raise Preprocessor.Error(self, "INVALID_VAR", args)
        if args in self.context:
            self.disableLevel = 1
        if replace:
            if args not in self.context:
                self.disableLevel = 0
            self.ifStates[-1] = self.disableLevel
        else:
            self.ifStates.append(self.disableLevel)

    def do_else(self, args, ifState=2):
        self.ensure_not_else()
        hadTrue = self.ifStates[-1] == 0
        self.ifStates[-1] = ifState  # in-else
        if hadTrue:
            self.disableLevel = 1
            return
        self.disableLevel = 0

    def do_elif(self, args):
        if self.disableLevel == 1:
            if self.ifStates[-1] == 1:
                self.do_if(args, replace=True)
        else:
            self.do_else(None, self.ifStates[-1])

    def do_elifdef(self, args):
        if self.disableLevel == 1:
            if self.ifStates[-1] == 1:
                self.do_ifdef(args, replace=True)
        else:
            self.do_else(None, self.ifStates[-1])

    def do_elifndef(self, args):
        if self.disableLevel == 1:
            if self.ifStates[-1] == 1:
                self.do_ifndef(args, replace=True)
        else:
            self.do_else(None, self.ifStates[-1])

    def do_endif(self, args):
        if self.disableLevel > 0:
            self.disableLevel -= 1
        if self.disableLevel == 0:
            self.ifStates.pop()

    # output processing
    def do_expand(self, args):
        lst = re.split(r"__(\w+)__", args, re.U)

        def vsubst(v):
            if v in self.context:
                return _to_text(self.context[v])
            return ""

        for i in range(1, len(lst), 2):
            lst[i] = vsubst(lst[i])
        lst.append("\n")  # add back the newline
        self.write("".join(lst))

    def do_literal(self, args):
        self.write(args + "\n")

    def do_filter(self, args):
        filters = [f for f in args.split(" ") if hasattr(self, "filter_" + f)]
        if len(filters) == 0:
            return
        current = dict(self.filters)
        for f in filters:
            current[f] = getattr(self, "filter_" + f)
        self.filters = [(fn, current[fn]) for fn in sorted(current.keys())]
        return

    def do_unfilter(self, args):
        filters = args.split(" ")
        current = dict(self.filters)
        for f in filters:
            if f in current:
                del current[f]
        self.filters = [(fn, current[fn]) for fn in sorted(current.keys())]

    # Filters
    #
    # emptyLines: Strips blank lines from the output.
    def filter_emptyLines(self, aLine):
        if aLine == "\n":
            return ""
        return aLine

    # dumbComments: Empties out lines that consists of optional whitespace
    # followed by a `//`.
    def filter_dumbComments(self, aLine):
        return re.sub(r"^\s*//.*", "", aLine)

    # substitution: variables wrapped in @ are replaced with their value.
    def filter_substitution(self, aLine, fatal=True):
        def repl(matchobj):
            varname = matchobj.group("VAR")
            if varname in self.context:
                return _to_text(self.context[varname])
            if fatal:
                raise Preprocessor.Error(self, "UNDEFINED_VAR", varname)
            return matchobj.group(0)

        return self.varsubst.sub(repl, aLine)

    # attemptSubstitution: variables wrapped in @ are replaced with their
    # value, or an empty string if the variable is not defined.
    def filter_attemptSubstitution(self, aLine):
        return self.filter_substitution(aLine, fatal=False)

    # File ops
    def do_include(self, args, filters=True):
        """
        Preprocess a given file.
        args can either be a file name, or a file-like object.
        Files should be opened, and will be closed after processing.
        """
        isName = isinstance(args, str)
        oldCheckLineNumbers = self.checkLineNumbers
        self.checkLineNumbers = False
        if isName:
            try:
                args = _to_text(args)
                if filters:
                    args = self.applyFilters(args)
                if not os.path.isabs(args):
                    args = os.path.join(self.curdir, args)
                args = open(args, encoding="utf-8")
            except Preprocessor.Error:
                raise
            except Exception:
                raise Preprocessor.Error(self, "FILE_NOT_FOUND", _to_text(args))
        self.checkLineNumbers = bool(
            re.search(r"\.(js|jsm|mjs|java|webidl)(?:\.in)?$", args.name)
        )
        oldFile = self.context["FILE"]
        oldLine = self.context["LINE"]
        oldDir = self.context["DIRECTORY"]
        oldCurdir = self.curdir
        self.noteLineInfo()

        if args.isatty():
            # we're stdin, use '-' and '' for file and dir
            self.context["FILE"] = "-"
            self.context["DIRECTORY"] = ""
            self.curdir = "."
        else:
            abspath = os.path.abspath(args.name)
            self.curdir = os.path.dirname(abspath)
            self.includes.add(abspath)
            if self.topobjdir and path_starts_with(abspath, self.topobjdir):
                abspath = "$OBJDIR" + normsep(abspath[len(self.topobjdir) :])
            elif self.topsrcdir and path_starts_with(abspath, self.topsrcdir):
                abspath = "$SRCDIR" + normsep(abspath[len(self.topsrcdir) :])
            self.context["FILE"] = abspath
            self.context["DIRECTORY"] = os.path.dirname(abspath)
        self.context["LINE"] = 0

        for l in args:
            self.context["LINE"] += 1
            self.handleLine(l)
        if isName:
            args.close()

        self.context["FILE"] = oldFile
        self.checkLineNumbers = oldCheckLineNumbers
        self.context["LINE"] = oldLine
        self.context["DIRECTORY"] = oldDir
        self.curdir = oldCurdir

    def do_includesubst(self, args):
        args = self.filter_substitution(args)
        self.do_include(args)

    def do_error(self, args):
        raise Preprocessor.Error(self, "Error: ", _to_text(args))


def preprocess(includes=[sys.stdin], defines={}, output=sys.stdout, marker="#"):
    pp = Preprocessor(defines=defines, marker=marker)
    for f in includes:
        with open(f, encoding="utf-8") as input:
            pp.processFile(input=input, output=output)
    return pp.includes


# Keep this module independently executable.
if __name__ == "__main__":
    pp = Preprocessor()
    pp.handleCommandLine(None, True)
