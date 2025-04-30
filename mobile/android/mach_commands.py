# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import logging
import os
import platform
import shutil
import sys
import tarfile
import time

import mozpack.path as mozpath
from mach.decorators import Command, CommandArgument, SubCommand
from mozbuild.base import MachCommandConditions as conditions
from mozbuild.shellutil import split as shell_split

# Mach's conditions facility doesn't support subcommands.  Print a
# deprecation message ourselves instead.
LINT_DEPRECATION_MESSAGE = """
Android lints are now integrated with mozlint.  Instead of
`mach android {api-lint,checkstyle,lint,test}`, run
`mach lint --linter android-{api-lint,checkstyle,lint,test}`.
Or run `mach lint`.
"""


# NOTE python/mach/mach/commands/commandinfo.py references this function
#      by name. If this function is renamed or removed, that file should
#      be updated accordingly as well.
def REMOVED(cls):
    """Command no longer exists! Use the Gradle configuration rooted in the top source directory
    instead.

    See https://developer.mozilla.org/en-US/docs/Simple_Firefox_for_Android_build#Developing_Firefox_for_Android_in_Android_Studio_or_IDEA_IntelliJ.  # NOQA: E501
    """
    return False


@Command(
    "android",
    category="devenv",
    description="Run Android-specific commands.",
    conditions=[conditions.is_android],
)
def android(command_context):
    pass


@SubCommand(
    "android",
    "export",
    """Generate SDK bindings and GeckoView JNI wrappers used when building GeckoView.""",
)
@CommandArgument(
    "inputs",
    nargs="+",
    help="config files, like [/path/to/ClassName-classes.txt]+",
)
@CommandArgument("args", nargs=argparse.REMAINDER)
def export(command_context, inputs, args):
    import itertools

    def stem(input):
        # Turn "/path/to/ClassName-classes.txt" into "ClassName".
        return os.path.basename(input).rsplit("-classes.txt", 1)[0]

    bindings_inputs = list(itertools.chain(*((input, stem(input)) for input in inputs)))
    bindings_args = "-Pgenerate_sdk_bindings_args={}".format(";".join(bindings_inputs))

    ret = gradle(
        command_context,
        command_context.substs["GRADLE_ANDROID_GENERATE_GENERATED_JNI_WRAPPERS_TASKS"]
        + command_context.substs["GRADLE_ANDROID_GENERATE_SDK_BINDINGS_TASKS"]
        + [bindings_args]
        + args,
        verbose=True,
    )

    return ret


@SubCommand(
    "android",
    "api-lint",
    """Run Android api-lint.
REMOVED/DEPRECATED: Use 'mach lint --linter android-api-lint'.""",
)
def android_apilint_REMOVED(command_context):
    print(LINT_DEPRECATION_MESSAGE)
    return 1


@SubCommand(
    "android",
    "test",
    """Run Android test.
REMOVED/DEPRECATED: Use 'mach lint --linter android-test'.""",
)
def android_test_REMOVED(command_context):
    print(LINT_DEPRECATION_MESSAGE)
    return 1


@SubCommand(
    "android",
    "lint",
    """Run Android lint.
REMOVED/DEPRECATED: Use 'mach lint --linter android-lint'.""",
)
def android_lint_REMOVED(command_context):
    print(LINT_DEPRECATION_MESSAGE)
    return 1


@SubCommand(
    "android",
    "checkstyle",
    """Run Android checkstyle.
REMOVED/DEPRECATED: Use 'mach lint --linter android-checkstyle'.""",
)
def android_checkstyle_REMOVED(command_context):
    print(LINT_DEPRECATION_MESSAGE)
    return 1


@SubCommand(
    "android",
    "gradle-dependencies",
    """Collect Android Gradle dependencies.
    See http://firefox-source-docs.mozilla.org/build/buildsystem/toolchains.html#firefox-for-android-with-gradle""",  # NOQA: E501
)
@CommandArgument("args", nargs=argparse.REMAINDER)
def android_gradle_dependencies(command_context, args):
    # We don't want to gate producing dependency archives on clean
    # lint or checkstyle, particularly because toolchain versions
    # can change the outputs for those processes.
    gradle(
        command_context,
        command_context.substs["GRADLE_ANDROID_DEPENDENCIES_TASKS"]
        + ["--continue"]
        + args,
        verbose=True,
    )

    return 0


