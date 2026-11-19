# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import logging
import os
import shutil
import subprocess
import sys

import mozpack.path as mozpath
from mach.decorators import Command, CommandArgument
from mozfile import json, which

from mozbuild.util import cpu_count


@Command(
    "ide",
    category="devenv",
    description="Generate a project and launch an IDE.",
    virtualenv_name="ide",
)
@CommandArgument(
    "ide", choices=["eclipse", "sublime", "visualstudio", "vscode", "vscodium", "zed"]
)
@CommandArgument(
    "--no-interactive",
    default=False,
    action="store_true",
    help="Just generate the configuration",
)
@CommandArgument("args", nargs=argparse.REMAINDER)
def run(command_context, ide, no_interactive, args):
    interactive = not no_interactive and command_context._mach_context.is_interactive

    if ide == "eclipse" and not which("eclipse"):
        command_context.log(
            logging.ERROR,
            "ide",
            {},
            "Eclipse CDT 8.4 or later must be installed in your PATH.",
        )
        command_context.log(
            logging.ERROR,
            "ide",
            {},
            "Download: http://www.eclipse.org/cdt/downloads.php",
        )
        return 1

    clangd_ides = {"sublime", "vscode", "vscodium", "zed"}
    if ide in clangd_ides:
        result = subprocess.run(
            [sys.executable, "mach", "configure"],
            check=False,
            cwd=command_context.topsrcdir,
        )
        if result.returncode:
            return result.returncode

        # First install what we can through install manifests.
        # Then build the rest of the build dependencies by running the full
        # export target, because we can't do anything better.
        result = subprocess.run(
            [sys.executable, "mach", "build", "analysis-prereqs"],
            check=False,
            cwd=command_context.topsrcdir,
        )
        if result.returncode:
            return result.returncode
    else:
        # Here we refresh the whole build. 'build export' is sufficient here and is
        # probably more correct but it's also nice having a single target to get a fully
        # built and indexed project (gives a easy target to use before go out to lunch).
        result = subprocess.run(
            [sys.executable, "mach", "build"],
            check=False,
            cwd=command_context.topsrcdir,
        )
        if result.returncode:
            return result.returncode

    backend = None
    if ide == "eclipse":
        backend = "CppEclipse"
    elif ide == "visualstudio":
        backend = "VisualStudio"
    elif ide in clangd_ides:
        if not command_context.config_environment.is_artifact_build:
            backend = "Clangd"

    if backend:
        # Generate or refresh the IDE backend.
        result = subprocess.run(
            [sys.executable, "mach", "build-backend", "-b", backend],
            check=False,
            cwd=command_context.topsrcdir,
        )
        if result.returncode:
            return result.returncode

    if ide == "eclipse":
        eclipse_workspace_dir = get_eclipse_workspace_path(command_context)
        subprocess.check_call(["eclipse", "-data", eclipse_workspace_dir])
    elif ide == "sublime":
        return setup_sublime(command_context, interactive)
    elif ide == "visualstudio":
        visual_studio_workspace_dir = get_visualstudio_workspace_path(command_context)
        subprocess.call(["explorer.exe", visual_studio_workspace_dir])
    elif ide in {"vscode", "vscodium"}:
        return setup_vscode_or_vscodium(ide, command_context, interactive)
    elif ide == "zed":
        return setup_zed(command_context, interactive)


def get_eclipse_workspace_path(command_context):
    from mozbuild.backend.cpp_eclipse import CppEclipseBackend

    return CppEclipseBackend.get_workspace_path(
        command_context.topsrcdir, command_context.topobjdir
    )


def get_visualstudio_workspace_path(command_context):
    return os.path.normpath(
        os.path.join(command_context.topobjdir, "msvc", "mozilla.sln")
    )


