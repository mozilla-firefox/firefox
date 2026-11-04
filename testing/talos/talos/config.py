# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.
import copy
import json
import os
import pathlib
import re
import sys
import urllib.parse

from mozlog.commandline import setup_logging

from talos import test, utils
from talos.cmdline import parse_args


class ConfigurationError(Exception):
    pass


DEFAULTS = dict(
    # args to pass to browser
    extra_args=[],
    buildid="testbuildid",
    init_url="getInfo.html",
    env={"NO_EM_RESTART": "1"},
    # base data for all tests. Note that any None here will end up converted to
    # an empty string in useBaseTestDefaults.
    basetest=dict(
        cycles=1,
        profile_path="${talos}/base_profile",
        gecko_profile=False,
        gecko_profile_interval=1,
        gecko_profile_entries=100000,
        resolution=1,
        mainthread=False,
        shutdown=False,
        timeout=3600,
        tpchrome=True,
        tpcycles=10,
        tpmozafterpaint=False,
        tphero=False,
        pdfpaint=True,
        fnbpaint=False,
        firstpaint=False,
        format_pagename=True,
        userready=False,
        testeventmap=[],
        base_vs_ref=False,
        tppagecycles=1,
        tploadnocache=False,
        tpscrolltest=False,
        win_counters=[],
        linux_counters=[],
        mac_counters=[],
        xperf_counters=[],
        setup=None,
        cleanup=None,
        preferences={},
        pine=True,
    ),
)


# keys to generated self.config that are global overrides to tests
GLOBAL_OVERRIDES = (
    "cycles",
    "gecko_profile",
    "gecko_profile_interval",
    "gecko_profile_entries",
    "gecko_profile_features",
    "gecko_profile_threads",
    "gecko_profile_extra_threads",
    "tpcycles",
    "tppagecycles",
    "tpmanifest",
    "tptimeout",
    "tpmozafterpaint",
    "tphero",
    "fnbpaint",
    "pdfpaint",
    "firstpaint",
    "userready",
)


CONF_VALIDATORS = []


def validator(func):
    """
    A decorator that register configuration validators to execute against the
    configuration.

    They will be executed in the order of declaration.
    """
    CONF_VALIDATORS.append(func)
    return func


def convert_url(config, url):
    webserver = config["webserver"]
    if not webserver:
        return url

    if "://" in url:
        # assume a fully qualified url
        return url

    if ".html" in url:
        url = "http://%s/%s" % (webserver, url)

    return url


@validator
def fix_xperf(config):
    # BBB: remove doubly-quoted xperf values from command line
    # (needed for buildbot)
    # https://bugzilla.mozilla.org/show_bug.cgi?id=704654#c43
    if config["xperf_path"]:
        xperf_path = config["xperf_path"]
        quotes = ('"', "'")
        for quote in quotes:
            if xperf_path.startswith(quote) and xperf_path.endswith(quote):
                config["xperf_path"] = xperf_path[1:-1]
                break


@validator
def set_webserver(config):
    # pick a free port
    import socket

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("", 0))
    port = sock.getsockname()[1]
    sock.close()

    config["webserver"] = "127.0.0.1:%d" % port


@validator
def update_prefs(config):
    config.setdefault("preferences", {})

    # update prefs from command line
    prefs = config.pop("extraPrefs")
    if prefs:
        for arg in prefs:
            k, v = arg.split("=", 1)
            config["preferences"][k] = utils.parse_pref(v)


@validator
def fix_init_url(config):
    if "init_url" in config:
        config["init_url"] = convert_url(config, config["init_url"])


@validator
def determine_local_symbols_path(config):
    if "symbols_path" not in config:
        return

    # use objdir/dist/crashreporter-symbols for symbolsPath if none provided
    if (
        not config["symbols_path"]
        and config["develop"]
        and "MOZ_DEVELOPER_OBJ_DIR" in os.environ
    ):
        config["symbols_path"] = os.path.join(
            os.environ["MOZ_DEVELOPER_OBJ_DIR"], "dist", "crashreporter-symbols"
        )


def get_active_tests(config):
    activeTests = config.pop("activeTests").strip().split(":")

    # ensure tests are available
    availableTests = test.test_dict()
    if not set(activeTests).issubset(availableTests):
        missing = [i for i in activeTests if i not in availableTests]
        raise ConfigurationError("No definition found for test(s): %s" % missing)

    return activeTests


def get_global_overrides(config):
    global_overrides = {}
    for key in GLOBAL_OVERRIDES:
        # get global overrides for all tests
        value = config[key]
        if value is not None:
            global_overrides[key] = value
        if key != "gecko_profile":
            config.pop(key)

    return global_overrides