def get_maven_archive_paths(maven_folder):
    for subdir, _, files in os.walk(maven_folder):
        if "-SNAPSHOT" in subdir:
            continue
        for file in files:
            yield os.path.join(subdir, file)


def create_maven_archive(topobjdir):
    gradle_folder = os.path.join(topobjdir, "gradle")
    maven_folder = os.path.join(gradle_folder, "maven")

    # Create the archive, with no compression: The archive contents are large
    # files which cannot be significantly compressed; attempting to compress
    # the archive is usually expensive in time and results in minimal
    # reduction in size.
    # Even though the archive is not compressed, use the .xz file extension
    # so that the taskcluster worker also skips compression.
    with tarfile.open(os.path.join(gradle_folder, "target.maven.tar.xz"), "w") as tar:
        for abs_path in get_maven_archive_paths(maven_folder):
            tar.add(
                abs_path,
                arcname=os.path.join(
                    "geckoview", os.path.relpath(abs_path, maven_folder)
                ),
            )


@SubCommand(
    "android",
    "archive-geckoview",
    """Create GeckoView archives.
    See http://firefox-source-docs.mozilla.org/build/buildsystem/toolchains.html#firefox-for-android-with-gradle""",  # NOQA: E501
)
@CommandArgument("args", nargs=argparse.REMAINDER)
def android_archive_geckoview(command_context, args):
    tasks = command_context.substs["GRADLE_ANDROID_ARCHIVE_GECKOVIEW_TASKS"]
    subproject = command_context.substs.get("MOZ_ANDROID_SUBPROJECT")
    if subproject in (None, "geckoview_example"):
        tasks += command_context.substs[
            "GRADLE_ANDROID_ARCHIVE_GECKOVIEW_SUBPROJECT_TASKS"
        ]
    ret = gradle(
        command_context,
        tasks + args,
        verbose=True,
    )

    if ret != 0:
        return ret
    if "MOZ_AUTOMATION" in os.environ:
        create_maven_archive(command_context.topobjdir)

    return 0


@SubCommand("android", "build-geckoview_example", """Build geckoview_example """)
@CommandArgument("args", nargs=argparse.REMAINDER)
def android_build_geckoview_example(command_context, args):
    gradle(
        command_context,
        command_context.substs["GRADLE_ANDROID_BUILD_GECKOVIEW_EXAMPLE_TASKS"] + args,
        verbose=True,
    )

    print(
        "Execute `mach android install-geckoview_example` "
        "to push the geckoview_example and test APKs to a device."
    )

    return 0


@SubCommand("android", "compile-all", """Build all source files""")
@CommandArgument("args", nargs=argparse.REMAINDER)
def android_compile_all(command_context, args):
    ret = gradle(
        command_context,
        command_context.substs["GRADLE_ANDROID_COMPILE_ALL_TASKS"] + args,
        verbose=True,
    )

    return ret


def install_app_bundle(command_context, bundle):
    from mozdevice import ADBDeviceFactory

    bundletool = mozpath.join(command_context._mach_context.state_dir, "bundletool.jar")
    device = ADBDeviceFactory(verbose=True)
    bundle_path = mozpath.join(command_context.topobjdir, bundle)
    java_home = java_home = os.path.dirname(
        os.path.dirname(command_context.substs["JAVA"])
    )
    device.install_app_bundle(bundletool, bundle_path, java_home, timeout=120)


@SubCommand("android", "install-geckoview_example", """Install geckoview_example """)
@CommandArgument("args", nargs=argparse.REMAINDER)
def android_install_geckoview_example(command_context, args):
    gradle(
        command_context,
        command_context.substs["GRADLE_ANDROID_INSTALL_GECKOVIEW_EXAMPLE_TASKS"] + args,
        verbose=True,
    )

    print(
        "Execute `mach android build-geckoview_example` "
        "to just build the geckoview_example and test APKs."
    )

    return 0


@SubCommand("android", "install-fenix", """Install fenix """)
@CommandArgument("args", nargs=argparse.REMAINDER)
def android_install_fenix(command_context, args):
    gradle(
        command_context,
        ["fenix:installFenixDebug"] + args,
        verbose=True,
    )
    return 0


@SubCommand("android", "install-focus", """Install focus """)
@CommandArgument("args", nargs=argparse.REMAINDER)
def android_install_focus(command_context, args):
    gradle(
        command_context,
        ["focus-android:installFocusDebug"] + args,
        verbose=True,
    )
    return 0


