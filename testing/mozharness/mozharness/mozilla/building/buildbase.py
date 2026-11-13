#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.
"""buildbase.py.

provides a base class for fx desktop builds

"""

import copy
import json
import os
import pathlib
import sys
import time
from datetime import datetime

import yaml
from yaml import YAMLError

from mozharness.base.config import DEFAULT_CONFIG_PATH, BaseConfig, parse_config_file
from mozharness.base.errors import MakefileErrorList
from mozharness.base.log import FATAL
from mozharness.base.python import PerfherderResourceOptionsMixin, VirtualenvMixin
from mozharness.base.script import PostScriptRun
from mozharness.base.vcs.vcsbase import MercurialScript
from mozharness.mozilla.automation import (
    EXIT_STATUS_DICT,
    TBPL_FAILURE,
    TBPL_STATUS_DICT,
    AutomationMixin,
)
from mozharness.mozilla.secrets import SecretsMixin

AUTOMATION_EXIT_CODES = sorted(EXIT_STATUS_DICT.values())

MISSING_CFG_KEY_MSG = "The key '%s' could not be determined \
Please add this to your config."

ERROR_MSGS = {
    "comments_undetermined": '"comments" could not be determined. This may be \
because it was a forced build.',
    "tooltool_manifest_undetermined": '"tooltool_manifest_src" not set, \
Skipping run_tooltool...',
}


class MozconfigPathError(Exception):
    """
    There was an error getting a mozconfig path from a mozharness config.
    """


def get_mozconfig_path(script, config, dirs):
    """
    Get the path to the mozconfig file to use from a mozharness config.

    :param script: The object to interact with the filesystem through.
    :type script: ScriptMixin:

    :param config: The mozharness config to inspect.
    :type config: dict

    :param dirs: The directories specified for this build.
    :type dirs: dict
    """
    COMPOSITE_KEYS = {"mozconfig_variant", "app_name", "mozconfig_platform"}
    have_composite_mozconfig = COMPOSITE_KEYS <= set(config.keys())
    have_partial_composite_mozconfig = len(COMPOSITE_KEYS & set(config.keys())) > 0
    have_src_mozconfig = "src_mozconfig" in config

    # first determine the mozconfig path
    if have_partial_composite_mozconfig and not have_composite_mozconfig:
        raise MozconfigPathError(
            "All or none of 'app_name', 'mozconfig_platform' and `mozconfig_variant' must be "
            "in the config in order to determine the mozconfig."
        )
    elif have_composite_mozconfig and have_src_mozconfig:
        raise MozconfigPathError(
            "'src_mozconfig' or 'mozconfig_variant' must be "
            "in the config but not both in order to determine the mozconfig."
        )
    elif have_composite_mozconfig:
        src_mozconfig = "%(app_name)s/config/mozconfigs/%(platform)s/%(variant)s" % {
            "app_name": config["app_name"],
            "platform": config["mozconfig_platform"],
            "variant": config["mozconfig_variant"],
        }
        abs_mozconfig_path = os.path.join(dirs["abs_src_dir"], src_mozconfig)
    elif have_src_mozconfig:
        abs_mozconfig_path = os.path.join(
            dirs["abs_src_dir"], config.get("src_mozconfig")
        )
    else:
        raise MozconfigPathError(
            "Must provide 'app_name', 'mozconfig_platform' and 'mozconfig_variant'; "
            "or 'src_mozconfig' in the config "
            "in order to determine the mozconfig."
        )

    return abs_mozconfig_path


class BuildingConfig(BaseConfig):
    # TODO add nosetests for this class
    def get_cfgs_from_files(self, all_config_files, options):
        """
        Determine the configuration from the normal options and from
        `--custom-build-variant-cfg`.  If the
        files for any of the latter options are also given with `--config-file`
        or `--opt-config-file`, they are only parsed once.

        The build variant has highest precedence, followed any normally-specified configuration files.
        """
        # override from BaseConfig

        # this is what we will return. It will represent each config
        # file name and its associated dict
        # eg ('builds/branch_specifics.py', {'foo': 'bar'})
        all_config_dicts = []
        # important config files
        variant_cfg_file = ""

        # ### The order from highest precedence to lowest is:
        # # There can only be one of these...
        # 1) build_variant: these could be known like asan and debug
        #                   or a custom config
        #
        # # There can be many of these
        # 2) all other configs: these are any configs that are passed with
        #                       --cfg and --opt-cfg. There order is kept in
        #                       which they were passed on the cmd line. This
        #                       behaviour maintains what happens by default
        #                       in mozharness

        # so, let's first assign the config that holds a known position of
        # importance
        for i, cf in enumerate(all_config_files):
            if cf == options.build_variant:
                variant_cfg_file = cf

        # now remove it from the list
        if variant_cfg_file:
            all_config_files.remove(variant_cfg_file)

        # now let's update config with the remaining config files.
        # this functionality is the same as the base class
        all_config_dicts.extend(super().get_cfgs_from_files(all_config_files, options))

        # stack variant cfg file on top of that, if it is present
        if variant_cfg_file:
            # take the whole config
            all_config_dicts.append((
                variant_cfg_file,
                parse_config_file(variant_cfg_file),
            ))
        return all_config_dicts