def setup_pdfpaint_test(config, test_instance):
    # Get the root of the location of the PDFs artifact
    if os.environ.get("MOZ_FETCHES_DIR", None):
        pdfs_root = pathlib.Path(os.environ.get("MOZ_FETCHES_DIR"), "talos-pdfs")
    else:
        pdfs_root = pathlib.Path(os.environ.get("MOZBUILD_PATH"), "talos-pdfs")
    if not pdfs_root.exists():
        raise Exception(f"Cannot find webserver root: {pdfs_root}")

    pdfpaint_manifest_path = pathlib.Path(pdfs_root, "pdfpaint.manifest")
    test_manifest_path = pathlib.Path(pdfs_root, "test_manifest.json")
    test_manifest = json.loads(test_manifest_path.read_text(encoding="utf8"))

    # If a pdfpaint test was specified, prevent any chunking
    chunk_number = config.get("pdfpaint_chunk", None)
    if config.get("pdfpaint_name") is not None:
        chunk_number = None

    # Gather all the pdf files that can be used in the test, and write
    # all the pdfs to be tested to the manifest file
    start_ind = 0
    end_ind = None
    pdfs_per_chunk = 100
    if chunk_number is not None:
        start_ind = pdfs_per_chunk * (chunk_number - 1)
        end_ind = pdfs_per_chunk * chunk_number

    pdf_files = set()
    for pdf_info in test_manifest:
        if not pdf_info.get("talos", False):
            continue
        if pdf_info.get("password", None) is not None:
            # PDFs that require passwords cause timeouts
            continue

        pdf_name = pathlib.Path(pdf_info["file"]).name
        if (
            config.get("pdfpaint_name", None) is not None
            and config["pdfpaint_name"] != pdf_name
        ):
            # If a user passed a name of a pdf, skip all the others
            continue

        pdf_files.add(pdf_name)

    if start_ind > len(pdf_files):
        raise ConfigurationError(
            f"Chunk {chunk_number} contains no PDFs to test. "
            f"For {len(pdf_files)} PDFs, the max chunk is "
            f"{int((len(pdf_files) - 1) / pdfs_per_chunk) + 1}."
        )

    # lh_pdfpaint.js renders the PDF named by the placeholder's `file` parameter.
    with pdfpaint_manifest_path.open("w") as f:
        for pdf_file in sorted(list(pdf_files))[start_ind:end_ind]:
            file_param = urllib.parse.quote(
                f"/tests/pdfpaint/pdfs/{pdf_file}", safe="/"
            )
            url = f"http://localhost/tests/pdfpaint/empty.pdf?file={file_param}"
            print(f"{url}{os.linesep}")
            f.write(f"{url}{os.linesep}")

    # Make a symbolic link to the mozbuild pdf folder since the talos
    # webserver depends on having the doc root as the talos folder for getInfo.html
    symlink_dest = pathlib.Path(__file__).parent / "tests" / "pdfpaint" / "pdfs"
    symlink_dest.unlink(missing_ok=True)
    symlink_dest.symlink_to(pdfs_root, target_is_directory=True)
    test_instance.tpmanifest = str(pdfpaint_manifest_path)


def get_test_host(manifest_line):
    match = re.match(r"^http://localhost/page_load_test/tp5n/([^/]+)/", manifest_line)
    host = match.group(1)
    return host + "-talos"


def build_manifest(config, is_multidomain, manifestName):
    # read manifest lines
    with open(manifestName) as fHandle:
        manifestLines = fHandle.readlines()

    # write modified manifest lines
    with open(manifestName + ".develop", "w") as newHandle:
        for line in manifestLines:
            new_host = get_test_host(line) if is_multidomain else config["webserver"]
            newline = line.replace("localhost", new_host)
            newline = newline.replace("page_load_test", "tests")
            newHandle.write(newline)

    newManifestName = manifestName + ".develop"

    # return new manifest
    return newManifestName


