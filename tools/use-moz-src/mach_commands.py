# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import asyncio
import logging
import os
import re
from shutil import which

from mach.decorators import (
    Command,
    CommandArgument,
)

_log = logging.getLogger(__name__)

NO_RIPGREP_MSG = """
Could not find ripgrep. Perhaps it needs installing via your package manager
 or `cargo install`.
""".strip()

RIPGREP_ERROR_MSG = """
An error occurred running ripgrep. Please check the following error messages:

{}
""".strip()

NO_MODULES_TO_REWRITE_MSG = """
Found no EXTRA_JS_MODULES or FINAL_TARGET_FILES.actors we could convert in the
moz.build file(s) passed.
""".strip()

CANNOT_CONVERT_ERROR_MSG = """
Unfortunately {} cannot be automatically converted.

The script isn't clever enough to convert everything else while leaving that
module alone. Consider (temporarily) removing this module from the moz.build
file, then running the script, and then re-adding the module into the
moz.build file in a copy of the original EXTRA_JS_MODULES instruction.

The list of excluded module patterns is in tools/use-moz-src/mach_commands.py
and should have a comment explaining why it cannot be automatically moved to
moz-src:///.
""".strip()

NEWTAB_WARNING = """
WARNING: Some files in browser/extensions/newtab/ cannot be automatically
updated for moz-src. You should manually replace the relevant imports
with the ImportHelper.import() method as needed.

The affected files are:
""".strip()

excluded_from_convert_re = list(
    map(
        re.compile,
        [
            "BackgroundTask_.*\\.sys\\.mjs",  # Depend on this resource: path.
            "BrowserUsageTelemetry\\.sys\\.mjs",  # Browser + Android duplicates.
            "BuiltInThemes\\.sys\\.mjs",  # Thunderbird override
            "CrashManager\\.sys.\\.mjs",  # Preprocessed.
            "ExtensionBrowsingData\\.sys\\.mjs",  # Browser + Android duplicates.
            "policies/schema\\.sys\\.mjs",  # Thunderbird override
            "Policies\\.sys\\.mjs",  # Thunderbird override
            "PromiseWorker\\.m?js",  # Preprocessed.
            "RFPTargetConstants\\.sys\\.mjs",  # Preprocessed.
            "ThemeVariableMap\\.sys\\.mjs",  # Thunderbird override
        ],
    )
)


def is_excluded_from_convert(path):
    """Returns true if the JSM file shouldn't be converted to ESM."""
    path_str = str(path)
    for expr in excluded_from_convert_re:
        if expr.search(path_str):
            return True

    return False


NEWTAB_NORM = os.path.normpath("browser/extensions/newtab/")
ACTOR_VARIABLE = "FINAL_TARGET_FILES"
MODULES_VARIABLE = "EXTRA_JS_MODULES"


def is_path_newtab_extension(path):
    """Returns true if the path is in browser/extensions/newtab/"""
    return os.path.normpath(path).startswith(NEWTAB_NORM)


def extract_info_from_mozbuild(command_context, paths):
    mozbuilds_for_fixing = set()
    urlmap = dict()
    reader = command_context.mozbuild_reader(config_mode="empty")
    for mozbuild_path in paths:
        for variables in [ACTOR_VARIABLE, MODULES_VARIABLE]:
            is_browser = mozbuild_path.startswith("browser") or (
                mozbuild_path.startswith("devtools")
                and not mozbuild_path.startswith("devtools/platform")
            )
            assignments = reader.find_variables_from_ast(
                variables, path=mozbuild_path, all_relevant_files=False
            )
            for path, _variable, key, value in assignments:
                module_path = path.replace("moz.build", "") + value
                if is_excluded_from_convert(module_path):
                    _log.log(logging.ERROR, CANNOT_CONVERT_ERROR_MSG.format(value))
                    return [], dict()

                if variables == ACTOR_VARIABLE and key != "actors":
                    continue

                prefix = "" if variables == ACTOR_VARIABLE else "modules/"
                newurl = "moz-src:///" + module_path
                module_name = os.path.basename(module_path)
                keystr = "/".join(key.split(".")) + "/" if key else ""
                resource_suffix = prefix + keystr + module_name
                # Handle aliases for modules in services/.
                if module_path.startswith("services/common/"):
                    urlmap["resource://services-common/" + module_name] = newurl
                elif module_path.startswith("services/crypto/"):
                    urlmap["resource://services-crypto/" + module_name] = newurl
                elif module_path.startswith("services/settings/"):
                    urlmap["resource://services-settings/" + module_name] = newurl
                elif module_path.startswith("services/sync/"):
                    urlmap["resource://services-sync/" + module_name] = newurl

                # Handle standard resource URLs.
                if is_browser:
                    urlmap["resource:///" + resource_suffix] = newurl
                else:
                    urlmap["resource://gre/" + resource_suffix] = newurl

                mozbuilds_for_fixing.add(mozbuild_path)

    return mozbuilds_for_fixing, urlmap


to_moz_src_re = re.compile(
    '(EXTRA_JS_MODULES|FINAL_TARGET_FILES.actors)(["\\.\\w/\\[\\]-]*) [+=]+'
)


