# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# This action is used to produce test archives.
#
# Ideally, the data in this file should be defined in moz.build files.
# It is defined inline because this was easiest to make test archive
# generation faster.

import argparse
import itertools
import os
import sys
import time

import buildconfig
import mozpack.path as mozpath
from manifestparser import TestManifest
from mozpack.archive import create_tar_gz_from_files, create_tar_zst_from_files
from mozpack.copier import FileRegistry
from mozpack.files import ExistingFile, FileFinder
from mozpack.manifests import InstallManifest
from mozpack.mozjar import JarWriter
from reftest import ReftestManifest

from mozbuild.dirutils import ensureParentDir

STAGE = mozpath.join(buildconfig.topobjdir, "dist", "test-stage")

TEST_HARNESS_BINS = [
    "BadCertAndPinningServer",
    "DelegatedCredentialsServer",
    "EncryptedClientHelloServer",
    "FaultyServer",
    "GenerateOCSPResponse",
    "OCSPStaplingServer",
    "SanctionsTestServer",
    "ZeroRttAcceptServer",
    "SmokeDMD",
    "certutil",
    "crashinject",
    "geckodriver",
    "http3server",
    "content_analysis_sdk_agent",
    "minidumpwriter",
    "pk12util",
    "screenshot",
    "screentopng",
    "ssltunnel",
    "test_stub_installer",
    "xpcshell",
    "plugin-container",
]

TEST_HARNESS_DLLS = ["crashinjectdll", "mozglue", "msvcp*", "vcruntime*"]

TRAIN_HOP_DLLS = ["xul", "nss3", "nssutil3", "gkcodecs", "lgpllibs", "mozinference"]

GMP_TEST_PLUGIN_DIRS = ["gmp-fake/**", "gmp-fakeopenh264/**"]

# These entries will be used by artifact builds to re-construct an
# objdir with the appropriate generated support files.
OBJDIR_TEST_FILES = {
    "xpcshell": {
        "source": buildconfig.topobjdir,
        "base": "_tests/xpcshell",
        "pattern": "**",
        "dest": "xpcshell/tests",
    },
    "mochitest": {
        "source": buildconfig.topobjdir,
        "base": "_tests/testing",
        "pattern": "mochitest/**",
    },
}


