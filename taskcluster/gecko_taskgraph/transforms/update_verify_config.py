# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the beetmover task into an actual task description.
"""

from urllib.parse import urlsplit

from mozilla_taskgraph.util.attributes import release_level
from mozilla_taskgraph.worker_types import get_release_config
from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import resolve_keyed_by

from gecko_taskgraph.transforms.task import get_branch_repo, get_branch_rev

transforms = TransformSequence()


# The beta regexes do not match point releases.
# In the rare event that we do ship a point
# release to beta, we need to either:
# 1) update these regexes to match that specific version
# 2) pass a second include version that matches that specific version
INCLUDE_VERSION_REGEXES = {
    "beta": r"'^(\d+\.\d+(b\d+)?)$'",
    "nonbeta": r"'^\d+\.\d+(\.\d+)?$'",
    # Same as nonbeta, except for the esr suffix
    "esr": r"'^\d+\.\d+(\.\d+)?esr$'",
}

MAR_CHANNEL_ID_OVERRIDE_REGEXES = {
    "beta": r"'^\d+\.\d+(\.\d+)?$$,firefox-mozilla-beta,firefox-mozilla-release'",
}


def ensure_wrapped_singlequote(regexes):
    """Ensure that a regex (from INCLUDE_VERSION_REGEXES or MAR_CHANNEL_ID_OVERRIDE_REGEXES)
    is wrapper in single quotes.
    """
    for name, regex in regexes.items():
        if regex[0] != "'" or regex[-1] != "'":
            raise Exception(
                f"Regex {name} is invalid: not wrapped with single quotes.\n{regex}"
            )


ensure_wrapped_singlequote(INCLUDE_VERSION_REGEXES)
ensure_wrapped_singlequote(MAR_CHANNEL_ID_OVERRIDE_REGEXES)


@transforms.add
def add_command(config, tasks):
    keyed_by_args = [
        "channel",
        "archive-prefix",
        "previous-archive-prefix",
        "aus-server",
        "override-certs",
        "include-version",
        "mar-channel-id-override",
        "last-watershed",
    ]
    optional_args = [
        "updater-platform",
    ]

    release_config = get_release_config(config)

    for task in tasks:
        task["description"] = "generate update verify config for {}".format(
            task["attributes"]["build_platform"]
        )

        llbv_arg = ()
        if last_linux_bz2_version := task["extra"].get("last-linux-bz2-version", None):
            llbv_arg = ("--last-linux-bz2-version", last_linux_bz2_version)

        command = [
            "python",
            "python/mozrelease/mozrelease/update_verify_config_creator.py",
            "--product",
            task["extra"]["product"],
            "--stage-product",
            task["shipping-product"],
            "--app-name",
            task["extra"]["app-name"],
            "--branch-prefix",
            task["extra"]["branch-prefix"],
            "--platform",
            task["extra"]["platform"],
            "--to-version",
            release_config["version"],
            "--to-app-version",
            release_config["appVersion"],
            "--to-build-number",
            str(release_config["build_number"]),
            "--to-buildid",
            config.params["moz_build_date"],
            "--to-revision",
            get_branch_rev(config),
            *llbv_arg,
            "--output-file",
            "update-verify.cfg",
            "--local-repo",
            ".",
        ]

        repo_path = urlsplit(get_branch_repo(config)).path.lstrip("/")
        command.extend(["--repo-path", repo_path])

        if release_config.get("partial_versions"):
            for partial in release_config["partial_versions"].split(","):
                command.extend(["--partial-version", partial.split("build")[0]])

        for arg in optional_args:
            if task["extra"].get(arg):
                command.append(f"--{arg}")
                command.append(task["extra"][arg])

        for arg in keyed_by_args:
            thing = f"extra.{arg}"
            resolve_keyed_by(
                task,
                thing,
                item_name=task["name"],
                platform=task["attributes"]["build_platform"],
                **{
                    "release-type": config.params["release_type"],
                    "release-level": release_level(
                        config.graph_config["release-branches"], config.params
                    ),
                },
            )
            # ignore things that resolved to null
            if not task["extra"].get(arg):
                continue
            if arg == "include-version":
                task["extra"][arg] = INCLUDE_VERSION_REGEXES[task["extra"][arg]]
            if arg == "mar-channel-id-override":
                task["extra"][arg] = MAR_CHANNEL_ID_OVERRIDE_REGEXES[task["extra"][arg]]

            command.append(f"--{arg}")
            command.append(task["extra"][arg])

        task["run"].update({
            "using": "mach",
            "clone-with": "hg",
            "mach": " ".join(command),
        })

        if task.get("index"):
            task["index"].setdefault(
                "job-name",
                f"update-verify-config-{task['name']}-{task['extra']['channel']}",
            )

        yield task
