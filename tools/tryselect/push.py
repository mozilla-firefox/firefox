# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import configparser
import json
import os
import re
import shlex
import stat
import subprocess
import sys
import tempfile
from functools import cache
from typing import Dict, Optional

import requests
from mach.util import get_state_dir
from mozbuild.base import MozbuildObject
from mozversioncontrol import MissingVCSExtension, get_repository_object

from .lando import get_lando_api_domain, push_to_lando_try
from .util.project import get_project_topsrcdir, is_thunderbird_try
from .util.taskcluster import get_client

GIT_CINNABAR_NOT_FOUND = """
Could not detect `git-cinnabar`.

The `mach try` command requires git-cinnabar to be installed when
pushing from git. Please install it by running:

    $ ./mach vcs-setup
""".lstrip()

HG_PUSH_TO_TRY_NOT_FOUND = """
Could not detect `push-to-try`.

The `mach try` command requires the push-to-try extension enabled
when pushing from hg. Please install it by running:

    $ ./mach vcs-setup
""".lstrip()

VCS_NOT_FOUND = """
error: could not detect version control (only `hg` or `git` are supported)
""".strip()

NO_REMOTE_CONFIGURED = """
error: no version control remote configured
""".strip()

UNCOMMITTED_CHANGES = """
error: commit changes before continuing
""".strip()

TREE_CLOSED = """
error: the '{tree}' tree is {status}{reason}.

Wait for it to re-open, or pass `--closed-tree` to push anyway.
""".strip()

TREESTATUS_URL = "https://{domain}/api/treestatus/trees/{tree}"

LARGE_PUSH_THRESHOLD = 1000
LARGE_PUSH_WARNING = f"""
Your push would schedule at least {{}} tasks. To avoid backlogs that cause delays for
others, your tasks will be scheduled at the lowest priority and may not run before
their deadline. Consider selecting fewer than {LARGE_PUSH_THRESHOLD} tasks to save resources and
get results faster.
"""

MAX_HISTORY = 10

MACH_TRY_PUSH_TO_VCS = os.getenv("MACH_TRY_PUSH_TO_VCS") == "1"

HG_TRY_URL = "ssh://hg.mozilla.org/try"
MACH_TRY_REMOTE: Optional[str] = None

GIT_BACKING_ENABLED = False
GIT_BACKING_REPO = "https://github.com/mozilla-releng/git-backing"
GIT_BACKING_SSH = "git@github.com:mozilla-releng/git-backing.git"
GIT_BACKING_SECRET = "project/releng/releng-github-git-backing-ssh"

GITHUB_REMOTE_RE = re.compile(r"github\.com[/:]([^/]+/[^/]+?)(?:\.git)?/?$")
TREEHERDER_REVISION_URL = (
    "https://treeherder.mozilla.org/jobs?repo={repo_name}&revision={revision}"
)
TREEHERDER_LANDO_TRY_RUN_URL = "https://treeherder.mozilla.org/jobs?repo={repo_name}&landoInstance={lando_instance}&landoCommitID={job_id}"
TREEHERDER_GITHUB_REMOTES = {
    "mozilla/enterprise-firefox-try",
    "mozilla-firefox/firefox-dev",
}

here = os.path.abspath(os.path.dirname(__file__))
build = MozbuildObject.from_environment(cwd=here)


def get_try_repo(topsrcdir):
    """Return the Treeherder repo name to use for a try push."""

    # Thunderbird uses try-comm-central.
    if is_thunderbird_try(build):
        return "try-comm-central"

    return "try"


topsrcdir = get_project_topsrcdir(build)
vcs = get_repository_object(topsrcdir)

history_path = os.path.join(
    get_state_dir(specific_to_topsrcdir=True), "history", "try_task_configs.json"
)


def write_task_config_history(msg, try_task_config):
    if not os.path.isfile(history_path):
        if not os.path.isdir(os.path.dirname(history_path)):
            os.makedirs(os.path.dirname(history_path))
        history = []
    else:
        with open(history_path) as fh:
            history = fh.read().strip().splitlines()

    history.insert(0, json.dumps([msg, try_task_config]))
    history = history[:MAX_HISTORY]
    with open(history_path, "w") as fh:
        fh.write("\n".join(history))