@SubCommand(
    "android", "install-geckoview-test_runner", """Install geckoview.test_runner """
)
@CommandArgument("args", nargs=argparse.REMAINDER)
def android_install_geckoview_test_runner(command_context, args):
    gradle(
        command_context,
        command_context.substs["GRADLE_ANDROID_INSTALL_GECKOVIEW_TEST_RUNNER_TASKS"]
        + args,
        verbose=True,
    )
    return 0


@SubCommand("android", "installFenixRelease", """Install fenix Release""")
@CommandArgument("args", nargs=argparse.REMAINDER)
def android_install_fenix_release(command_context, args):
    gradle(
        command_context,
        ["-p", "mobile/android/fenix", "installFenixRelease"] + args,
        verbose=True,
    )
    return 0


@SubCommand(
    "android",
    "install-geckoview-test_runner-aab",
    """Install geckoview.test_runner with AAB""",
)
@CommandArgument("args", nargs=argparse.REMAINDER)
def android_install_geckoview_test_runner_aab(command_context, args):
    install_app_bundle(
        command_context,
        command_context.substs["GRADLE_ANDROID_GECKOVIEW_TEST_RUNNER_BUNDLE"],
    )
    return 0


@SubCommand(
    "android",
    "install-geckoview_example-aab",
    """Install geckoview_example with AAB""",
)
@CommandArgument("args", nargs=argparse.REMAINDER)
def android_install_geckoview_example_aab(command_context, args):
    install_app_bundle(
        command_context,
        command_context.substs["GRADLE_ANDROID_GECKOVIEW_EXAMPLE_BUNDLE"],
    )
    return 0


@SubCommand("android", "install-geckoview-test", """Install geckoview.test """)
@CommandArgument("args", nargs=argparse.REMAINDER)
def android_install_geckoview_test(command_context, args):
    gradle(
        command_context,
        command_context.substs["GRADLE_ANDROID_INSTALL_GECKOVIEW_TEST_TASKS"] + args,
        verbose=True,
    )
    return 0


@SubCommand(
    "android",
    "geckoview-docs",
    """Create GeckoView javadoc and optionally upload to Github""",
)
@CommandArgument("--archive", action="store_true", help="Generate a javadoc archive.")
@CommandArgument(
    "--upload",
    metavar="USER/REPO",
    help="Upload geckoview documentation to Github, using the specified USER/REPO.",
)
@CommandArgument(
    "--upload-branch",
    metavar="BRANCH[/PATH]",
    default="gh-pages",
    help="Use the specified branch/path for documentation commits.",
)
@CommandArgument(
    "--javadoc-path",
    metavar="/PATH",
    default="javadoc",
    help="Use the specified path for javadoc commits.",
)
@CommandArgument(
    "--upload-message",
    metavar="MSG",
    default="GeckoView docs upload",
    help="Use the specified message for commits.",
)
def android_geckoview_docs(
    command_context,
    archive,
    upload,
    upload_branch,
    javadoc_path,
    upload_message,
):
    tasks = (
        command_context.substs["GRADLE_ANDROID_GECKOVIEW_DOCS_ARCHIVE_TASKS"]
        if archive or upload
        else command_context.substs["GRADLE_ANDROID_GECKOVIEW_DOCS_TASKS"]
    )

    ret = gradle(command_context, tasks, verbose=True)
    if ret or not upload:
        return ret

    # Upload to Github.
    fmt = {
        "level": os.environ.get("MOZ_SCM_LEVEL", "0"),
        "project": os.environ.get("MH_BRANCH", "unknown"),
        "revision": os.environ.get("GECKO_HEAD_REV", "tip"),
    }
    env = {}

    # In order to push to GitHub from TaskCluster, we store a private key
    # in the TaskCluster secrets store in the format {"content": "<KEY>"},
    # and the corresponding public key as a writable deploy key for the
    # destination repo on GitHub.
    secret = os.environ.get("GECKOVIEW_DOCS_UPLOAD_SECRET", "").format(**fmt)
    if secret:
        # Set up a private key from the secrets store if applicable.
        import requests

        req = requests.get("http://taskcluster/secrets/v1/secret/" + secret)
        req.raise_for_status()

        keyfile = mozpath.abspath("gv-docs-upload-key")
        with open(keyfile, "w") as f:
            os.chmod(keyfile, 0o600)
            f.write(req.json()["secret"]["content"])

        # Turn off strict host key checking so ssh does not complain about
        # unknown github.com host. We're not pushing anything sensitive, so
        # it's okay to not check GitHub's host keys.
        env["GIT_SSH_COMMAND"] = 'ssh -i "%s" -o StrictHostKeyChecking=no' % keyfile

    # Clone remote repo.
    branch = upload_branch.format(**fmt)
    repo_url = "git@github.com:%s.git" % upload
    repo_path = mozpath.abspath("gv-docs-repo")
    command_context.run_process(
        [
            "git",
            "clone",
            "--branch",
            upload_branch,
            "--depth",
            "1",
            repo_url,
            repo_path,
        ],
        append_env=env,
        pass_thru=True,
    )
    env["GIT_DIR"] = mozpath.join(repo_path, ".git")
    env["GIT_WORK_TREE"] = repo_path
    env["GIT_AUTHOR_NAME"] = env["GIT_COMMITTER_NAME"] = "GeckoView Docs Bot"
    env["GIT_AUTHOR_EMAIL"] = env["GIT_COMMITTER_EMAIL"] = "nobody@mozilla.com"

    # Copy over user documentation.
    import mozfile

    # Extract new javadoc to specified directory inside repo.
    src_tar = mozpath.join(
        command_context.topobjdir,
        "gradle",
        "build",
        "mobile",
        "android",
        "geckoview",
        "libs",
        "geckoview-javadoc.jar",
    )
    dst_path = mozpath.join(repo_path, javadoc_path.format(**fmt))
    mozfile.remove(dst_path)
    mozfile.extract_zip(src_tar, dst_path)

    # Commit and push.
    command_context.run_process(["git", "add", "--all"], append_env=env, pass_thru=True)
    if (
        command_context.run_process(
            ["git", "diff", "--cached", "--quiet"],
            append_env=env,
            pass_thru=True,
            ensure_exit_code=False,
        )
        != 0
    ):
        # We have something to commit.
        command_context.run_process(
            ["git", "commit", "--message", upload_message.format(**fmt)],
            append_env=env,
            pass_thru=True,
        )
        command_context.run_process(
            ["git", "push", "origin", branch], append_env=env, pass_thru=True
        )

    mozfile.remove(repo_path)
    if secret:
        mozfile.remove(keyfile)
    return 0


