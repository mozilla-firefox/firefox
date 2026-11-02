# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""jarmaker.py provides a python class to package up chrome content by
processing jar.mn files.

See the documentation for jar.mn on MDC for further details on the format.
"""

import errno
import io
import logging
import os
import re
import sys
from time import localtime

import mozpack.path as mozpath

from mozbuild.action.buildlist import addEntriesToListFile
from mozbuild.preprocessor import Preprocessor
from mozbuild.util import ensure_bytes

if sys.platform == "win32":
    from ctypes import WinError, windll

    CreateHardLink = windll.kernel32.CreateHardLinkA

__all__ = ["JarMaker"]


class ZipEntry:
    """Helper class for jar output.

    This class defines a simple file-like object for a zipfile.ZipEntry
    so that we can consecutively write to it and then close it.
    This methods hooks into ZipFile.writestr on close().
    """

    def __init__(self, name, zipfile):
        self._zipfile = zipfile
        self._name = name
        self._inner = io.BytesIO()

    def write(self, content):
        """Append the given content to this zip entry"""

        self._inner.write(ensure_bytes(content))

    def close(self):
        """The close method writes the content back to the zip file."""

        self._zipfile.writestr(self._name, self._inner.getvalue())


def getModTime(aPath):
    if not os.path.isfile(aPath):
        return localtime(0)
    mtime = os.stat(aPath).st_mtime
    return localtime(mtime)


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


class JarMaker:
    """JarMaker reads jar.mn files and process those into jar files or
    flat directories, along with chrome.manifest files.
    """

    def __init__(
        self, outputFormat="flat", useJarfileManifest=True, useChromeManifest=False
    ):
        self.outputFormat = outputFormat
        self.useJarfileManifest = useJarfileManifest
        self.useChromeManifest = useChromeManifest
        self.pp = Preprocessor()
        self.topsourcedir = None
        self.sourcedirs = []
        self.localedirs = None
        self.l10nbase = None
        self.relativesrcdir = None
        self._seen_output = set()

    def getCommandLineParser(self):
        """Get a optparse.OptionParser for jarmaker.

        This OptionParser has the options for jarmaker as well as
        the options for the inner PreProcessor.
        """

        # HACK, we need to unescape the string variables we get,
        # the perl versions didn't grok strings right

        p = self.pp.getCommandLineParser(unescapeDefines=True)
        p.add_option(
            "-f",
            type="choice",
            default="jar",
            choices=("jar", "flat", "symlink"),
            help="fileformat used for output",
            metavar="[jar, flat, symlink]",
        )
        p.add_option("-v", action="store_true", dest="verbose", help="verbose output")
        p.add_option("-q", action="store_false", dest="verbose", help="verbose output")
        p.add_option(
            "-e",
            action="store_true",
            help="create chrome.manifest instead of jarfile.manifest",
        )
        p.add_option(
            "-s", type="string", action="append", default=[], help="source directory"
        )
        p.add_option("-t", type="string", help="top source directory")
        p.add_option(
            "-c",
            "--l10n-src",
            type="string",
            action="append",
            help="localization directory",
        )
        p.add_option(
            "--l10n-base",
            type="string",
            action="store",
            help="merged directory to be used for localization (requires relativesrcdir)",
        )
        p.add_option(
            "--relativesrcdir",
            type="string",
            help="relativesrcdir to be used for localization",
        )
        p.add_option("-d", type="string", help="base directory")
        return p

    def finalizeJar(
        self, jardir, jarbase, jarname, chromebasepath, register, doZip=True
    ):
        """Helper method to write out the chrome registration entries to
         jarfile.manifest or chrome.manifest, or both.

        The actual file processing is done in updateManifest.
        """

        # rewrite the manifest, if entries given
        if not register:
            return

        chromeManifest = os.path.join(jardir, jarbase, "chrome.manifest")

        if self.useJarfileManifest:
            self.updateManifest(
                os.path.join(jardir, jarbase, jarname + ".manifest"),
                chromebasepath.format(""),
                register,
            )
            if jarname != "chrome":
                addEntriesToListFile(chromeManifest, [f"manifest {jarname}.manifest"])
        if self.useChromeManifest:
            chromebase = os.path.dirname(jarname) + "/"
            self.updateManifest(
                chromeManifest, chromebasepath.format(chromebase), register
            )

    def updateManifest(self, manifestPath, chromebasepath, register):
        """updateManifest replaces the % in the chrome registration entries
        with the given chrome base path, and updates the given manifest file.
        """
        myregister = dict.fromkeys(
            map(lambda s: s.replace("%", chromebasepath), register)
        )
        addEntriesToListFile(manifestPath, myregister.keys())

    def makeJar(self, infile, jardir):
        """makeJar is the main entry point to JarMaker.

        It takes the input file, the output directory, the source dirs and the
        top source dir as argument, and optionally the l10n dirs.
        """

        # making paths absolute, guess srcdir if file and add to sourcedirs
        def _normpath(p):
            return os.path.normpath(os.path.abspath(p))

        self.topsourcedir = _normpath(self.topsourcedir)
        self.sourcedirs = [_normpath(p) for p in self.sourcedirs]
        if self.localedirs:
            self.localedirs = [_normpath(p) for p in self.localedirs]
        elif self.relativesrcdir:
            self.localedirs = self.generateLocaleDirs(self.relativesrcdir)
        if isinstance(infile, str):
            logging.info("processing " + infile)
            self.sourcedirs.append(_normpath(os.path.dirname(infile)))
        pp = self.pp.clone()
        pp.out = JarManifestParser()
        pp.do_include(infile)

        for info in pp.out:
            self.processJarSection(info, jardir)

    def generateLocaleDirs(self, relativesrcdir):
        if os.path.basename(relativesrcdir) == "locales":
            # strip locales
            l10nrelsrcdir = os.path.dirname(relativesrcdir)
        else:
            l10nrelsrcdir = relativesrcdir
        locdirs = []

        # generate locales merge or en-US
        if self.l10nbase:
            locdirs.append(os.path.join(self.l10nbase, l10nrelsrcdir))
        else:
            # add en-US if it's not l10n
            locdirs.append(os.path.join(self.topsourcedir, relativesrcdir, "en-US"))
        return locdirs

    def processJarSection(self, jarinfo, jardir):
        """Internal method called by makeJar to actually process a section
        of a jar.mn file.
        """

        # chromebasepath is used for chrome registration manifests
        # {0} is getting replaced with chrome/ for chrome.manifest, and with
        # an empty string for jarfile.manifest

        chromebasepath = "{0}" + os.path.basename(jarinfo.name)
        if self.outputFormat == "jar":
            chromebasepath = "jar:" + chromebasepath + ".jar!"
        chromebasepath += "/"

        jarfile = os.path.join(jardir, jarinfo.base, jarinfo.name)
        jf = None
        if self.outputFormat == "jar":
            # jar
            jarfilepath = jarfile + ".jar"
            os.makedirs(os.path.dirname(jarfilepath), exist_ok=True)
            from MozZipFile import ZipFile

            jf = ZipFile(jarfilepath, "a", lock=True)
            outHelper = self.OutputHelper_jar(jf)
        else:
            outHelper = getattr(self, "OutputHelper_" + self.outputFormat)(jarfile)

        if jarinfo.relativesrcdir:
            self.localedirs = self.generateLocaleDirs(jarinfo.relativesrcdir)

        for e in jarinfo.entries:
            self._processEntryLine(e, outHelper, jf)

        self.finalizeJar(
            jardir, jarinfo.base, jarinfo.name, chromebasepath, jarinfo.chrome_manifests
        )
        if jf is not None:
            jf.close()

    def _processEntryLine(self, e, outHelper, jf):
        out = e.output
        src = e.source

        # pick the right sourcedir -- l10n, topsrc or src

        if e.is_locale:
            # If the file is a Fluent l10n resource, we want to skip the
            # 'en-US' fallbacking.
            #
            # To achieve that, we're testing if we have more than one localedir,
            # and if the last of those has 'en-US' in it.
            # If that's the case, we're removing the last one.
            if (
                e.source.endswith(".ftl")
                and len(self.localedirs) > 1
                and "en-US" in self.localedirs[-1]
            ):
                src_base = self.localedirs[:-1]
            else:
                src_base = self.localedirs
        elif src.startswith("/"):
            # path/in/jar/file_name.xul     (/path/in/sourcetree/file_name.xul)
            # refers to a path relative to topsourcedir, use that as base
            # and strip the leading '/'
            src_base = [self.topsourcedir]
            src = src[1:]
        else:
            # use srcdirs and the objdir (current working dir) for relative paths
            src_base = self.sourcedirs + [os.getcwd()]

        if "*" in src:

            def _prefix(s):
                for p in s.split("/"):
                    if "*" not in p:
                        yield p + "/"

            prefix = "".join(_prefix(src))
            emitted = set()
            from mozpack.files import FileFinder

            for _srcdir in src_base:
                finder = FileFinder(_srcdir)
                for path, _ in finder.find(src):
                    # If the path was already seen in one of the other source
                    # directories, skip it. That matches the non-wildcard case
                    # below, where we pick the first existing file.
                    reduced_path = path[len(prefix) :]
                    if reduced_path in emitted:
                        continue
                    emitted.add(reduced_path)
                    e = JarManifestEntry(
                        mozpath.join(out, reduced_path),
                        path,
                        is_locale=e.is_locale,
                        preprocess=e.preprocess,
                    )
                    self._processEntryLine(e, outHelper, jf)
            return

        # check if the source file exists
        realsrc = None
        for _srcdir in src_base:
            if os.path.isfile(os.path.join(_srcdir, src)):
                realsrc = os.path.join(_srcdir, src)
                break
        if realsrc is None:
            if jf is not None:
                jf.close()
            raise RuntimeError(
                'File "{}" not found in {}'.format(src, ", ".join(src_base))
            )

        if out in self._seen_output:
            raise RuntimeError("%s already added" % out)
        self._seen_output.add(out)

        if e.preprocess:
            outf = outHelper.getOutput(out, mode="w")
            inf = open(realsrc, encoding="utf-8")
            pp = self.pp.clone()
            if src[-4:] == ".css":
                pp.setMarker("%")
            pp.out = outf
            pp.do_include(inf)
            pp.failUnused(realsrc)
            outf.close()
            inf.close()
            return

        # copy or symlink if newer

        # if the output doesn't exist, we can skip an os.stat call
        out_mod_time = outHelper.getDestModTime(e.output)
        if out_mod_time == localtime(0) or getModTime(realsrc) > out_mod_time:
            if self.outputFormat == "symlink":
                outHelper.symlink(realsrc, out)
                return
            outf = outHelper.getOutput(out)

            # open in binary mode, this can be images etc

            inf = open(realsrc, "rb")
            outf.write(inf.read())
            outf.close()
            inf.close()

    class OutputHelper_jar:
        """Provide getDestModTime and getOutput for a given jarfile."""

        def __init__(self, jarfile):
            self.jarfile = jarfile

        def getDestModTime(self, aPath):
            try:
                info = self.jarfile.getinfo(aPath)
                return info.date_time
            except Exception:
                return localtime(0)

        def getOutput(self, name, mode="wb"):
            return ZipEntry(name, self.jarfile)

    class OutputHelper_flat:
        """Provide getDestModTime and getOutput for a given flat
        output directory. The helper method ensureDirFor is used by
        the symlink subclass.
        """

        def __init__(self, basepath):
            self.basepath = basepath

        def getDestModTime(self, aPath):
            return getModTime(os.path.join(self.basepath, aPath))

        def getOutput(self, name, mode="wb"):
            out = self.ensureDirFor(name)

            # remove previous link or file
            try:
                os.remove(out)
            except OSError as e:
                if e.errno != errno.ENOENT:
                    raise
            if "b" in mode:
                return open(out, mode)
            else:
                return open(out, mode, encoding="utf-8", newline="\n")

        def ensureDirFor(self, name):
            out = os.path.join(self.basepath, name)
            outdir = os.path.dirname(out)
            if not os.path.isdir(outdir):
                os.makedirs(outdir, exist_ok=True)

            return out

    class OutputHelper_symlink(OutputHelper_flat):
        """Subclass of OutputHelper_flat that provides a helper for
        creating a symlink including creating the parent directories.
        """

        def symlink(self, src, dest):
            out = self.ensureDirFor(dest)

            # remove previous link or file
            try:
                os.remove(out)
            except OSError as e:
                if e.errno != errno.ENOENT:
                    raise
            if sys.platform != "win32":
                os.symlink(src, out)
            else:
                # On Win32, use ctypes to create a hardlink
                rv = CreateHardLink(ensure_bytes(out), ensure_bytes(src), None)
                if rv == 0:
                    raise WinError()


def main(args=None):
    args = args or sys.argv
    jm = JarMaker()
    p = jm.getCommandLineParser()
    (options, args) = p.parse_args(args)
    jm.outputFormat = options.f
    jm.sourcedirs = options.s
    jm.topsourcedir = options.t
    if options.e:
        jm.useChromeManifest = True
        jm.useJarfileManifest = False
    if options.l10n_base:
        if not options.relativesrcdir:
            p.error("relativesrcdir required when using l10n-base")
        if options.l10n_src:
            p.error("both l10n-src and l10n-base are not supported")
        jm.l10nbase = options.l10n_base
        jm.relativesrcdir = options.relativesrcdir
    jm.localedirs = options.l10n_src
    noise = logging.INFO
    if options.verbose is not None:
        noise = options.verbose and logging.DEBUG or logging.WARN
    logging.basicConfig(format="%(message)s")
    logging.getLogger().setLevel(noise)
    topsrc = options.t
    topsrc = os.path.normpath(os.path.abspath(topsrc))
    if not args:
        infile = sys.stdin
    else:
        (infile,) = args
        if isinstance(infile, bytes):
            infile = infile.decode()
    jm.makeJar(infile, options.d)
