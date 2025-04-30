#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

# This script provides one-line bootstrap support to configure systems to build
# the tree. It does so by cloning the repo before calling directly into `mach
# bootstrap`.

# Note that this script can't assume anything in particular about the host
# Python environment (except that it's run with a sufficiently recent version of
# Python 3), so we are restricted to stdlib modules.

import sys

major, minor = sys.version_info[:2]
if (major < 3) or (major == 3 and minor < 8):
    print(
        "Bootstrap currently only runs on Python 3.8+."
        "Please try re-running with python3.8+."
    )
    sys.exit(1)

import ctypes
import os
import shutil
import subprocess
import tempfile
from optparse import OptionParser
from pathlib import Path

CLONE_MERCURIAL_PULL_FAIL = """
Failed to pull from hg.mozilla.org.

This is most likely because of unstable network connection.
Try running `cd %s && hg pull https://hg.mozilla.org/mozilla-unified` manually,
or download a mercurial bundle and use it:
https://firefox-source-docs.mozilla.org/contributing/vcs/mercurial_bundles.html"""

WINDOWS = sys.platform.startswith("win32") or sys.platform.startswith("msys")
VCS_HUMAN_READABLE = {
    "hg": "Mercurial",
    "git": "Git",
}


def which(name):
    """Python implementation of which.

    It returns the path of an executable or None if it couldn't be found.
    """
    search_dirs = os.environ["PATH"].split(os.pathsep)
    potential_names = [name]
    if WINDOWS:
        potential_names.insert(0, name + ".exe")

    for path in search_dirs:
        for executable_name in potential_names:
            test = Path(path) / executable_name
            if test.is_file() and os.access(test, os.X_OK):
                return test

    return None


def validate_clone_dest(dest: Path):
    dest = dest.resolve()

    if not dest.exists():
        return dest

    if not dest.is_dir():
        print(f"ERROR! Destination {dest} exists but is not a directory.")
        return None

    if not any(dest.iterdir()):
        return dest
    else:
        print(f"ERROR! Destination directory {dest} exists but is nonempty.")
        print(
            f"To re-bootstrap the existing checkout, go into '{dest}' and run './mach bootstrap'."
        )
        return None


def input_clone_dest(vcs, no_interactive):
    repo_name = "mozilla-unified"
    print(f"Cloning into {repo_name} using {VCS_HUMAN_READABLE[vcs]}...")
    while True:
        dest = None
        if not no_interactive:
            dest = input(
                f"Destination directory for clone (leave empty to use "
                f"default destination of {repo_name}): "
            ).strip()
        if not dest:
            dest = repo_name
        dest = validate_clone_dest(Path(dest).expanduser())
        if dest:
            return dest
        if no_interactive:
            return None


def hg_clone_firefox(hg: Path, dest: Path, head_repo, head_rev):
    # We create an empty repo then modify the config before adding data.
    # This is necessary to ensure storage settings are optimally
    # configured.
    args = [
        str(hg),
        # The unified repo is generaldelta, so ensure the client is as
        # well.
        "--config",
        "format.generaldelta=true",
        "init",
        str(dest),
    ]
    res = subprocess.call(args)
    if res:
        print("unable to create destination repo; please try cloning manually")
        return None

    # Strictly speaking, this could overwrite a config based on a template
    # the user has installed. Let's pretend this problem doesn't exist
    # unless someone complains about it.
    with open(dest / ".hg" / "hgrc", "a") as fh:
        fh.write("[paths]\n")
        fh.write("default = https://hg.mozilla.org/mozilla-unified\n")
        fh.write("\n")

        # The server uses aggressivemergedeltas which can blow up delta chain
        # length. This can cause performance to tank due to delta chains being
        # too long. Limit the delta chain length to something reasonable
        # to bound revlog read time.
        fh.write("[format]\n")
        fh.write("# This is necessary to keep performance in check\n")
        fh.write("maxchainlen = 10000\n")

    # Pulling a specific revision into an empty repository induces a lot of
    # load on the Mercurial server, so we always pull from mozilla-unified (which,
    # when done from an empty repository, is equivalent to a clone), and then pull
    # the specific revision we want (if we want a specific one, otherwise we just
    # use the "central" bookmark), at which point it will be an incremental pull,
    # that the server can process more easily.
    # This is the same thing that robustcheckout does on automation.
    res = subprocess.call(
        [str(hg), "pull", "https://hg.mozilla.org/mozilla-unified"], cwd=str(dest)
    )
    if not res and head_repo:
        res = subprocess.call(
            [str(hg), "pull", head_repo, "-r", head_rev], cwd=str(dest)
        )
    print("")
    if res:
        print(CLONE_MERCURIAL_PULL_FAIL % dest)
        return None

    head_rev = head_rev or "central"
    print(f'updating to "{head_rev}" - the development head of Gecko and Firefox')
    res = subprocess.call([str(hg), "update", "-r", head_rev], cwd=str(dest))
    if res:
        print(
            f"error updating; you will need to `cd {dest} && hg update -r central` "
            "manually"
        )
    return dest


