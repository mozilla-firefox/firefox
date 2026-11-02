# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import sys
import unittest
from contextlib import contextmanager
from tempfile import mkdtemp

import buildconfig
import mozpack.path as mozpath
from mach.util import get_state_dir
from mozfile import which
from mozpack.files import FileFinder
from mozunit import main

from mozbuild.backend import get_backend_class
from mozbuild.backend.configenvironment import ConfigEnvironment
from mozbuild.backend.fastermake import FasterMakeBackend
from mozbuild.backend.recursivemake import RecursiveMakeBackend
from mozbuild.base import MozbuildObject
from mozbuild.dirutils import ensureParentDir
from mozbuild.frontend.emitter import TreeMetadataEmitter
from mozbuild.frontend.reader import BuildReader


def make_path():
    try:
        return buildconfig.substs["GMAKE"]
    except KeyError:
        toolchains_dir = os.environ.get("MOZ_FETCHES_DIR") or get_state_dir()
        extra_search_dirs = (os.path.join(toolchains_dir, "mozmake"),)
        # Fallback for when running the test without an objdir.
        for name in ("gmake", "make", "mozmake", "gnumake", "mingw32-make"):
            path = which(name, extra_search_dirs=extra_search_dirs)
            if path:
                return path


BASE_SUBSTS = [
    ("PYTHON", mozpath.normsep(sys.executable)),
    ("PYTHON3", mozpath.normsep(sys.executable)),
    ("MOZ_UI_LOCALE", "en-US"),
    ("GMAKE", make_path()),
]