@Command(
    "gradle",
    category="devenv",
    description="Run gradle.",
    conditions=[conditions.is_android],
)
@CommandArgument(
    "-v",
    "--verbose",
    action="store_true",
    help="Verbose output for what commands the build is running.",
)
@CommandArgument("args", nargs=argparse.REMAINDER)
def gradle(command_context, args, verbose=False, gradle_path=None, topsrcdir=None):
    if not verbose:
        # Avoid logging the command
        command_context.log_manager.terminal_handler.setLevel(logging.CRITICAL)

    if not gradle_path:
        gradle_path = command_context.substs["GRADLE"]

    if not topsrcdir:
        topsrcdir = mozpath.join(command_context.topsrcdir)

    # In automation, JAVA_HOME is set via mozconfig, which needs
    # to be specially handled in each mach command. This turns
    # $JAVA_HOME/bin/java into $JAVA_HOME.
    java_home = os.path.dirname(os.path.dirname(command_context.substs["JAVA"]))

    gradle_flags = command_context.substs.get("GRADLE_FLAGS", "") or os.environ.get(
        "GRADLE_FLAGS", ""
    )
    gradle_flags = shell_split(gradle_flags)

    # We force the Gradle JVM to run with the UTF-8 encoding, since we
    # filter strings.xml, which is really UTF-8; the ellipsis character is
    # replaced with ??? in some encodings (including ASCII).  It's not yet
    # possible to filter with encodings in Gradle
    # (https://github.com/gradle/gradle/pull/520) and it's challenging to
    # do our filtering with Gradle's Ant support.  Moreover, all of the
    # Android tools expect UTF-8: see
    # http://tools.android.com/knownissues/encoding.  See
    # http://stackoverflow.com/a/21267635 for discussion of this approach.
    #
    # It's not even enough to set the encoding just for Gradle; it
    # needs to be for JVMs spawned by Gradle as well.  This
    # happens during the maven deployment generating the GeckoView
    # documents; this works around "error: unmappable character
    # for encoding ASCII" in exoplayer2.  See
    # https://discuss.gradle.org/t/unmappable-character-for-encoding-ascii-when-building-a-utf-8-project/10692/11  # NOQA: E501
    # and especially https://stackoverflow.com/a/21755671.

    if command_context.substs.get("MOZ_AUTOMATION"):
        gradle_flags += ["--console=plain"]

    env = os.environ.copy()

    env.update(
        {
            "GRADLE_OPTS": "-Dfile.encoding=utf-8",
            "JAVA_HOME": java_home,
            "JAVA_TOOL_OPTIONS": "-Dfile.encoding=utf-8",
            # Let Gradle get the right Python path on Windows
            "GRADLE_MACH_PYTHON": sys.executable,
        }
    )
    # Set ANDROID_SDK_ROOT if --with-android-sdk was set.
    # See https://bugzilla.mozilla.org/show_bug.cgi?id=1576471
    android_sdk_root = command_context.substs.get("ANDROID_SDK_ROOT", "")
    if android_sdk_root:
        env["ANDROID_HOME"] = android_sdk_root
        env["ANDROID_SDK_ROOT"] = android_sdk_root

    should_print_status = env.get("MACH") and not env.get("NO_BUILDSTATUS_MESSAGES")
    if should_print_status:
        print("BUILDSTATUS " + str(time.time()) + " START_Gradle " + args[0])
    rv = command_context.run_process(
        [gradle_path] + gradle_flags + args,
        explicit_env=env,
        pass_thru=True,  # Allow user to run gradle interactively.
        ensure_exit_code=False,  # Don't throw on non-zero exit code.
        cwd=topsrcdir,
    )
    if should_print_status:
        print("BUILDSTATUS " + str(time.time()) + " END_Gradle " + args[0])
    return rv


