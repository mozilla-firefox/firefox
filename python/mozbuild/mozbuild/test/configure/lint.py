# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import unittest

import six
from buildconfig import topobjdir, topsrcdir
from mozunit import main

from mozbuild.configure.lint import LintSandbox

test_path = os.path.abspath(__file__)


class LintMeta(type):
    def __new__(mcs, name, bases, attrs):
        def create_test(project, func):
            def test(self):
                return func(self, project)

            return test

        for project in (
            "browser",
            "js",
            "memory",
            "mobile/android",
        ):
            attrs["test_%s" % project.replace("/", "_")] = create_test(
                project, attrs["lint"]
            )

        return type.__new__(mcs, name, bases, attrs)


# We don't actually need python2 compat, but this makes flake8 happy.
@six.add_metaclass(LintMeta)
class Lint(unittest.TestCase):
    def setUp(self):
        self._curdir = os.getcwd()
        os.chdir(topobjdir)

    def tearDown(self):
        os.chdir(self._curdir)

    def lint(self, project):
        sandbox = LintSandbox(
            {
                "MOZCONFIG": os.path.join(
                    os.path.dirname(test_path), "data", "empty_mozconfig"
                ),
            },
            ["configure", "--enable-project=%s" % project, "--help"],
        )
        sandbox.run(os.path.join(topsrcdir, "moz.configure"))


if __name__ == "__main__":
    main()
