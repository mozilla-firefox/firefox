# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Support for running tasks that download remote content and re-export
# it as task artifacts.


import os
import re
from typing import Literal, Optional

import attr
import taskgraph
from mozpack import path as mozpath
from mozshellutil import quote as shell_quote
from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import Schema, validate_schema
from taskgraph.util.treeherder import join_symbol

import gecko_taskgraph
from gecko_taskgraph import GECKO
from gecko_taskgraph.transforms.task import TaskDescriptionSchema
from gecko_taskgraph.util.hash import hash_paths

from ..util.cached_tasks import add_optimization

CACHE_TYPE = "content.v1"

CRX3_SCRIPT = "taskcluster/scripts/misc/fetch-crx3.py"
ONNXRUNTIME_DEPS_SCRIPT = "taskcluster/scripts/misc/fetch-onnxruntime-deps.sh"


class FetchTypeSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    type: str


class FetchSchema(Schema, kw_only=True):
    # Name of the task.
    name: str
    # Relative path (from config.path) to the file the task was defined in.
    task_from: Optional[str] = None
    # Description of the task.
    description: str
    # An alias that can be used instead of the real fetch job name in
    # fetch stanzas for jobs.
    fetch_alias: Optional[str] = None
    # The prefix of the taskcluster artifact being uploaded.
    # Defaults to `public/`; if it starts with something other than
    # `public/` the artifact will require scopes to access.
    artifact_prefix: Optional[str] = None
    attributes: Optional[dict[str, object]] = None
    run_on_repo_type: TaskDescriptionSchema.__annotations__["run_on_repo_type"] = None
    fetch: FetchTypeSchema  # noqa: F821


# define a collection of payload builders, depending on the worker implementation
fetch_builders = {}


@attr.s(frozen=True)
class FetchBuilder:
    schema = attr.ib()
    builder = attr.ib()


def fetch_builder(name, schema):
    def wrap(func):
        fetch_builders[name] = FetchBuilder(schema, func)
        return func

    return wrap


transforms = TransformSequence()
transforms.add_validate(FetchSchema)


@transforms.add
def process_fetch_job(config, jobs):
    # Converts fetch-url entries to the job schema.
    for job in jobs:
        typ = job["fetch"]["type"]
        name = job["name"]
        fetch = job.pop("fetch")

        if typ not in fetch_builders:
            raise Exception(f"Unknown fetch type {typ} in fetch {name}")
        validate_schema(fetch_builders[typ].schema, fetch, f"In task.fetch {name!r}:")

        job.update(configure_fetch(config, typ, name, fetch))

        yield job


def configure_fetch(config, typ, name, fetch):
    if typ not in fetch_builders:
        raise Exception(f"No fetch type {typ} in fetch {name}")
    validate_schema(fetch_builders[typ].schema, fetch, f"In task.fetch {name!r}:")

    return fetch_builders[typ].builder(config, name, fetch)