def setup_sublime(command_context, interactive):
    # generate settings based on `vscode`.
    vscode_settings = setup_clangd_rust_in_vscode(command_context)
    sublime_settings = {
        "folders": [
            {
                "path": ".",
            },
        ],
        "settings": {
            "LSP": {
                # <https://github.com/sublimelsp/LSP-clangd/blob/190c5d30e62e86a3c9dd94a72c8e3f96a96eecf8/sublime-package.json>
                "LSP-clangd": {
                    "binary": "custom",
                    "initialization_options": {
                        "custom_command": [
                            # populated below
                        ],
                        # populated below
                    },
                },
                # <https://github.com/sublimelsp/LSP-rust-analyzer/blob/03300e283b885128cc5c384b8bc9c5a8a16bafe2/sublime-package.json>
                "LSP-rust-analyzer": {
                    "settings": {
                        # populated below
                    },
                },
            },
        },
    }

    sublime_clangd_settings = sublime_settings["settings"]["LSP"]["LSP-clangd"]
    sublime_rust_analyzer_settings = sublime_settings["settings"]["LSP"][
        "LSP-rust-analyzer"
    ]

    # `clangd.path` and `clangd.arguments` are joined to form `custom_command`
    sublime_clangd_settings["initialization_options"]["custom_command"].append(
        vscode_settings["clangd.path"]
    )
    sublime_clangd_settings["initialization_options"]["custom_command"].extend(
        vscode_settings["clangd.arguments"]
    )

    for key, value in vscode_settings.items():
        if key in ["clangd.path", "clangd.arguments"]:
            pass  # already handled above
        elif key.startswith("clangd."):
            sublime_clangd_settings["initialization_options"][key] = value
        elif key.startswith("rust-analyzer."):
            sublime_rust_analyzer_settings["settings"][key] = value
        else:
            print(f"warning: TODO: unknown key {key!r}", file=sys.stderr)

    out_path = mozpath.join("firefox.sublime-project")
    try:
        with open(out_path, "x") as file:
            # Sublime Text’s sublime-project format supports comments and trailing commas,
            # so if we ever need to *read* a sublime-project file, we will need to use a
            # different JSON impl
            json.dump(sublime_settings, file, indent=2)
            file.write("\n")
    except FileExistsError:
        print(f"fatal: refusing to overwrite file: {out_path!r}", file=sys.stderr)
        return 1

    print("done! now run: `subl firefox.sublime-project`", file=sys.stderr)


def find_zed_cmd():
    # See https://github.com/zed-industries/zed/blob/816e5f5c7358bc2410ae1c7d3473ee43ce2ef92b/crates/install_cli/src/install_cli_binary.rs#L65
    for variant in ["zed", "zeditor", "zedit", "zed-editor"]:
        cmd = which(variant)
        if cmd:
            return cmd
    return None


def setup_zed(command_context, interactive):
    try:
        import json5 as json

        dump_extra = {"quote_keys": True, "trailing_commas": False}
    except ImportError:
        import json

    zed_cmd = find_zed_cmd()
    if interactive and zed_cmd is None:
        choice = prompt_bool("Zed cannot be found, and may not be installed. Proceed?")
        if not choice:
            return 1

    print("Configuring Zed...")

    new_settings = {}
    if not command_context.config_environment.is_artifact_build:
        lsp_settings = {}

        # Generate settings for Zed based on the settings for VSCode.
        for key, value in setup_clangd_rust_in_vscode(command_context).items():
            key_chain = key.split(".")
            # Most options go into initialization_options, except the ones that
            # specify the LSP binary / arguments / env themselves, see:
            # https://zed.dev/docs/configuring-languages#possible-configuration-options
            last_key = key_chain[-1]
            section = (
                "binary"
                if last_key in {"path", "arguments", "env"}
                else "initialization_options"
            )
            key_chain.insert(1, section)
            dest_dict = lsp_settings
            for k in key_chain[:-1]:
                dest_dict = dest_dict.setdefault(k, {})
            # Now key_chain looks like ["clangd", "binary", "path"]
            dest_dict[last_key] = value

        # rust analyzer might not use the right cargo binary / sysroot by
        # default, let's match the build.
        rust_analyzer_binary_settings = lsp_settings["rust-analyzer"].setdefault(
            "binary", {}
        )
        cargo_path = command_context.config_environment.substs.get("CARGO")
        rust_analyzer_path = mozpath.join(mozpath.dirname(cargo_path), "rust-analyzer")
        if os.path.exists(rust_analyzer_path):
            rust_analyzer_binary_settings["path"] = rust_analyzer_path
        rust_analyzer_binary_env_settings = rust_analyzer_binary_settings.setdefault(
            "env", {}
        )
        for env in ["CARGO", "RUSTC"]:
            rust_analyzer_binary_env_settings[env] = (
                command_context.config_environment.substs.get(env)
            )

        new_settings["lsp"] = lsp_settings

    # Our C/C++ tab size does not match the default
    new_settings["languages"] = {"C": {"tab_size": 2}, "C++": {"tab_size": 2}}
    new_settings["file_types"] = {
        "Python": [
            "**/moz.build",
            "**/*.configure",
        ],
        "C++": [
            "**/*.mm",
        ],
    }

    zed_settings_dir = mozpath.join(command_context.topsrcdir, ".zed")
    zed_settings_file = mozpath.join(zed_settings_dir, "settings.json")
    if not os.path.isdir(zed_settings_dir):
        os.mkdir(zed_settings_dir)

    old_settings = {}
    if os.path.exists(zed_settings_file):
        try:
            with open(zed_settings_file) as fh:
                old_settings = json.load(fh)
        except ValueError:
            command_context.log(
                logging.ERROR,
                "ide",
                {},
                f"Failed to parse {zed_settings_file}, refusing to override",
            )
            return 1

    settings = {**old_settings, **new_settings}
    with open(zed_settings_file, "w") as fh:
        json.dump(settings, fh, indent=4, **dump_extra)

    if interactive and zed_cmd:
        subprocess.call([zed_cmd, command_context.topsrcdir])


