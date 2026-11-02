# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Parsing of jar.mn files into the entries they declare.

What is done with those entries is decided elsewhere. See the jar.mn
documentation for the format.
"""

import re

import mozpack.path as mozpath


class JarManifestEntry:
    def __init__(self, output, source, is_locale=False, preprocess=False):
        self.output = output
        self.source = source
        self.is_locale = is_locale
        self.preprocess = preprocess


class JarInfo:
    def __init__(self, base_or_jarinfo, name=None):
        if name is None:
            assert isinstance(base_or_jarinfo, JarInfo)
            self.base = base_or_jarinfo.base
            self.name = base_or_jarinfo.name
        else:
            assert not isinstance(base_or_jarinfo, JarInfo)
            self.base = base_or_jarinfo or ""
            self.name = name
            # For compatibility with existing jar.mn files, if there is no
            # base, the jar name is under chrome/
            if not self.base:
                self.name = mozpath.join("chrome", self.name)
        self.relativesrcdir = None
        self.chrome_manifests = []
        self.entries = []


class DeprecatedJarManifest(Exception):
    pass


class JarManifestParser:
    ignore = re.compile(r"\s*(#.*)?$")
    jarline = re.compile(
        r"""
        (?:
            (?:\[(?P<base>[\w\d.\-_\/{}@]+)\]\s*)? # optional [base/path]
            (?P<jarfile>[\w\d.\-_\/{}@]+)\.jar:    # filename.jar:
        |
            (?:\s*(\#.*)?)                         # comment
        )\s*$                                      # whitespaces
        """,
        re.VERBOSE,
    )
    relsrcline = re.compile(r"relativesrcdir\s+(?P<relativesrcdir>.+?):")
    regline = re.compile(r"%\s+(.*)$")
    entryre = r"(?P<optPreprocess>\*)?(?P<optOverwrite>\+?)\s+"
    entryline = re.compile(
        entryre
        + (
            r"(?P<output>[\w\d.\-\_\/+@]+)\s*"
            r"(\((?P<locale>%?)(?P<source>[\w\d.\-\_\/@*]+)\))?\s*$"
        )
    )

    def __init__(self):
        self._current_jar = None
        self._jars = []

    def write(self, line):
        # A Preprocessor instance feeds the parser through calls to this method.

        # Ignore comments and empty lines
        if self.ignore.match(line):
            return

        # A jar manifest file can declare several different sections, each of
        # which applies to a given "jar file". Each of those sections starts
        # with "<name>.jar:", in which case the path is assumed relative to
        # a "chrome" directory, or "[<base/path>] <subpath/name>.jar:", where
        # a base directory is given (usually pointing at the root of the
        # application or addon) and the jar path is given relative to the base
        # directory.
        if self._current_jar is None:
            m = self.jarline.match(line)
            if not m:
                raise RuntimeError(line)
            if m.group("jarfile"):
                self._current_jar = JarInfo(m.group("base"), m.group("jarfile"))
                self._jars.append(self._current_jar)
            return

        # Within each section, there can be three different types of entries:

        # - indications of the relative source directory we pretend to be in
        # when considering localization files, in the following form;
        # "relativesrcdir <path>:"
        m = self.relsrcline.match(line)
        if m:
            if self._current_jar.chrome_manifests or self._current_jar.entries:
                self._current_jar = JarInfo(self._current_jar)
                self._jars.append(self._current_jar)
            self._current_jar.relativesrcdir = m.group("relativesrcdir")
            return

        # - chrome manifest entries, prefixed with "%".
        m = self.regline.match(line)
        if m:
            rline = " ".join(m.group(1).split())
            if rline not in self._current_jar.chrome_manifests:
                self._current_jar.chrome_manifests.append(rline)
            return

        # - entries indicating files to be part of the given jar. They are
        # formed thusly:
        #   "<dest_path>"
        # or
        #   "<dest_path> (<source_path>)"
        # The <dest_path> is where the file(s) will be put in the chrome jar.
        # The <source_path> is where the file(s) can be found in the source
        # directory. The <source_path> may start with a "%" for files part
        # of a localization directory, in which case the "%" counts as the
        # locale.
        # Each entry can be prefixed with "*" for preprocessing.
        m = self.entryline.match(line)
        if m:
            if m.group("optOverwrite"):
                raise DeprecatedJarManifest('The "+" prefix is not supported anymore')
            self._current_jar.entries.append(
                JarManifestEntry(
                    m.group("output"),
                    m.group("source") or mozpath.basename(m.group("output")),
                    is_locale=bool(m.group("locale")),
                    preprocess=bool(m.group("optPreprocess")),
                )
            )
            return

        self._current_jar = None
        self.write(line)

    def __iter__(self):
        return iter(self._jars)