def git_clone_firefox(git: Path, dest: Path, watchman: Path, head_repo, head_rev):
    tempdir = None
    cinnabar = None
    env = dict(os.environ)
    try:
        cinnabar = which("git-cinnabar")
        if not cinnabar:
            from urllib.request import urlopen

            cinnabar_url = "https://github.com/glandium/git-cinnabar/"
            # If git-cinnabar isn't installed already, that's fine; we can
            # download a temporary copy. `mach bootstrap` will install a copy
            # in the state dir; we don't want to copy all that logic to this
            # tiny bootstrapping script.
            tempdir = Path(tempfile.mkdtemp())
            with open(tempdir / "download.py", "wb") as fh:
                shutil.copyfileobj(
                    urlopen(f"{cinnabar_url}/raw/master/download.py"), fh
                )

            subprocess.check_call(
                [sys.executable, str(tempdir / "download.py")],
                cwd=str(tempdir),
            )
            env["PATH"] = str(tempdir) + os.pathsep + env["PATH"]
            print(
                "WARNING! git-cinnabar is required for Firefox development  "
                "with git. After the clone is complete, the bootstrapper "
                "will ask if you would like to configure git; answer yes, "
                "and be sure to add git-cinnabar to your PATH according to "
                "the bootstrapper output."
            )

        # We're guaranteed to have `git-cinnabar` installed now.
        # Configure git per the git-cinnabar requirements.
        subprocess.check_call(
            [
                str(git),
                "-c",
                "fetch.prune=true",
                "-c",
                "cinnabar.graft=https://github.com/mozilla-firefox/firefox",
                "-c",
                "cinnabar.refs=bookmarks",
                "-c",
                "remote.origin.fetch=refs/heads/central:refs/remotes/origin/main",
                "clone",
                "--no-checkout",
                "hg::https://hg.mozilla.org/mozilla-unified",
                str(dest),
            ],
            env=env,
        )
        subprocess.check_call(
            [str(git), "config", "fetch.prune", "true"], cwd=str(dest), env=env
        )
        subprocess.check_call(
            [str(git), "config", "cinnabar.refs", "bookmarks"], cwd=str(dest), env=env
        )
        subprocess.check_call(
            [
                str(git),
                "config",
                "--add",
                "remote.origin.fetch",
                "refs/heads/central:refs/remotes/origin/main",
            ],
            cwd=str(dest),
            env=env,
        )
        subprocess.check_call(
            [str(git), "config", "pull.ff", "only"], cwd=str(dest), env=env
        )

        if head_repo:
            subprocess.check_call(
                [str(git), "cinnabar", "fetch", f"hg::{head_repo}", head_rev],
                cwd=str(dest),
                env=env,
            )

        subprocess.check_call(
            [
                str(git),
                "checkout",
                "FETCH_HEAD" if head_rev else "main",
                "--",
            ],
            cwd=str(dest),
            env=env,
        )

        watchman_sample = dest / ".git/hooks/fsmonitor-watchman.sample"
        # Older versions of git didn't include fsmonitor-watchman.sample.
        if watchman and watchman_sample.exists():
            print("Configuring watchman")
            watchman_config = dest / ".git/hooks/query-watchman"
            if not watchman_config.exists():
                print(f"Copying {watchman_sample} to {watchman_config}")
                copy_args = [
                    "cp",
                    ".git/hooks/fsmonitor-watchman.sample",
                    ".git/hooks/query-watchman",
                ]
                subprocess.check_call(copy_args, cwd=str(dest))

            config_args = [
                str(git),
                "config",
                "core.fsmonitor",
                ".git/hooks/query-watchman",
            ]
            subprocess.check_call(config_args, cwd=str(dest), env=env)
        return dest
    finally:
        if tempdir:
            shutil.rmtree(str(tempdir))