def rust_analyzer_config(command_context):
    rust_analyzer_extra_includes = [command_context.topobjdir]

    if windows_rs_dir := command_context.config_environment.substs.get(
        "MOZ_WINDOWS_RS_DIR"
    ):
        rust_analyzer_extra_includes.append(windows_rs_dir)

    # The location of the comm/ directory if we're building Thunderbird. `None`
    # if we're building Firefox.
    commtopsrcdir = command_context.substs.get("commtopsrcdir")

    cargo_check_command = [
        sys.executable,
        mozpath.join(command_context.topsrcdir, "mach"),
        "--log-no-times",
        "cargo",
        "check",
        "-j",
        str(cpu_count() // 2),
        "--all-crates",
        "--message-format-json",
        "--workspace",
        "--keep-going",
    ]

    config = {
        "cargo": {
            # Ensure that cargo uses our vendored sources, even when ran from outside
            # the objdir.
            "configPath": f"{command_context.topobjdir}/.cargo/config.toml",
            "extraEnv": {
                # Point rust-analyzer at the real target directory used by our
                # build, so it can discover the files created when we run `./mach
                # cargo check`.
                "CARGO_TARGET_DIR": command_context.topobjdir,
                "MOZ_TOPOBJDIR": command_context.topobjdir,
                "MOZ_TOPSRCDIR": command_context.topsrcdir,
            },
            "buildScripts": {
                "overrideCommand": cargo_check_command,
            },
        },
        "vfs": {
            "extraIncludes": rust_analyzer_extra_includes,
        },
        "check": {
            "overrideCommand": cargo_check_command,
        },
    }

    # If we're building Thunderbird, configure rust-analyzer to use its Cargo
    # workspace rather than Firefox's. `linkedProjects` disables rust-analyzer's
    # project auto-discovery, therefore setting it ensures we use the correct
    # workspace.
    if commtopsrcdir:
        config["linkedProjects"] = [os.path.join(commtopsrcdir, "rust", "Cargo.toml")]

    return config


def setup_vscode_or_vscodium(ide, command_context, interactive):
    from mozbuild.backend.clangd import find_vscode_or_vscodium_cmd

    # Check if platform has VSCode installed
    if interactive:
        vscode_cmd = find_vscode_or_vscodium_cmd(ide)
        if vscode_cmd is None:
            choice = prompt_bool(
                "VSCode cannot be found, and may not be installed. Proceed?"
            )
            if not choice:
                return 1

    vscode_settings = mozpath.join(
        command_context.topsrcdir, ".vscode", "settings.json"
    )

    new_settings = {}
    artifact_prefix = ""
    if command_context.config_environment.is_artifact_build:
        artifact_prefix = (
            "\nArtifact build configured: Skipping clang and rust setup. "
            "If you later switch to a full build, please re-run this command."
        )
    else:
        return_value = setup_clangd_rust_in_vscode(command_context)
        if isinstance(return_value, int):
            return return_value
        new_settings = return_value

    relobjdir = mozpath.relpath(command_context.topobjdir, command_context.topsrcdir)

    # Add file associations.
    new_settings = {
        **new_settings,
        "files.associations": {
            "*.sjs": "javascript",
        },
        "files.exclude": {"obj-*": True, relobjdir: True},
        "files.watcherExclude": {"obj-*": True, relobjdir: True},
    }
    # These are added separately because vscode doesn't override user settings
    # otherwise which leads to the wrong auto-formatting.
    prettier_languages = [
        "javascript",
        "javascriptreact",
        "typescript",
        "typescriptreact",
        "json",
        "jsonc",
        "html",
        "css",
    ]
    for lang in prettier_languages:
        new_settings[f"[{lang}]"] = {
            "editor.defaultFormatter": "esbenp.prettier-vscode",
            "editor.formatOnSave": True,
        }

    # Add matchers for autolinking bugs and revisions in the terminal.
    new_settings = {
        **new_settings,
        "terminalLinks.matchers": [
            {
                "regex": "\\b[Bb]ug\\s*(\\d+)\\b",
                "uri": "https://bugzilla.mozilla.org/show_bug.cgi?id=$1",
            },
            {
                "regex": "\\b(D\\d+)\\b",
                "uri": "https://phabricator.services.mozilla.com/$1",
            },
            {
                "regex": "\\bmoz-src://\\w*/([^\\s:,\"'\\)\\}\\]>]+)(?:[\\s:,\"']+)(\\d+)",
                "uri": "vscode://file${workspaceFolder}/$1:$2",
            },
            {
                "regex": "\\bmoz-src://\\w*/([^\\s:,\"'\\)\\}\\]>]+)",
                "uri": "vscode://file${workspaceFolder}/$1",
            },
        ],
    }

    import difflib

    try:
        import json5 as json

        dump_extra = {"quote_keys": True, "trailing_commas": False}
    except ImportError:
        import json

        dump_extra = {}

    # Load the existing .vscode/settings.json file, to check if if needs to
    # be created or updated.
    try:
        with open(vscode_settings) as fh:
            old_settings_str = fh.read()
    except FileNotFoundError:
        print(f"Configuration for {vscode_settings} will be created.{artifact_prefix}")
        old_settings_str = None

    if old_settings_str is None:
        # No old settings exist
        with open(vscode_settings, "w") as fh:
            json.dump(new_settings, fh, indent=4, **dump_extra)
    else:
        # Merge our new settings with the existing settings, and check if we
        # need to make changes. Only prompt & write out the updated config
        # file if settings actually changed.
        try:
            old_settings = json.loads(old_settings_str)
            prompt_prefix = ""
        except ValueError:
            old_settings = {}
            prompt_prefix = (
                "\n**WARNING**: Parsing of existing settings file failed. "
                "Existing settings will be lost!"
            )

        # If we've got an old section with the formatting configuration, remove it
        # so that we effectively "upgrade" the user to include json from the new
        # settings. The user is presented with the diffs so should spot any issues.
        deprecated = [
            "[javascript][javascriptreact][typescript][typescriptreact]",
            "[javascript][javascriptreact][typescript][typescriptreact][json]",
            "[javascript][javascriptreact][typescript][typescriptreact][json][html]",
            "[javascript][javascriptreact][typescript][typescriptreact][json][jsonc][html]",
            "rust-analyzer.server.extraEnv",
        ]
        for entry in deprecated:
            if entry in old_settings:
                old_settings.pop(entry)

        settings = {**old_settings, **new_settings}

        if old_settings != settings:
            # Prompt the user with a diff of the changes we're going to make
            new_settings_str = json.dumps(settings, indent=4, **dump_extra)
            if interactive:
                print(
                    "\nThe following modifications to {settings} will occur:\n{diff}".format(
                        settings=vscode_settings,
                        diff="".join(
                            difflib.unified_diff(
                                old_settings_str.splitlines(keepends=True),
                                new_settings_str.splitlines(keepends=True),
                                "a/.vscode/settings.json",
                                "b/.vscode/settings.json",
                                n=30,
                            )
                        ),
                    )
                )
                choice = prompt_bool(
                    f"{artifact_prefix}{prompt_prefix}\nProceed with modifications to {vscode_settings}?"
                )
                if not choice:
                    return 1

            with open(vscode_settings, "w") as fh:
                fh.write(new_settings_str)

    if not interactive:
        return 0

    # Open vscode with new configuration, or ask the user to do so if the
    # binary was not found.
    if vscode_cmd is None:
        print(
            f"Please open VS Code manually and load directory: {command_context.topsrcdir}"
        )
        return 0

    rc = subprocess.call(vscode_cmd + [command_context.topsrcdir])

    if rc != 0:
        command_context.log(
            logging.ERROR,
            "ide",
            {},
            "Unable to open VS Code. Please open VS Code manually and load "
            f"directory: {command_context.topsrcdir}",
        )
        return rc

    return 0


def setup_clangd_rust_in_vscode(command_context):
    clangd_cc_path = mozpath.join(command_context.topobjdir, "clangd")

    # Verify if the required files are present
    clang_tools_path = mozpath.join(
        command_context._mach_context.state_dir, "clang-tools"
    )
    clang_tidy_bin = mozpath.join(clang_tools_path, "clang-tidy", "bin")

    clangd_path = mozpath.join(
        clang_tidy_bin,
        "clangd" + command_context.config_environment.substs.get("HOST_BIN_SUFFIX", ""),
    )

    if not os.path.exists(clangd_path):
        from mozbuild.bootstrap import bootstrap_toolchain

        bootstrap_toolchain("clang-tools/clang-tidy")

        if not os.path.exists(clangd_path):
            command_context.log(
                logging.ERROR,
                "ide",
                {},
                f"Unable to locate clangd in {clang_tidy_bin}.",
            )
            return 1

    from mozbuild.code_analysis.utils import ClangTidyConfig

    clang_tidy_cfg = ClangTidyConfig(command_context.topsrcdir)

    clang_tidy = {}
    clang_tidy["Checks"] = ",".join(clang_tidy_cfg.checks)
    clang_tidy.update(clang_tidy_cfg.checks_config)

    # Write .clang-tidy yml
    import yaml

    with open(".clang-tidy", "w") as file:
        yaml.dump(clang_tidy, file)

    clangd_cfg = {
        "CompileFlags": {
            "CompilationDatabase": clangd_cc_path,
        },
        "Completion": {
            # NOTE(emilio): This got ported from previous code, but it's unclear if it's
            # what we want...
            "HeaderInsertion": "Never",
        },
    }

    with open(".clangd", "w") as file:
        yaml.dump(clangd_cfg, file)

    shutil.copyfile(".clangd", mozpath.join(command_context.topobjdir, ".clangd"))

    config = {
        "clangd.path": clangd_path,
        "clangd.arguments": [
            "-j",
            str(cpu_count() // 2),
            "--limit-results",
            "0",
            "--completion-style",
            "detailed",
        ],
    }

    # TODO: Once rust-analyzer.toml is better supported, do that instead for the
    # non-editor-specific options, see
    # https://github.com/rust-lang/rust-analyzer/issues/13529
    rust_analyzer_cfg = rust_analyzer_config(command_context)

    def add_flattened_keys(prefix, d):
        for key, value in d.items():
            full_key = f"{prefix}.{key}"
            if isinstance(value, dict):
                add_flattened_keys(full_key, value)
            else:
                config[full_key] = value

    add_flattened_keys("rust-analyzer", rust_analyzer_cfg)

    return config


def prompt_bool(prompt, limit=5):
    """Prompts the user with prompt and requires a boolean value."""
    from mach.util import strtobool

    for _ in range(limit):
        try:
            return strtobool(input(prompt + " [Y/N]\n"))
        except ValueError:
            print(
                "ERROR! Please enter a valid option! Please use any of the following:"
                " Y, N, True, False, 1, 0"
            )
    return False


@Command(
    "rust-analyzer-config",
    category="devenv",
    description="Output rust-analyzer configuration in json format.",
    virtualenv_name="common",
)
@CommandArgument("--output", "-o", type=str, help="Output to the given file.")
def rust_analyzer_config_cmd(command_context, output):
    import json

    config = rust_analyzer_config(command_context)
    result = json.dumps(config, indent=2)
    if output:
        with open(output, "w") as fh:
            fh.write(result)
    else:
        print(result)
