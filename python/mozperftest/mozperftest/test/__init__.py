# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
from mozperftest.layers import Layers
from mozperftest.test.alert import AlertTestRunner
from mozperftest.test.androidlog import AndroidLog
from mozperftest.test.browsertime import BrowsertimeRunner
from mozperftest.test.mochitest import EvalMochitest, PerfMochitest
from mozperftest.test.shellscript import ShellScriptRunner
from mozperftest.test.xpcshell import XPCShell


def get_layers():
    return (
        BrowsertimeRunner,
        AndroidLog,
        XPCShell,
        PerfMochitest,
        ShellScriptRunner,
        AlertTestRunner,
    )


def pick_test(env, flavor, mach_cmd):
    if flavor == "xpcshell":
        return Layers(env, mach_cmd, (XPCShell,))
    if flavor == "desktop-browser":
        return Layers(env, mach_cmd, (BrowsertimeRunner,))
    if flavor == "mobile-browser":
        return Layers(env, mach_cmd, (BrowsertimeRunner, AndroidLog))
    if flavor == "mochitest":
        return Layers(env, mach_cmd, (PerfMochitest,))
    if flavor == "eval-mochitest":
        return Layers(env, mach_cmd, (EvalMochitest,))
    if flavor == "custom-script":
        return Layers(env, mach_cmd, (ShellScriptRunner,))
    if flavor == "alert":
        return Layers(env, mach_cmd, (AlertTestRunner,))

    raise NotImplementedError(flavor)