# noinspection PyUnusedLocal
class BuildOptionParser:
    # TODO add nosetests for this class
    platform = None
    bits = None

    # add to this list and you can automagically do things like
    # --custom-build-variant-cfg asan
    # and the script will pull up the appropriate path for the config
    # against the current platform and bits.
    # *It will warn and fail if there is not a config for the current
    # platform/bits
    path_base = "builds/releng_sub_%s_configs/"
    build_variants = {
        "add-on-devel": path_base + "%s_add-on-devel.py",
        "asan-tc": path_base + "%s_asan_tc.py",
        "fuzzing-asan-tc": path_base + "%s_fuzzing_asan_tc.py",
        "tsan-tc": path_base + "%s_tsan_tc.py",
        "fuzzing-tsan-tc": path_base + "%s_fuzzing_tsan_tc.py",
        "cross-debug": path_base + "%s_cross_debug.py",
        "cross-debug-searchfox": path_base + "%s_cross_debug_searchfox.py",
        "cross-opt-searchfox": path_base + "%s_cross_opt_searchfox.py",
        "cross-noopt-debug": path_base + "%s_cross_noopt_debug.py",
        "cross-fuzzing-asan": path_base + "%s_cross_fuzzing_asan.py",
        "cross-fuzzing-debug": path_base + "%s_cross_fuzzing_debug.py",
        "debug": path_base + "%s_debug.py",
        "fuzzing-debug": path_base + "%s_fuzzing_debug.py",
        "asan-tc-and-debug": path_base + "%s_asan_tc_and_debug.py",
        "code-coverage-debug": path_base + "%s_code_coverage_debug.py",
        "code-coverage-opt": path_base + "%s_code_coverage_opt.py",
        "source": path_base + "%s_source.py",
        "noopt-debug": path_base + "%s_noopt_debug.py",
        "arm": path_base + "%s_arm.py",
        "arm-lite": path_base + "%s_arm_lite.py",
        "arm-debug": path_base + "%s_arm_debug.py",
        "arm-profile-generate": path_base + "%s_arm_profile_generate.py",
        "rusttests": path_base + "%s_rusttests.py",
        "rusttests-debug": path_base + "%s_rusttests_debug.py",
        "x86": path_base + "%s_x86.py",
        "x86-lite": path_base + "%s_x86_lite.py",
        "x86-profile-generate": path_base + "%s_x86_profile_generate.py",
        "x86_64": path_base + "%s_x86_64.py",
        "x86_64-ccov": path_base + "%s_x86_64_ccov.py",
        "x86_64-lite": path_base + "%s_x86_64_lite.py",
        "x86_64-debug": path_base + "%s_x86_64_debug.py",
        "x86_64-profile-generate": path_base + "%s_x86_64_profile_generate.py",
        "aarch64": path_base + "%s_aarch64.py",
        "aarch64-lite": path_base + "%s_aarch64_lite.py",
        "aarch64-debug": path_base + "%s_aarch64_debug.py",
        "aarch64-fenix-debug": path_base + "%s_aarch64_fenix_debug.py",
        "aarch64-debug-searchfox": path_base + "%s_aarch64_debug_searchfox.py",
        "aarch64-profile-generate": path_base + "%s_aarch64_profile_generate.py",
        "android-geckoview-docs": path_base + "%s_geckoview_docs.py",
        "valgrind": path_base + "%s_valgrind.py",
    }

    @classmethod
    def _query_pltfrm_and_bits(cls, target_option, options):
        """determine platform and bits

        This can be from either from a supplied --platform and --bits
        or parsed from given config file names.
        """
        error_msg = (
            "Whoops!\nYou are trying to pass a shortname for "
            "%s. \nHowever, I need to know the %s to find the appropriate "
            'filename. You can tell me by passing:\n\t"%s" or a config '
            'filename via "--config" with %s in it. \nIn either case, these '
            "option arguments must come before --custom-build-variant."
        )
        current_config_files = options.config_files or []
        if not cls.bits:
            # --bits has not been supplied
            # lets parse given config file names for 32 or 64
            for cfg_file_name in current_config_files:
                if "32" in cfg_file_name:
                    cls.bits = "32"
                    break
                if "64" in cfg_file_name:
                    cls.bits = "64"
                    break
            else:
                sys.exit(error_msg % (target_option, "bits", "--bits", '"32" or "64"'))

        if not cls.platform:
            # --platform has not been supplied
            # lets parse given config file names for platform
            for cfg_file_name in current_config_files:
                if "windows" in cfg_file_name:
                    cls.platform = "windows"
                    break
                if "mac" in cfg_file_name:
                    cls.platform = "mac"
                    break
                if "linux" in cfg_file_name:
                    cls.platform = "linux"
                    break
                if "android" in cfg_file_name:
                    cls.platform = "android"
                    break
            else:
                sys.exit(
                    error_msg
                    % (
                        target_option,
                        "platform",
                        "--platform",
                        '"linux", "windows", "mac", or "android"',
                    )
                )
        return cls.bits, cls.platform

    @classmethod
    def find_variant_cfg_path(cls, opt, value, parser):
        valid_variant_cfg_path = None
        # first let's see if we were given a valid short-name
        if cls.build_variants.get(value):
            bits, pltfrm = cls._query_pltfrm_and_bits(opt, parser.values)
            prospective_cfg_path = cls.build_variants[value] % (pltfrm, bits)
        else:
            # this is either an incomplete path or an invalid key in
            # build_variants
            prospective_cfg_path = value

        if os.path.exists(prospective_cfg_path):
            # now let's see if we were given a valid pathname
            valid_variant_cfg_path = value
        else:
            # FIXME: We should actually wait until we have parsed all arguments
            # before looking at this, otherwise the behavior will depend on the
            # order of arguments. But that isn't a problem as long as --extra-config-path
            # is always passed first.
            extra_config_paths = parser.values.config_paths or []
            config_paths = extra_config_paths + [DEFAULT_CONFIG_PATH]
            # let's take our prospective_cfg_path and see if we can
            # determine an existing file
            for path in config_paths:
                if os.path.exists(os.path.join(path, prospective_cfg_path)):
                    # success! we found a config file
                    valid_variant_cfg_path = os.path.join(path, prospective_cfg_path)
                    break
        return valid_variant_cfg_path, prospective_cfg_path

    @classmethod
    def set_build_variant(cls, option, opt, value, parser):
        """sets an extra config file.

        This is done by either taking an existing filepath or by taking a valid
        shortname coupled with known platform/bits.
        """
        valid_variant_cfg_path, prospective_cfg_path = cls.find_variant_cfg_path(
            "--custom-build-variant-cfg", value, parser
        )

        if not valid_variant_cfg_path:
            # either the value was an indeterminable path or an invalid short
            # name
            sys.exit(
                "Whoops!\n'--custom-build-variant' was passed but an "
                "appropriate config file could not be determined. Tried "
                "using: '%s' but it was not:"
                "\n\t-- a valid shortname: %s "
                "\n\t-- a valid variant for the given platform and bits."
                % (prospective_cfg_path, str(list(cls.build_variants.keys())))
            )
        parser.values.config_files.append(valid_variant_cfg_path)
        setattr(parser.values, option.dest, value)  # the variant

    @classmethod
    def set_build_branch(cls, option, opt, value, parser):
        # Store the branch name we are using
        setattr(parser.values, option.dest, value)  # the branch name

    @classmethod
    def set_platform(cls, option, opt, value, parser):
        cls.platform = value
        setattr(parser.values, option.dest, value)

    @classmethod
    def set_bits(cls, option, opt, value, parser):
        cls.bits = value
        setattr(parser.values, option.dest, value)


