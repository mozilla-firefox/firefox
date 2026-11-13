# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import resolve_keyed_by

transforms = TransformSequence()


@transforms.add
def drop_x86_only_on_aarch64(config, tasks):
    """Drop tasks marked x86-only (e.g. cipher runs that disable an x86 CPU
    feature) on aarch64, where they would repeat the default run."""
    for task in tasks:
        attributes = task.get("attributes", {})
        if attributes.get("x86-only") and "aarch64" in attributes["build_platform"]:
            continue
        yield task


@transforms.add
def resolve_platform_differences(config, tasks):
    for task in tasks:
        for key in ("worker-type", "run.command", "scopes", "worker.artifacts", "worker.taskcluster-proxy", "worker.max-run-time"):
            resolve_keyed_by(task, key, task["name"], platform=task["attributes"]["build_platform"])
        yield task


@transforms.add
def add_env_vars(config, tasks):
    for task in tasks:
        env = task["worker"].setdefault("env", {})
        if task["attributes"]["build_platform"].startswith(("mac", "win")):
            env.update({"DOMSUF": "localdomain", "HOST": "localhost"})

        if task["attributes"]["build_platform"].startswith("mac"):
            env.update({"NSS_TASKCLUSTER_MAC": "1"})

        if task["attributes"].get("static"):
            env.update({"NSS_DISABLE_LIBPKIX": "1"})

        if config.params["try_options"].get("nspr_patch"):
            env.update({"ALLOW_NSPR_PATCH": "1"})

        yield task


@transforms.add
def remove_docker_image_for_gw(config, tasks):
    for task in tasks:
        if task["attributes"]["build_platform"].startswith(("mac", "win")):
            task["worker"].pop("docker-image", None)
        yield task


@transforms.add
def select_arch_docker_image(config, tasks):
    """Select the architecture-appropriate variant of in-tree docker images.

    Docker images are architecture specific, and `base` is now neutral:

    * Linux aarch64 tasks rewrite `{in-tree: <image>}` to `{in-tree: <image>-aarch64}`
      so they pick up the natively-built arm64 image.
    * 32-bit x86 (linux-x86) tasks rewrite `{in-tree: base}` to
      `{in-tree: base-i386}`, the i386 layer needed to build and run ia32
      binaries.

    See taskcluster/kinds/docker-image/kind.yml. This covers build, certs, and
    test tasks uniformly.
    """
    for task in tasks:
        platform = task["attributes"]["build_platform"]
        image = task.get("worker", {}).get("docker-image")
        if not (isinstance(image, dict) and "in-tree" in image):
            yield task
            continue
        name = image["in-tree"]
        if "linux" in platform and "aarch64" in platform:
            if not name.endswith("-aarch64"):
                image["in-tree"] = f"{name}-aarch64"
        elif "linux" in platform and "64" not in platform and name == "base":
            image["in-tree"] = "base-i386"
        yield task
