# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from functools import cached_property

from mozunit import main

from common import BaseConfigureTest, ConfigureTestSandbox
from mozbuild.util import ReadOnlyNamespace


def sandbox_class(platform):
    class ConfigureTestSandboxOverridingPlatform(ConfigureTestSandbox):
        @cached_property
        def _wrapped_sys(self):
            sys = {}
            exec("from sys import *", sys)
            sys["platform"] = platform
            return ReadOnlyNamespace(**sys)

    return ConfigureTestSandboxOverridingPlatform


class TargetTest(BaseConfigureTest):
    def get_target(self, args, env={}):
        if "linux" in self.HOST:
            platform = "linux2"
        elif "mingw" in self.HOST or "windows" in self.HOST:
            platform = "win32"
        elif "openbsd6" in self.HOST:
            platform = "openbsd6"
        elif "apple-darwin" in self.HOST:
            platform = "darwin"
        else:
            raise Exception(f"Missing platform for HOST {self.HOST}")
        sandbox = self.get_sandbox({}, {}, args, env, cls=sandbox_class(platform))
        return sandbox._value_for(sandbox["target"]).alias


class TestTargetLinux(TargetTest):
    def test_target(self):
        self.assertEqual(self.get_target([]), self.HOST)
        self.assertEqual(self.get_target(["--target=i686"]), "i686-pc-linux-gnu")
        self.assertEqual(
            self.get_target(["--target=i686-unknown-linux-gnu"]),
            "i686-unknown-linux-gnu",
        )
        self.assertEqual(
            self.get_target(["--target=i686-pc-windows-msvc"]), "i686-pc-windows-msvc"
        )


class TestTargetWindows(TargetTest):
    # BaseConfigureTest uses this as the return value for config.guess
    HOST = "i686-pc-windows-msvc"

    def test_target(self):
        self.assertEqual(self.get_target([]), self.HOST)
        self.assertEqual(
            self.get_target(["--target=x86_64-pc-windows-msvc"]),
            "x86_64-pc-windows-msvc",
        )
        self.assertEqual(self.get_target(["--target=x86_64"]), "x86_64-pc-windows-msvc")

        # The tests above are actually not realistic, because most Windows
        # machines will have a few environment variables that make us not
        # use config.guess.

        # 32-bits process on x86_64 host.
        env = {
            "PROCESSOR_ARCHITECTURE": "x86",
            "PROCESSOR_ARCHITEW6432": "AMD64",
        }
        self.assertEqual(self.get_target([], env), "x86_64-pc-windows-msvc")
        self.assertEqual(
            self.get_target(["--target=i686-pc-windows-msvc"]), "i686-pc-windows-msvc"
        )
        self.assertEqual(self.get_target(["--target=i686"]), "i686-pc-windows-msvc")

        # 64-bits process on x86_64 host.
        env = {
            "PROCESSOR_ARCHITECTURE": "AMD64",
        }
        self.assertEqual(self.get_target([], env), "x86_64-pc-windows-msvc")
        self.assertEqual(
            self.get_target(["--target=i686-pc-windows-msvc"]), "i686-pc-windows-msvc"
        )
        self.assertEqual(self.get_target(["--target=i686"]), "i686-pc-windows-msvc")

        # 32-bits process on x86 host.
        env = {
            "PROCESSOR_ARCHITECTURE": "x86",
        }
        self.assertEqual(self.get_target([], env), "i686-pc-windows-msvc")
        self.assertEqual(
            self.get_target(["--target=x86_64-pc-windows-msvc"]),
            "x86_64-pc-windows-msvc",
        )
        self.assertEqual(self.get_target(["--target=x86_64"]), "x86_64-pc-windows-msvc")

        # While host autodection will give us a -windows-msvc triplet, setting host
        # is expecting to implicitly set the target.
        self.assertEqual(
            self.get_target(["--host=x86_64-pc-windows-gnu"]), "x86_64-pc-windows-gnu"
        )