# this global depends on BuildOptionParser and therefore can not go at the
# top of the file
BUILD_BASE_CONFIG_OPTIONS = [
    [
        ["--developer-run"],
        {
            "action": "store_false",
            "dest": "is_automation",
            "default": True,
            "help": "If this is running outside of Mozilla's build"
            "infrastructure, use this option. It ignores actions"
            "that are not needed and adds config checks.",
        },
    ],
    [
        ["--platform"],
        {
            "action": "callback",
            "callback": BuildOptionParser.set_platform,
            "type": "string",
            "dest": "platform",
            "help": "Sets the platform we are running this against"
            " valid values: 'windows', 'mac', 'linux'",
        },
    ],
    [
        ["--bits"],
        {
            "action": "callback",
            "callback": BuildOptionParser.set_bits,
            "type": "string",
            "dest": "bits",
            "help": "Sets which bits we are building this against"
            " valid values: '32', '64'",
        },
    ],
    [
        ["--custom-build-variant-cfg"],
        {
            "action": "callback",
            "callback": BuildOptionParser.set_build_variant,
            "type": "string",
            "dest": "build_variant",
            "help": "Sets the build type and will determine appropriate"
            " additional config to use. Either pass a config path"
            " or use a valid shortname from: "
            "%s" % (list(BuildOptionParser.build_variants.keys()),),
        },
    ],
    [
        ["--branch"],
        {
            "action": "callback",
            "callback": BuildOptionParser.set_build_branch,
            "type": "string",
            "dest": "branch",
            "help": "This sets the branch we will be building this for.",
        },
    ],
    [
        ["--enable-nightly"],
        {
            "action": "store_true",
            "dest": "nightly_build",
            "default": False,
            "help": "Sets the build to run in nightly mode",
        },
    ],
    [
        ["--who"],
        {
            "dest": "who",
            "default": "",
            "help": "stores who made the created the change.",
        },
    ],
]


