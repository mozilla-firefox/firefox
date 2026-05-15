# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Apply some defaults and minor modifications to the jobs defined in the
build-apk and build-bundle kinds.
"""

from taskgraph.transforms.base import TransformSequence
from taskgraph.util import path

from android_taskgraph.build_config import get_variant

transforms = TransformSequence()


@transforms.add
def add_common_config(config, tasks):
    for task in tasks:
        fetches = task.setdefault("fetches", {})
        fetches["toolchain"] = [
            "android-sdk-linux",
            "linux64-embedded-uniffi-bindgen",
            "linux64-nimbus-fml",
            "android-gradle-dependencies",
            "linux64-jdk",
        ]
        fetches["build-fat-aar"] = [
            "target.maven.zip",
            {"artifact": "mozconfig", "extract": False},
        ]

        run = task.setdefault("run", {})
        run["using"] = "gradlew"
        run["use-caches"] = False

        treeherder = task.setdefault("treeherder", {})
        treeherder["kind"] = "build"
        treeherder["tier"] = 1

        task["worker-type"] = "b-linux-docker-amd"

        worker = task.setdefault("worker", {})
        worker["docker-image"] = {}
        worker["docker-image"]["in-tree"] = "android-components"
        worker["max-run-time"] = 7200
        worker["chain-of-trust"] = True
        worker.setdefault("env", {}).setdefault(
            "MOZCONFIG", "/builds/worker/fetches/mozconfig"
        )
        build_fat_aar = config.kind_dependencies_tasks[
            task["dependencies"]["build-fat-aar"]
        ]
        if build_fat_aar.attributes.get("shippable", False):
            worker["env"].setdefault(
                "MOZ_UPDATE_CHANNEL",
                build_fat_aar.attributes.get(
                    "update-channel", "nightly-{}".format(config.params["project"])
                ),
            )

        yield task


@transforms.add
def add_variant_config(config, tasks):
    for task in tasks:
        attributes = task.setdefault("attributes", {})
        if not attributes.get("build-type"):
            attributes["build-type"] = task["name"]
        yield task


@transforms.add
def add_shippable_secrets(config, tasks):
    for task in tasks:
        secrets = task["run"].setdefault("secrets", [])
        dummy_secrets = task["run"].setdefault("dummy-secrets", [])

        if (
            task.pop("include-shippable-secrets", False)
            and config.params["level"] == "3"
        ):
            secrets.extend([
                {
                    "key": key,
                    "name": _get_secret_index(task["name"]),
                    "path": target_file,
                }
                for key, target_file in _get_secrets_keys_and_target_files(task)
            ])
        else:
            dummy_secrets.extend([
                {
                    "content": fake_value,
                    "path": target_file,
                }
                for fake_value, target_file in (
                    ("faketoken", ".adjust_token"),
                    ("faketoken", ".mls_token"),
                    ("https://fake@sentry.prod.mozaws.net/368", ".sentry_token"),
                    ("0", ".gps_integrity_token"),
                )
            ])

        yield task


def _get_secrets_keys_and_target_files(task):
    secrets = [
        ("adjust", ".adjust_token"),
        ("sentry_dsn", ".sentry_token"),
        ("mls", ".mls_token"),
        ("nimbus_url", ".nimbus"),
        ("gps_integrity_token", ".gps_integrity_token"),
    ]

    if task["name"].startswith("fenix-"):
        gradle_build_type = task["run"]["gradle-build-type"]
        secrets.extend([
            (
                "firebase",
                f"app/src/{gradle_build_type}/res/values/firebase.xml",
            ),
            ("wallpaper_url", ".wallpaper_url"),
        ])

    return secrets


def _get_secret_index(task_name):
    product_name = task_name.split("-")[0]
    secret_name = task_name[len(product_name) + 1 :]
    secret_project_name = (
        "focus-android" if product_name in ("focus", "klar") else product_name
    )
    return f"project/mobile/firefox-android/{secret_project_name}/{secret_name}"


@transforms.add
def build_pre_gradle_command(config, tasks):
    for task in tasks:
        source_project_name = task["source-project-name"]
        pre_gradlew = task["run"].setdefault("pre-gradlew", [])
        pre_gradlew.append(["cd", path.join("mobile", "android", source_project_name)])

        yield task


@transforms.add
def build_gradle_command(config, tasks):
    for task in tasks:
        gradle_build_type = task["run"].get("gradle-build-type")
        gradle_build_name = task["run"].get("gradle-build-name")

        variant_name = ""
        if gradle_build_type and gradle_build_name:
            variant_name = get_variant(gradle_build_type, gradle_build_name)[
                "name"
            ].capitalize()

        package_command = task["run"].pop("gradle-package-command", "assemble")
        gradle_command = [
            "clean",
            f"{package_command}{variant_name}",
        ]

        if task["run"].pop("track-apk-size", False):
            gradle_command.append(f"apkSize{variant_name}")

        task["run"]["gradlew"] = gradle_command

        yield task


@transforms.add
def add_baseline_profile_path(config, tasks):
    for task in tasks:
        baseline_profile_path = task["run"].pop("baseline-profile-path", "")
        if baseline_profile_path:
            task["run"]["gradlew"].append(
                f"-PbaselineProfilePath={baseline_profile_path}"
            )

        yield task


@transforms.add
def extra_gradle_options(config, tasks):
    for task in tasks:
        for extra in task["run"].pop("gradle-extra-options", []):
            task["run"]["gradlew"].append(extra)

        yield task


@transforms.add
def add_test_build_type(config, tasks):
    for task in tasks:
        test_build_type = task["run"].pop("test-build-type", "")
        if test_build_type:
            task["run"]["gradlew"].append(f"-PtestBuildType={test_build_type}")
        yield task


@transforms.add
def add_disable_optimization(config, tasks):
    for task in tasks:
        if task.pop("disable-optimization", False):
            task["run"]["gradlew"].append("-PdisableOptimization")
        yield task


@transforms.add
def add_nightly_version(config, tasks):
    for task in tasks:
        if task.pop("include-nightly-version", False):
            task["run"]["gradlew"].extend([
                # We only set the `official` flag here. The actual version name will be determined
                # by Gradle (depending on the Gecko/A-C version being used)
                "-Pofficial"
            ])
        yield task


@transforms.add
def add_release_version(config, tasks):
    for task in tasks:
        if task.pop("include-release-version", False):
            task["run"]["gradlew"].extend([
                "-PversionName={}".format(config.params["version"]),
                "-Pofficial",
            ])
        yield task


@transforms.add
def add_artifacts(config, tasks):
    for task in tasks:
        gradle_build_type = task["run"].pop("gradle-build-type", "")
        gradle_build_name = task["run"].pop("gradle-build-name", "")
        gradle_build = task["run"].pop("gradle-build", "")
        source_project_name = task.pop("source-project-name")

        if not gradle_build_type or not gradle_build_name or not gradle_build:
            yield task
            continue

        variant_config = get_variant(gradle_build_type, gradle_build_name)
        artifacts = task.setdefault("worker", {}).setdefault("artifacts", [])

        task["attributes"]["apks"] = apks = {}

        if "apk-artifact-template" in task:
            artifact_template = task.pop("apk-artifact-template")

            # Fenix has no product flavor, so APK paths don't include it.
            if gradle_build == "fenix":
                apk_path = gradle_build_type
            else:
                apk_path = f"{gradle_build}/{gradle_build_type}"

            for apk in variant_config["apks"]:
                apk_name = artifact_template["name"].format(
                    gradle_build=gradle_build, **apk
                )
                artifacts.append({
                    "type": artifact_template["type"],
                    "name": apk_name,
                    "path": artifact_template["path"].format(
                        gradle_build_type=gradle_build_type,
                        gradle_build=gradle_build,
                        apk_path=apk_path,
                        source_project_name=source_project_name,
                        **apk,
                    ),
                })
                apks[apk["abi"]] = {
                    "name": apk_name,
                }
        elif "aab-artifact-template" in task:
            variant_name = variant_config["name"]
            artifact_template = task.pop("aab-artifact-template")

            # Fenix has no product flavor, so AAB filenames don't include it.
            if gradle_build == "fenix":
                aab_filename = f"app-{gradle_build_type}.aab"
            else:
                aab_filename = f"app-{gradle_build}-{gradle_build_type}.aab"

            artifacts.append({
                "type": artifact_template["type"],
                "name": artifact_template["name"],
                "path": artifact_template["path"].format(
                    gradle_build_type=gradle_build_type,
                    gradle_build=gradle_build,
                    source_project_name=source_project_name,
                    variant_name=variant_name,
                    aab_filename=aab_filename,
                ),
            })
            task["attributes"]["aab"] = artifact_template["name"]

        # Only bundles are minified and consumed by the sentry-upload kind. Debug builds have no
        # mapping file at all, so declaring these artifacts there would leave them permanently
        # missing.
        if config.kind == "build-bundle" and gradle_build_type != "debug":
            artifacts.append({
                "type": "file",
                "name": "public/build/mapping.txt",
                "path": (
                    "/builds/worker/workspace/obj-build/gradle/build/mobile/android"
                    f"/{source_project_name}/app/outputs/mapping"
                    f"/{gradle_build_name}/mapping.txt"
                ),
            })

            artifacts.append({
                "type": "file",
                "name": "public/build/sentry-proguard-uuid.txt",
                "path": (
                    "/builds/worker/workspace/obj-build/gradle/build/mobile/android"
                    f"/{source_project_name}/app/sentry"
                    f"/{gradle_build_name}/proguard-uuid.txt"
                ),
            })

        yield task
