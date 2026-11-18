# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from typing import Optional

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import Schema
from taskgraph.util.templates import merge


class TaskSchema(Schema, forbid_unknown_fields=False, kw_only=True, rename="kebab"):
    install_method: Optional[str] = None
    install_script_command: Optional[str] = None
    lang_env: Optional[str] = None
    pre_install: Optional[str] = None
    distro_symbol: Optional[str] = None
    channels: Optional[list[str]] = None
    locales: Optional[list[str]] = None
    platform: Optional[str] = None
    architectures: Optional[dict] = None
    distros: Optional[dict] = None


transforms = TransformSequence()
transforms.add_validate(TaskSchema)


def _resolve_docker_image(distro_name, distro_cfg, arch_name):
    docker_image_cfg = distro_cfg["docker-image"]
    if not isinstance(docker_image_cfg, dict):
        raise ValueError(
            f"Distro '{distro_name}' docker-image must be a dict, got: {docker_image_cfg!r}"
        )
    if arch_name in docker_image_cfg:
        return docker_image_cfg[arch_name]
    raise ValueError(
        f"Distro '{distro_name}' has no docker-image entry for arch '{arch_name}'. "
        f"Add it or restrict architectures."
    )


def _build_worker_config(distro_cfg, docker_image):
    worker_cfg = {"docker-image": docker_image}
    if distro_cfg.get("max-run-time") is not None:
        worker_cfg["max-run-time"] = distro_cfg["max-run-time"]
    return worker_cfg


def _expand_distro_matrix(generator):
    base = {
        k: v
        for k, v in generator.items()
        if k not in ("name", "architectures", "channels", "locales", "distros")
    }
    architectures = generator.get("architectures", {})
    channels = generator.get("channels", [])
    locales = generator.get("locales", [])

    for distro_name, distro_cfg in generator["distros"].items():
        allowed_arches = distro_cfg.get("architectures", list(architectures.keys()))
        for arch_name, arch_cfg in architectures.items():
            if arch_name not in allowed_arches:
                continue
            docker_image = _resolve_docker_image(distro_name, distro_cfg, arch_name)
            worker_cfg = _build_worker_config(distro_cfg, docker_image)
            for method in distro_cfg["methods"]:
                task = {
                    "name": f"{distro_name}-{arch_name}-{method}",
                    "distro-symbol": distro_cfg["symbol"],
                    "install-method": method,
                    "worker-type": arch_cfg["worker-type"],
                    "worker": worker_cfg,
                    "platform": arch_cfg["platform"],
                    "channels": distro_cfg.get("channels", channels),
                    "locales": distro_cfg.get("locales", locales),
                }
                if distro_cfg.get("pre-install") is not None:
                    task["pre-install"] = distro_cfg["pre-install"]
                yield merge(base, task)


@transforms.add
def expand_distros(config, tasks):
    tasks = list(tasks)
    explicit_names = {t["name"] for t in tasks if "distros" not in t}

    for task in tasks:
        if "distros" not in task:
            yield task
            continue
        for generated in _expand_distro_matrix(task):
            if generated["name"] in explicit_names:
                raise ValueError(
                    f"Generated task '{generated['name']}' conflicts with an explicit task"
                )
            yield generated


@transforms.add
def set_treeherder_platform(config, tasks):
    for task in tasks:
        platform = task.pop("platform", None)
        if platform:
            task.setdefault("treeherder", {})["platform"] = platform
        yield task


@transforms.add
def build_run_command(config, tasks):
    for task in tasks:
        method = task.get("install-method")
        pre_install = task.pop("pre-install", None)
        fx_cmd = task.pop("install-script-command", None)
        lang_env = task.pop("lang-env", None)

        if method is None:
            yield task
            continue

        name = task.get("name", "?")
        if not fx_cmd:
            raise ValueError(
                f"Task '{name}' sets install-method but no install-script-command"
            )
        if task.get("run", {}).get("command"):
            raise ValueError(f"Task '{name}' sets both install-method and run.command")

        # {{...}} produces a literal {matrix[channel]} placeholder substituted by taskgraph.transforms.matrix.
        fx_args = f"--channel {{matrix[channel]}} --install-method {method}"
        if lang_env:
            cmd = f"LANG={lang_env} {fx_cmd} {fx_args}"
        else:
            cmd = f"{fx_cmd} {fx_args} --lang {{matrix[locale]}}"
        parts = [pre_install, cmd] if pre_install else [cmd]
        task.setdefault("run", {})["command"] = " && ".join(parts)

        yield task


@transforms.add
def inject_matrix(config, tasks):
    for task in tasks:
        channels = task.pop("channels", None)
        locales = task.pop("locales", None)

        if channels is not None:
            # Deliberate runtime check: the schema allows both fields to be
            # absent, so this guards against a distro entry that sets channels
            # without locales — caught at graph-generation time, not YAML-parse time.
            if locales is None:
                raise ValueError(
                    f"Task '{task.get('label', task.get('name', '?'))}' has channels but no locales"
                )
            task["matrix"] = {
                "substitution-fields": ["run.command"],
                "channel": channels,
                "locale": locales,
            }
        yield task


@transforms.add
def stash_distro_attributes(config, tasks):
    for task in tasks:
        method = task.pop("install-method", None)
        distro_symbol = task.pop("distro-symbol", None)

        if distro_symbol is not None:
            attrs = task.setdefault("attributes", {})
            attrs["distro-symbol"] = distro_symbol
            attrs["install-method"] = method

        yield task
