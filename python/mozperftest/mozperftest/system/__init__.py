# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
from mozperftest.layers import Layers
from mozperftest.system.android import AndroidDevice
from mozperftest.system.binarysetup import BinarySetup
from mozperftest.system.geckoprofiler import GeckoProfiler
from mozperftest.system.macos import MacosDevice
from mozperftest.system.ml_services import MLServices
from mozperftest.system.pingserver import PingServer
from mozperftest.system.profile import Profile
from mozperftest.system.proxy import ProxyRunner
from mozperftest.system.simpleperf import SimpleperfProfiler
from mozperftest.system.versionproducer import VersionProducer


def get_layers():
    return (
        PingServer,
        Profile,
        MLServices,
        ProxyRunner,
        AndroidDevice,
        MacosDevice,
        SimpleperfProfiler,
        GeckoProfiler,
    )


def pick_system(env, flavor, mach_cmd):
    desktop_layers = [
        PingServer,  # needs to come before Profile
        BinarySetup,  # needs to come before macos
        MacosDevice,
        Profile,
        ProxyRunner,
        VersionProducer,
    ]
    mobile_layers = [
        Profile,
        ProxyRunner,
        BinarySetup,
        AndroidDevice,
        VersionProducer,
    ]

    if flavor == "desktop-browser":
        return Layers(
            env,
            mach_cmd,
            desktop_layers,
        )
    if flavor == "xpcshell":
        return Layers(
            env,
            mach_cmd,
            [
                PingServer,  # needs to come before Profile
                BinarySetup,  # needs to come before macos
                MacosDevice,
                Profile,
                ProxyRunner,
            ],
        )
    if flavor == "mochitest":
        return Layers(
            env,
            mach_cmd,
            [
                PingServer,  # needs to come before Profile
                BinarySetup,  # needs to come before macos
                MacosDevice,
                MLServices,
                Profile,
                ProxyRunner,
                AndroidDevice,
                VersionProducer,
            ],
        )
    if flavor == "eval-mochitest":
        env.set_arg("ml_services", True)

        return Layers(
            env,
            mach_cmd,
            [
                PingServer,  # needs to come before Profile
                BinarySetup,  # needs to come before macos
                MacosDevice,
                MLServices,
                Profile,
                ProxyRunner,
                AndroidDevice,
                VersionProducer,
            ],
        )
    if flavor == "mobile-browser":
        return Layers(env, mach_cmd, mobile_layers)
    if flavor == "custom-script":
        layers = [
            PingServer,  # needs to come before Profile
            Profile,
            ProxyRunner,
            BinarySetup,  # needs to come before macos
            AndroidDevice,
            MacosDevice,
            VersionProducer,
            SimpleperfProfiler,
            GeckoProfiler,
        ]
        return Layers(env, mach_cmd, layers)
    if flavor == "alert":
        # The alert flavor runs other test harnesses that
        # do their own setups so there's no need to setup
        # the system in mozperftest
        return Layers(env, mach_cmd, [])
    raise NotImplementedError(flavor)