ARCHIVE_FILES = {
    "common": [
        {
            "source": STAGE,
            "base": "",
            "pattern": "**",
            "ignore": [
                "cppunittest/**",
                "condprof/**",
                "gtest/**",
                "mochitest/**",
                "reftest/**",
                "talos/**",
                "raptor/**",
                "awsy/**",
                "web-platform/**",
                "xpcshell/**",
                "updater-dep/**",
                "jsreftest/**",
                "jit-test/**",
                "jittest/**",  # To make the ignore checker happy
                "perftests/**",
                "fuzztest/**",
                "trainhop/**",
            ],
        },
        {"source": buildconfig.topobjdir, "base": "_tests", "pattern": "modules/**"},
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/marionette",
            "patterns": ["client/**", "harness/**", "mach_test_package_commands.py"],
            "dest": "marionette",
            "ignore": ["client/docs", "harness/marionette_harness/tests"],
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "",
            "manifests": [
                "testing/marionette/harness/marionette_harness/tests/unit-tests.toml"
            ],
            # We also need the manifests and harness_unit tests
            "pattern": "testing/marionette/harness/marionette_harness/tests/**",
            "dest": "marionette/tests",
        },
        {"source": buildconfig.topobjdir, "base": "_tests", "pattern": "mozbase/**"},
        {
            "source": buildconfig.topsrcdir,
            "base": "testing",
            "pattern": "firefox-ui/**",
            "ignore": ["firefox-ui/tests"],
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "",
            "pattern": "testing/firefox-ui/tests",
            "dest": "firefox-ui/tests",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "toolkit/components/telemetry/tests/marionette",
            "pattern": "/**",
            "dest": "telemetry/marionette",
        },
        {"source": buildconfig.topsrcdir, "base": "testing", "pattern": "tps/**"},
        {
            "source": buildconfig.topsrcdir,
            "base": "services/sync/",
            "pattern": "tps/**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "services/sync/tests/tps",
            "pattern": "**",
            "dest": "tps/tests",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/web-platform/tests/tools/wptserve",
            "pattern": "**",
            "dest": "tools/wptserve",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/web-platform/tests/tools/third_party",
            "pattern": "**",
            "dest": "tools/wpt_third_party",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "python/mozterm",
            "pattern": "**",
            "dest": "tools/mozterm",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "xpcom/geckoprocesstypes_generator",
            "pattern": "**",
            "dest": "tools/geckoprocesstypes_generator",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "third_party/python/six",
            "pattern": "**",
            "dest": "tools/six",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "third_party/python/distro",
            "pattern": "**",
            "dest": "tools/distro",
        },
        {"source": buildconfig.topobjdir, "base": "", "pattern": "mozinfo.json"},
        {
            "source": buildconfig.topobjdir,
            "base": "dist/bin",
            "patterns": [
                "%s%s" % (f, buildconfig.substs["BIN_SUFFIX"])
                for f in TEST_HARNESS_BINS
            ]
            + [
                "%s%s%s"
                % (
                    buildconfig.substs["DLL_PREFIX"],
                    f,
                    buildconfig.substs["DLL_SUFFIX"],
                )
                for f in TEST_HARNESS_DLLS
            ],
            "dest": "bin",
        },
        {
            "source": buildconfig.topobjdir,
            "base": "dist/bin",
            "patterns": GMP_TEST_PLUGIN_DIRS,
            "dest": "bin/plugins",
        },
        {
            "source": buildconfig.topobjdir,
            "base": "dist/bin",
            "patterns": ["dmd.py", "fix_stacks.py"],
            "dest": "bin",
        },
        {
            "source": buildconfig.topobjdir,
            "base": "dist/bin/components",
            "patterns": ["httpd.sys.mjs"],
            "dest": "bin/components",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "build/pgo/certs",
            "pattern": "**",
            "dest": "certs",
        },
        # Harness used by testing/mozharness/scripts/devtools_compat.py.
        {
            "source": buildconfig.topobjdir,
            "base": "_tests/testing",
            "pattern": "devtools_compat/**",
        },
    ],
    "cppunittest": [
        {"source": STAGE, "base": "", "pattern": "cppunittest/**"},
        # We don't ship these files if startup cache is disabled, which is
        # rare. But it shouldn't matter for test archives.
        {
            "source": buildconfig.topsrcdir,
            "base": "startupcache/test",
            "pattern": "TestStartupCacheTelemetry.*",
            "dest": "cppunittest",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing",
            "pattern": "runcppunittests.py",
            "dest": "cppunittest",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing",
            "pattern": "remotecppunittests.py",
            "dest": "cppunittest",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing",
            "pattern": "cppunittest.toml",
            "dest": "cppunittest",
        },
        {
            "source": buildconfig.topobjdir,
            "base": "",
            "pattern": "mozinfo.json",
            "dest": "cppunittest",
        },
    ],
    "gtest": [{"source": STAGE, "base": "", "pattern": "gtest/**"}],
    "mochitest": [
        OBJDIR_TEST_FILES["mochitest"],
        {
            "source": buildconfig.topobjdir,
            "base": "_tests/testing",
            "pattern": "mochitest/**",
        },
        {"source": STAGE, "base": "", "pattern": "mochitest/**"},
        {
            "source": buildconfig.topobjdir,
            "base": "",
            "pattern": "mozinfo.json",
            "dest": "mochitest",
        },
        {
            "source": buildconfig.topobjdir,
            "base": "dist/xpi-stage",
            "pattern": "mochijar/**",
            "dest": "mochitest",
        },
        {
            "source": buildconfig.topobjdir,
            "base": "dist/xpi-stage",
            "pattern": "specialpowers/**",
            "dest": "mochitest/extensions",
        },
        # Needed by Windows a11y browser tests.
        {
            "source": buildconfig.topobjdir,
            "base": "accessible/interfaces/ia2",
            "pattern": "IA2Typelib.tlb",
            "dest": "mochitest",
        },
        {
            "source": buildconfig.topobjdir,
            "base": "dist/bin",
            "pattern": "IA2Marshal.dll",
            "dest": "mochitest",
        },
    ],
    "mozharness": [
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/mozharness",
            "pattern": "**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "",
            "pattern": "third_party/python/_venv/**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/mozbase/manifestparser",
            "pattern": "manifestparser/**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/mozbase/mozfile",
            "pattern": "mozfile/**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/mozbase/mozinfo",
            "pattern": "mozinfo/**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/mozbase/mozlog",
            "pattern": "mozlog/**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "python/mozterm",
            "pattern": "mozterm/**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/mozbase/mozprocess",
            "pattern": "mozprocess/**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/mozbase/mozsystemmonitor",
            "pattern": "mozsystemmonitor/**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "third_party/python/six",
            "pattern": "six.py",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "third_party/python/toml",
            "pattern": "**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "third_party/python/tomlkit",
            "pattern": "**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "third_party/python/distro",
            "pattern": "distro/**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "third_party/python/packaging",
            "pattern": "**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "python/mozbuild/mozbuild/action",
            "pattern": "tooltool.py",
            "dest": "external_tools",
        },
    ],
    "reftest": [
        {"source": buildconfig.topobjdir, "base": "_tests", "pattern": "reftest/**"},
        {
            "source": buildconfig.topobjdir,
            "base": "",
            "pattern": "mozinfo.json",
            "dest": "reftest",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "",
            "manifests": [
                "layout/reftests/reftest.list",
                "layout/reftests/reftest-qr.list",
                "testing/crashtest/crashtests.list",
            ],
            "dest": "reftest/tests",
        },
        {
            "source": buildconfig.topobjdir,
            "base": "dist/xpi-stage",
            "pattern": "reftest/**",
            "dest": "reftest",
        },
        {
            "source": buildconfig.topobjdir,
            "base": "dist/xpi-stage",
            "pattern": "specialpowers/**",
            "dest": "reftest",
        },
    ],
    "talos": [
        {"source": buildconfig.topsrcdir, "base": "testing", "pattern": "talos/**"},
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/profiles",
            "pattern": "**",
            "dest": "talos/talos/profile_data",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "third_party/webkit/PerformanceTests",
            "pattern": "**",
            "dest": "talos/talos/tests/webkit/PerformanceTests/",
        },
    ],
    "perftests": [
        {"source": buildconfig.topsrcdir, "pattern": "testing/mozbase/**"},
        {"source": buildconfig.topsrcdir, "pattern": "testing/condprofile/**"},
        {"source": buildconfig.topsrcdir, "pattern": "testing/performance/**"},
        {"source": buildconfig.topsrcdir, "pattern": "third_party/python/**"},
        {"source": buildconfig.topsrcdir, "pattern": "tools/lint/eslint/**"},
        {"source": buildconfig.topsrcdir, "pattern": "**/perftest_*.js"},
        {"source": buildconfig.topsrcdir, "pattern": "**/hooks_*py"},
        {"source": buildconfig.topsrcdir, "pattern": "build/autoconf/**"},
        {"source": buildconfig.topsrcdir, "pattern": "build/moz.configure/**"},
        {"source": buildconfig.topsrcdir, "pattern": "python/**"},
        {"source": buildconfig.topsrcdir, "pattern": "build/mach_initialize.py"},
        {
            "source": buildconfig.topsrcdir,
            "pattern": "python/sites/build.txt",
        },
        {
            "source": buildconfig.topsrcdir,
            "pattern": "python/sites/common.txt",
        },
        {
            "source": buildconfig.topsrcdir,
            "pattern": "python/sites/mach.txt",
        },
        {"source": buildconfig.topsrcdir, "pattern": "mach/**"},
        {
            "source": buildconfig.topsrcdir,
            "pattern": "testing/web-platform/tests/tools/third_party/certifi/**",
        },
        {"source": buildconfig.topsrcdir, "pattern": "testing/mozharness/**"},
        {"source": buildconfig.topsrcdir, "pattern": "browser/config/**"},
        # Certificates for the HTTP/2 server used by the mobile-startup tests.
        {
            "source": buildconfig.topsrcdir,
            "pattern": "testing/raptor/browsertime/utils/http2-cert.pem",
        },
        {
            "source": buildconfig.topsrcdir,
            "pattern": "testing/raptor/browsertime/utils/http2-cert.key",
        },
        {"source": buildconfig.topsrcdir, "pattern": "netwerk/test/unit/http2-ca.pem"},
        {
            "source": buildconfig.topobjdir,
            "base": "_tests/modules",
            "pattern": "**",
            "dest": "bin/modules",
        },
        {
            "source": buildconfig.topobjdir,
            "base": "dist/bin",
            "patterns": [
                "browser/**",
                "chrome/**",
                "chrome.manifest",
                "components/**",
                "content_analysis_sdk_agent",
                "http3server",
                "*.ini",
                "*.toml",
                "localization/**",
                "modules/**",
                "default.locale",
                "greprefs.js",
            ],
            "dest": "bin",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "netwerk/test/http3serverDB",
            "pattern": "**",
            "dest": "netwerk/test/http3serverDB",
        },
    ],
    "condprof": [
        {
            "source": buildconfig.topsrcdir,
            "base": "testing",
            "pattern": "condprofile/**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/mozbase/mozfile",
            "pattern": "**",
            "dest": "condprofile/mozfile",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/mozbase/mozprofile",
            "pattern": "**",
            "dest": "condprofile/mozprofile",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/mozbase/mozdevice",
            "pattern": "**",
            "dest": "condprofile/mozdevice",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/mozbase/mozlog",
            "pattern": "**",
            "dest": "condprofile/mozlog",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "third_party/python/virtualenv",
            "pattern": "**",
            "dest": "condprofile/virtualenv",
        },
    ],
    "raptor": [
        {"source": buildconfig.topsrcdir, "base": "testing", "pattern": "raptor/**"},
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/profiles",
            "pattern": "**",
            "dest": "raptor/raptor/profile_data",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "third_party/webkit/PerformanceTests",
            "pattern": "**",
            "dest": "raptor/raptor/tests/webkit/PerformanceTests/",
        },
    ],
    "awsy": [
        {"source": buildconfig.topsrcdir, "base": "testing", "pattern": "awsy/**"}
    ],
    "web-platform": [
        {
            "source": buildconfig.topsrcdir,
            "base": "testing",
            "pattern": "web-platform/meta/**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing",
            "pattern": "web-platform/mozilla/**",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing",
            "pattern": "web-platform/tests/**",
            "ignore": ["web-platform/tests/tools/wpt_third_party"],
        },
        {
            "source": buildconfig.topobjdir,
            "base": "_tests",
            "pattern": "web-platform/**",
        },
        {
            "source": buildconfig.topobjdir,
            "base": "",
            "pattern": "mozinfo.json",
            "dest": "web-platform",
        },
    ],
    "xpcshell": [
        OBJDIR_TEST_FILES["xpcshell"],
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/xpcshell",
            "patterns": [
                "head.js",
                "mach_test_package_commands.py",
                "moz-http2/**",
                "node-http2/**",
                "node_ip/**",
                "node-ws/**",
                "dns-packet/**",
                "remotexpcshelltests.py",
                "runxpcshelltests.py",
                "selftest.py",
                "xpcshellcommandline.py",
            ],
            "dest": "xpcshell",
        },
        {"source": STAGE, "base": "", "pattern": "xpcshell/**"},
        {
            "source": buildconfig.topobjdir,
            "base": "",
            "pattern": "mozinfo.json",
            "dest": "xpcshell",
        },
        {
            "source": buildconfig.topobjdir,
            "base": "build",
            "pattern": "automation.py",
            "dest": "xpcshell",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "testing/profiles",
            "pattern": "**",
            "dest": "xpcshell/profile_data",
        },
        {
            "source": buildconfig.topobjdir,
            "base": "dist/bin",
            "pattern": "http3server%s" % buildconfig.substs["BIN_SUFFIX"],
            "dest": "xpcshell/http3server",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "netwerk/test/http3serverDB",
            "pattern": "**",
            "dest": "xpcshell/http3server/http3serverDB",
        },
    ],
    "updater-dep": [
        {
            "source": buildconfig.topobjdir,
            "base": "_tests/updater-dep",
            "pattern": "**",
            "dest": "updater-dep",
        },
        # Required by the updater on Linux
        {
            "source": buildconfig.topobjdir,
            "base": "config/external/sqlite",
            "pattern": "libmozsqlite3.so",
            "dest": "updater-dep",
        },
    ],
    "jsreftest": [{"source": STAGE, "base": "", "pattern": "jsreftest/**"}],
    "fuzztest": [
        {"source": buildconfig.topsrcdir, "pattern": "tools/fuzzing/smoke/**"}
    ],
    "jittest": [
        {
            "source": buildconfig.topsrcdir,
            "base": "js/src",
            "pattern": "jit-test/**",
            "dest": "jit-test",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "js/src/tests",
            "pattern": "non262/shell.js",
            "dest": "jit-test/tests",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "js/src/tests",
            "pattern": "non262/Math/shell.js",
            "dest": "jit-test/tests",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "js/src/tests",
            "pattern": "non262/reflect-parse/Match.js",
            "dest": "jit-test/tests",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "js/src/tests",
            "pattern": "lib/**",
            "dest": "jit-test/tests",
        },
        {
            "source": buildconfig.topsrcdir,
            "base": "js/src",
            "pattern": "jsapi.h",
            "dest": "jit-test",
        },
    ],
    "trainhop": [
        {
            "source": buildconfig.topobjdir,
            "base": "dist/bin",
            "patterns": [
                "%s%s" % (f, buildconfig.substs["BIN_SUFFIX"])
                for f in TEST_HARNESS_BINS
            ]
            + [
                "%s%s%s"
                % (
                    buildconfig.substs["DLL_PREFIX"],
                    f,
                    buildconfig.substs["DLL_SUFFIX"],
                )
                for f in TRAIN_HOP_DLLS
            ],
            "dest": "bin",
        },
    ],
}