@Command("gradle-install", category="devenv", conditions=[REMOVED])
def gradle_install_REMOVED(command_context):
    pass


@Command(
    "android-emulator",
    category="devenv",
    conditions=[],
    description="Run the Android emulator with an AVD from test automation. "
    "Environment variable MOZ_EMULATOR_COMMAND_ARGS, if present, will "
    "over-ride the command line arguments used to launch the emulator.",
)
@CommandArgument(
    "--version",
    metavar="VERSION",
    choices=["arm", "arm64", "x86_64"],
    help="Specify which AVD to run in emulator. "
    'One of "arm" (Android supporting armv7 binaries), '
    '"arm64" (for Apple Silicon), or '
    '"x86_64" (Android supporting x86 or x86_64 binaries, '
    "recommended for most applications). "
    "By default, the value will match the current build environment.",
)
@CommandArgument("--wait", action="store_true", help="Wait for emulator to be closed.")
@CommandArgument("--gpu", help="Over-ride the emulator -gpu argument.")
@CommandArgument(
    "--verbose", action="store_true", help="Log informative status messages."
)
def emulator(
    command_context,
    version,
    wait=False,
    gpu=None,
    verbose=False,
):
    """
    Run the Android emulator with one of the AVDs used in the Mozilla
    automated test environment. If necessary, the AVD is fetched from
    the taskcluster server and installed.
    """
    from mozrunner.devices.android_device import AndroidEmulator

    emulator = AndroidEmulator(
        version,
        verbose,
        substs=command_context.substs,
        device_serial="emulator-5554",
    )
    if emulator.is_running():
        # It is possible to run multiple emulators simultaneously, but:
        #  - if more than one emulator is using the same avd, errors may
        #    occur due to locked resources;
        #  - additional parameters must be specified when running tests,
        #    to select a specific device.
        # To avoid these complications, allow just one emulator at a time.
        command_context.log(
            logging.ERROR,
            "emulator",
            {},
            "An Android emulator is already running.\n"
            "Close the existing emulator and re-run this command.",
        )
        return 1

    if not emulator.check_avd():
        command_context.log(
            logging.WARN,
            "emulator",
            {},
            "AVD not found. Please run |mach bootstrap|.",
        )
        return 2

    if not emulator.is_available():
        command_context.log(
            logging.WARN,
            "emulator",
            {},
            "Emulator binary not found.\n"
            "Install the Android SDK and make sure 'emulator' is in your PATH.",
        )
        return 2

    command_context.log(
        logging.INFO,
        "emulator",
        {},
        "Starting Android emulator running %s..." % emulator.get_avd_description(),
    )
    emulator.start(gpu)
    if emulator.wait_for_start():
        command_context.log(
            logging.INFO, "emulator", {}, "Android emulator is running."
        )
    else:
        # This is unusual but the emulator may still function.
        command_context.log(
            logging.WARN,
            "emulator",
            {},
            "Unable to verify that emulator is running.",
        )

    if conditions.is_android(command_context):
        command_context.log(
            logging.INFO,
            "emulator",
            {},
            "Use 'mach install' to install or update Firefox on your emulator.",
        )
    else:
        command_context.log(
            logging.WARN,
            "emulator",
            {},
            "No Firefox for Android build detected.\n"
            "Switch to a Firefox for Android build context or use 'mach bootstrap'\n"
            "to setup an Android build environment.",
        )

    if wait:
        command_context.log(
            logging.INFO, "emulator", {}, "Waiting for Android emulator to close..."
        )
        rc = emulator.wait()
        if rc is not None:
            command_context.log(
                logging.INFO,
                "emulator",
                {},
                "Android emulator completed with return code %d." % rc,
            )
        else:
            command_context.log(
                logging.WARN,
                "emulator",
                {},
                "Unable to retrieve Android emulator return code.",
            )
    return 0