class TestBuild(unittest.TestCase):
    def setUp(self):
        self._old_env = dict(os.environ)
        os.environ.pop("MOZCONFIG", None)
        os.environ.pop("MOZ_OBJDIR", None)
        os.environ.pop("MOZ_PGO", None)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self._old_env)

    @contextmanager
    def do_test_backend(self, *backends, **kwargs):
        # Create the objdir in the srcdir to ensure that they share
        # the same drive on Windows.
        topobjdir = mkdtemp(dir=buildconfig.topsrcdir)
        try:
            config = ConfigEnvironment(buildconfig.topsrcdir, topobjdir, **kwargs)
            reader = BuildReader(config)
            emitter = TreeMetadataEmitter(config)
            moz_build = mozpath.join(config.topsrcdir, "test.mozbuild")
            definitions = list(emitter.emit(reader.read_mozbuild(moz_build, config)))
            for backend in backends:
                backend(config).consume(definitions)

            yield config
        except Exception:
            raise
        finally:
            if not os.environ.get("MOZ_NO_CLEANUP"):
                shutil.rmtree(topobjdir)

    @contextmanager
    def line_handler(self):
        lines = []

        def handle_make_line(line):
            lines.append(line)

        try:
            yield handle_make_line
        except Exception:
            print("\n".join(lines))
            raise

        if os.environ.get("MOZ_VERBOSE_MAKE"):
            print("\n".join(lines))

    def test_recursive_make(self):
        substs = list(BASE_SUBSTS)
        with self.do_test_backend(RecursiveMakeBackend, substs=substs) as config:
            build = MozbuildObject(config.topsrcdir, None, None, config.topobjdir)
            build._config_environment = config
            overrides = [
                "install_manifest_depends=",
                "TEST_MOZBUILD=1",
            ]
            with self.line_handler() as handle_make_line:
                build._run_make(
                    directory=config.topobjdir,
                    target=overrides,
                    silent=False,
                    line_handler=handle_make_line,
                )

            self.validate(config)

    def test_faster_recursive_make(self):
        substs = list(BASE_SUBSTS) + [
            ("BUILD_BACKENDS", "FasterMake+RecursiveMake"),
        ]
        with self.do_test_backend(
            get_backend_class("FasterMake+RecursiveMake"), substs=substs
        ) as config:
            buildid = mozpath.join(config.topobjdir, "config", "buildid")
            ensureParentDir(buildid)
            with open(buildid, "w") as fh:
                fh.write("20100101012345\n")

            build = MozbuildObject(config.topsrcdir, None, None, config.topobjdir)
            build._config_environment = config
            overrides = [
                "install_manifest_depends=",
                "TEST_MOZBUILD=1",
            ]
            with self.line_handler() as handle_make_line:
                build._run_make(
                    directory=config.topobjdir,
                    target=overrides,
                    silent=False,
                    line_handler=handle_make_line,
                )

            self.validate(config)

    def test_faster_make(self):
        substs = list(BASE_SUBSTS) + [
            ("MOZ_BUILD_APP", "dummy_app"),
            ("MOZ_WIDGET_TOOLKIT", "dummy_widget"),
        ]
        with self.do_test_backend(
            RecursiveMakeBackend, FasterMakeBackend, substs=substs
        ) as config:
            buildid = mozpath.join(config.topobjdir, "config", "buildid")
            ensureParentDir(buildid)
            with open(buildid, "w") as fh:
                fh.write("20100101012345\n")

            build = MozbuildObject(config.topsrcdir, None, None, config.topobjdir)
            build._config_environment = config
            overrides = [
                "TEST_MOZBUILD=1",
            ]
            with self.line_handler() as handle_make_line:
                build._run_make(
                    directory=mozpath.join(config.topobjdir, "faster"),
                    target=overrides,
                    silent=False,
                    line_handler=handle_make_line,
                )

            self.validate(config)

    def validate(self, config):
        self.maxDiff = None
        test_path = mozpath.join(
            "$SRCDIR",
            "python",
            "mozbuild",
            "mozbuild",
            "test",
            "backend",
            "data",
            "build",
        )

        result = {
            p: f.open().read().decode()
            for p, f in FileFinder(mozpath.join(config.topobjdir, "dist"))
        }
        self.assertTrue(len(result))
        self.assertEqual(
            result,
            {
                "bin/baz.ini": "baz.ini: FOO is foo\n",
                "bin/child/bar.ini": "bar.ini\n",
                "bin/child2/foo.css": "foo.css: FOO is foo\n",
                "bin/child2/qux.ini": "qux.ini: BAR is not defined\n",
                "bin/chrome.manifest": "manifest chrome/foo.manifest\n"
                "manifest components/components.manifest\n",
                "bin/chrome/foo.manifest": "content bar foo/child/\n"
                "content foo foo/\n"
                "override chrome://foo/bar.svg#hello "
                "chrome://bar/bar.svg#hello\n",
                "bin/chrome/foo/bar.js": "bar.js\n",
                "bin/chrome/foo/child/baz.sys.mjs": '//@line 2 "%s/baz.sys.mjs"\nbaz.sys.mjs: FOO is foo\n'
                % (test_path),
                "bin/chrome/foo/child/hoge.js": '//@line 2 "%s/bar.js"\nbar.js: FOO is foo\n'
                % (test_path),
                "bin/chrome/foo/foo.css": "foo.css: FOO is foo\n",
                "bin/chrome/foo/foo.js": "foo.js\n",
                "bin/chrome/foo/qux.js": "bar.js\n",
                "bin/components/bar.js": '//@line 2 "%s/bar.js"\nbar.js: FOO is foo\n'
                % (test_path),
                "bin/components/components.manifest": "component {foo} foo.js\ncomponent {bar} bar.js\n",  # NOQA: E501
                "bin/components/foo.js": "foo.js\n",
                "bin/defaults/pref/prefs.js": "prefs.js\n",
                "bin/foo.ini": "foo.ini\n",
                "bin/modules/baz.sys.mjs": '//@line 2 "%s/baz.sys.mjs"\nbaz.sys.mjs: FOO is foo\n'
                % (test_path),
                "bin/modules/child/bar.sys.mjs": "bar.sys.mjs\n",
                "bin/modules/child2/qux.sys.mjs": '//@line 4 "%s/qux.sys.mjs"\nqux.sys.mjs: BAR is not defined\n'  # NOQA: E501
                % (test_path),
                "bin/modules/foo.sys.mjs": "foo.sys.mjs\n",
                "bin/res/resource": "resource\n",
                "bin/res/child/resource2": "resource2\n",
                "bin/app/baz.ini": "baz.ini: FOO is bar\n",
                "bin/app/child/bar.ini": "bar.ini\n",
                "bin/app/child2/qux.ini": "qux.ini: BAR is defined\n",
                "bin/app/chrome.manifest": "manifest chrome/foo.manifest\n"
                "manifest components/components.manifest\n",
                "bin/app/chrome/foo.manifest": "content bar foo/child/\n"
                "content foo foo/\n"
                "override chrome://foo/bar.svg#hello "
                "chrome://bar/bar.svg#hello\n",
                "bin/app/chrome/foo/bar.js": "bar.js\n",
                "bin/app/chrome/foo/child/baz.sys.mjs": '//@line 2 "%s/baz.sys.mjs"\nbaz.sys.mjs: FOO is bar\n'
                % (test_path),
                "bin/app/chrome/foo/child/hoge.js": '//@line 2 "%s/bar.js"\nbar.js: FOO is bar\n'
                % (test_path),
                "bin/app/chrome/foo/foo.css": "foo.css: FOO is bar\n",
                "bin/app/chrome/foo/foo.js": "foo.js\n",
                "bin/app/chrome/foo/qux.js": "bar.js\n",
                "bin/app/components/bar.js": '//@line 2 "%s/bar.js"\nbar.js: FOO is bar\n'
                % (test_path),
                "bin/app/components/components.manifest": "component {foo} foo.js\ncomponent {bar} bar.js\n",  # NOQA: E501
                "bin/app/components/foo.js": "foo.js\n",
                "bin/app/defaults/preferences/prefs.js": "prefs.js\n",
                "bin/app/foo.css": "foo.css: FOO is bar\n",
                "bin/app/foo.ini": "foo.ini\n",
                "bin/app/modules/baz.sys.mjs": '//@line 2 "%s/baz.sys.mjs"\nbaz.sys.mjs: FOO is bar\n'
                % (test_path),
                "bin/app/modules/child/bar.sys.mjs": "bar.sys.mjs\n",
                "bin/app/modules/child2/qux.sys.mjs": '//@line 2 "%s/qux.sys.mjs"\nqux.sys.mjs: BAR is defined\n'  # NOQA: E501
                % (test_path),
                "bin/app/modules/foo.sys.mjs": "foo.sys.mjs\n",
            },
        )


if __name__ == "__main__":
    main()
