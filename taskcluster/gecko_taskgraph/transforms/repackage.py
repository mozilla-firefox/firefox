# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the repackage task into an actual task description.
"""

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.copy import deepcopy
from taskgraph.util.dependencies import get_primary_dependency
from taskgraph.util.schema import Schema, optionally_keyed_by, resolve_keyed_by
from taskgraph.util.taskcluster import get_artifact_prefix
from voluptuous import Any, Extra, Optional, Required

from gecko_taskgraph.transforms.job import job_description_schema
from gecko_taskgraph.util.attributes import copy_attributes_from_dependent_job
from gecko_taskgraph.util.platforms import architecture, archive_format
from gecko_taskgraph.util.workertypes import worker_type_implementation

packaging_description_schema = Schema(
    {
        # unique label to describe this repackaging task
        Optional("label"): str,
        Optional("worker-type"): str,
        Optional("worker"): object,
        Optional("attributes"): job_description_schema["attributes"],
        Optional("dependencies"): job_description_schema["dependencies"],
        # treeherder is allowed here to override any defaults we use for repackaging.  See
        # taskcluster/gecko_taskgraph/transforms/task.py for the schema details, and the
        # below transforms for defaults of various values.
        Optional("treeherder"): job_description_schema["treeherder"],
        # If a l10n task, the corresponding locale
        Optional("locale"): str,
        # Routes specific to this task, if defined
        Optional("routes"): [str],
        # passed through directly to the job description
        Optional("extra"): job_description_schema["extra"],
        # passed through to job description
        Optional("fetches"): job_description_schema["fetches"],
        Optional("run-on-projects"): job_description_schema["run-on-projects"],
        # Shipping product and phase
        Optional("shipping-product"): job_description_schema["shipping-product"],
        Optional("shipping-phase"): job_description_schema["shipping-phase"],
        Required("package-formats"): optionally_keyed_by(
            "build-platform", "release-type", "build-type", [str]
        ),
        Optional("msix"): {
            Optional("channel"): optionally_keyed_by(
                "package-format",
                "level",
                "build-platform",
                "release-type",
                "shipping-product",
                str,
            ),
            Optional("identity-name"): optionally_keyed_by(
                "package-format",
                "level",
                "build-platform",
                "release-type",
                "shipping-product",
                str,
            ),
            Optional("publisher"): optionally_keyed_by(
                "package-format",
                "level",
                "build-platform",
                "release-type",
                "shipping-product",
                str,
            ),
            Optional("publisher-display-name"): optionally_keyed_by(
                "package-format",
                "level",
                "build-platform",
                "release-type",
                "shipping-product",
                str,
            ),
            Optional("vendor"): str,
        },
        Optional("flatpak"): {
            Required("name"): optionally_keyed_by(
                "level",
                "build-platform",
                "release-type",
                "shipping-product",
                str,
            ),
            Required("branch"): optionally_keyed_by(
                "level",
                "build-platform",
                "release-type",
                "shipping-product",
                str,
            ),
        },
        # All l10n jobs use mozharness
        Required("mozharness"): {
            Extra: object,
            # Config files passed to the mozharness script
            Required("config"): optionally_keyed_by("build-platform", [str]),
            # Additional paths to look for mozharness configs in. These should be
            # relative to the base of the source checkout
            Optional("config-paths"): [str],
            # if true, perform a checkout of a comm-central based branch inside the
            # gecko checkout
            Optional("comm-checkout"): bool,
            Optional("run-as-root"): bool,
            Optional("use-caches"): Any(bool, [str]),
        },
        Optional("task-from"): job_description_schema["task-from"],
    }
)

# The configuration passed to the mozharness repackage script. This defines the
# arguments passed to `mach repackage`
# - `args` is interpolated by mozharness (`{package-name}`, `{installer-tag}`,
#   `{stub-installer-tag}`, `{sfx-stub}`, `{wsx-stub}`, `{fetch-dir}`), with values
#    from mozharness.
# - `inputs` are passed as long-options, with the filename prefixed by
#   `MOZ_FETCH_DIR`. The filename is interpolated by taskgraph
#   (`{archive_format}`).
# - `output` is passed to `--output`, with the filename prefixed by the output
#   directory.
PACKAGE_FORMATS = {
    "mar": {
        "args": [
            "mar",
            "--arch",
            "{architecture}",
            "--mar-channel-id",
            "{mar-channel-id}",
        ],
        "inputs": {
            "input": "target{archive_format}",
            "mar": "mar-tools/mar",
        },
        "output": "target.complete.mar",
    },
    "msi": {
        "args": [
            "msi",
            "--wsx",
            "{wsx-stub}",
            "--version",
            "{version_display}",
            "--locale",
            "{_locale}",
            "--arch",
            "{architecture}",
            "--candle",
            "{fetch-dir}/candle.exe",
            "--light",
            "{fetch-dir}/light.exe",
        ],
        "inputs": {
            "setupexe": "target.installer.exe",
        },
        "output": "target.installer.msi",
    },
    "msix": {
        "args": [
            "msix",
            "--channel",
            "{msix-channel}",
            "--publisher",
            "{msix-publisher}",
            "--publisher-display-name",
            "{msix-publisher-display-name}",
            "--identity-name",
            "{msix-identity-name}",
            "--vendor",
            "{msix-vendor}",
            "--arch",
            "{architecture}",
            # For langpacks.  Ignored if directory does not exist.
            "--distribution-dir",
            "{fetch-dir}/distribution",
            "--verbose",
            "--makeappx",
            "{fetch-dir}/msix-packaging/makemsix",
        ],
        "inputs": {
            "input": "target{archive_format}",
        },
        "output": "target.installer.msix",
    },
    "msix-store": {
        "args": [
            "msix",
            "--channel",
            "{msix-channel}",
            "--publisher",
            "{msix-publisher}",
            "--publisher-display-name",
            "{msix-publisher-display-name}",
            "--identity-name",
            "{msix-identity-name}",
            "--vendor",
            "{msix-vendor}",
            "--arch",
            "{architecture}",
            # For langpacks.  Ignored if directory does not exist.
            "--distribution-dir",
            "{fetch-dir}/distribution",
            "--verbose",
            "--makeappx",
            "{fetch-dir}/msix-packaging/makemsix",
        ],
        "inputs": {
            "input": "target{archive_format}",
        },
        "output": "target.store.msix",
    },
    "dmg": {
        "args": [
            "dmg",
            "--compression",
            "lzma",
        ],
        "inputs": {
            "input": "target{archive_format}",
        },
        "output": "target.dmg",
    },
    "dmg-attrib": {
        "args": [
            "dmg",
            "--compression",
            "lzma",
            "--attribution_sentinel",
            "__MOZCUSTOM__",
        ],
        "inputs": {
            "input": "target{archive_format}",
        },
        "output": "target.dmg",
    },
    "pkg": {
        "args": ["pkg"],
        "inputs": {
            "input": "target{archive_format}",
        },
        "output": "target.pkg",
    },
    "installer": {
        "args": [
            "installer",
            "--package-name",
            "{package-name}",
            "--tag",
            "{installer-tag}",
            "--sfx-stub",
            "{sfx-stub}",
        ],
        "inputs": {
            "package": "target{archive_format}",
            "setupexe": "setup.exe",
        },
        "output": "target.installer.exe",
    },
    "installer-stub": {
        "args": [
            "installer",
            "--tag",
            "{stub-installer-tag}",
            "--sfx-stub",
            "{sfx-stub}",
        ],
        "inputs": {
            "setupexe": "setup-stub.exe",
        },
        "output": "target.stub-installer.exe",
    },
    "deb": {
        "args": [
            "deb",
            "--arch",
            "{architecture}",
            "--templates",
            "{deb-templates}",
            "--version",
            "{version_display}",
            "--build-number",
            "{build_number}",
            "--release-product",
            "{release_product}",
            "--release-type",
            "{release_type}",
        ],
        "inputs": {
            "input": "target{archive_format}",
        },
        "output": "target.deb",
    },
    "deb-l10n": {
        "args": [
            "deb-l10n",
            "--version",
            "{version_display}",
            "--build-number",
            "{build_number}",
            "--templates",
            "{deb-l10n-templates}",
            "--release-product",
            "{release_product}",
        ],
        "inputs": {
            "input-xpi-file": "target.langpack.xpi",
            "input-tar-file": "target{archive_format}",
        },
        "output": "target.langpack.deb",
    },
    "rpm": {
        "args": [
            "rpm",
            "--arch",
            "{architecture}",
            "--templates",
            "{rpm-templates}",
            "--version",
            "{version_display}",
            "--build-number",
            "{build_number}",
            "--release-product",
            "{release_product}",
            "--release-type",
            "{release_type}",
            "--input-xpi-dir",
            "{fetch-dir}",
        ],
        "inputs": {
            "input": "target{archive_format}",
        },
        "output": "target.rpm",
    },
    "desktop-file": {
        "args": [
            "desktop-file",
            "--flavor",
            "flatpak",
            "--release-product",
            "firefox",
            "--release-type",
            "{release_type}",
        ],
        "inputs": {},
        "output": "target.flatpak.desktop",
    },
    "flatpak": {
        "args": [
            "flatpak",
            "--name",
            "{flatpak-name}",
            "--arch",
            "{architecture}",
            "--version",
            "{version_display}",
            "--product",
            "{package-name}",
            "--release-type",
            "{release_type}",
            "--flatpak-branch",
            "{flatpak-branch}",
            "--template-dir",
            "{flatpak-templates}",
            "--langpack-pattern",
            "{fetch-dir}/extensions/*/target.langpack.xpi",
        ],
        "inputs": {
            "input": "target{archive_format}",
        },
        "output": "target.flatpak.tar.xz",
    },
}
MOZHARNESS_EXPANSIONS = [
    "package-name",
    "installer-tag",
    "fetch-dir",
    "stub-installer-tag",
    "deb-templates",
    "rpm-templates",
    "deb-l10n-templates",
    "sfx-stub",
    "wsx-stub",
    "flatpak-templates",
]

transforms = TransformSequence()


@transforms.add
def remove_name(config, jobs):
    for job in jobs:
        if "name" in job:
            del job["name"]
        yield job


transforms.add_validate(packaging_description_schema)


@transforms.add
def copy_in_useful_magic(config, jobs):
    """Copy attributes from upstream task to be used for keyed configuration."""
    for job in jobs:
        dep = get_primary_dependency(config, job)
        assert dep

        job["build-platform"] = dep.attributes.get("build_platform")
        job["shipping-product"] = dep.attributes.get("shipping_product")
        job["build-type"] = dep.attributes.get("build_type")
        yield job


@transforms.add
def handle_keyed_by(config, jobs):
    """Resolve fields that can be keyed by platform, etc, but not `msix.*` fields
    that can be keyed by `package-format`.  Such fields are handled specially below.
    """
    fields = [
        "mozharness.config",
        "package-formats",
        "worker.max-run-time",
        "flatpak.name",
        "flatpak.branch",
    ]
    for job in jobs:
        job = deepcopy(job)  # don't overwrite dict values here
        for field in fields:
            resolve_keyed_by(
                item=job,
                field=field,
                item_name="?",
                **{
                    "release-type": config.params["release_type"],
                    "level": config.params["level"],
                },
            )
        yield job


@transforms.add
def make_repackage_description(config, jobs):
    for job in jobs:
        dep_job = get_primary_dependency(config, job)
        assert dep_job

        label = job.get("label", dep_job.label.replace("signing-", "repackage-"))
        job["label"] = label

        yield job


@transforms.add
def make_job_description(config, jobs):
    for job in jobs:
        dep_job = get_primary_dependency(config, job)
        assert dep_job

        dependencies = {dep_job.kind: dep_job.label}

        attributes = copy_attributes_from_dependent_job(dep_job)
        attributes["repackage_type"] = "repackage"

        locale = attributes.get("locale", job.get("locale"))
        if locale:
            attributes["locale"] = locale

        description = (
            "Repackaging for locale '{locale}' for build '"
            "{build_platform}/{build_type}'".format(
                locale=attributes.get("locale", "en-US"),
                build_platform=attributes.get("build_platform"),
                build_type=attributes.get("build_type"),
            )
        )

        treeherder = job.get("treeherder", {})
        treeherder.setdefault("symbol", "Rpk")
        dep_th_platform = dep_job.task.get("extra", {}).get("treeherder-platform")
        treeherder.setdefault("platform", dep_th_platform)
        treeherder.setdefault("tier", 1)
        treeherder.setdefault("kind", "build")

        # Search dependencies before adding langpack dependencies.
        signing_task = None
        repackage_signing_task = None
        for dependency in dependencies.keys():
            if "repackage-signing" in dependency:
                repackage_signing_task = dependency
            elif "signing" in dependency or "notarization" in dependency:
                signing_task = dependency

        if config.kind == "repackage-msi":
            treeherder["symbol"] = "MSI({})".format(locale or "N")

        elif config.kind == "repackage-msix":
            assert not locale

            # Like "MSIXs(Bs)".
            treeherder["symbol"] = "MSIX({})".format(
                dep_job.task.get("extra", {}).get("treeherder", {}).get("symbol", "B")
            )

        elif config.kind == "repackage-shippable-l10n-msix":
            assert not locale

            if attributes.get("l10n_chunk") or attributes.get("chunk_locales"):
                # We don't want to produce MSIXes for single-locale repack builds.
                continue

            description = (
                "Repackaging with multiple locales for build '"
                "{build_platform}/{build_type}'".format(
                    build_platform=attributes.get("build_platform"),
                    build_type=attributes.get("build_type"),
                )
            )

            # Like "MSIXs(Bs-multi)".
            treeherder["symbol"] = "MSIX({}-multi)".format(
                dep_job.task.get("extra", {}).get("treeherder", {}).get("symbol", "B")
            )

            fetches = job.setdefault("fetches", {})

            # The keys are unique, like `shippable-l10n-signing-linux64-shippable-1/opt`, so we
            # can't ask for the tasks directly, we must filter for them.
            for t in config.kind_dependencies_tasks.values():
                if t.kind != "shippable-l10n-signing":
                    continue
                if t.attributes["build_platform"] != "linux64-shippable":
                    continue
                if t.attributes["build_type"] != "opt":
                    continue

                dependencies.update({t.label: t.label})

                fetches.update(
                    {
                        t.label: [
                            {
                                "artifact": f"{loc}/target.langpack.xpi",
                                "extract": False,
                                # Otherwise we can't disambiguate locales!
                                "dest": f"distribution/extensions/{loc}",
                            }
                            for loc in t.attributes["chunk_locales"]
                        ]
                    }
                )

        elif config.kind in ("repackage-deb", "repackage-rpm"):
            attributes["repackage_type"] = config.kind
            description = (
                "Repackaging the '{build_platform}/{build_type}' "
                "{version} build into a '.{package}' package"
            ).format(
                build_platform=attributes.get("build_platform"),
                build_type=attributes.get("build_type"),
                version=config.params["version"],
                package=config.kind.split("-")[1],
            )

        elif config.kind == "repackage-flatpak":
            assert not locale

            if attributes.get("l10n_chunk") or attributes.get("chunk_locales"):
                # We don't want to produce flatpaks for single-locale repack builds.
                continue

            fetches = job.setdefault("fetches", {})
            # The keys are unique, like `shippable-l10n-signing-linux64-shippable-1/opt`, so we
            # can't ask for the tasks directly, we must filter for them.
            for t in config.kind_dependencies_tasks.values():
                if attributes.get("shippable"):
                    if (
                        t.kind != "shippable-l10n-signing"
                        or t.attributes["build_platform"] != "linux64-shippable"
                    ):
                        continue
                elif t.kind != "l10n" or t.attributes["build_platform"] != "linux64":
                    continue
                if t.attributes["build_type"] != "opt":
                    continue

                locales = t.attributes.get(
                    "chunk_locales", t.attributes.get("all_locales")
                )

                dependencies.update({t.label: t.label})

                fetches.update(
                    {
                        t.label: [
                            {
                                "artifact": f"{loc}/target.langpack.xpi",
                                "extract": False,
                                # Otherwise we can't disambiguate locales!
                                "dest": f"extensions/{loc}",
                            }
                            for loc in locales
                        ]
                    }
                )

        _fetch_subst_locale = "en-US"
        if locale:
            _fetch_subst_locale = locale

        worker_type = job["worker-type"]
        build_platform = attributes["build_platform"]

        use_stub = attributes.get("stub-installer")

        repackage_config = []
        package_formats = job.get("package-formats")
        if use_stub and not repackage_signing_task and "msix" not in package_formats:
            # if repackage_signing_task doesn't exists, generate the stub installer
            package_formats += ["installer-stub"]
        for format in package_formats:
            command = deepcopy(PACKAGE_FORMATS[format])
            substs = {
                "archive_format": archive_format(build_platform),
                "_locale": _fetch_subst_locale,
                "architecture": architecture(build_platform),
                "version_display": config.params["version"],
                "mar-channel-id": attributes["mar-channel-id"],
                "build_number": config.params["build_number"],
                "release_product": config.params["release_product"],
                "release_type": config.params["release_type"],
                "flatpak-name": job.get("flatpak", {}).get("name"),
                "flatpak-branch": job.get("flatpak", {}).get("branch"),
            }
            # Allow us to replace `args` as well, but specifying things expanded in mozharness
            # without breaking .format and without allowing unknown through.
            substs.update({name: f"{{{name}}}" for name in MOZHARNESS_EXPANSIONS})

            # We need to resolve `msix.*` values keyed by `package-format` for each format, not
            # just once, so we update a temporary copy just for extracting these values.
            temp_job = deepcopy(job)
            for msix_key in (
                "channel",
                "identity-name",
                "publisher",
                "publisher-display-name",
                "vendor",
            ):
                resolve_keyed_by(
                    item=temp_job,
                    field=f"msix.{msix_key}",
                    item_name="?",
                    **{
                        "package-format": format,
                        "release-type": config.params["release_type"],
                        "level": config.params["level"],
                    },
                )

                # Turn `msix.channel` into `msix-channel`, etc.
                value = temp_job.get("msix", {}).get(msix_key)
                if value:
                    substs.update(
                        {f"msix-{msix_key}": value},
                    )

            command["inputs"] = {
                name: filename.format(**substs)
                for name, filename in command["inputs"].items()
            }
            command["args"] = [arg.format(**substs) for arg in command["args"]]
            if "installer" in format and "aarch64" not in build_platform:
                command["args"].append("--use-upx")

            repackage_config.append(command)

        run = job.get("mozharness", {})
        run.update(
            {
                "using": "mozharness",
                "script": "mozharness/scripts/repackage.py",
                "job-script": "taskcluster/scripts/builder/repackage.sh",
                "actions": ["setup", "repackage"],
                "extra-config": {
                    "repackage_config": repackage_config,
                },
                "run-as-root": run.get("run-as-root", False),
            }
        )

        worker = job.get("worker", {})
        worker.update(
            {
                "chain-of-trust": True,
                # Don't add generic artifact directory.
                "skip-artifacts": True,
            }
        )
        worker.setdefault("max-run-time", 3600)

        if locale:
            # Make sure we specify the locale-specific upload dir
            worker.setdefault("env", {})["LOCALE"] = locale

        worker["artifacts"] = _generate_task_output_files(
            dep_job,
            worker_type_implementation(config.graph_config, config.params, worker_type),
            repackage_config=repackage_config,
            locale=locale,
        )
        attributes["release_artifacts"] = [
            artifact["name"] for artifact in worker["artifacts"]
        ]

        task = {
            "label": job["label"],
            "description": description,
            "worker-type": worker_type,
            "dependencies": dependencies,
            "if-dependencies": [dep_job.kind],
            "attributes": attributes,
            "run-on-projects": job.get(
                "run-on-projects", dep_job.attributes.get("run_on_projects")
            ),
            "optimization": dep_job.optimization,
            "treeherder": treeherder,
            "routes": job.get("routes", []),
            "extra": job.get("extra", {}),
            "worker": worker,
            "run": run,
            "fetches": _generate_download_config(
                config,
                dep_job,
                build_platform,
                signing_task,
                repackage_signing_task,
                locale=locale,
                existing_fetch=job.get("fetches"),
            ),
        }

        if build_platform.startswith("macosx"):
            task.setdefault("fetches", {}).setdefault("toolchain", []).extend(
                [
                    "linux64-libdmg",
                    "linux64-hfsplus",
                    "linux64-node",
                    "linux64-xar",
                    "linux64-mkbom",
                ]
            )

        if "shipping-phase" in job:
            task["shipping-phase"] = job["shipping-phase"]

        yield task


def _generate_download_config(
    config,
    task,
    build_platform,
    signing_task,
    repackage_signing_task,
    locale=None,
    existing_fetch=None,
):
    locale_path = f"{locale}/" if locale else ""
    fetch = {}
    if existing_fetch:
        fetch.update(existing_fetch)

    if repackage_signing_task and build_platform.startswith("win"):
        fetch.update(
            {
                repackage_signing_task: [f"{locale_path}target.installer.exe"],
            }
        )
    elif build_platform.startswith("linux") or build_platform.startswith("macosx"):
        signing_fetch = [
            {
                "artifact": f"{locale_path}target{archive_format(build_platform)}",
                "extract": False,
            },
        ]
        if config.kind == "repackage-deb-l10n":
            signing_fetch.append(
                {
                    "artifact": f"{locale_path}target.langpack.xpi",
                    "extract": False,
                }
            )
        fetch.update({signing_task: signing_fetch})
    elif build_platform.startswith("win"):
        fetch.update(
            {
                signing_task: [
                    {
                        "artifact": f"{locale_path}target.zip",
                        "extract": False,
                    },
                    f"{locale_path}setup.exe",
                ],
            }
        )

        use_stub = task.attributes.get("stub-installer")
        if use_stub:
            fetch[signing_task].append(f"{locale_path}setup-stub.exe")

    if fetch:
        return fetch

    raise NotImplementedError(f'Unsupported build_platform: "{build_platform}"')


def _generate_task_output_files(
    task, worker_implementation, repackage_config, locale=None
):
    locale_output_path = f"{locale}/" if locale else ""
    artifact_prefix = get_artifact_prefix(task)

    if worker_implementation == ("docker-worker", "linux"):
        local_prefix = "/builds/worker/workspace/"
    elif worker_implementation == ("generic-worker", "windows"):
        local_prefix = "workspace/"
    else:
        raise NotImplementedError(
            f'Unsupported worker implementation: "{worker_implementation}"'
        )

    output_files = []
    for config in repackage_config:
        output_files.append(
            {
                "type": "file",
                "path": "{}outputs/{}{}".format(
                    local_prefix, locale_output_path, config["output"]
                ),
                "name": "{}/{}{}".format(
                    artifact_prefix, locale_output_path, config["output"]
                ),
            }
        )
    return output_files
