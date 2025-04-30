# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import shutil
import subprocess
import tempfile
from datetime import datetime
from pathlib import Path

from colorama import Fore, Style
from mach.decorators import (
    Command,
    SubCommand,
)

FIREFOX_L10N_REPO = "https://github.com/mozilla-l10n/firefox-l10n.git"
FLUENT_FILE = "newtab.ftl"
WEBEXT_LOCALES_PATH = Path("browser", "extensions", "newtab", "webext-glue", "locales")
LOCAL_EN_US_PATH = Path("browser", "locales", "en-US", "browser", "newtab", FLUENT_FILE)
COMPARE_TOOL_PATH = Path(
    "third_party", "python", "moz.l10n", "moz", "l10n", "bin", "compare.py"
)
REPORT_PATH = Path(WEBEXT_LOCALES_PATH, "locales-report.json")
REPORT_LEFT_JUSTIFY_CHARS = 15
FLUENT_FILE_ANCESTRY = Path("browser", "newtab")
SUPPORTED_LOCALES_PATH = Path(WEBEXT_LOCALES_PATH, "supported-locales.json")


@Command(
    "newtab",
    category="misc",
    description="Run a command for the newtab built-in addon",
    virtualenv_name="newtab",
)
def newtab(command_context):
    """
    Desktop New Tab build and update utilities.
    """
    command_context._sub_mach(["help", "newtab"])
    return 1


@SubCommand(
    "newtab",
    "update-locales",
    description="Update the locales snapshot.",
    virtualenv_name="newtab",
)
def update_locales(command_context):
    try:
        os.mkdir(WEBEXT_LOCALES_PATH)
    except FileExistsError:
        pass

    # Step 1: We download the latest reckoning of strings from firefox-l10n
    print("Cloning the latest HEAD of firefox-l10n repository")
    with tempfile.TemporaryDirectory() as clone_dir:
        subprocess.check_call(
            ["git", "clone", "--depth=1", FIREFOX_L10N_REPO, clone_dir]
        )
        # Step 2: Get some metadata about what we just pulled down -
        # specifically, the revision.
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"],
            cwd=str(clone_dir),
            universal_newlines=True,
        ).strip()

        # Step 3: Recursively find all files matching the filename for our
        # FLUENT_FILE, and copy them into WEBEXT_LOCALES_PATH/AB_CD/FLUENT_FILE
        root_dir = Path(clone_dir)
        fluent_file_matches = list(root_dir.rglob(FLUENT_FILE))
        for fluent_file_abs_path in fluent_file_matches:
            relative_path = fluent_file_abs_path.relative_to(root_dir)
            # The first element of the path is the locale code, which we want
            # to recreate under WEBEXT_LOCALES_PATH
            locale = relative_path.parts[0]
            destination_file = WEBEXT_LOCALES_PATH.joinpath(
                locale, FLUENT_FILE_ANCESTRY, FLUENT_FILE
            )
            destination_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(fluent_file_abs_path, destination_file)

        # Now clean up the temporary directory.
        shutil.rmtree(clone_dir)

    # Step 4: Now copy the local version of FLUENT_FILE in LOCAL_EN_US_PATH
    # into WEBEXT_LOCALES_PATH/en-US/FLUENT_FILE
    print(f"Cloning local en-US copy of {FLUENT_FILE}")
    dest_en_ftl_path = WEBEXT_LOCALES_PATH.joinpath(
        "en-US", FLUENT_FILE_ANCESTRY, FLUENT_FILE
    )
    dest_en_ftl_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(LOCAL_EN_US_PATH, dest_en_ftl_path)

    # Step 5: Now compare that en-US Fluent file with all of the ones we just
    # cloned and create a report with how many strings are still missing.
    print("Generating localization report…")

    source_ftl_path = WEBEXT_LOCALES_PATH.joinpath("en-US")
    paths = list(WEBEXT_LOCALES_PATH.rglob(FLUENT_FILE))

    # There are 2 parent folders of each FLUENT_FILE (see FLUENT_FILE_ANCESTRY),
    # and we want to get at the locale folder root for our comparison.
    ANCESTRY_LENGTH = 2

    # Get the full list of supported locales that we just pulled down
    supported_locales = [path.parents[ANCESTRY_LENGTH].name for path in paths]
    path_strs = [path.parents[ANCESTRY_LENGTH].as_posix() for path in paths]

    # Verbosity on the compare.py tool appears to be a count value, which is
    # incremented for each -v flag. We want an elevated verbosity so that we
    # get back
    verbosity = ["-v", "-v"]
    # A bug in compare.py means that the source folder must be passed in as
    # an absolute path.
    source = ["--source=%s" % source_ftl_path.absolute().as_posix()]
    other_flags = ["--json"]

    # The moz.l10n compare tool is currently designed to be invoked from the
    # command line interface. We'll use subprocess to invoke it and capture
    # its output.
    python = command_context.virtualenv_manager.python_path

    def on_line(line):
        locales = json.loads(line)
        report = {
            "locales": locales,
            "meta": {
                "repository": FIREFOX_L10N_REPO,
                "revision": revision,
                "updated": datetime.utcnow().isoformat(),
            },
        }
        with open(REPORT_PATH, "w") as file:
            json.dump(report, file)
        display_report(report)
        print("Wrote report to %s" % REPORT_PATH)

    command_context.run_process(
        [python, str(COMPARE_TOOL_PATH)] + other_flags + source + verbosity + path_strs,
        pass_thru=False,
        line_handler=on_line,
    )

    print("Writing supported locales to %s" % SUPPORTED_LOCALES_PATH)
    with open(SUPPORTED_LOCALES_PATH, "w") as file:
        json.dump(supported_locales, file)

    print("Done")


@SubCommand(
    "newtab",
    "locales-report",
    description="Parses the current locales-report.json and produces something human readable.",
    virtualenv_name="newtab",
)
def locales_report(command_context):
    with open(REPORT_PATH) as file:
        report = json.load(file)
        display_report(report)


def display_report(report):
    meta = report["meta"]
    print("New Tab locales report")
    print("Locales last updated: %s" % meta["updated"])
    print("From %s - revision: %s" % (meta["repository"], meta["revision"]))
    print("------")
    sorted_locales = sorted(report["locales"].keys(), key=lambda x: x.lower())
    for locale in sorted_locales:
        print(Style.RESET_ALL, end="")
        if report["locales"][locale]["missing"]:
            missing_translations = report["locales"][locale]["missing"][
                str(FLUENT_FILE_ANCESTRY.joinpath(FLUENT_FILE))
            ]
            total_missing_translations = len(missing_translations)
            if total_missing_translations > 10:
                color = Fore.RED
            else:
                color = Fore.YELLOW
            print(
                color
                + "%s%s missing translations"
                % (locale.ljust(REPORT_LEFT_JUSTIFY_CHARS), total_missing_translations)
            )
        else:
            print(
                Fore.GREEN
                + "%s0 missing translations" % locale.ljust(REPORT_LEFT_JUSTIFY_CHARS)
            )
    print(Style.RESET_ALL, end="")