def rewrite_mozbuilds(mozbuilds):
    for path in mozbuilds:
        with open(path, "r+", encoding="utf-8", newline="\n") as f:
            contents = f.read()
            contents = re.sub(to_moz_src_re, "MOZ_SRC_FILES +=", contents)
            f.seek(0)
            f.write(contents)
            f.truncate()


def _get_re_for_urls(urlmap):
    old_urls = list(urlmap.keys())

    prefix = os.path.commonprefix(old_urls)
    prefixlen = len(prefix)
    old_urls_without_prefix = map(lambda u: u[prefixlen:], old_urls)

    escaped_prefix = re.escape(prefix)
    # Put the "app" option in if needed:
    escaped_prefix = re.sub("resource:///", "resource://(app)?/", escaped_prefix)
    suffixes = "|".join(map(re.escape, old_urls_without_prefix))
    return escaped_prefix + "(" + suffixes + ")"


def handle_matching_file(file, urlmap_re, replace_fn):
    with open(file, "r+", encoding="utf-8", newline="\n") as f:
        contents = f.read()
        f.seek(0)
        contents = re.sub(urlmap_re, replace_fn, contents)
        f.write(contents)
        f.truncate()


async def find_and_replace_refs(urlmap):
    urlmap_re = _get_re_for_urls(urlmap)

    encoding = "utf-8"

    cmd_args = [which("rg"), "-l", "-e", urlmap_re]

    proc = await asyncio.subprocess.create_subprocess_exec(
        *cmd_args, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE
    )

    pending_file_writes = set()
    affected_files = set()

    def replace_fn(matchobj):
        old_url = matchobj.group(0)
        # Replace with the mapped URL or with itself if we can't find it.
        return urlmap.get(old_url, old_url)

    async with asyncio.TaskGroup() as tg:

        async def handle_matching_files() -> None:
            assert proc.stdout is not None
            while matching_file := await proc.stdout.readline():
                matching_file = matching_file.decode(encoding, "replace")
                if not matching_file.endswith("\n"):
                    continue
                matching_file = matching_file.strip()
                if not matching_file:
                    continue
                _log.log(logging.INFO, f"Rewriting {matching_file}")
                affected_files.add(matching_file)
                task = tg.create_task(
                    asyncio.to_thread(
                        handle_matching_file, matching_file, urlmap_re, replace_fn
                    )
                )
                pending_file_writes.add(task)
                task.add_done_callback(pending_file_writes.discard)

        async def handle_errors() -> None:
            assert proc.stderr is not None
            while chunk := await proc.stderr.read(10000):
                _log.log(
                    logging.ERROR,
                    RIPGREP_ERROR_MSG.format(chunk.decode("utf-8", "replace")),
                )
                # We assume that if anything gets written to stderr by ripgrep,
                # it'll also exit shortly and we don't need to deal with that
                # separately by raising exceptions or whatever.

        await asyncio.gather(handle_matching_files(), handle_errors(), proc.wait())
    return affected_files


@Command(
    "use-moz-src",
    category="misc",
    description="Convert use of EXTRA_JS_MODULES to MOZ_SRC_FILES",
)
@CommandArgument(
    "paths",
    nargs="+",
    help="Path to the moz.build file(s) to convert.",
)
def use_moz_src(command_context, paths):
    """
    This command does two things:
    1. replace use of EXTRA_JS_MODULES and FINAL_TARGET_FILES.actors in the moz.build file
       passed with MOZ_SRC_FILES.
    2. fix up consumers across the tree that rely on those files using `resource` URLs to
       use `moz-src` ones instead.

    Note that this only uses the moz.build file in question; it will not
    recurse into `DIRS`. If you want to convert subdirectories, use
    e.g. `find` to collect the moz.build files and pass all of them on
    the commandline.

    Example:

    ./mach use-moz-src browser/components/moz.build

    Recursive example:

    find browser/components -iname 'moz.build' | xargs ./mach use-moz-src

    """
    if not which("rg"):
        _log.log(logging.ERROR, NO_RIPGREP_MSG)
        return

    mozbuilds_for_fixing, urlmap = extract_info_from_mozbuild(command_context, paths)

    if not mozbuilds_for_fixing:
        _log.log(logging.INFO, NO_MODULES_TO_REWRITE_MSG)
        return

    _log.log(
        logging.INFO,
        "Searching the tree and updating all references to:\n  {}".format(
            "\n  ".join(urlmap.keys())
        ),
    )
    updated_files = asyncio.run(find_and_replace_refs(urlmap))

    _log.log(
        logging.INFO,
        "Updating build files:\n  {}".format("\n  ".join(mozbuilds_for_fixing)),
    )

    rewrite_mozbuilds(mozbuilds_for_fixing)

    if len(updated_files) > 0:
        _log.log(logging.INFO, f"Formatting {len(updated_files)} affected files...")
        command_context._mach_context.commands.dispatch(
            "lint",
            command_context._mach_context,
            linters=["eslint"],
            paths=updated_files,
            fix=True,
        )
        newtab_files = list(filter(is_path_newtab_extension, updated_files))
        if len(newtab_files) > 0:
            _log.log(
                logging.ERROR,
                NEWTAB_WARNING + "\n  {}".format("\n  ".join(newtab_files)),
            )

        command_context._mach_context.commands.dispatch(
            "ts",
            command_context._mach_context,
            subcommand="update",
        )

    _log.log(
        logging.INFO, "Done. Make sure to test the result before submitting patches."
    )