def check_working_directory(push=True):
    if not push:
        return

    if not vcs.working_directory_clean():
        print(UNCOMMITTED_CHANGES)
        sys.exit(1)


VERIFY_PATH_RULE = (
    "test-verify-path only accepts directories that contain a test manifest, "
    "directly or in one of their direct subdirectories; the tests deeper below "
    "them are verified too."
)


def check_test_verify_paths(labels, env):
    """Exit when test-verify-path is selected with paths it refuses."""
    if not any("test-verify-path" in label for label in labels):
        return
    paths = sorted({
        path
        for suite_paths in json.loads(env.get("MOZHARNESS_TEST_PATHS", "{}")).values()
        for path in suite_paths
    })
    if not paths:
        return

    from .tasks import unverifiable_test_paths

    rejected = unverifiable_test_paths(paths)
    if rejected:
        print(
            f"error: cannot verify {', '.join(rejected)}: {VERIFY_PATH_RULE}",
            file=sys.stderr,
        )
        sys.exit(1)


def generate_try_task_config(method, labels, params=None, routes=None):
    params = params or {}

    # The user has explicitly requested a set of jobs, so run them all
    # regardless of optimization (unless the selector explicitly sets this to
    # True). Their dependencies can be optimized though.
    params.setdefault("optimize_target_tasks", False)

    # Remove selected labels from 'existing_tasks' parameter if present
    if "existing_tasks" in params:
        params["existing_tasks"] = {
            label: tid
            for label, tid in params["existing_tasks"].items()
            if label not in labels
        }

    try_config = params.setdefault("try_task_config", {})
    try_config.setdefault("env", {})["TRY_SELECTOR"] = method

    check_test_verify_paths(labels, try_config["env"])

    # In reality, the number of tasks will likely be much larger thanks to test
    # chunks being collapsed behind a wildcard. However because we use
    # `taskgraph.fast` when generating tasks, we don't process test manifests
    # and have no way of knowing how many chunks will be scheduled for a given
    # task. For the purposes of this check, we'll ignore test chunks as it's
    # causing us to underestimate anyway.
    rebuild = try_config.get("rebuild", 1)
    if isinstance(rebuild, dict):
        num_tasks = sum(rebuild.get(label, 1) for label in labels)
    else:
        num_tasks = len(labels) * rebuild
    if "priority" not in try_config and num_tasks > LARGE_PUSH_THRESHOLD:
        print(LARGE_PUSH_WARNING.format(num_tasks))
        while True:
            answer = input("Would you like to proceed anyway? [Y/n]: ").lower()
            if answer in ("n", "no"):
                sys.exit(1)

            if answer in ("y", "yes"):
                break

            print(f"Invalid answer: '{answer}'")

        try_config["priority"] = "lowest"

    try_config["tasks"] = sorted(labels)

    if routes:
        try_config["routes"] = routes

    try_task_config = {"version": 2, "parameters": params}
    return try_task_config


def task_labels_from_try_config(try_task_config):
    if try_task_config["version"] == 2:
        parameters = try_task_config.get("parameters", {})
        if "try_task_config" in parameters:
            return parameters["try_task_config"].get("tasks")
        else:
            return None
    else:
        return None


# improves on `" ".join(sys.argv[:])` by requoting argv items containing spaces or single quotes
def get_sys_argv(injected_argv=None):
    argv_to_use = injected_argv or sys.argv[:]

    formatted_argv = []
    for item in argv_to_use:
        if " " in item or "'" in item:
            formatted_item = f'"{item}"'
        else:
            formatted_item = item
        formatted_argv.append(formatted_item)

    return " ".join(formatted_argv)


def get_github_repo(remote):
    """Return the ``owner/repo`` of a GitHub remote, or None if not on GitHub."""
    if remote_url := vcs.get_remote_url(remote, push=True):
        remote = remote_url
    if match := GITHUB_REMOTE_RE.search(remote):
        return match.group(1).lower()


@cache
def _is_hg_try(remote):
    if not remote:
        return False

    if remote_url := vcs.get_remote_url(remote, push=True):
        remote = remote_url
    return HG_TRY_URL in remote


def _is_git_try(remote):
    return not _is_hg_try(remote)


