#!/usr/bin/python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import glob
import os
import re
import subprocess
import sys

import mozcrash
from mozbuild.base import BinaryNotFoundException, MozbuildObject
from mozfile import TemporaryDirectory, json
from mozhttpd import MozHttpd
from mozprofile import FirefoxProfile, Preferences
from mozprofile.permissions import ServerLocations
from mozrunner import CLI, FirefoxRunner

PORT = 8888

PATH_MAPPINGS = {
    "/webkit/PerformanceTests": "third_party/webkit/PerformanceTests",
    # It is tempting to map to `testing/talos/talos/tests` instead, to avoid
    # writing `tests/` in every path, but we can't do that because some files
    # refer to scripts located in `../..`.
    "/talos": "testing/talos/talos",
}


BENCHMARK_DONE = (
    "globalThis.opener && globalThis.opener.postMessage({pgoBenchmarkDone: true}, '*');"
)


def benchmark_done_handler(url_path, file_path, transform):
    """Serve the benchmark script at file_path after passing it through
    transform, which adds a completion message to the PGO driver page
    (see index.html)."""
    with open(file_path, encoding="utf-8") as f:
        content = transform(f.read(), file_path)

    def handler(request):
        return (200, {"Content-Type": "text/javascript"}, content)

    return {"method": "GET", "path": re.escape(url_path) + "$", "function": handler}


def after_anchor(anchor):
    def transform(content, file_path):
        if anchor not in content:
            raise Exception(
                f"Could not find {anchor!r} in {file_path}. If this benchmark was "
                "updated, update its completion anchor in build/pgo/profileserver.py."
            )
        return content.replace(anchor, anchor + BENCHMARK_DONE, 1)

    return transform


def on_event(event):
    def transform(content, file_path):
        if f'"{event}"' not in content:
            raise Exception(
                f"{file_path} no longer dispatches {event!r}. If this benchmark was "
                "updated, update its completion event in build/pgo/profileserver.py."
            )
        listener = (
            f"globalThis.addEventListener({event!r}, () => {{{BENCHMARK_DONE}}});"
        )
        return f"{content}\n{listener}\n"

    return transform


def get_crashreports(directory, name=None):
    rc = 0
    upload_path = os.environ.get("UPLOAD_PATH")
    if not upload_path:
        upload_path = os.environ.get("UPLOAD_DIR")
    if upload_path:
        # For automation, log the minidumps with stackwalk and get them moved to
        # the artifacts directory.
        fetches_dir = os.environ.get("MOZ_FETCHES_DIR")
        if not fetches_dir:
            raise Exception(
                "Unable to process minidump in automation because "
                "$MOZ_FETCHES_DIR is not set in the environment"
            )
        stackwalk_binary = os.path.join(
            fetches_dir, "minidump-stackwalk", "minidump-stackwalk"
        )
        if sys.platform == "win32":
            stackwalk_binary += ".exe"
        minidump_path = os.path.join(directory, "minidumps")
        rc = mozcrash.check_for_crashes(
            minidump_path,
            symbols_path=fetches_dir,
            stackwalk_binary=stackwalk_binary,
            dump_save_path=upload_path,
            test_name=name,
        )
    return rc


class ProfileServerCLI(CLI):
    def __init__(self, args=sys.argv[1:]):
        CLI.__init__(self, args=args)

    def add_options(self, parser):
        CLI.add_options(self, parser)

        # add profileserver options
        parser.add_option(
            "-e",
            "--extended-corpus",
            dest="extended_corpus_dir",
            help="Directory of the optional extended corpus.",
            metavar=None,
            default=None,
        )

    def extended_corpus_dir(self):
        return self.options.extended_corpus_dir