if buildconfig.substs.get("MOZ_CODE_COVERAGE"):
    ARCHIVE_FILES["common"].append({
        "source": buildconfig.topsrcdir,
        "base": "python/mozbuild/",
        "patterns": ["mozpack/**", "mozbuild/codecoverage/**"],
    })


if (
    buildconfig.substs.get("MOZ_ASAN")
    and buildconfig.substs.get("CC_TYPE") == "clang-cl"
):
    asan_dll = {
        "source": buildconfig.topobjdir,
        "base": "dist/bin",
        "pattern": os.path.basename(buildconfig.substs["MOZ_CLANG_RT_ASAN_LIB_PATH"]),
        "dest": "bin",
    }
    ARCHIVE_FILES["common"].append(asan_dll)


if buildconfig.substs.get("commtopsrcdir"):
    commtopsrcdir = buildconfig.substs.get("commtopsrcdir")
    mozharness_comm = {
        "source": commtopsrcdir,
        "base": "testing/mozharness",
        "pattern": "**",
    }
    ARCHIVE_FILES["mozharness"].append(mozharness_comm)
    marionette_comm = {
        "source": commtopsrcdir,
        "base": "",
        "manifest": "testing/marionette/unit-tests.toml",
        "dest": "marionette/tests/comm",
    }
    ARCHIVE_FILES["common"].append(marionette_comm)
    thunderbirdinstance = {
        "source": commtopsrcdir,
        "base": "testing/marionette",
        "pattern": "thunderbirdinstance.py",
        "dest": "marionette/client/marionette_driver",
    }
    ARCHIVE_FILES["common"].append(thunderbirdinstance)