@transforms.add
def make_task(config, jobs):
    # Fetch tasks are idempotent and immutable. Have them live for
    # essentially forever.
    if config.params["level"] == "3":
        expires = "1000 years"
    else:
        expires = "28 days"

    for job in jobs:
        name = job["name"]
        artifact_prefix = job.get("artifact-prefix", "public")
        env = job.get("env", {})
        env.update({"UPLOAD_DIR": "/builds/worker/artifacts"})
        attributes = job.get("attributes", {})
        attributes["artifact_prefix"] = artifact_prefix
        attributes["fetch-artifact"] = mozpath.join(
            artifact_prefix, job["artifact_name"]
        )
        alias = job.get("fetch-alias")
        if alias:
            attributes["fetch-alias"] = alias

        task_expires = "28 days" if attributes.get("cached_task") is False else expires
        artifact_expires = (
            "2 days" if attributes.get("cached_task") is False else expires
        )

        task = {
            "attributes": attributes,
            "name": name,
            "description": job["description"],
            "expires-after": task_expires,
            "label": "fetch-%s" % name,
            "run-on-projects": [],
            "run-on-repo-type": job.get("run-on-repo-type", ["git", "hg"]),
            "treeherder": {
                "symbol": join_symbol("Fetch", name),
                "kind": "build",
                "platform": "fetch/opt",
                "tier": 1,
            },
            "run": {
                "using": "run-task",
                "clone-with": "hg",
                "checkout": False,
                "command": job["command"],
            },
            "worker-type": "b-linux",
            "worker": {
                "chain-of-trust": True,
                "docker-image": {"in-tree": job.get("docker-image", "fetch")},
                "env": env,
                "max-run-time": 900,
                "artifacts": [
                    {
                        "type": "directory",
                        "name": artifact_prefix,
                        "path": "/builds/worker/artifacts",
                        "expires-after": artifact_expires,
                    }
                ],
            },
        }

        if job.get("secret", None):
            task["scopes"] = ["secrets:get:" + job.get("secret")]
            task["worker"]["taskcluster-proxy"] = True

        # Fetches that are used for local development need to be built on a
        # level-3 branch to be installable via `mach bootstrap`.
        if attributes.get("local-fetch"):
            task["run-on-projects"] = ["integration", "release"]

        if not taskgraph.fast:
            cache_name = task["label"].replace(f"{config.kind}-", "", 1)

            # This adds the level to the index path automatically.
            add_optimization(
                config,
                task,
                cache_type=CACHE_TYPE,
                cache_name=cache_name,
                digest_data=job["digest_data"],
            )
        yield task


class GpgSignatureSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    # URL where GPG signature document can be obtained. Can contain the
    # value ``{url}``, which will be substituted with the value from ``url``.
    sig_url: str
    # Path to file containing GPG public key(s) used to validate download.
    key_path: str


class StaticUrlFetchSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    type: Literal["static-url"]
    # The URL to download.
    url: str
    # The SHA-256 of the downloaded content.
    sha256: str
    # Size of the downloaded entity, in bytes.
    size: int
    # GPG signature verification.
    gpg_signature: Optional[GpgSignatureSchema] = None
    headers: Optional[list[str]] = None
    # The name to give to the generated artifact. Defaults to the file
    # portion of the URL. Using a different extension converts the
    # archive to the given type. Only conversion to .tar.zst is supported.
    artifact_name: Optional[str] = None
    # Strip the given number of path components at the beginning of
    # each file entry in the archive. Requires an artifact-name ending with .tar.zst.
    strip_components: Optional[int] = None
    # Add the given prefix to each file entry in the archive.
    # Requires an artifact-name ending with .tar.zst.
    add_prefix: Optional[str] = None
    # IMPORTANT: when adding anything that changes the behavior of the task,
    # it is important to update the digest data used to compute cache hits.


@fetch_builder("static-url", schema=StaticUrlFetchSchema)
def create_fetch_url_task(config, name, fetch):
    artifact_name = fetch.get("artifact-name")
    if not artifact_name:
        artifact_name = fetch["url"].split("/")[-1]

    command = [
        "/builds/worker/bin/fetch-content",
        "static-url",
    ]

    # Arguments that matter to the cache digest
    args = [
        "--sha256",
        fetch["sha256"],
        "--size",
        "%d" % fetch["size"],
    ]

    if fetch.get("strip-components"):
        args.extend(["--strip-components", "%d" % fetch["strip-components"]])

    if fetch.get("add-prefix"):
        args.extend(["--add-prefix", fetch["add-prefix"]])

    command.extend(args)

    env = {}

    if "gpg-signature" in fetch:
        sig_url = fetch["gpg-signature"]["sig-url"].format(url=fetch["url"])
        key_path = os.path.join(
            gecko_taskgraph.GECKO, fetch["gpg-signature"]["key-path"]
        )

        with open(key_path) as fh:
            gpg_key = fh.read()

        env["FETCH_GPG_KEY"] = gpg_key
        command.extend([
            "--gpg-sig-url",
            sig_url,
            "--gpg-key-env",
            "FETCH_GPG_KEY",
        ])

    for header in fetch.get("headers", []):
        command.extend(["--header", header])

    command.extend([
        fetch["url"],
        "/builds/worker/artifacts/%s" % artifact_name,
    ])

    return {
        "command": command,
        "artifact_name": artifact_name,
        "env": env,
        # We don't include the GPG signature in the digest because it isn't
        # materially important for caching: GPG signatures are supplemental
        # trust checking beyond what the shasum already provides.
        "digest_data": args + [artifact_name],
    }


class GitFetchSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    type: Literal["git"]
    repo: str
    # Either revision or branch must be provided (validated at runtime).
    revision: Optional[str] = None
    branch: Optional[str] = None
    include_dot_git: Optional[bool] = None
    artifact_name: Optional[str] = None
    path_prefix: Optional[str] = None
    # ssh-key is a taskcluster secret path (e.g. project/civet/github-deploy-key)
    # The path may contain `{level}`, which is replaced by the SCM level.
    # In the secret dictionary, the key should be specified as
    #  "ssh_privkey": "-----BEGIN OPENSSH PRIVATE KEY-----\nkfksnb3jc..."
    # n.b. The OpenSSH private key file format requires a newline at the end of the file.
    ssh_key: Optional[str] = None


@fetch_builder("git", schema=GitFetchSchema)
def create_git_fetch_task(config, name, fetch):
    path_prefix = fetch.get("path-prefix")
    if not path_prefix:
        path_prefix = fetch["repo"].rstrip("/").rsplit("/", 1)[-1]
    artifact_name = fetch.get("artifact-name")
    if not artifact_name:
        artifact_name = f"{path_prefix}.tar.zst"

    if "revision" in fetch and "branch" in fetch:
        raise Exception("revision and branch cannot be used in the same context")

    revision_or_branch = None

    if "revision" in fetch:
        revision_or_branch = fetch["revision"]
        if not re.match(r"[0-9a-fA-F]{40}", fetch["revision"]):
            raise Exception(f'Revision is not a sha1 in fetch task "{name}"')
    else:
        # we are sure we are dealing with a branch
        revision_or_branch = fetch["branch"]

    args = [
        "/builds/worker/bin/fetch-content",
        "git-checkout-archive",
        "--path-prefix",
        path_prefix,
        fetch["repo"],
        revision_or_branch,
        "/builds/worker/artifacts/%s" % artifact_name,
    ]

    ssh_key = fetch.get("ssh-key")
    if ssh_key:
        ssh_key = ssh_key.format(level=config.params["level"])
        args.append("--ssh-key-secret")
        args.append(ssh_key)

    digest_data = [revision_or_branch, path_prefix, artifact_name]
    if fetch.get("include-dot-git", False):
        args.append("--include-dot-git")
        digest_data.append(".git")

    return {
        "command": args,
        "artifact_name": artifact_name,
        "digest_data": digest_data,
        "secret": ssh_key,
    }


class OnnxruntimeDepsFetchSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    type: Literal["onnxruntime-deps-fetch"]
    repo: str
    revision: str
    artifact_name: str


@fetch_builder("onnxruntime-deps-fetch", schema=OnnxruntimeDepsFetchSchema)
def create_onnxruntime_deps_fetch_task(config, name, fetch):
    artifact_name = fetch.get("artifact-name")
    workdir = "/builds/worker"

    script = os.path.join(workdir, "bin", os.path.basename(ONNXRUNTIME_DEPS_SCRIPT))
    repo = fetch["repo"]
    revision = fetch["revision"]

    cmd = ["bash", "-c", f"cd {workdir} && /bin/sh {script} {repo} {revision}"]

    return {
        "command": cmd,
        "artifact_name": artifact_name,
        "docker-image": "fetch-more",
        "digest_data": [
            f"repo={repo}",
            f"revision={revision}",
            f"artifact_name={artifact_name}",
            hash_paths(GECKO, [ONNXRUNTIME_DEPS_SCRIPT]),
        ],
    }


class ChromiumFetchSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    type: Literal["chromium-fetch"]
    script: str
    # Platform type for chromium build
    platform: str
    # Chromium revision to obtain
    revision: Optional[str] = None
    # The name to give to the generated artifact.
    artifact_name: str


