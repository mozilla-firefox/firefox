# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import Schema
from voluptuous import Any, Optional, Required

transforms = TransformSequence()

bootstrap_schema = Schema(
    {
        # Name of the bootstrap task.
        Required("name"): str,
        # Name of the docker image. Ideally, we'd also have tasks for mac and windows,
        # but we unfortunately don't have workers barebones enough for such testing
        # to be satisfactory.
        Required("image"): Any(str, {"in-tree": str}),
        # Initialization commands.
        Required("pre-commands"): [str],
        # relative path (from config.path) to the file task was defined in
        Optional("task-from"): str,
    }
)


transforms.add_validate(bootstrap_schema)


@transforms.add
def bootstrap_tasks(config, tasks):
    for task in tasks:
        name = task.pop("name")
        image = task.pop("image")
        pre_commands = task.pop("pre-commands")

        head_repo = config.params["head_repository"]
        head_rev = config.params["head_rev"]

        # Get all the non macos/windows local toolchains (the only ones bootstrap can use),
        # and use them as dependencies for the tasks we create, so that they don't start
        # before any potential toolchain task that would be triggered on the same push
        # (which would lead to bootstrap failing).
        dependencies = {
            name: name
            for name, task in config.kind_dependencies_tasks.items()
            if task.attributes.get("local-toolchain")
            and not name.startswith(("toolchain-macos", "toolchain-win"))
        }
        # We don't test the artifacts variants, or js, because they are essentially subsets.
        # Mobile and browser are different enough to warrant testing them separately.
        for app in ("browser", "mobile_android"):
            commands = pre_commands + [
                # MOZ_AUTOMATION changes the behavior, and we want something closer to user
                # machines.
                "unset MOZ_AUTOMATION",
                f"curl --retry 5 -L -f -O {head_repo}/raw-file/{head_rev}/python/mozboot/bin/bootstrap.py",
                f"python3 bootstrap.py --vcs=git --no-interactive --application-choice {app}",
                "cd mozilla-unified",
                # After bootstrap, configure should go through without its own auto-bootstrap.
                "./mach configure --enable-bootstrap=no-update",
                # Then a build should go through too.
                "./mach build",
            ]

            os_specific = []
            if app == "mobile_android":
                os_specific += ["android*"]
            for os, filename in (
                ("debian", "debian.py"),
                ("ubuntu", "debian.py"),
                ("fedora", "centosfedora.py"),
                ("rockylinux", "centosfedora.py"),
                ("opensuse", "opensuse.py"),
                ("gentoo", "gentoo.py"),
                ("archlinux", "archlinux.py"),
                ("voidlinux", "void.py"),
            ):
                if name.startswith(os):
                    os_specific.append(filename)
                    break
            else:
                raise Exception(f"Missing OS specific bootstrap file for {name}")

            taskdesc = {
                "label": f"{config.kind}-{name}-{app}",
                "description": f"Bootstrap {app} build on {name}",
                "always-target": True,
                "scopes": [],
                "treeherder": {
                    "symbol": f"Boot({name})",
                    "platform": {
                        "browser": "linux64/opt",
                        "mobile_android": "android-5-0-armv7/opt",
                    }[app],
                    "kind": "other",
                    "tier": 2,
                },
                "run-on-projects": ["trunk"],
                "worker-type": "b-linux-gcp",
                "worker": {
                    "implementation": "docker-worker",
                    "docker-image": image,
                    "os": "linux",
                    "env": {
                        "GECKO_HEAD_REPOSITORY": head_repo,
                        "GECKO_HEAD_REV": head_rev,
                        "MACH_NO_TERMINAL_FOOTER": "1",
                        "MOZ_SCM_LEVEL": config.params["level"],
                    },
                    "command": ["sh", "-c", "-x", "-e", " && ".join(commands)],
                    "max-run-time": 7200,
                },
                "dependencies": dependencies,
                "optimization": {
                    "skip-unless-changed": [
                        "python/mozboot/bin/bootstrap.py",
                        "python/mozboot/mozboot/base.py",
                        "python/mozboot/mozboot/bootstrap.py",
                        "python/mozboot/mozboot/linux_common.py",
                        "python/mozboot/mozboot/mach_commands.py",
                        "python/mozboot/mozboot/mozconfig.py",
                        "python/mozboot/mozboot/rust.py",
                        "python/mozboot/mozboot/sccache.py",
                        "python/mozboot/mozboot/util.py",
                    ]
                    + [f"python/mozboot/mozboot/{f}" for f in os_specific]
                },
            }

            yield taskdesc