class TestTargetAndroid(TargetTest):
    HOST = "x86_64-pc-linux-gnu"

    def test_target(self):
        self.assertEqual(
            self.get_target(["--enable-project=mobile/android"]),
            "x86_64-unknown-linux-android",
        )
        self.assertEqual(
            self.get_target(["--enable-project=mobile/android", "--target=i686"]),
            "i686-unknown-linux-android",
        )
        self.assertEqual(
            self.get_target(["--enable-project=mobile/android", "--target=x86_64"]),
            "x86_64-unknown-linux-android",
        )
        self.assertEqual(
            self.get_target(["--enable-project=mobile/android", "--target=aarch64"]),
            "aarch64-unknown-linux-android",
        )
        self.assertEqual(
            self.get_target(["--enable-project=mobile/android", "--target=arm"]),
            "arm-unknown-linux-androideabi",
        )


class TestTargetAndroidAppleSiliconHost(TargetTest):
    HOST = "aarch64-apple-darwin"

    def test_target(self):
        self.assertEqual(
            self.get_target(["--enable-project=mobile/android"]),
            "aarch64-unknown-linux-android",
        )


class TestTargetOpenBSD(TargetTest):
    # config.guess returns amd64 on OpenBSD, which we need to pass through to
    # config.sub so that it canonicalizes to x86_64.
    HOST = "amd64-unknown-openbsd6.4"

    def test_target(self):
        self.assertEqual(self.get_target([]), "x86_64-unknown-openbsd6.4")

    def config_sub(self, stdin, args):
        if args[0] == "amd64-unknown-openbsd6.4":
            return 0, "x86_64-unknown-openbsd6.4", ""
        return super().config_sub(stdin, args)


class TestMozConfigure(BaseConfigureTest):
    def test_nsis_version(self):
        this = self

        class FakeNSIS:
            def __init__(self, version):
                self.version = version

            def __call__(self, stdin, args):
                this.assertEqual(args, ("-version",))
                return 0, self.version, ""

        def check_nsis_version(version):
            sandbox = self.get_sandbox(
                {"/usr/bin/makensis": FakeNSIS(version)},
                {},
                ["--target=x86_64-pc-windows-msvc", "--disable-bootstrap"],
                {"PATH": "/usr/bin", "MAKENSISU": "/usr/bin/makensis"},
            )
            return sandbox._value_for(sandbox["nsis_version"])

        with self.assertRaises(SystemExit):
            check_nsis_version("v2.5")

        with self.assertRaises(SystemExit):
            check_nsis_version("v3.0a2")

        self.assertEqual(check_nsis_version("v3.0b1"), "3.0b1")
        self.assertEqual(check_nsis_version("v3.0b2"), "3.0b2")
        self.assertEqual(check_nsis_version("v3.0rc1"), "3.0rc1")
        self.assertEqual(check_nsis_version("v3.0"), "3.0")
        self.assertEqual(check_nsis_version("v3.0-2"), "3.0")
        self.assertEqual(check_nsis_version("v3.0.1"), "3.0")
        self.assertEqual(check_nsis_version("v3.1"), "3.1")

    def test_want_wine(self):
        def want_wine(args):
            sandbox = self.get_sandbox(
                {}, {}, ["--target=x86_64-pc-windows-msvc", *args]
            )
            return sandbox._value_for(sandbox["want_wine"])

        self.assertTrue(want_wine([]))
        self.assertFalse(want_wine(["--enable-project=js"]))

    def test_wine_missing(self):
        sandbox = self.get_sandbox(
            {},
            {},
            ["--target=x86_64-pc-windows-msvc", "--disable-bootstrap"],
            {"PATH": ""},
        )
        self.assertIsNone(sandbox._value_for(sandbox["wine"]))


class TestConfVars(BaseConfigureTest):
    def test_loading(self):
        sandbox = self.get_sandbox(
            paths={},
            config={},
            args=[
                "--enable-project=python/mozbuild/mozbuild/test/configure/data/confvars"
            ],
        )
        self.assertEqual(
            list(sandbox._helper),
            ["CONFVAR= a b c", "OTHER_CONFVAR=d"],
        )


if __name__ == "__main__":
    main()