def add_microsoft_defender_antivirus_exclusions(dest, no_system_changes):
    if no_system_changes:
        return

    if not WINDOWS:
        return

    powershell_exe = which("powershell")

    if not powershell_exe:
        return

    def print_attempt_exclusion(path):
        print(
            f"Attempting to add exclusion path to Microsoft Defender Antivirus for: {path}"
        )

    powershell_exe = str(powershell_exe)
    paths = []

    # mozilla-unified / clone dest
    repo_dir = Path.cwd() / dest
    paths.append(repo_dir)
    print_attempt_exclusion(repo_dir)

    # MOZILLABUILD
    mozillabuild_dir = os.getenv("MOZILLABUILD")
    if mozillabuild_dir:
        paths.append(mozillabuild_dir)
        print_attempt_exclusion(mozillabuild_dir)

    # .mozbuild
    mozbuild_dir = Path.home() / ".mozbuild"
    paths.append(mozbuild_dir)
    print_attempt_exclusion(mozbuild_dir)

    args = ";".join(f"Add-MpPreference -ExclusionPath '{path}'" for path in paths)
    command = f'-Command "{args}"'

    # This will attempt to run as administrator by triggering a UAC prompt
    # for admin credentials. If "No" is selected, no exclusions are added.
    ctypes.windll.shell32.ShellExecuteW(None, "runas", powershell_exe, command, None, 0)


def clone(options):
    vcs = options.vcs
    no_interactive = options.no_interactive
    no_system_changes = options.no_system_changes

    if vcs == "hg":
        hg = which("hg")
        if not hg:
            print("Mercurial is not installed. Mercurial is required to clone Firefox.")
            try:
                # We're going to recommend people install the Mercurial package with
                # pip3. That will work if `pip3` installs binaries to a location
                # that's in the PATH, but it might not be. To help out, if we CAN
                # import "mercurial" (in which case it's already been installed),
                # offer that as a solution.
                import mercurial  # noqa: F401

                print(
                    "Hint: have you made sure that Mercurial is installed to a "
                    "location in your PATH?"
                )
            except ImportError:
                print("Try installing hg with `pip3 install Mercurial`.")
            return None
        binary = hg
    else:
        binary = which(vcs)
        if not binary:
            print("Git is not installed.")
            print("Try installing git using your system package manager.")
            return None

    dest = input_clone_dest(vcs, no_interactive)
    if not dest:
        return None

    add_microsoft_defender_antivirus_exclusions(dest, no_system_changes)

    print(f"Cloning Firefox {VCS_HUMAN_READABLE[vcs]} repository to {dest}")

    head_repo = os.environ.get("GECKO_HEAD_REPOSITORY")
    head_rev = os.environ.get("GECKO_HEAD_REV")

    if vcs == "hg":
        return hg_clone_firefox(binary, dest, head_repo, head_rev)
    else:
        watchman = which("watchman")
        return git_clone_firefox(binary, dest, watchman, head_repo, head_rev)


def bootstrap(srcdir: Path, application_choice, no_interactive, no_system_changes):
    args = [sys.executable, "mach"]

    if no_interactive:
        # --no-interactive is a global argument, not a command argument,
        # so it needs to be specified before "bootstrap" is appended.
        args += ["--no-interactive"]

    args += ["bootstrap"]

    if application_choice:
        args += ["--application-choice", application_choice]
    if no_system_changes:
        args += ["--no-system-changes"]

    print("Running `%s`" % " ".join(args))
    return subprocess.call(args, cwd=str(srcdir))


def main(args):
    parser = OptionParser()
    parser.add_option(
        "--application-choice",
        dest="application_choice",
        help='Pass in an application choice (see "APPLICATIONS" in '
        "python/mozboot/mozboot/bootstrap.py) instead of using the "
        "default interactive prompt.",
    )
    parser.add_option(
        "--vcs",
        dest="vcs",
        default="hg",
        choices=["git", "hg"],
        help="VCS (hg or git) to use for downloading the source code, "
        "instead of using the default interactive prompt.",
    )
    parser.add_option(
        "--no-interactive",
        dest="no_interactive",
        action="store_true",
        help="Answer yes to any (Y/n) interactive prompts.",
    )
    parser.add_option(
        "--no-system-changes",
        dest="no_system_changes",
        action="store_true",
        help="Only executes actions that leave the system " "configuration alone.",
    )

    options, leftover = parser.parse_args(args)
    try:
        srcdir = clone(options)
        if not srcdir:
            return 1
        print("Clone complete.")
        print(
            "If you need to run the tooling bootstrapping again, "
            "then consider running './mach bootstrap' instead."
        )
        if not options.no_interactive:
            remove_bootstrap_file = input(
                "Unless you are going to have more local copies of Firefox source code, "
                "this 'bootstrap.py' file is no longer needed and can be deleted. "
                "Clean up the bootstrap.py file? (Y/n)"
            )
            if not remove_bootstrap_file:
                remove_bootstrap_file = "y"
        if options.no_interactive or remove_bootstrap_file == "y":
            try:
                Path(sys.argv[0]).unlink()
            except FileNotFoundError:
                print("File could not be found !")
        return bootstrap(
            srcdir,
            options.application_choice,
            options.no_interactive,
            options.no_system_changes,
        )
    except Exception:
        print("Could not bootstrap Firefox! Consider filing a bug.")
        raise


if __name__ == "__main__":
    sys.exit(main(sys.argv))