if __name__ == "__main__":
    cli = ProfileServerCLI()
    debug_args, interactive = cli.debugger_arguments()
    runner_args = cli.runner_args()

    build = MozbuildObject.from_environment()

    binary = runner_args.get("binary")
    if not binary:
        try:
            binary = build.get_binary_path(where="staged-package")
        except BinaryNotFoundException as e:
            print(f"{e}\n\n{e.help()}\n")
            sys.exit(1)
    binary = os.path.normpath(os.path.abspath(binary))

    extended_corpus_dir = cli.extended_corpus_dir()
    has_extended_corpus = extended_corpus_dir is not None

    path_mappings = {
        k: os.path.join(build.topsrcdir, v) for k, v in PATH_MAPPINGS.items()
    }
    webkit_dir = os.path.join(
        build.topsrcdir, "third_party", "webkit", "PerformanceTests"
    )
    httpd = MozHttpd(
        port=PORT,
        docroot=os.path.join(build.topsrcdir, "build", "pgo"),
        path_mappings=path_mappings,
        urlhandlers=[
            benchmark_done_handler(
                "/webkit/PerformanceTests/Speedometer/resources/main.js",
                os.path.join(webkit_dir, "Speedometer", "resources", "main.js"),
                after_anchor("didFinishLastIteration: function () {"),
            ),
            benchmark_done_handler(
                "/webkit/PerformanceTests/webaudio/webaudio-bench.js",
                os.path.join(webkit_dir, "webaudio", "webaudio-bench.js"),
                after_anchor("window.postMessage(_data, '*');"),
            ),
        ],
    )
    httpd.start(block=False)

    # Speedometer3 must run in its own server. The benchmark assumes that it
    # is in a root path, and will fail if it's not.
    sp3_dir = os.path.join(webkit_dir, "Speedometer3")
    sp3_httpd = MozHttpd(
        port=8000,
        docroot=sp3_dir,
        path_mappings=path_mappings,
        urlhandlers=[
            benchmark_done_handler(
                "/resources/main.mjs",
                os.path.join(sp3_dir, "resources", "main.mjs"),
                after_anchor("didFinishLastIteration(metrics) {"),
            )
        ],
    )
    sp3_httpd.start(block=False)
    print("started SP3 server on port 8000")

    if has_extended_corpus:
        js3_dir = os.path.join(extended_corpus_dir, "JetStream")

        if not os.path.exists(js3_dir):
            print(f"Error: JetStream directory does not exist at {js3_dir}")
            sys.exit(1)

        # JetStream3 must run in its own server. The benchmark assumes that it
        # is in a root path, and will fail if it's not.
        js3_httpd = MozHttpd(
            port=8001,
            docroot=js3_dir,
            urlhandlers=[
                benchmark_done_handler(
                    "/JetStreamDriver.js",
                    os.path.join(js3_dir, "JetStreamDriver.js"),
                    on_event("JetStreamDone"),
                )
            ],
        )
        js3_httpd.start(block=False)
        print("started JS3 server on port 8001")

    locations = ServerLocations()
    locations.add_host(host="127.0.0.1", port=PORT, options="primary,privileged")

    old_profraw_files = glob.glob("*.profraw")
    for f in old_profraw_files:
        os.remove(f)

    with TemporaryDirectory() as profilePath:
        # TODO: refactor this into mozprofile
        profile_data_dir = os.path.join(build.topsrcdir, "testing", "profiles")
        with open(os.path.join(profile_data_dir, "profiles.json")) as fh:
            base_profiles = json.load(fh)["profileserver"]

        prefpaths = [
            os.path.join(profile_data_dir, profile, "user.js")
            for profile in base_profiles
        ]

        prefs = {}
        for path in prefpaths:
            prefs.update(Preferences.read_prefs(path))

        interpolation = {"server": "%s:%d" % httpd.httpd.server_address}
        sp3_interpolation = {"server": "%s:%d" % sp3_httpd.httpd.server_address}
        for k, v in prefs.items():
            if isinstance(v, str):
                v = v.format(**interpolation)
            prefs[k] = Preferences.cast(v)

        profile = FirefoxProfile(
            profile=profilePath,
            preferences=prefs,
            addons=[
                os.path.join(
                    build.topsrcdir, "tools", "quitter", "quitter@mozilla.org.xpi"
                )
            ],
            locations=locations,
        )

        env = os.environ.copy()
        env["MOZ_CRASHREPORTER_NO_REPORT"] = "1"
        env["MOZ_CRASHREPORTER_SHUTDOWN"] = "1"
        env["XPCOM_DEBUG_BREAK"] = "warn"

        # Ensure different pids write to different files
        # Use absolute path to ensure that Sandbox computes the correct permissions
        env["LLVM_PROFILE_FILE"] = os.path.join(
            os.getcwd(), "default_%p_random_%m.profraw"
        )

        # Write to an output file if we're running in automation
        process_args = {"universal_newlines": True}
        if "UPLOAD_PATH" in env:
            process_args["logfile"] = os.path.join(
                env["UPLOAD_PATH"], "profile-run-1.log"
            )

        # Run Firefox a first time to initialize its profile
        runner = FirefoxRunner(
            profile=profile,
            binary=binary,
            cmdargs=[f"http://localhost:{PORT}/quit.html"],
            env=env,
            process_args=process_args,
        )
        runner.start()
        ret = runner.wait()
        if ret:
            print("Firefox exited with code %d during profile initialization" % ret)
            logfile = process_args.get("logfile")
            if logfile:
                print("Firefox output (%s):" % logfile)
                with open(logfile) as f:
                    print(f.read())
            sp3_httpd.stop()
            if has_extended_corpus:
                js3_httpd.stop()
            httpd.stop()
            get_crashreports(profilePath, name="Profile initialization")
            sys.exit(ret)

        jarlog = os.getenv("JARLOG_FILE")
        if jarlog:
            env["MOZ_JAR_LOG_FILE"] = os.path.abspath(jarlog)
            print("jarlog: %s" % env["MOZ_JAR_LOG_FILE"])
            if os.path.exists(jarlog):
                os.remove(jarlog)

        if "UPLOAD_PATH" in env:
            process_args["logfile"] = os.path.join(
                env["UPLOAD_PATH"], "profile-run-2.log"
            )
        cmdargs = [
            "http://localhost:%d/index.html?extendedCorpus=%s"
            % (PORT, str(has_extended_corpus).lower())
        ]
        runner = FirefoxRunner(
            profile=profile,
            binary=binary,
            cmdargs=cmdargs,
            env=env,
            process_args=process_args,
        )
        runner.start(debug_args=debug_args, interactive=interactive)
        ret = runner.wait()
        sp3_httpd.stop()
        if has_extended_corpus:
            js3_httpd.stop()
        httpd.stop()
        if ret:
            print("Firefox exited with code %d during profiling" % ret)
            logfile = process_args.get("logfile")
            if logfile:
                print("Firefox output (%s):" % logfile)
                with open(logfile) as f:
                    print(f.read())
            get_crashreports(profilePath, name="Profiling run")
            sys.exit(ret)

        if "UPLOAD_PATH" in env:
            should_err = False
            print("Verify log for LLVM Profile Error")
            for n in range(1, 2):
                log = os.path.join(env["UPLOAD_PATH"], f"profile-run-{n}.log")
                with open(log) as f:
                    for line in f.readlines():
                        if "LLVM Profile Error" in line:
                            print(f"Error [{log}]: '{line.strip()}'")
                            should_err = True

            if should_err:
                print("Found some LLVM Profile Error in logs, see above.")
                sys.exit(1)

        # Try to move the crash reports to the artifacts even if Firefox appears
        # to exit successfully, in case there's a crash that doesn't set the
        # return code to non-zero for some reason.
        if get_crashreports(profilePath, name="Firefox exited successfully?") != 0:
            print("Firefox exited successfully, but produced a crashreport")
            sys.exit(1)

        llvm_profdata = env.get("LLVM_PROFDATA")
        if llvm_profdata:
            profraw_files = glob.glob("*.profraw")
            if not profraw_files:
                print(
                    "Could not find profraw files in the current directory: %s"
                    % os.getcwd()
                )
                sys.exit(1)

            merged_profdata = "merged.profdata"
            merge_cmd = [
                llvm_profdata,
                "merge",
                "-o",
                merged_profdata,
            ] + profraw_files
            rc = subprocess.call(merge_cmd)
            if rc != 0:
                print("INFRA-ERROR: Failed to merge profile data. Corrupt profile?")
                # exit with TBPL_RETRY
                sys.exit(4)

            # llvm-profdata may fail while still exiting without an error.
            if not os.path.isfile(merged_profdata):
                print(merged_profdata, "was not created", file=sys.stderr)
                sys.exit(1)

            if os.path.getsize(merged_profdata) == 0:
                print(merged_profdata, "was created but it is empty", file=sys.stderr)
                sys.exit(1)