@SubCommand(
    command="android-emulator",
    subcommand="reset",
    description="Resets the emulator and Android Virtual Device (AVD) by removing the "
    "'ANDROID_AVD_HOME' directory and re-bootstrapping the emulator and AVD.",
)
def emulator_reset(command_context):
    from mozboot import android

    os_arch = platform.machine()
    os_name = None
    if platform.system() == "Windows":
        os_name = "windows"
    elif platform.system() == "Linux":
        os_name = "linux"
    elif platform.system() == "Darwin":
        os_name = "macosx"
    else:
        raise Exception("Can't reset AVD on an unknown system")

    avd_home_path = android.AVD_HOME_PATH

    if avd_home_path.exists():
        command_context.log(
            logging.INFO, "emulator", {}, f"Removing AVD directory: '{avd_home_path}'"
        )
        try:
            shutil.rmtree(avd_home_path)
            command_context.log(
                logging.INFO,
                "emulator",
                {},
                f"Successfully removed AVD directory: '{avd_home_path}'",
            )
        except FileNotFoundError:
            pass  # Directory doesn't exist, do nothing
        except Exception as e:
            command_context.log(
                logging.ERROR,
                "emulator",
                {},
                f"Failed to remove the AVD directory: '{avd_home_path}': {e}",
            )

    sdk_manager_tool_path = android.get_sdkmanager_tool_path(
        android.get_sdk_path(os_name)
    )
    if not sdk_manager_tool_path.exists():
        command_context.log(
            logging.ERROR,
            "emulator",
            {},
            f"Unable to proceed – 'sdkmanager' not found at {sdk_manager_tool_path}. "
            f"Please run './mach bootstrap' to reinstall your Android SDK.",
        )
        return 1

    normalized_arch = os_arch.lower()

    if "x86" in normalized_arch or "amd64" in normalized_arch:
        avd_manifest_path_for_arch = android.AVD_MANIFEST_X86_64
    else:
        avd_manifest_path_for_arch = android.AVD_MANIFEST_ARM64

    command_context.log(
        logging.INFO,
        "emulator",
        {},
        f"Resetting emulator and AVD. AVD_MANIFEST_PATH='{avd_manifest_path_for_arch}'",
    )

    packages = android.get_android_packages(android.AndroidPackageList.EMULATOR)
    avd_manifest = android.get_avd_manifest(avd_manifest_path_for_arch)

    android.ensure_android_packages(
        os_name,
        os_arch,
        packages,
        no_interactive=True,
        avd_manifest=avd_manifest,
    )

    android.ensure_android_avd(
        os_name,
        os_arch,
        no_interactive=True,
        avd_manifest=avd_manifest,
    )


@Command(
    "adb",
    category="devenv",
    description="Run the version of Android Debug Bridge (adb) utility that the build system would use.",
)
@CommandArgument("args", nargs=argparse.REMAINDER)
def adb(
    command_context,
    args,
):
    """Run the version of Android Debug Bridge (adb) utility that the build
    system would use."""
    from mozrunner.devices.android_device import get_adb_path

    adb_path = get_adb_path(command_context)
    if not adb_path:
        command_context.log(
            logging.ERROR,
            "adb",
            {},
            "ADB not found. Did you run `mach bootstrap` with Android selected yet?",
        )
        return 1

    return command_context.run_process(
        [adb_path] + args, pass_thru=True, ensure_exit_code=False
    )