def check_tree_status(remote):
    if remote_url := vcs.get_remote_url(remote, push=True):
        remote = remote_url
    tree = re.split(r"[/:]", remote.rstrip("/"))[-1].removesuffix(".git")

    try:
        url = TREESTATUS_URL.format(domain=get_lando_api_domain(vcs), tree=tree)
        response = requests.get(url, timeout=10)
        if response.status_code == 404:
            return
        response.raise_for_status()
        result = response.json().get("result")
    except (requests.RequestException, configparser.Error, ValueError) as e:
        print(f"warning: could not query treestatus for '{tree}': {e}")
        return

    if not result or result.get("status", "open") == "open":
        return

    reason = f" ({result['reason']})" if result.get("reason") else ""
    print(TREE_CLOSED.format(tree=tree, status=result["status"], reason=reason))
    sys.exit(1)


def push_to_git_backing(
    prefix: str,
    commit_message: str = "",
    files_to_change: Optional[Dict[str, str]] = None,
) -> str:
    """Push the current head to the git-backing repo and return the git SHA."""
    print("Pushing to git-backing...")
    client = get_client("secrets", [f"secrets:get:{GIT_BACKING_SECRET}"])
    key = client.get(GIT_BACKING_SECRET)["secret"]["ssh_privkey"]

    fd, keyfile_name = tempfile.mkstemp(suffix=".pem")
    try:
        with os.fdopen(fd, "w") as keyfile:
            keyfile.write(key)
        os.chmod(keyfile_name, stat.S_IRUSR | stat.S_IWUSR)
        if sys.platform == "win32":
            # os.chmod can't restrict the ACL on Windows, so lock the key
            # down to the owner or ssh will ignore it.
            subprocess.run(
                [
                    "icacls",
                    keyfile_name,
                    "/inheritance:r",
                    "/grant:r",
                    f"{os.environ['USERNAME']}:F",
                ],
                check=True,
                capture_output=True,
            )

        ssh_command = (
            f"ssh -F /dev/null -i {shlex.quote(keyfile_name)} "
            "-o IdentitiesOnly=yes -o IdentityAgent=none "
            "-o StrictHostKeyChecking=accept-new"
        )

        def push(sha):
            vcs.push(
                GIT_BACKING_SSH,
                ref=sha,
                dest_branch=f"{prefix}/{sha}",
                force=True,
                env={"GIT_SSH_COMMAND": ssh_command},
            )
            return sha

        if not files_to_change:
            return push(vcs.head_rev)

        # Tasks clone the tree from git-backing, so it needs the same changes
        # as the commit pushed to try.
        with vcs.try_commit(commit_message, files_to_change) as head:
            return push(vcs.head_rev if vcs.name == "jj" else head)
    finally:
        os.remove(keyfile_name)