class Crx3UrlFetchSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    type: Literal["crx3-url"]
    # The URL of the CRX3 to download.
    url: str
    # The SHA-256 of the downloaded content.
    sha256: str
    # Size of the downloaded entity, in bytes.
    size: int
    # The name to give to the generated artifact. Must end with .tar.zst.
    artifact_name: str
    # Add the given prefix to each file entry in the archive.
    add_prefix: Optional[str] = None
    # IMPORTANT: when adding anything that changes the behavior of the task,
    # it is important to update the digest data used to compute cache hits.


@fetch_builder("crx3-url", schema=Crx3UrlFetchSchema)
def create_crx3_fetch_task(config, name, fetch):
    artifact_name = fetch["artifact-name"]
    if not artifact_name.endswith(".tar.zst"):
        raise Exception(f"Only producing .tar.zst archives is supported: {name}")

    add_prefix = fetch.get("add-prefix", "")
    command = [
        "/builds/worker/bin/fetch-crx3.py",
        "--sha256",
        fetch["sha256"],
        "--size",
        str(fetch["size"]),
    ]
    if add_prefix:
        command.extend(["--add-prefix", add_prefix])
    command.extend([fetch["url"], f"/builds/worker/artifacts/{artifact_name}"])

    return {
        "command": command,
        "artifact_name": artifact_name,
        "docker-image": "fetch-more",
        # The script decides the layout of the archive, so a change to it has
        # to produce a new archive rather than a hit on the old one.
        "digest_data": [
            fetch["sha256"],
            add_prefix,
            artifact_name,
            hash_paths(GECKO, [CRX3_SCRIPT]),
        ],
    }


@fetch_builder("chromium-fetch", schema=ChromiumFetchSchema)
def create_chromium_fetch_task(config, name, fetch):
    artifact_name = fetch.get("artifact-name")

    workdir = "/builds/worker"

    platform = fetch.get("platform")
    revision = fetch.get("revision")

    args = "--platform " + shell_quote(platform)
    if revision:
        args += " --revision " + shell_quote(revision)

    cmd = [
        "bash",
        "-c",
        "cd {} && /usr/bin/python3 {} {}".format(workdir, fetch["script"], args),
    ]

    return {
        "command": cmd,
        "artifact_name": artifact_name,
        "docker-image": "fetch-more",
        "digest_data": [
            f"revision={revision}",
            f"platform={platform}",
            f"artifact_name={artifact_name}",
        ],
    }


class CftChromedriverFetchSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    type: Literal["cft-chromedriver-fetch"]
    script: str
    # Platform type for chromium build
    platform: str
    # The name to give to the generated artifact.
    artifact_name: str
    # The chrome channel to download from.
    channel: Optional[str] = None
    # Determine if we are fetching a backup (stable version - 1) driver.
    backup: Optional[bool] = None
    # Pin a stable version of chrome to download from. To be used together with `backup`.
    version: Optional[str] = None


@fetch_builder("cft-chromedriver-fetch", schema=CftChromedriverFetchSchema)
def create_cft_canary_fetch_task(config, name, fetch):
    artifact_name = fetch.get("artifact-name")

    workdir = "/builds/worker"

    platform = fetch.get("platform")
    channel = fetch.get("channel")
    version = fetch.get("version")
    backup = fetch.get("backup", False)

    args = "--platform " + shell_quote(platform)
    if channel:
        args += " --channel " + shell_quote(channel)

    if backup:
        args += " --backup"
        # only allow pinning version with backup
        if version:
            args += " --version " + shell_quote(version)

    cmd = [
        "bash",
        "-c",
        "cd {} && /usr/bin/python3 {} {}".format(workdir, fetch["script"], args),
    ]

    return {
        "command": cmd,
        "artifact_name": artifact_name,
        "docker-image": "fetch-more",
        "digest_data": [
            f"platform={platform}",
            f"artifact_name={artifact_name}",
        ],
    }
