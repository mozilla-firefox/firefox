# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess
import threading
import time
from pathlib import Path

import mozpack.path as mozpath
from mach.decorators import Command, CommandArgument, SubCommand


@Command(
    "storybook",
    category="misc",
    description="Start the Storybook server and launch the site in a local build of Firefox. This will install npm dependencies, if necessary.",
)
@CommandArgument(
    "--no-open",
    action="store_true",
    help="Start the Storybook server without opening a local Firefox build.",
)
def storybook_server(command_context, no_open=False):
    ensure_env(command_context)
    if not no_open:
        start_browser_thread = threading.Thread(
            target=start_browser, args=(command_context,)
        )
        start_browser_thread.start()
    return run_npm(command_context, args=["run", "storybook"])


@SubCommand(
    "storybook",
    "build",
    description="Build the Storybook for export.",
)
def storybook_build(command_context):
    ensure_env(command_context)
    return run_npm(command_context, args=["run", "build-storybook"])


@SubCommand(
    "storybook",
    "upgrade",
    description="Upgrade all storybook dependencies to latest of ranges in package.json",
)
def storybook_upgrade(command_context):
    delete_storybook_node_modules()
    package_lock_path = "browser/components/storybook/package-lock.json"
    if os.path.exists(package_lock_path):
        os.unlink(package_lock_path)
    return run_npm(command_context, args=["install"])


@SubCommand(
    "storybook", "launch", description="Launch the Storybook site in your local build."
)
def storybook_launch(command_context):
    return run_mach(
        command_context,
        "run",
        argv=["http://localhost:5703"],
        setpref=[
            "svg.context-properties.content.enabled=true",
            "layout.forms.input-type-search.enabled=true",
            "layout.forms.reveal-password-button.enabled=true",
        ],
    )


def delete_path(path):
    if path.is_file() or path.is_symlink():
        path.unlink()
        return
    for p in path.iterdir():
        delete_path(p)
    path.rmdir()


def delete_storybook_node_modules():
    node_modules_path = Path("browser/components/storybook/node_modules")
    if node_modules_path.exists():
        delete_path(node_modules_path)


def start_browser(command_context):
    # This delay is used to avoid launching the browser before the Storybook server has started.
    time.sleep(5)
    subprocess.run(
        run_mach(command_context, "storybook", subcommand="launch"), check=True
    )


def build_storybook_manifest(command_context):
    print("Build ChromeMap backend")
    run_mach(command_context, "build-backend", backend=["ChromeMap"])
    config_environment = command_context.config_environment
    storybook_chrome_map_path = "browser/components/storybook/.storybook/chrome-map.js"
    chrome_map_path = mozpath.join(config_environment.topobjdir, "chrome-map.json")
    with open(chrome_map_path) as chrome_map_f:
        with open(storybook_chrome_map_path, "w") as storybook_chrome_map_f:
            storybook_chrome_map_f.write("export const chromeMap = ")
            storybook_chrome_map_f.write(chrome_map_f.read())
            storybook_chrome_map_f.write(";")


def run_mach(command_context, cmd, **kwargs):
    return command_context._mach_context.commands.dispatch(
        cmd, command_context._mach_context, **kwargs
    )


def run_npm(command_context, args):
    return run_mach(
        command_context, "npm", args=[*args, "--prefix=browser/components/storybook"]
    )


def ensure_env(command_context):
    ensure_npm_deps(command_context)
    build_storybook_manifest(command_context)


def ensure_npm_deps(command_context):
    if not check_npm_deps(command_context):
        install_npm_deps(command_context)
    else:
        print("Dependencies up to date\n")


def check_npm_deps(command_context):
    print("Checking installed npm dependencies")
    return not run_npm(command_context, args=["ls"])


def install_npm_deps(command_context):
    print("Installing missing npm dependencies")
    run_npm(command_context, args=["ci"])