class BuildScript(
    AutomationMixin,
    VirtualenvMixin,
    MercurialScript,
    SecretsMixin,
    PerfherderResourceOptionsMixin,
):
    def __init__(self, **kwargs):
        # objdir is referenced in _query_abs_dirs() so let's make sure we
        # have that attribute before calling BaseScript.__init__
        self.objdir = None
        super().__init__(**kwargs)
        # epoch is only here to represent the start of the build
        # that this mozharn script came from. until I can grab bbot's
        # status.build.gettime()[0] this will have to do as a rough estimate
        # although it is about 4s off from the time it would be if it was
        # done through MBF.
        # TODO find out if that time diff matters or if we just use it to
        # separate each build
        self.epoch_timestamp = int(time.mktime(datetime.now().timetuple()))
        self.branch = self.config.get("branch")
        if not self.branch:
            self.error("'branch' not determined and is required")
            self.fatal("Please add missing items to your config")
        self.client_id = None
        self.access_token = None

        # Call this before creating the virtualenv so that we can support
        # substituting config values with other config values.
        self.query_build_env()

        # We need to create the virtualenv directly (without using an action) in
        # order to use python modules in PreScriptRun/Action listeners
        self.create_virtualenv()

    def _pre_config_lock(self, rw_config):
        c = self.config
        cfg_files_and_dicts = rw_config.all_cfg_files_and_dicts
        build_variant = c.get("build_variant", "")
        variant_cfg = ""
        if build_variant:
            variant_cfg = BuildOptionParser.build_variants[build_variant] % (
                BuildOptionParser.platform,
                BuildOptionParser.bits,
            )

        cfg_match_msg = "Script was run with '%(option)s %(type)s' and \
'%(type)s' matches a key in '%(type_config_file)s'. Updating self.config with \
items from that key's value."

        for i, (target_file, target_dict) in enumerate(cfg_files_and_dicts):
            if variant_cfg and variant_cfg in target_file:
                self.info(
                    cfg_match_msg
                    % {
                        "option": "--custom-build-variant-cfg",
                        "type": build_variant,
                        "type_config_file": variant_cfg,
                    }
                )
        self.info(
            "To generate a config file based upon options passed and "
            "config files used, run script as before but extend options "
            'with "--dump-config"'
        )
        self.info(
            "For a diff of where self.config got its items, "
            "run the script again as before but extend options with: "
            '"--dump-config-hierarchy"'
        )
        self.info(
            "Both --dump-config and --dump-config-hierarchy don't "
            "actually run any actions."
        )

    def _query_objdir(self):
        if self.objdir:
            return self.objdir

        if not self.config.get("objdir"):
            return self.fatal(MISSING_CFG_KEY_MSG % ("objdir",))
        self.objdir = self.config["objdir"]
        return self.objdir

    def query_build_env(self, **kwargs):
        c = self.config

        # let's evoke the base query_env and make a copy of it
        # as we don't always want every key below added to the same dict
        env = copy.deepcopy(super().query_env(**kwargs))

        if self.query_is_nightly():
            # taskcluster sets the update channel for shipping builds
            # explicitly
            if c.get("update_channel"):
                update_channel = c["update_channel"]
                env["MOZ_UPDATE_CHANNEL"] = update_channel
            else:  # let's just give the generic channel based on branch
                env["MOZ_UPDATE_CHANNEL"] = "nightly-%s" % (self.branch,)
            self.info("Update channel set to: {}".format(env["MOZ_UPDATE_CHANNEL"]))

        return env

    def query_mach_build_env(self, multiLocale=None):
        c = self.config
        if multiLocale is None and self.query_is_nightly():
            multiLocale = c.get("multi_locale", False)
        mach_env = {}
        if c.get("upload_env"):
            mach_env.update(c["upload_env"])

        # this prevents taskcluster from overwriting the target files with
        # the multilocale files. Put everything from the en-US build in a
        # separate folder.
        if multiLocale and self.config.get("taskcluster_nightly"):
            if "UPLOAD_PATH" in mach_env:
                mach_env["UPLOAD_PATH"] = os.path.join(mach_env["UPLOAD_PATH"], "en-US")
        return mach_env

    def _get_mozconfig(self):
        """assign mozconfig."""
        dirs = self.query_abs_dirs()

        src_mozconfig = os.environ.get("MOZCONFIG")
        if not src_mozconfig:
            self.fatal("MOZCONFIG is not set in the environment")
        abs_mozconfig_path = os.path.join(dirs["abs_src_dir"], src_mozconfig)

        self.info(f"Use mozconfig: {abs_mozconfig_path}")

        # print its contents
        self.info("mozconfig content:")
        self.info(self.read_from_file(abs_mozconfig_path, error_level=FATAL))

    def _run_tooltool(self):
        env = self.query_build_env()
        env.update(self.query_mach_build_env())

        c = self.config
        dirs = self.query_abs_dirs()
        manifest_src = os.environ.get("TOOLTOOL_MANIFEST")
        if not manifest_src:
            manifest_src = c.get("tooltool_manifest_src")
        if not manifest_src:
            return self.warning(ERROR_MSGS["tooltool_manifest_undetermined"])
        cmd = [
            sys.executable,
            "-u",
            os.path.join(dirs["abs_src_dir"], "mach"),
            "artifact",
            "toolchain",
            "-v",
            "--retry",
            "4",
            "--artifact-manifest",
            os.path.join(dirs["abs_src_dir"], "toolchains.json"),
        ]
        if manifest_src:
            cmd.extend([
                "--tooltool-manifest",
                os.path.join(dirs["abs_src_dir"], manifest_src),
            ])
        cache = c["env"].get("TOOLTOOL_CACHE")
        if cache:
            cmd.extend(["--cache-dir", cache])
        self.info(str(cmd))
        self.run_command(cmd, cwd=dirs["abs_src_dir"], halt_on_failure=True, env=env)

    def _create_mozbuild_dir(self, mozbuild_path=None):
        if not mozbuild_path:
            env = self.query_build_env()
            mozbuild_path = env.get("MOZBUILD_STATE_PATH")
        if mozbuild_path:
            self.mkdir_p(mozbuild_path)
        else:
            self.warning("mozbuild_path could not be determined. skipping creating it.")

    def preflight_build(self):
        """set up machine state for a complete build."""
        self._get_mozconfig()
        self._run_tooltool()
        self._create_mozbuild_dir()
        self._ensure_upload_path()

    def build(self):
        """builds application."""

        args = ["build", "-v"]

        # This will error on non-0 exit code.
        self._run_mach_command_in_build_env(args)

        self._generate_build_stats()

    def _query_mach(self):
        return [sys.executable, "mach"]

    def _run_mach_command_in_build_env(self, args):
        """Run a mach command in a build context."""
        env = self.query_build_env()
        env.update(self.query_mach_build_env())

        dirs = self.query_abs_dirs()

        mach = self._query_mach()

        return_code = self.run_command(
            command=mach + ["--log-no-times"] + args,
            cwd=dirs["abs_src_dir"],
            env=env,
            error_list=MakefileErrorList,
            output_timeout=self.config.get("max_build_output_timeout", 60 * 40),
        )

        if return_code:
            self.return_code = self.worst_level(
                EXIT_STATUS_DICT[TBPL_FAILURE],
                self.return_code,
                AUTOMATION_EXIT_CODES[::-1],
            )
            self.fatal(
                "'mach %s' did not run successfully. Please check "
                "log for errors." % " ".join(args)
            )

    def multi_l10n(self):
        if not self.query_is_nightly():
            self.info("Not a nightly build, skipping multi l10n.")
            return

        dirs = self.query_abs_dirs()
        base_work_dir = dirs["base_work_dir"]
        work_dir = dirs["abs_work_dir"]
        branch = self.branch

        # Building a nightly with the try repository fails because a
        # config-file does not exist for try. Default to mozilla-central
        # settings (arbitrarily).
        if branch == "try":
            branch = "mozilla-central"

        multil10n_path = os.path.join(
            dirs["abs_src_dir"],
            "testing/mozharness/scripts/multil10n.py",
        )

        cmd = [
            sys.executable,
            multil10n_path,
            "--work-dir",
            work_dir,
            "--config-file",
            "multi_locale/android-mozharness-build.json",
            "--config-file",
            "multi_locale/tc_common.py",
            "--pull-locale-source",
            "--package-multi",
            "--summary",
        ]

        self.run_command(
            cmd, env=self.query_build_env(), cwd=base_work_dir, halt_on_failure=True
        )

        upload_cmd = self._query_mach() + [
            "--log-no-times",
            "package-multi-locale",
            "upload",
        ]
        env = self.query_build_env()
        env.update(self.query_mach_build_env(multiLocale=False))
        self.run_command(
            upload_cmd,
            env=env,
            cwd=dirs["abs_src_dir"],
            halt_on_failure=True,
        )

    def postflight_build(self):
        """grabs properties from post build and calls ccache -s"""
        # A list of argument lists.  Better names gratefully accepted!
        mach_commands = self.config.get("postflight_build_mach_commands", [])
        for mach_command in mach_commands:
            self._execute_postflight_build_mach_command(mach_command)

    def _execute_postflight_build_mach_command(self, mach_command_args):
        env = self.query_build_env()
        env.update(self.query_mach_build_env())

        command = [sys.executable, "mach", "--log-no-times"]
        command.extend(mach_command_args)

        self.run_command(
            command=command,
            cwd=self.query_abs_dirs()["abs_src_dir"],
            env=env,
            output_timeout=self.config.get("max_build_output_timeout", 60 * 20),
            halt_on_failure=True,
        )

    def preflight_package_source(self):
        self._get_mozconfig()

    def package_source(self):
        """generates source archives and uploads them"""
        env = self.query_build_env()
        env.update(self.query_mach_build_env())
        dirs = self.query_abs_dirs()

        self.run_command(
            command=[sys.executable, "mach", "--log-no-times", "configure"],
            cwd=dirs["abs_src_dir"],
            env=env,
            output_timeout=60 * 3,
            halt_on_failure=True,
        )
        self.run_command(
            command=[
                sys.executable,
                "mach",
                "source-package",
                "--output=source.tar.xz",
                f"--upload={env['UPLOAD_PATH']}",
            ],
            cwd=dirs["abs_src_dir"],
            env=env,
            output_timeout=60 * 45,
            halt_on_failure=True,
        )

    def _is_configuration_shipped(self):
        """Determine if the current build configuration is shipped to users.

        This is used to drive alerting so we don't see alerts for build
        configurations we care less about.
        """
        # Ideally this would be driven by a config option. However, our
        # current inheritance mechanism of using a base config and then
        # one-off configs for variants isn't conducive to this since derived
        # configs we need to be reset and we don't like requiring boilerplate
        # in derived configs.

        # Debug builds are never shipped.
        if self.config.get("debug_build"):
            return False

        # shippable builds set nightly_build
        if self.query_is_nightly():
            return True

        # Android opt builds without a variant are shipped.
        if self.config.get("platform") == "android":
            if not self.config.get("build_variant"):
                return True

        return False

    def _load_build_times(self):
        dirs = self.query_abs_dirs()
        p = os.path.join(dirs["abs_obj_dir"], ".mozbuild", "build_times.json")
        if os.path.exists(p):
            with open(p) as fh:
                build_times = json.load(fh)
            return build_times["duration"], build_times["phases"]

        p = self.config.get("profile_build_resources_path") % dirs
        if not os.path.exists(p):
            self.info("%s does not exist; not loading build profile data" % p)
            return None

        with open(p) as fh:
            profile = json.load(fh)

        thread = profile.get("threads", [])[0]
        duration = profile["meta"]["profilingEndTime"] / 1000
        markers = thread["markers"]
        phases = {}
        for n, marker in enumerate(markers["data"]):
            if marker.get("type") == "Phase":
                phases[marker["phase"]] = (
                    markers["endTime"][n] - markers["startTime"][n]
                ) / 1000
        return duration, phases

    def _load_build_resources(self):
        try:
            build_times = self._load_build_times()
        except Exception:
            self.info("build profile lacks data; ignoring")
            return None
        if build_times is None:
            return None
        duration, phases = build_times

        # We want to always collect metrics. But alerts with sccache enabled
        # we should disable automatic alerting
        should_alert = False if os.environ.get("USE_SCCACHE") == "1" else True

        data = {
            "name": "build times",
            "value": duration,
            "extraOptions": self.perfherder_resource_options(),
            "shouldAlert": should_alert,
            "alertNotifyEmails": ["ahochheiden@mozilla.com"],
            "subtests": [],
        }

        for name, duration in phases.items():
            data["subtests"].append({
                "name": name,
                "value": duration,
            })

        return data

    def _load_sccache_stats(self):
        stats_file = os.path.join(
            self.query_abs_dirs()["base_work_dir"], "artifacts", "sccache-stats.json"
        )
        if not os.path.exists(stats_file):
            msg = "%s does not exist; not loading sccache stats" % stats_file
            if (
                os.environ.get("USE_SCCACHE") == "1"
                and not os.environ.get("SCCACHE_DISABLE") == "1"
            ):
                # We know we use sccache but we didn't find it.
                # Fails to make sure the dev knows it
                self.fatal(msg)
            else:
                self.info(msg)
                return

        with open(stats_file) as fh:
            stats = json.load(fh)

        def get_stat(key):
            val = stats["stats"][key]
            # Future versions of sccache will distinguish stats by language
            # and store them as a dict.
            if isinstance(val, dict):
                val = sum(val["counts"].values())
            return val

        def hit_rate(hits, total):
            if hits == total:
                return 100
            rate = hits * 100 / total
            # If the rate is not quite 100%, avoid values that will round to 100%.
            if rate > 99.99:
                return 99.99
            return rate

        total = get_stat("requests_executed")
        hits = get_stat("cache_hits")
        errors = get_stat("cache_errors")

        yield {
            "name": "sccache percent hit",
            "value": hit_rate(hits, total),
            "subtests": [],
            "alertThreshold": 50.0,
            "lowerIsBetter": False,
            # We want to always collect metrics.
            # But disable automatic alerting on it
            "shouldAlert": False,
        }

        if isinstance(stats["stats"]["cache_hits"]["counts"], dict):
            for lang, value in stats["stats"]["cache_hits"]["counts"].items():
                yield {
                    "name": f"sccache percent hit {lang}",
                    "value": hit_rate(
                        value,
                        (value + stats["stats"]["cache_misses"]["counts"].get(lang, 0)),
                    ),
                    "subtests": [],
                    "alertThreshold": 50.0,
                    "lowerIsBetter": False,
                    # We want to always collect metrics.
                    # But disable automatic alerting on it
                    "shouldAlert": False,
                }

        yield {
            "name": "sccache cache_read_errors",
            "value": stats["stats"]["cache_read_errors"],
            "alertThreshold": 50.0,
            "subtests": [],
        }

        yield {
            "name": "sccache cache_write_errors",
            "value": stats["stats"]["cache_write_errors"],
            "alertThreshold": 50.0,
            "subtests": [],
        }

        yield {
            "name": "sccache cache_errors",
            "value": errors,
            "alertThreshold": 50.0,
            "subtests": [],
        }

        yield {
            "name": "sccache requests_not_cacheable",
            "value": stats["stats"]["requests_not_cacheable"],
            "alertThreshold": 50.0,
            "subtests": [],
        }

    def _get_package_metrics(self):
        import tarfile
        import zipfile

        dirs = self.query_abs_dirs()

        dist_dir = os.path.join(dirs["abs_obj_dir"], "dist")
        for ext in ["apk", "dmg", "tar.bz2", "zip", "tar.xz"]:
            name = "target." + ext
            if os.path.exists(os.path.join(dist_dir, name)):
                packageName = name
                break
        else:
            self.fatal("could not determine packageName")

        interests = ["libxul.so", "classes.dex", "omni.ja", "xul.dll"]
        installer = os.path.join(dist_dir, packageName)
        installer_size = 0
        size_measurements = []

        def paths_with_sizes(installer):
            if zipfile.is_zipfile(installer):
                with zipfile.ZipFile(installer, "r") as zf:
                    for zi in zf.infolist():
                        yield zi.filename, zi.file_size
            elif tarfile.is_tarfile(installer):
                with tarfile.open(installer, "r:*") as tf:
                    for ti in tf:
                        yield ti.name, ti.size

        if os.path.exists(installer):
            installer_size = self.query_filesize(installer)
            self.info("Size of %s: %s bytes" % (packageName, installer_size))
            try:
                subtests = {}
                for path, size in paths_with_sizes(installer):
                    name = os.path.basename(path)
                    if name in interests:
                        # We have to be careful here: desktop Firefox installers
                        # contain two omni.ja files: one for the general runtime,
                        # and one for the browser proper.
                        if name == "omni.ja":
                            containing_dir = os.path.basename(os.path.dirname(path))
                            if containing_dir == "browser":
                                name = "browser-omni.ja"
                        if name in subtests:
                            self.fatal(
                                "should not see %s (%s) multiple times!" % (name, path)
                            )
                        subtests[name] = size
                for name in subtests:
                    self.info("Size of %s: %s bytes" % (name, subtests[name]))
                    size_measurements.append({"name": name, "value": subtests[name]})
            except Exception:
                self.info("Unable to search %s for component sizes." % installer)
                size_measurements = []

        if not installer_size and not size_measurements:
            return

        # We want to always collect metrics. But alerts for installer size are
        # only use for builds with ship. So nix the alerts for builds we don't
        # ship.
        def filter_alert(alert):
            if not self._is_configuration_shipped():
                alert["shouldAlert"] = False

            return alert

        if installer.endswith(".apk"):  # Android
            yield filter_alert({
                "name": "installer size",
                "value": installer_size,
                "alertChangeType": "absolute",
                "alertThreshold": (200 * 1024),
                "subtests": size_measurements,
            })
        else:
            yield filter_alert({
                "name": "installer size",
                "value": installer_size,
                "alertChangeType": "absolute",
                "alertThreshold": (100 * 1024),
                "subtests": size_measurements,
            })

    def _get_sections(self, file, filter=None):
        """
        Returns a dictionary of sections and their sizes.
        """
        # Check for `rust_size`, our cross platform version of size. It should
        # be fetched by run-task in $MOZ_FETCHES_DIR/rust-size/rust-size
        rust_size = os.path.join(
            os.environ["MOZ_FETCHES_DIR"], "rust-size", "rust-size"
        )
        size_prog = self.which(rust_size)
        if not size_prog:
            self.info("Couldn't find `rust-size` program")
            return {}

        self.info("Using %s" % size_prog)
        cmd = [size_prog, file]
        output = self.get_output_from_command(cmd)
        if not output:
            self.info("`rust-size` failed")
            return {}

        # Format is JSON:
        # {
        #   "section_type": {
        #     "section_name": size, ....
        #   },
        #   ...
        # }
        try:
            parsed = json.loads(output)
        except ValueError:
            self.info("`rust-size` failed: %s" % output)
            return {}

        sections = {}
        for sec_type in list(parsed.values()):
            for name, size in list(sec_type.items()):
                if not filter or name in filter:
                    sections[name] = size

        return sections

    def _get_binary_metrics(self):
        """
        Provides metrics on interesting compenents of the built binaries.
        Currently just the sizes of interesting sections.
        """
        lib_interests = {
            "XUL": ("libxul.so", "xul.dll", "XUL"),
            "NSS": ("libnss3.so", "nss3.dll", "libnss3.dylib"),
            "NSPR": ("libnspr4.so", "nspr4.dll", "libnspr4.dylib"),
            "avcodec": ("libmozavcodec.so", "mozavcodec.dll", "libmozavcodec.dylib"),
            "avutil": ("libmozavutil.so", "mozavutil.dll", "libmozavutil.dylib"),
        }
        section_interests = (
            ".text",
            ".data",
            ".rodata",
            ".rdata",
            ".cstring",
            ".data.rel.ro",
            ".bss",
        )
        lib_details = []

        dirs = self.query_abs_dirs()
        dist_dir = os.path.join(dirs["abs_obj_dir"], "dist")
        bin_dir = os.path.join(dist_dir, "bin")

        for lib_type, lib_names in list(lib_interests.items()):
            for lib_name in lib_names:
                lib = os.path.join(bin_dir, lib_name)
                if os.path.exists(lib):
                    lib_size = 0
                    section_details = self._get_sections(lib, section_interests)
                    section_measurements = []
                    # Build up the subtests

                    # Lump rodata sections together
                    # - Mach-O separates out read-only string data as .cstring
                    # - PE really uses .rdata, but XUL at least has a .rodata as well
                    for ro_alias in (".cstring", ".rdata"):
                        if ro_alias in section_details:
                            if ".rodata" in section_details:
                                section_details[".rodata"] += section_details[ro_alias]
                            else:
                                section_details[".rodata"] = section_details[ro_alias]
                            del section_details[ro_alias]

                    for k, v in list(section_details.items()):
                        section_measurements.append({"name": k, "value": v})
                        lib_size += v
                    lib_details.append({
                        "name": lib_type,
                        "size": lib_size,
                        "sections": section_measurements,
                    })

        for lib_detail in lib_details:
            yield {
                "name": "%s section sizes" % lib_detail["name"],
                "value": lib_detail["size"],
                "shouldAlert": False,
                "subtests": lib_detail["sections"],
            }

    def _generate_build_stats(self):
        """grab build stats following a compile.

        This action handles all statistics from a build: 'count_ctors'
        and then posts to graph server the results.
        We only post to graph server for non nightly build
        """
        self.info("Collecting build metrics")

        if os.environ.get("USE_ARTIFACT"):
            self.info("Skipping due to forced artifact build.")
            return

        c = self.config

        # Report some important file sizes for display in treeherder

        perfherder_data = {
            "framework": {"name": "build_metrics"},
            "suites": [],
        }

        if not c.get("debug_build") and not c.get("disable_package_metrics"):
            perfherder_data["suites"].extend(self._get_package_metrics())
            perfherder_data["suites"].extend(self._get_binary_metrics())

        # Extract compiler warnings count.
        warnings = self.get_output_from_command(
            command=[sys.executable, "mach", "warnings-list"],
            cwd=self.query_abs_dirs()["abs_src_dir"],
            env=self.query_build_env(),
            # No need to pollute the log.
            silent=True,
            # Fail fast.
            halt_on_failure=True,
        )

        if warnings is not None:
            perfherder_data["suites"].append({
                "name": "compiler warnings",
                "value": len(warnings.strip().splitlines()),
                "alertThreshold": 100.0,
                "subtests": [],
            })

        build_metrics = self._load_build_resources()
        if build_metrics:
            perfherder_data["suites"].append(build_metrics)
        perfherder_data["suites"].extend(self._load_sccache_stats())

        # Ensure all extra options for this configuration are present.
        for opt in os.environ.get("PERFHERDER_EXTRA_OPTIONS", "").split():
            for suite in perfherder_data["suites"]:
                if opt not in suite.get("extraOptions", []):
                    suite.setdefault("extraOptions", []).append(opt)

        if self.query_is_nightly():
            for suite in perfherder_data["suites"]:
                suite.setdefault("extraOptions", []).insert(0, "nightly")

        if perfherder_data["suites"]:
            self.info("PERFHERDER_DATA: %s" % json.dumps(perfherder_data))
            if "MOZ_AUTOMATION" in os.environ:
                upload_dir = pathlib.Path(os.environ.get("UPLOAD_DIR"))
                upload_dir.mkdir(parents=True, exist_ok=True)
                upload_path = upload_dir / "perfherder-data-building.json"
                with upload_path.open("w", encoding="utf-8") as f:
                    json.dump(perfherder_data, f)

    def valgrind_test(self):
        """Execute mach's valgrind-test for memory leaks"""
        env = self.query_build_env()
        env.update(self.query_mach_build_env())

        return_code = self.run_command(
            command=[sys.executable, "mach", "valgrind-test"],
            cwd=self.query_abs_dirs()["abs_src_dir"],
            env=env,
            output_timeout=self.config.get("max_build_output_timeout", 60 * 40),
        )
        if return_code:
            self.return_code = self.worst_level(
                EXIT_STATUS_DICT[TBPL_FAILURE],
                self.return_code,
                AUTOMATION_EXIT_CODES[::-1],
            )
            self.fatal(
                "'mach valgrind-test' did not run successfully. Please check "
                "log for errors."
            )

    def _ensure_upload_path(self):
        env = self.query_mach_build_env()

        # Some Taskcluster workers don't like it if an artifacts directory
        # is defined but no artifacts are uploaded. Guard against this by always
        # ensuring the artifacts directory exists.
        if "UPLOAD_PATH" in env and not os.path.exists(env["UPLOAD_PATH"]):
            self.mkdir_p(env["UPLOAD_PATH"])

    def _post_fatal(self, message=None, exit_code=None):
        if not self.return_code:  # only overwrite return_code if it's 0
            self.error("setting return code to 2 because fatal was called")
            self.return_code = 2

    @PostScriptRun
    def _summarize(self):
        """If this is run in automation, ensure the return code is valid and
        set it to one if it's not. Finally, log any summaries we collected
        from the script run.
        """
        if self.config.get("is_automation"):
            # let's ignore all mention of tbpl status until this
            # point so it will be easier to manage
            if self.return_code not in AUTOMATION_EXIT_CODES:
                self.error(
                    "Return code is set to: %s and is outside of "
                    "automation's known values. Setting to 2(failure). "
                    "Valid return codes %s" % (self.return_code, AUTOMATION_EXIT_CODES)
                )
                self.return_code = 2
            for status, return_code in list(EXIT_STATUS_DICT.items()):
                if return_code == self.return_code:
                    self.record_status(status, TBPL_STATUS_DICT[status])
        self.summary()

    @PostScriptRun
    def _parse_build_tests_ccov(self):
        if "MOZ_FETCHES_DIR" not in os.environ:
            return

        dirs = self.query_abs_dirs()
        topsrcdir = dirs["abs_src_dir"]
        base_work_dir = dirs["base_work_dir"]

        env = self.query_build_env()

        grcov_path = os.path.join(os.environ["MOZ_FETCHES_DIR"], "grcov", "grcov")
        if not os.path.isabs(grcov_path):
            grcov_path = os.path.join(base_work_dir, grcov_path)
        if self._is_windows():
            grcov_path += ".exe"
        env["GRCOV_PATH"] = grcov_path

        cmd = self._query_mach() + [
            "python",
            os.path.join("testing", "parse_build_tests_ccov.py"),
        ]
        self.run_command(command=cmd, cwd=topsrcdir, env=env, halt_on_failure=True)

    @PostScriptRun
    def _relocate_artifacts(self):
        """Move certain artifacts out of the default upload directory.

        These artifacts will be moved to a secondary directory called `cidata`.
        Then they will be uploaded with different expiration values."""
        dirs = self.query_abs_dirs()
        topsrcdir = dirs["abs_src_dir"]
        base_work_dir = dirs["base_work_dir"]

        build_platform = os.environ.get("MOZ_ARTIFACT_PLATFORM")
        if build_platform is not None:
            build_platform = build_platform.lower()
        else:
            return
        try:
            upload_dir = os.environ["UPLOAD_DIR"]
        except KeyError:
            self.fatal("The env. var. UPLOAD_DIR is not set.")

        artifact_yml_path = os.path.join(
            topsrcdir, "taskcluster/gecko_taskgraph/transforms/artifacts.yml"
        )

        upload_short_dir = os.path.join(base_work_dir, "cidata")

        # Choose artifacts based on build platform
        if build_platform.startswith("win"):
            main_platform = "win"
        elif build_platform.startswith("linux"):
            main_platform = "linux"
        elif build_platform.startswith("mac"):
            main_platform = "macos"
        elif build_platform.startswith("android"):
            if build_platform == "android-geckoview-docs":
                return
            main_platform = "android"
        elif build_platform.startswith("ios"):
            return
        else:
            err = f"Build platform {build_platform} didn't start with 'mac', 'linux', 'win', 'android' or 'ios'"
            self.fatal(err)
        try:
            with open(artifact_yml_path) as artfile:
                arts = []
                platforms = yaml.safe_load(artfile.read())
                for artifact in platforms[main_platform]:
                    arts.append(artifact)
        except FileNotFoundError:
            self.fatal("Could not read artifacts.yml; file not found. Exiting.")
        except PermissionError:
            self.fatal("Could not read artifacts.yml; permission error.")
        except YAMLError as ye:
            self.fatal(f"Failed to parse artifacts.yml with error:\n{ye}")

        try:
            os.makedirs(upload_short_dir)
        except FileExistsError:
            pass
        except PermissionError:
            self.fatal(f'Failed to create dir. "{upload_short_dir}"; permission error.')

        for art in arts:
            source_file = os.path.join(upload_dir, art)
            if not os.path.exists(source_file):
                self.info(
                    f"The artifact {source_file} is not present in this build. Skipping"
                )
                continue
            dest_file = os.path.join(upload_short_dir, art)
            try:
                os.rename(source_file, dest_file)
                if os.path.exists(dest_file):
                    self.info(
                        f"Successfully moved artifact {source_file} to {dest_file}"
                    )
                else:
                    self.fatal(
                        f"Move of {source_file} to {dest_file} was not successful."
                    )
            except (PermissionError, FileNotFoundError) as err:
                self.fatal(
                    f'Failed to move file "{art}" from {source_file} to {dest_file}:\n{err}'
                )
                continue