# "common" is our catch all archive and it ignores things from other archives.
# Verify nothing sneaks into ARCHIVE_FILES without a corresponding exclusion
# rule in the "common" archive.
for k, v in ARCHIVE_FILES.items():
    # Skip mozharness because it isn't staged.
    if k in ("common", "mozharness"):
        continue

    ignores = set(
        itertools.chain(*(e.get("ignore", []) for e in ARCHIVE_FILES["common"]))
    )

    if not any(p.startswith("%s/" % k) for p in ignores):
        raise Exception('"common" ignore list probably should contain %s' % k)


def find_generated_harness_files():
    # TEST_HARNESS_FILES end up in an install manifest at
    # $topsrcdir/_build_manifests/install/_tests.
    manifest = InstallManifest(
        mozpath.join(buildconfig.topobjdir, "_build_manifests", "install", "_tests")
    )
    registry = FileRegistry()
    manifest.populate_registry(registry)
    # Conveniently, the generated files we care about will already
    # exist in the objdir, so we can identify relevant files if
    # they're an `ExistingFile` instance.
    return [
        mozpath.join("_tests", p)
        for p in registry.paths()
        if isinstance(registry[p], ExistingFile)
    ]


def find_files(archive):
    extra_entries = []
    generated_harness_files = find_generated_harness_files()

    if archive == "common":
        # Construct entries ensuring all our generated harness files are
        # packaged in the common tests archive.
        packaged_paths = set()
        for entry in OBJDIR_TEST_FILES.values():
            pat = mozpath.join(entry["base"], entry["pattern"])
            del entry["pattern"]
            patterns = []
            for path in generated_harness_files:
                if mozpath.match(path, pat):
                    patterns.append(path[len(entry["base"]) + 1 :])
                    packaged_paths.add(path)
            if patterns:
                entry["patterns"] = patterns
                extra_entries.append(entry)
        entry = {"source": buildconfig.topobjdir, "base": "_tests", "patterns": []}
        for path in set(generated_harness_files) - packaged_paths:
            entry["patterns"].append(path[len("_tests") + 1 :])
        extra_entries.append(entry)

    for entry in ARCHIVE_FILES[archive] + extra_entries:
        source = entry["source"]
        dest = entry.get("dest")
        base = entry.get("base", "")

        pattern = entry.get("pattern")
        patterns = entry.get("patterns", [])
        if pattern:
            patterns.append(pattern)

        manifest = entry.get("manifest")
        manifests = entry.get("manifests", [])
        if manifest:
            manifests.append(manifest)
        if manifests:
            dirs = find_manifest_dirs(os.path.join(source, base), manifests)
            patterns.extend({f"{d}/**" for d in dirs})

        ignore = list(entry.get("ignore", []))
        ignore.extend(["**/.flake8", "**/.mkdir.done", "**/*.pyc"])

        if archive not in ("common", "updater-dep") and base.startswith("_tests"):
            # We may have generated_harness_files to exclude from this entry.
            for path in generated_harness_files:
                if path.startswith(base):
                    ignore.append(path[len(base) + 1 :])

        common_kwargs = {"find_dotfiles": True, "ignore": ignore}

        finder = FileFinder(os.path.join(source, base), **common_kwargs)

        for pattern in patterns:
            for p, f in finder.find(pattern):
                if dest:
                    p = mozpath.join(dest, p)
                yield p, f