def get_test(config, global_overrides, counters, test_instance):
    mozAfterPaint = getattr(test_instance, "tpmozafterpaint", None)
    hero = getattr(test_instance, "tphero", None)
    firstPaint = getattr(test_instance, "firstpaint", None)
    userReady = getattr(test_instance, "userready", None)
    firstNonBlankPaint = getattr(test_instance, "fnbpaint", None)
    pdfPaint = getattr(test_instance, "pdfpaint", None)

    test_instance.update(**global_overrides)

    # update original value of mozAfterPaint, this could be 'false',
    # so check for None
    if mozAfterPaint is not None:
        test_instance.tpmozafterpaint = mozAfterPaint
    if firstNonBlankPaint is not None:
        test_instance.fnbpaint = firstNonBlankPaint
    if firstPaint is not None:
        test_instance.firstpaint = firstPaint
    if userReady is not None:
        test_instance.userready = userReady
    if hero is not None:
        test_instance.tphero = hero
    if pdfPaint is not None:
        test_instance.pdfpaint = pdfPaint

    if test_instance.pdfpaint:
        setup_pdfpaint_test(config, test_instance)

    # fix up url
    url = getattr(test_instance, "url", None)
    if url:
        test_instance.url = utils.interpolate(convert_url(config, url))

    # fix up tpmanifest
    tpmanifest = getattr(test_instance, "tpmanifest", None)
    if tpmanifest:
        is_multidomain = getattr(test_instance, "multidomain", False)
        test_instance.tpmanifest = build_manifest(
            config, is_multidomain, utils.interpolate(tpmanifest)
        )

    # add any counters
    if counters:
        keys = (
            "linux_counters",
            "mac_counters",
            "win_counters",
            "xperf_counters",
        )
        for key in keys:
            if key not in test_instance.keys:
                # only populate attributes that will be output
                continue
            if not isinstance(getattr(test_instance, key, None), list):
                setattr(test_instance, key, [])
            _counters = getattr(test_instance, key)
            _counters.extend([
                counter for counter in counters if counter not in _counters
            ])

    return dict(test_instance.items())


@validator
def tests(config):
    counters = set()
    global_overrides = get_global_overrides(config)
    activeTests = get_active_tests(config)
    test_dict = test.test_dict()

    tests = []
    for test_name in activeTests:
        test_class = test_dict[test_name]
        tests.append(get_test(config, global_overrides, counters, test_class()))
    config["tests"] = tests


def get_browser_config(config):
    required = (
        "extensions",
        "browser_path",
        "browser_wait",
        "extra_args",
        "buildid",
        "env",
        "init_url",
        "webserver",
    )
    optional = {
        "bcontroller_config": "${talos}/bcontroller.json",
        "child_process": "plugin-container",
        "debug": False,
        "debugger": None,
        "debugger_args": None,
        "develop": False,
        "fission": True,
        "process": "",
        "framework": "talos",
        "repository": None,
        "sourcestamp": None,
        "symbols_path": None,
        "test_timeout": 1200,
        "xperf_path": None,
        "error_filename": None,
        "no_upload_results": False,
        "subtests": None,
        "preferences": {},
    }
    browser_config = dict(title=config["title"])
    browser_config.update(dict([(i, config[i]) for i in required]))
    browser_config.update(dict([(i, config.get(i, j)) for i, j in optional.items()]))
    return browser_config


def suites_conf():
    import json

    with open(os.path.join(os.path.dirname(utils.here), "talos.json")) as f:
        return json.load(f)["suites"]


def get_config(argv=None):
    argv = argv or sys.argv[1:]
    cli_opts = parse_args(argv=argv)
    if cli_opts.suite:
        # read the suite config, update the args
        try:
            suite_conf = suites_conf()[cli_opts.suite]
        except KeyError:
            raise ConfigurationError("No such suite: %r" % cli_opts.suite)
        argv += ["-a", ":".join(suite_conf["tests"])]
        # talos_options in the suite config should not override command line
        # options, so we prepend argv with talos_options so that, when parsed,
        # the command line options will clobber the suite config options.
        argv = suite_conf.get("talos_options", []) + argv
        # args needs to be reparsed now
    elif not cli_opts.activeTests:
        raise ConfigurationError("--activeTests or --suite required!")

    if cli_opts.pdfpaint_chunk is not None and cli_opts.pdfpaint_chunk < 1:
        raise ConfigurationError(
            "pdfpaint chunk must be a positive integer greater than or equal to 1"
        )

    cli_opts = parse_args(argv=argv)
    setup_logging("talos", cli_opts, {"tbpl": sys.stdout})
    config = copy.deepcopy(DEFAULTS)
    config.update(cli_opts.__dict__)
    for validate in CONF_VALIDATORS:
        validate(config)
    # remove None Values
    for k, v in config.copy().items():
        if v is None:
            del config[k]
    return config


def get_configs(argv=None):
    config = get_config(argv=argv)
    browser_config = get_browser_config(config)
    return config, browser_config


if __name__ == "__main__":
    cfgs = get_configs()
    print(cfgs[0])
    print()
    print(cfgs[1])
