# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import sys
import types

SEARCH_PATHS = [
    "gtest",
    "marionette/client",
    "marionette/harness",
    "mochitest",
    "mozbase/manifestparser",
    "mozbase/mozcrash",
    "mozbase/mozdebug",
    "mozbase/mozdevice",
    "mozbase/mozfile",
    "mozbase/mozgeckoprofile",
    "mozbase/mozhttpd",
    "mozbase/mozinfo",
    "mozbase/mozinstall",
    "mozbase/mozleak",
    "mozbase/mozlog",
    "mozbase/moznetwork",
    "mozbase/mozpower",
    "mozbase/mozprocess",
    "mozbase/mozprofile",
    "mozbase/mozrunner",
    "mozbase/mozscreenshot",
    "mozbase/mozserve",
    "mozbase/mozshellutil",
    "mozbase/mozsystemmonitor",
    "mozbase/moztest",
    "mozbase/mozversion",
    "reftest",
    "tools/mach",
    "tools/mozterm",
    "tools/geckoprocesstypes_generator",
    "tools/six",
    "tools/wptserve",
    "web-platform",
    "web-platform/tests/tools/wptrunner",
    "xpcshell",
]


CATEGORIES = {
    "testing": {
        "short": "Testing",
        "long": "Run tests.",
        "priority": 30,
    },
    "devenv": {
        "short": "Development Environment",
        "long": "Set up and configure your development environment.",
        "priority": 20,
    },
    "misc": {
        "short": "Potpourri",
        "long": "Potent potables and assorted snacks.",
        "priority": 10,
    },
    "disabled": {
        "short": "Disabled",
        "long": "The disabled commands are hidden by default. Use -v to display them. "
        "These commands are unavailable for your current context, "
        'run "mach <command>" to see why.',
        "priority": 0,
    },
}


IS_WIN = sys.platform in ("win32", "cygwin")


def ancestors(path, depth=0):
    """Emit the parent directories of a path."""
    count = 1
    while path and count != depth:
        yield path
        newpath = os.path.dirname(path)
        if newpath == path:
            break
        path = newpath
        count += 1


def activate_mozharness_venv(context):
    """Activate the mozharness virtualenv in-process."""
    venv = os.path.join(
        context.mozharness_workdir,
        context.mozharness_config.get("virtualenv_path", "venv"),
    )

    if not os.path.isdir(venv):
        print(f"No mozharness virtualenv detected at '{venv}'.")
        return 1

    venv_bin = os.path.join(venv, "Scripts" if IS_WIN else "bin")
    activate_path = os.path.join(venv_bin, "activate_this.py")

    exec(open(activate_path).read(), dict(__file__=activate_path))

    if isinstance(os.environ["PATH"], str):
        os.environ["PATH"] = os.environ["PATH"].encode("utf-8")

    # sys.executable is used by mochitest-media to start the websocketprocessbridge,
    # for some reason it doesn't get set when calling `activate_this.py` so set it
    # here instead.
    binary = "python"
    if IS_WIN:
        binary += ".exe"
    sys.executable = os.path.join(venv_bin, binary)


def find_firefox(context):
    """Try to automagically find the firefox binary."""
    import mozinstall

    search_paths = []

    # Check for a mozharness setup
    config = context.mozharness_config
    if config and "binary_path" in config:
        return config["binary_path"]
    elif config:
        search_paths.append(os.path.join(context.mozharness_workdir, "application"))

    # Check for test-stage setup
    dist_bin = os.path.join(os.path.dirname(context.package_root), "bin")
    if os.path.isdir(dist_bin):
        search_paths.append(dist_bin)

    for path in search_paths:
        try:
            return mozinstall.get_binary(path, "firefox")
        except mozinstall.InvalidBinary:
            continue


def find_hostutils(context):
    workdir = context.mozharness_workdir
    hostutils = os.path.join(workdir, "hostutils")
    for fname in os.listdir(hostutils):
        fpath = os.path.join(hostutils, fname)
        if os.path.isdir(fpath) and fname.startswith("host-utils"):
            return fpath


def normalize_test_path(test_root, path):
    if os.path.isabs(path) or os.path.exists(path):
        return os.path.normpath(os.path.abspath(path))

    for parent in ancestors(test_root):
        test_path = os.path.join(parent, path)
        if os.path.exists(test_path):
            return os.path.normpath(os.path.abspath(test_path))
    # Not a valid path? Return as is and let test harness deal with it
    return path


def bootstrap(test_package_root):
    test_package_root = os.path.abspath(test_package_root)

    sys.path[0:0] = [os.path.join(test_package_root, path) for path in SEARCH_PATHS]
    import mach.main
    from mach.command_util import MachCommandReference, load_commands_from_spec

    # Centralized registry of available mach commands
    MACH_COMMANDS = {
        "gtest": MachCommandReference("gtest/mach_test_package_commands.py"),
        "marionette-test": MachCommandReference(
            "marionette/mach_test_package_commands.py"
        ),
        "mochitest": MachCommandReference("mochitest/mach_test_package_commands.py"),
        "reftest": MachCommandReference("reftest/mach_test_package_commands.py"),
        "mach-commands": MachCommandReference(
            "python/mach/mach/commands/commandinfo.py"
        ),
        "mach-debug-commands": MachCommandReference(
            "python/mach/mach/commands/commandinfo.py"
        ),
        "mach-completion": MachCommandReference(
            "python/mach/mach/commands/commandinfo.py"
        ),
        "web-platform-tests": MachCommandReference(
            "web-platform/mach_test_package_commands.py"
        ),
        "wpt": MachCommandReference("web-platform/mach_test_package_commands.py"),
        "xpcshell-test": MachCommandReference("xpcshell/mach_test_package_commands.py"),
    }

    def populate_context(context, key=None):
        # These values will be set lazily, and cached after first being invoked.
        if key == "package_root":
            return test_package_root

        if key == "bin_dir":
            return os.path.join(test_package_root, "bin")

        if key == "certs_dir":
            return os.path.join(test_package_root, "certs")

        if key == "module_dir":
            return os.path.join(test_package_root, "modules")

        if key == "ancestors":
            return ancestors

        if key == "normalize_test_path":
            return normalize_test_path

        if key == "firefox_bin":
            return find_firefox(context)

        if key == "hostutils":
            return find_hostutils(context)

        if key == "mozharness_config":
            for dir_path in ancestors(context.package_root):
                mozharness_config = os.path.join(dir_path, "logs", "localconfig.json")
                if os.path.isfile(mozharness_config):
                    with open(mozharness_config, "rb") as f:
                        return json.load(f)
            return {}

        if key == "mozharness_workdir":
            config = context.mozharness_config
            if config:
                return os.path.join(config["base_work_dir"], config["work_dir"])

        if key == "activate_mozharness_venv":
            return types.MethodType(activate_mozharness_venv, context)

    mach = mach.main.Mach(os.getcwd())
    mach.populate_context_handler = populate_context

    for category, meta in CATEGORIES.items():
        mach.define_category(category, meta["short"], meta["long"], meta["priority"])

    # Depending on which test zips were extracted,
    # the command module might not exist
    load_commands_from_spec(MACH_COMMANDS, test_package_root, missing_ok=True)

    return mach