def find_manifest_dirs(topsrcdir, manifests):
    """Routine to retrieve directories specified in a manifest, relative to topsrcdir.

    It does not recurse into manifests, as we currently have no need for that.
    """
    dirs = set()

    for p in manifests:
        p = os.path.join(topsrcdir, p)

        if p.endswith(".ini") or p.endswith(".toml"):
            test_manifest = TestManifest()
            test_manifest.read(p)
            dirs |= set([os.path.dirname(m) for m in test_manifest.manifests()])

        elif p.endswith(".list"):
            m = ReftestManifest()
            m.load(p)
            dirs |= m.dirs

        else:
            raise Exception(
                f'"{os.path.splitext(p)[1]}" is not a supported manifest format.'
            )

    dirs = {mozpath.normpath(d[len(topsrcdir) :]).lstrip("/") for d in dirs}

    # Filter out children captured by parent directories because duplicates
    # will confuse things later on.
    def parents(p):
        while True:
            p = mozpath.dirname(p)
            if not p:
                break
            yield p

    seen = set()
    for d in sorted(dirs, key=len):
        if not any(p in seen for p in parents(d)):
            seen.add(d)

    return sorted(seen)


def main(argv):
    parser = argparse.ArgumentParser(description="Produce test archives")
    parser.add_argument("archive", help="Which archive to generate")
    parser.add_argument("outputfile", help="File to write output to")

    args = parser.parse_args(argv)

    out_file = args.outputfile
    if not out_file.endswith((".tar.gz", ".tar.zst", ".zip")):
        raise Exception("expected tar.gz, tar.zst or zip output file")

    file_count = 0
    t_start = time.monotonic()
    ensureParentDir(out_file)
    res = find_files(args.archive)
    with open(out_file, "wb") as fh:
        # Experimentation revealed that level 5 is significantly faster and has
        # marginally larger sizes than higher values and is the sweet spot
        # for optimal compression. Read the detailed commit message that
        # introduced this for raw numbers.
        if out_file.endswith(".tar.gz"):
            files = dict(res)
            create_tar_gz_from_files(fh, files, compresslevel=5)
            file_count = len(files)
        elif out_file.endswith(".tar.zst"):
            files = dict(res)
            create_tar_zst_from_files(fh, files, compresslevel=5, threads=-1)
            file_count = len(files)
        elif out_file.endswith(".zip"):
            with JarWriter(fileobj=fh, compress_level=5) as writer:
                for p, f in res:
                    writer.add(
                        p.encode("utf-8"), f.read(), mode=f.mode, skip_duplicates=True
                    )
                    file_count += 1
        else:
            raise Exception("unhandled file extension: %s" % out_file)

    duration = time.monotonic() - t_start
    zip_size = os.path.getsize(args.outputfile)
    basename = os.path.basename(args.outputfile)
    print(
        "Wrote %d files in %d bytes to %s in %.2fs"
        % (file_count, zip_size, basename, duration)
    )


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
