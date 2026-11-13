# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# This module produces a JSON file that provides basic build info and
# configuration metadata.

import json
import os
import re


def build_dict(config, env=os.environ):
    """
    Build a dict containing data about the build configuration from
    the environment.
    """
    substs = config.substs

    # Check that all required variables are present first.
    required = ["TARGET_CPU", "OS_TARGET"]
    missing = [r for r in required if r not in substs]
    if missing:
        raise Exception(
            "Missing required environment variables: {}".format(", ".join(missing))
        )

    d = {}
    d["topsrcdir"] = config.topsrcdir
    d["topobjdir"] = config.topobjdir

    if config.mozconfig:
        d["mozconfig"] = config.mozconfig

    # os
    o = substs["OS_TARGET"]
    known_os = {"Linux": "linux", "WINNT": "win", "Darwin": "mac", "Android": "android"}
    if o in known_os:
        d["os"] = known_os[o]
    else:
        # Allow unknown values, just lowercase them.
        d["os"] = o.lower()

    # Widget toolkit, just pass the value directly through.
    d["toolkit"] = substs.get("MOZ_WIDGET_TOOLKIT")

    # Application name
    if "MOZ_APP_NAME" in substs:
        d["appname"] = substs["MOZ_APP_NAME"]

    # Build app name
    if "MOZ_BUILD_APP" in substs:
        d["buildapp"] = substs["MOZ_BUILD_APP"]

    # processor
    p = substs["TARGET_CPU"]
    # do some slight massaging for some values
    # TODO: retain specific values in case someone wants them?
    if p.startswith("arm"):
        p = "arm"
    elif re.match("i[3-9]86", p):
        p = "x86"
    d["processor"] = p
    # hardcoded list of 64-bit CPUs
    if p in ["x86_64", "ppc64", "aarch64"]:
        d["bits"] = 64
    # hardcoded list of known 32-bit CPUs
    elif p in ["x86", "arm", "ppc"]:
        d["bits"] = 32
    # other CPUs will wind up with unknown bits

    d["mingw"] = substs.get("CC_TYPE") == "clang" and d["os"] == "win"
    d["debug"] = substs.get("MOZ_DEBUG") == "1"
    d["nightly_build"] = substs.get("NIGHTLY_BUILD") == "1"
    d["early_beta_or_earlier"] = substs.get("EARLY_BETA_OR_EARLIER") == "1"
    d["release_or_beta"] = substs.get("RELEASE_OR_BETA") == "1"
    d["devedition"] = substs.get("MOZ_DEV_EDITION") == "1"
    d["pgo"] = substs.get("MOZ_PGO") == "1"
    d["crashreporter"] = bool(substs.get("MOZ_CRASHREPORTER"))
    d["normandy"] = substs.get("MOZ_NORMANDY") == "1"
    d["datareporting"] = bool(substs.get("MOZ_DATA_REPORTING"))
    d["healthreport"] = substs.get("MOZ_SERVICES_HEALTHREPORT") == "1"
    d["sync"] = substs.get("MOZ_SERVICES_SYNC") == "1"
    # FIXME(emilio): We need to update a lot of WPT expectations before removing this.
    d["stylo"] = True
    d["asan"] = substs.get("MOZ_ASAN") == "1"
    d["tsan"] = substs.get("MOZ_TSAN") == "1"
    d["ubsan"] = substs.get("MOZ_UBSAN") == "1"
    d["telemetry"] = substs.get("MOZ_TELEMETRY_REPORTING") == "1"
    d["tests_enabled"] = substs.get("ENABLE_TESTS") == "1"
    d["bin_suffix"] = substs.get("BIN_SUFFIX", "")
    d["require_signing"] = substs.get("MOZ_REQUIRE_SIGNING") == "1"
    d["official"] = bool(substs.get("MOZILLA_OFFICIAL"))
    d["updater"] = substs.get("MOZ_UPDATER") == "1"
    d["artifact"] = substs.get("MOZ_ARTIFACT_BUILDS") == "1"
    d["ccov"] = substs.get("MOZ_CODE_COVERAGE") == "1"
    d["cc_type"] = substs.get("CC_TYPE")
    d["automation"] = substs.get("MOZ_AUTOMATION") == "1"
    d["dbus_enabled"] = bool(substs.get("MOZ_ENABLE_DBUS"))
    d["enterprise"] = substs.get("MOZ_ENTERPRISE") == "1"

    d["opt"] = not d["debug"] and not d["asan"] and not d["tsan"] and not d["ccov"]

    def guess_platform():
        if d["buildapp"] == "browser":
            p = d["os"]
            if p == "mac":
                p = "macosx64"
            elif d["bits"] == 64:
                p = f"{p}64"
            elif p in ("win",):
                p = f"{p}32"

            if d["asan"]:
                p = f"{p}-asan"

            return p

        if d["buildapp"] == "mobile/android":
            if d["processor"] == "x86_64":
                return "android-x86_64"
            if d["processor"] == "aarch64":
                return "android-aarch64"
            return "android-arm"

    def guess_buildtype():
        if d["asan"]:
            return "asan"
        if d["tsan"]:
            return "tsan"
        if d["ccov"]:
            return "ccov"
        if d["debug"]:
            return "debug"
        if d["pgo"]:
            return "pgo"
        return "opt"

    # if buildapp or bits are unknown, we don't have a configuration similar to
    # any in automation and the guesses are useless.
    if "buildapp" in d and (d["os"] == "mac" or "bits" in d):
        d["platform_guess"] = guess_platform()
        d["buildtype_guess"] = guess_buildtype()
    d["buildtype"] = guess_buildtype()

    if (
        d.get("buildapp", "") == "mobile/android"
        and "MOZ_ANDROID_MIN_SDK_VERSION" in substs
    ):
        d["android_min_sdk"] = substs["MOZ_ANDROID_MIN_SDK_VERSION"]

    return d


def write_mozinfo(file, config, env=os.environ):
    """Write JSON data about the configuration specified in config and an
    environment variable dict to ``|file|``, which may be a filename or file-like
    object.
    See build_dict for information about what  environment variables are used,
    and what keys are produced.
    """
    build_conf = build_dict(config, env)
    if isinstance(file, str):
        file = open(file, "w")

    json.dump(build_conf, file, sort_keys=True, indent=4)