def push_to_try(
    method,
    msg,
    metrics,
    try_task_config=None,
    stage_changes=False,
    dry_run=False,
    write_task_config=False,
    closed_tree=False,
    files_to_change=None,
    allow_log_capture=False,
    push_to_vcs=False,
    force_old_lando=False,
):
    push = not stage_changes and not dry_run

    if push and is_thunderbird_try(build):
        remote = HG_TRY_URL + "-comm-central"
    else:
        remote = os.environ.get("MACH_TRY_REMOTE") or MACH_TRY_REMOTE

    metrics.mach_try.commit_prep.start()

    if push and not remote:
        print(NO_REMOTE_CONFIGURED)
        sys.exit(1)

    # Use direct push if explicitly requested or we aren't pushing to hg.mozilla.org/try.
    push_to_vcs |= MACH_TRY_PUSH_TO_VCS or not _is_hg_try(remote)
    check_working_directory(push)

    if (
        push
        and push_to_vcs
        and not closed_tree
        and not (write_task_config and try_task_config)
    ):
        check_tree_status(remote)

    # Format the commit message
    closed_tree_string = (
        " ON A CLOSED TREE"
        if closed_tree and push_to_vcs and _is_hg_try(remote)
        else ""
    )
    the_cmdline = get_sys_argv()
    full_commandline_entry = f"mach try command: `{the_cmdline}`"
    commit_message = f"{msg}{closed_tree_string}\n\n{full_commandline_entry}\n\nPushed via `mach try {method}`"

    changed_files = {}

    if not push:
        print("Commit message:")
        print(commit_message)

        if try_task_config:
            print("Calculated try_task_config.json:")
            print(
                json.dumps(
                    try_task_config, indent=4, separators=(",", ": "), sort_keys=True
                )
                + "\n"
            )

        if stage_changes and files_to_change:
            vcs.stage_changes(files_to_change)
        return

    metrics.mach_try.commit_prep.stop()

    if GIT_BACKING_ENABLED and _is_hg_try(remote) and vcs.name != "hg":
        # Push the source tree to the git-backing repo so tasks can clone from GitHub,
        # then inject the resulting git rev into the try_task_config parameters.
        prefix = "try"
        metrics.mach_try.git_backing_push_duration.start()
        backing_sha = push_to_git_backing(
            prefix=prefix,
            commit_message=commit_message,
            files_to_change=files_to_change,
        )
        metrics.mach_try.git_backing_push_duration.stop()

        try_task_config = try_task_config or {}
        try_task_config.setdefault("version", 2)
        try_task_config.setdefault("parameters", {})
        try_task_config["parameters"]["head_git_repository"] = GIT_BACKING_REPO
        try_task_config["parameters"]["head_git_rev"] = backing_sha
        try_task_config["parameters"]["head_git_ref"] = (
            f"refs/heads/{prefix}/{backing_sha}"
        )

    if _is_git_try(remote) and (try_task_config or {}).get("version") == 2:
        # Github reports a null `before` revision for a push that creates the branch,
        # so the decision task can't tell what the push changed. Record the real base
        # for it.
        try_task_config.setdefault("parameters", {})["base_rev"] = (
            vcs.base_ref_as_commit()
        )

    if try_task_config:
        try_task_config_file = (
            json.dumps(
                try_task_config, indent=4, separators=(",", ": "), sort_keys=True
            )
            + "\n"
        )
        changed_files["try_task_config.json"] = try_task_config_file
        if write_task_config:
            path = os.path.join(vcs.path, "try_task_config.json")
            with open(path, "w") as fh:
                fh.write(try_task_config_file)
            print(f"Wrote {path}")
            return
        if method not in ("again", "auto", "empty"):
            write_task_config_history(msg, try_task_config)

    if files_to_change:
        changed_files.update(files_to_change.items())

    print("Pushing to try...")
    try:
        if push_to_vcs:
            rev = vcs.push_to_try(
                commit_message,
                changed_files=changed_files,
                allow_log_capture=allow_log_capture,
                remote=remote,
            )
            if rev and (github_repo := get_github_repo(remote)) in (
                TREEHERDER_GITHUB_REMOTES
            ):
                repo_name = github_repo.rsplit("/", 1)[-1]
                treeherder_url = TREEHERDER_REVISION_URL.format(
                    repo_name=repo_name, revision=rev
                )
                print(f"\nView push in Treeherder:\n {treeherder_url}")
        else:
            try_repo = get_try_repo(vcs.path)
            push_data = push_to_lando_try(
                vcs,
                commit_message,
                changed_files,
                metrics,
                force_old_lando=force_old_lando,
                repo_name=try_repo,
            )
            if not push_data:
                sys.exit(1)
            lando_instance = push_data["lando_instance"]
            job_id = push_data["lando_job_id"]
            if lando_instance and job_id:
                treeherder_url = TREEHERDER_LANDO_TRY_RUN_URL.format(
                    repo_name=try_repo,
                    lando_instance=lando_instance,
                    job_id=job_id,
                )
                print(
                    f"try submission success in {push_data['duration']:.1f}s:\n"
                    f" {treeherder_url}"
                )

            return push_data
    except MissingVCSExtension as e:
        if e.ext == "push-to-try":
            print(HG_PUSH_TO_TRY_NOT_FOUND)
        elif e.ext == "cinnabar":
            print(GIT_CINNABAR_NOT_FOUND)
        else:
            raise
        sys.exit(1)
    finally:
        if "try_task_config.json" in changed_files and os.path.isfile(
            "try_task_config.json"
        ):
            os.remove("try_task_config.json")
