#!/usr/bin/env python
# Documentation: https://firefox-source-docs.mozilla.org/taskcluster/partner-repacks.html

import logging
import os
import re
import stat
import sys
import tarfile
import urllib.parse
import urllib.request
import zipfile
from optparse import OptionParser
from pathlib import Path
from shutil import copy, copytree, move, rmtree, which
from subprocess import Popen

from mozfile import json
from redo import retry

logging.basicConfig(
    stream=sys.stdout,
    level=logging.INFO,
    format="%(asctime)-15s - %(levelname)s - %(message)s",
)
log = logging.getLogger(__name__)


# Set default values.
PARTNERS_DIR = Path("..") / ".." / "workspace" / "partners"
# No platform in this path because script only supports repacking a single platform at once
DEFAULT_OUTPUT_DIR = "%(partner)s/%(partner_distro)s/%(locale)s"
TASKCLUSTER_ARTIFACTS = (
    os.environ.get("TASKCLUSTER_ROOT_URL", "https://firefox-ci-tc.services.mozilla.com")
    + "/api/queue/v1/task/{taskId}/artifacts"
)
UPSTREAM_ENUS_PATH = "public/build/{filename}"
UPSTREAM_L10N_PATH = "public/build/{locale}/{filename}"

WINDOWS_DEST_DIR = Path("firefox")
MAC_DEST_DIR = Path("Contents/Resources")
LINUX_DEST_DIR = Path("firefox")

BOUNCER_PRODUCT_TEMPLATE = (
    "partner-firefox-{release_type}-{partner}-{partner_distro}-latest"
)


class StrictFancyURLopener(urllib.request.FancyURLopener):
    """Unlike FancyURLopener this class raises exceptions for generic HTTP
    errors, like 404, 500. It reuses URLopener.http_error_default redefined in
    FancyURLopener"""

    def http_error_default(self, url, fp, errcode, errmsg, headers):
        urllib.request.URLopener.http_error_default(
            self, url, fp, errcode, errmsg, headers
        )


def rmdirRecursive(directory: Path):
    """
    This is similar to a call of shutil.rmtree(), except that it
    should work better on Windows since it will more aggressively
    attempt to remove files marked as "read-only".
    """

    def rmdir_including_read_only(func, path: str, exc_info):
        """
        Source: https://stackoverflow.com/a/4829285
        path contains the path of the file that couldn't be removed.
        Let's just assume that it's read-only and unlink it.
        """
        path = Path(path)

        path.chmod(mode=stat.S_IWRITE)
        path.unlink()

    rmtree(str(directory), onerror=rmdir_including_read_only)


def printSeparator():
    log.info("##################################################")


def shellCommand(cmd):
    log.debug("Executing %s" % cmd)
    log.debug(f"in {Path.cwd()}")
    # Shell command output gets dumped immediately to stdout, whereas
    # print statements get buffered unless we flush them explicitly.
    sys.stdout.flush()
    p = Popen(cmd, shell=True)
    (_, ret) = os.waitpid(p.pid, 0)
    if ret != 0:
        ret_real = (ret & 0xFF00) >> 8
        log.error("Error: shellCommand had non-zero exit status: %d" % ret_real)
        log.error("Command: %s" % cmd, exc_info=True)
        sys.exit(ret_real)
    return True


def isLinux(platform: str):
    return "linux" in platform


def isLinux64(platform: str):
    return "linux64" in platform or "linux-x86_64" in platform


def isMac(platform: str):
    return "mac" in platform


def isWin(platform: str):
    return "win" in platform


def isWin32(platform: str):
    return "win32" in platform


def isWin64(platform: str):
    return platform == "win64"


def isWin64Aarch64(platform: str):
    return platform == "win64-aarch64"


def isValidPlatform(platform: str):
    return (
        isLinux64(platform)
        or isMac(platform)
        or isWin64(platform)
        or isWin64Aarch64(platform)
        or isWin32(platform)
    )


def parseRepackConfig(file: Path, platform: str):
    """Did you hear about this cool file format called yaml ? json ? Yeah, me neither"""
    config = {}
    config["platforms"] = []
    for line in file.open():
        line = line.rstrip("\n")
        # Ignore empty lines
        if line.strip() == "":
            continue
        # Ignore comments
        if line.startswith("#"):
            continue
        [key, value] = line.split("=", 2)
        value = value.strip('"')
        # strings that don't need special handling
        if key in ("dist_id", "replacement_setup_exe"):
            config[key] = value
            continue
        # booleans that don't need special handling
        if key in ("migrationWizardDisabled", "oem", "repack_stub_installer"):
            if value.lower() == "true":
                config[key] = True
            continue
        # special cases
        if key == "locales":
            config["locales"] = value.split(" ")
            continue
        if key.startswith("locale."):
            config[key] = value
            continue
        if key == "deb_section":
            config["deb_section"] = re.sub("/", r"\/", value)
            continue
        if isValidPlatform(key):
            ftp_platform = getFtpPlatform(key)
            if ftp_platform == getFtpPlatform(platform) and value.lower() == "true":
                config["platforms"].append(ftp_platform)
            continue

    # this only works for one locale because setup.exe is localised
    if config.get("replacement_setup_exe") and len(config.get("locales", [])) > 1:
        log.error(
            "Error: replacement_setup_exe is only supported for one locale, got %s"
            % config["locales"]
        )
        sys.exit(1)
    # also only works for one platform because setup.exe is platform-specific

    if config["platforms"]:
        return config


def getFtpPlatform(platform: str):
    """Returns the platform in the format used in building package names.
    Note: we rely on this code being idempotent
    i.e. getFtpPlatform(getFtpPlatform(foo)) should work
    """
    if isLinux64(platform):
        return "linux-x86_64"
    if isLinux(platform):
        return "linux-i686"
    if isMac(platform):
        return "mac"
    if isWin64Aarch64(platform):
        return "win64-aarch64"
    if isWin64(platform):
        return "win64"
    if isWin32(platform):
        return "win32"


def getFileExtension(platform: str):
    """The extension for the output file, which may be passed to the internal-signing task"""
    if isLinux(platform):
        return "tar.xz"
    elif isMac(platform):
        return "tar.gz"
    elif isWin(platform):
        return "zip"


def getFilename(platform: str):
    """Returns the filename to be repacked for the platform"""
    return f"target.{getFileExtension(platform)}"


def getAllFilenames(platform: str, repack_stub_installer):
    """Returns the full list of filenames we want to downlaod for each platform"""
    file_names = [getFilename(platform)]
    if isWin(platform):
        # we want to copy forward setup.exe from upstream tasks to make it easier to repackage
        # windows installers later
        file_names.append("setup.exe")
        # Same for the stub installer with setup-stub.exe, but only in win32 repack jobs
        if isWin32(platform) and repack_stub_installer:
            file_names.append("setup-stub.exe")
    return tuple(file_names)


def getTaskArtifacts(taskId):
    try:
        retrieveFile(
            TASKCLUSTER_ARTIFACTS.format(taskId=taskId), Path("tc_artifacts.json")
        )
        tc_index = json.load(open("tc_artifacts.json"))
        return tc_index["artifacts"]
    except (ValueError, KeyError):
        log.error("Failed to get task artifacts from TaskCluster")
        raise


def getUpstreamArtifacts(upstream_tasks, repack_stub_installer):
    useful_artifacts = getAllFilenames(options.platform, repack_stub_installer)

    artifact_ids = {}
    for taskId in upstream_tasks:
        for artifact in getTaskArtifacts(taskId):
            name = artifact["name"]
            if not name.endswith(useful_artifacts):
                continue
            if name in artifact_ids:
                log.error(
                    "Duplicated artifact %s processing tasks %s & %s",
                    name,
                    taskId,
                    artifacts[name],
                )
                sys.exit(1)
            else:
                artifact_ids[name] = taskId
    log.debug(
        "Found artifacts: %s" % json.dumps(artifact_ids, indent=2, sort_keys=True)
    )
    return artifact_ids


def getArtifactNames(platform: str, locale, repack_stub_installer):
    file_names = getAllFilenames(platform, repack_stub_installer)
    if locale == "en-US":
        names = [UPSTREAM_ENUS_PATH.format(filename=f) for f in file_names]
    else:
        names = [
            UPSTREAM_L10N_PATH.format(locale=locale, filename=f) for f in file_names
        ]
    return names


def retrieveFile(url, file_path: Path):
    success = True
    url = urllib.parse.quote(url, safe=":/")
    log.info(f"Downloading from {url}")
    log.info(f"To: {file_path}")
    log.info(f"CWD: {Path.cwd()}")
    try:
        # use URLopener, which handles errors properly
        retry(
            StrictFancyURLopener().retrieve,
            kwargs=dict(url=url, filename=str(file_path)),
        )
    except OSError:
        log.error("Error downloading %s" % url, exc_info=True)
        success = False
        try:
            file_path.unlink()
        except OSError:
            log.info(f"Cannot remove {file_path}", exc_info=True)

    return success


def getBouncerProduct(partner, partner_distro):
    if "RELEASE_TYPE" not in os.environ:
        log.fatal("RELEASE_TYPE must be set in the environment")
        sys.exit(1)
    release_type = os.environ["RELEASE_TYPE"]
    return BOUNCER_PRODUCT_TEMPLATE.format(
        release_type=release_type,
        partner=partner,
        partner_distro=partner_distro,
    )


class RepackBase:
    def __init__(
        self,
        build: str,
        partner_dir: Path,
        build_dir: Path,
        final_dir: Path,
        ftp_platform: str,
        repack_info,
        file_mode=0o644,
        quiet=False,
        source_locale=None,
        locale=None,
    ):
        self.base_dir = Path.cwd()
        self.build = build
        self.full_build_path = build_dir / build
        if not self.full_build_path.is_absolute():
            self.full_build_path = self.base_dir / self.full_build_path
        self.full_partner_path = self.base_dir / partner_dir
        self.working_dir = final_dir / "working"
        self.final_dir = final_dir
        self.final_build = final_dir / Path(build).name
        self.ftp_platform = ftp_platform
        self.repack_info = repack_info
        self.file_mode = file_mode
        self.quiet = quiet
        self.source_locale = source_locale
        self.locale = locale
        self.working_dir.mkdir(mode=0o755, exist_ok=True, parents=True)

    def announceStart(self):
        log.info(
            "Repacking %s %s build %s" % (self.ftp_platform, self.locale, self.build)
        )

    def announceSuccess(self):
        log.info(
            "Done repacking %s %s build %s"
            % (self.ftp_platform, self.locale, self.build)
        )

    def unpackBuild(self):
        copy(str(self.full_build_path), ".")

    def createOverrideIni(self, partner_path: Path):
        """If this is a partner specific locale (like en-HK), set the
        distribution.ini to use that locale, not the default locale.
        """
        if self.locale != self.source_locale:
            file_path = partner_path / "distribution" / "distribution.ini"
            with file_path.open(file_path.is_file() and "a" or "w") as open_file:
                open_file.write("[Locale]\n")
                open_file.write("locale=" + self.locale + "\n")

        """ Some partners need to override the migration wizard. This is done
            by adding an override.ini file to the base install dir.
        """
        # modify distribution.ini if 44 or later and we have migrationWizardDisabled
        if int(options.version.split(".")[0]) >= 44:
            file_path = partner_path / "distribution" / "distribution.ini"
            with file_path.open() as open_file:
                ini = open_file.read()

            if ini.find("EnableProfileMigrator") >= 0:
                return
        else:
            browser_dir = partner_path / "browser"
            if not browser_dir.exists():
                browser_dir.mkdir(mode=0o755, exist_ok=True, parents=True)
            file_path = browser_dir / "override.ini"
        if "migrationWizardDisabled" in self.repack_info:
            log.info("Adding EnableProfileMigrator to %r" % (file_path,))
            with file_path.open(file_path.is_file() and "a" or "w") as open_file:
                open_file.write("[XRE]\n")
                open_file.write("EnableProfileMigrator=0\n")

    def obfuscateCfg(self, autoconfig_src: Path, dest_dir: Path):
        """Obfuscate the .cfg files copied out of autoconfig_src by adding 13
        to each byte. Firefox unobfuscates them by subtracting
        general.config.obscure_value, which defaults to 13.
        """
        for cfg in autoconfig_src.rglob("*.cfg"):
            dest_cfg = dest_dir / cfg.relative_to(autoconfig_src)
            data = dest_cfg.read_bytes()
            dest_cfg.write_bytes(bytes((b + 13) & 0xFF for b in data))

    def copyFiles(self, platform_dir: Path):
        log.info(f"Copying files into {platform_dir}")
        # Check whether we've already copied files over for this partner.
        if not platform_dir.exists():
            platform_dir.mkdir(mode=0o755, exist_ok=True, parents=True)
            autoconfig_path = self.full_partner_path / "autoconfig"
            if autoconfig_path.exists():
                copytree(str(autoconfig_path), str(platform_dir), dirs_exist_ok=True)
                self.obfuscateCfg(autoconfig_path, platform_dir)
            for i in ["distribution", "extensions"]:
                full_path = self.full_partner_path / i
                if full_path.exists():
                    copytree(str(full_path), str(platform_dir / i))
            self.createOverrideIni(platform_dir)

    def repackBuild(self):
        pass

    def stage(self):
        move(self.build, str(self.final_dir))
        self.final_build.chmod(self.file_mode)

    def cleanup(self):
        self.final_build.unlink()

    def doRepack(self):
        self.announceStart()
        os.chdir(self.working_dir)
        self.unpackBuild()
        self.copyFiles()
        self.repackBuild()
        self.stage()
        os.chdir(self.base_dir)
        rmdirRecursive(self.working_dir)
        self.announceSuccess()


class RepackLinux(RepackBase):
    def __init__(
        self,
        build: str,
        partner_dir: Path,
        build_dir: Path,
        final_dir: Path,
        ftp_platform: str,
        repack_info,
        **kwargs,
    ):
        super().__init__(
            build,
            partner_dir,
            build_dir,
            final_dir,
            ftp_platform,
            repack_info,
            **kwargs,
        )
        self.uncompressed_build = build.replace(".xz", "")

    def unpackBuild(self):
        super().unpackBuild()
        target_path = Path(self.uncompressed_build)
        unpack_cmd = f"xz -c -d {self.build} > {target_path.absolute()}"
        shellCommand(unpack_cmd)
        if not target_path.exists():
            log.error(
                f"Error: Unable to uncompress build {self.build} - {target_path} doesn't exist"
            )
            sys.exit(1)

    def copyFiles(self):
        super().copyFiles(LINUX_DEST_DIR)

    def repackBuild(self):
        if options.quiet:
            tar_flags = "rf"
        else:
            tar_flags = "rvf"
        tar_cmd = "tar %s %s %s" % (tar_flags, self.uncompressed_build, LINUX_DEST_DIR)
        shellCommand(tar_cmd)
        compress_cmd = "xz -f -z -e -9 %s" % self.uncompressed_build
        shellCommand(compress_cmd)


class RepackMac(RepackBase):
    def __init__(
        self,
        build: str,
        partner_dir: Path,
        build_dir: Path,
        final_dir: Path,
        ftp_platform: str,
        repack_info,
        **kwargs,
    ):
        super().__init__(
            build,
            partner_dir,
            build_dir,
            final_dir,
            ftp_platform,
            repack_info,
            **kwargs,
        )
        self.uncompressed_build = build.replace(".gz", "")

    def unpackBuild(self):
        super().unpackBuild()
        gunzip_cmd = "gunzip %s" % self.build
        shellCommand(gunzip_cmd)
        if not Path(self.uncompressed_build).exists():
            log.error(f"Error: Unable to uncompress build {self.build}")
            sys.exit(1)
        self.appName = self.getAppName()

    def getAppName(self):
        # Cope with Firefox.app vs Firefox Nightly.app by returning the first root object/folder found
        t = tarfile.open(self.build.rsplit(".", 1)[0])
        for name in t.getnames():
            root_object = name.split("/")[0]
            if root_object.endswith(".app"):
                log.info(f"Found app name in tarball: {root_object}")
                return root_object
        log.error(
            f"Error: Unable to determine app name from tarball: {self.build} - Expected .app in root"
        )
        sys.exit(1)

    def copyFiles(self):
        super().copyFiles(Path(self.appName) / MAC_DEST_DIR)

    def repackBuild(self):
        if options.quiet:
            tar_flags = "rf"
        else:
            tar_flags = "rvf"
        # the final arg is quoted because it may contain a space, eg Firefox Nightly.app/....
        tar_cmd = "tar %s %s '%s'" % (
            tar_flags,
            self.uncompressed_build,
            Path(self.appName) / MAC_DEST_DIR,
        )
        shellCommand(tar_cmd)
        gzip_command = f"gzip -3 {self.uncompressed_build}"
        shellCommand(gzip_command)


class RepackWin(RepackBase):
    def __init__(
        self,
        build: str,
        partner_dir: Path,
        build_dir: Path,
        final_dir: Path,
        ftp_platform: str,
        repack_info,
        **kwargs,
    ):
        super().__init__(
            build,
            partner_dir,
            build_dir,
            final_dir,
            ftp_platform,
            repack_info,
            **kwargs,
        )

    def copyFiles(self):
        super().copyFiles(WINDOWS_DEST_DIR)

    def repackBuild(self):
        if options.quiet:
            zip_flags = "-rq"
        else:
            zip_flags = "-r"
        zip_cmd = f"zip {zip_flags} {self.build} {WINDOWS_DEST_DIR}"
        shellCommand(zip_cmd)

        # we generate the stub installer during the win32 build, so repack it on win32 too
        if isWin32(options.platform) and self.repack_info.get("repack_stub_installer"):
            log.info("Creating target-stub.zip to hold custom urls")
            dest = str(self.final_build).replace("target.zip", "target-stub.zip")
            z = zipfile.ZipFile(dest, "w")
            # load the partner.ini template and interpolate %LOCALE% to the actual locale
            with (self.full_partner_path / "stub" / "partner.ini").open() as open_file:
                partner_ini_template = open_file.readlines()
            partner_ini = ""
            for l in partner_ini_template:
                l = l.replace("%LOCALE%", self.locale)
                l = l.replace("%BOUNCER_PRODUCT%", self.repack_info["bouncer_product"])
                partner_ini += l
            z.writestr("partner.ini", partner_ini)
            # we need an empty firefox directory to use the repackage code
            d = zipfile.ZipInfo("firefox/")
            # https://stackoverflow.com/a/6297838, zip's representation of drwxr-xr-x permissions
            # is 040755 << 16L, bitwise OR with 0x10 for the MS-DOS directory flag
            d.external_attr = 1106051088
            z.writestr(d, "")
            z.close()

    def stage(self):
        super().stage()
        setup_dest = Path(str(self.final_build).replace("target.zip", "setup.exe"))
        if "replacement_setup_exe" in self.repack_info:
            log.info("Overriding setup.exe with custom copy")
            retrieveFile(self.repack_info["replacement_setup_exe"], setup_dest)
        else:
            # otherwise copy forward the vanilla copy
            log.info("Copying vanilla setup.exe forward for installer creation")
            setup = str(self.full_build_path).replace("target.zip", "setup.exe")
            copy(setup, str(setup_dest))
        setup_dest.chmod(self.file_mode)

        # we generate the stub installer in the win32 build, so repack it on win32 too
        if isWin32(options.platform) and self.repack_info.get("repack_stub_installer"):
            log.info(
                "Copying vanilla setup-stub.exe forward for stub installer creation"
            )
            setup_dest = Path(
                str(self.final_build).replace("target.zip", "setup-stub.exe")
            )
            setup_source = str(self.full_build_path).replace(
                "target.zip", "setup-stub.exe"
            )
            copy(setup_source, str(setup_dest))
            setup_dest.chmod(self.file_mode)


if __name__ == "__main__":
    error = False
    partner_builds = {}
    repack_build = {
        "linux-i686": RepackLinux,
        "linux-x86_64": RepackLinux,
        "mac": RepackMac,
        "win32": RepackWin,
        "win64": RepackWin,
        "win64-aarch64": RepackWin,
    }

    parser = OptionParser(usage="usage: %prog [options]")
    parser.add_option(
        "-d",
        "--partners-dir",
        dest="partners_dir",
        default=str(PARTNERS_DIR),
        help="Specify the directory where the partner config files are found",
    )
    parser.add_option(
        "-p",
        "--partner",
        dest="partner",
        help="Repack for a single partner, specified by name",
    )
    parser.add_option(
        "-v", "--version", dest="version", help="Set the version number for repacking"
    )
    parser.add_option(
        "-n",
        "--build-number",
        dest="build_number",
        default=1,
        help="Set the build number for repacking",
    )
    parser.add_option("--platform", dest="platform", help="Set the platform to repack")
    parser.add_option(
        "--include-oem",
        action="store_true",
        dest="include_oem",
        default=False,
        help="Process partners marked as OEM (these are usually one-offs)",
    )
    parser.add_option(
        "-q",
        "--quiet",
        action="store_true",
        dest="quiet",
        default=False,
        help="Suppress standard output from the packaging tools",
    )
    parser.add_option(
        "--taskid",
        action="append",
        dest="upstream_tasks",
        help="Specify taskIds for upstream artifacts, using 'internal sign' tasks. Multiples "
        "expected, e.g. --taskid foo --taskid bar. Alternatively, use a space-separated list "
        "stored in UPSTREAM_TASKIDS in the environment.",
    )
    parser.add_option(
        "-l",
        "--limit-locale",
        action="append",
        dest="limit_locales",
        default=[],
    )

    (options, args) = parser.parse_args()

    if not options.quiet:
        log.setLevel(logging.DEBUG)
    else:
        log.setLevel(logging.WARNING)

    options.partners_dir = Path(options.partners_dir.rstrip("/"))
    if not options.partners_dir.is_dir():
        log.error(f"Error: partners dir {options.partners_dir} is not a directory.")
        error = True

    if not options.version:
        log.error("Error: you must specify a version number.")
        error = True

    if not options.platform:
        log.error("No platform specified.")
        error = True

    if not isValidPlatform(options.platform):
        log.error("Invalid platform %s." % options.platform)
        error = True

    upstream_tasks = options.upstream_tasks or os.getenv("UPSTREAM_TASKIDS", "").split()
    if not upstream_tasks:
        log.error(
            "upstream tasks should be defined using --taskid args or "
            "UPSTREAM_TASKIDS in env."
        )
        error = True

    for tool in ("tar", "bunzip2", "bzip2", "gunzip", "gzip", "zip"):
        if not which(tool):
            log.error(f"Error: couldn't find the {tool} executable in PATH.")
            error = True

    if error:
        sys.exit(1)

    base_workdir = Path.cwd()

    # Look up the artifacts available on our upstreams, but only if we need to
    artifact_ids = {}

    # Local directories for builds
    script_directory = Path.cwd()
    original_builds_dir = (
        script_directory
        / "original_builds"
        / options.version
        / f"build{options.build_number}"
    )
    repack_version = f"{options.version}-{options.build_number}"
    if os.getenv("MOZ_AUTOMATION"):
        # running in production
        repacked_builds_dir = Path("/builds/worker/artifacts")
    else:
        # local development
        repacked_builds_dir = script_directory / "artifacts"
    original_builds_dir.mkdir(mode=0o755, exist_ok=True, parents=True)
    repacked_builds_dir.mkdir(mode=0o755, exist_ok=True, parents=True)
    printSeparator()

    # For each partner in the partners dir
    #    Read/check the config file
    #    Download required builds (if not already on disk)
    #    Perform repacks

    # walk the partner dirs, find valid repack.cfg configs, and load them
    partner_dirs = []
    need_stub_installers = False
    for root, _, all_files in os.walk(options.partners_dir):
        root = root.lstrip("/")
        partner = root[len(str(options.partners_dir)) + 1 :].split("/")[0]
        partner_distro = os.path.split(root)[-1]
        if options.partner:
            if (
                options.partner != partner
                and options.partner != partner_distro[: len(options.partner)]
            ):
                continue

        for file in all_files:
            if file == "repack.cfg":
                log.debug(
                    "Found partner config: {} ['{}'] {}".format(
                        root, "', '".join(_), file
                    )
                )
                root = Path(root)
                repack_cfg = root / file
                repack_info = parseRepackConfig(repack_cfg, options.platform)
                if not repack_info:
                    log.debug(
                        "no repack_info for platform %s in %s, skipping"
                        % (options.platform, repack_cfg)
                    )
                    continue
                if repack_info.get("repack_stub_installer"):
                    need_stub_installers = True
                    repack_info["bouncer_product"] = getBouncerProduct(
                        partner, partner_distro
                    )
                partner_dirs.append((partner, partner_distro, root, repack_info))

    log.info("Retrieving artifact lists from upstream tasks")
    artifact_ids = getUpstreamArtifacts(upstream_tasks, need_stub_installers)
    if not artifact_ids:
        log.fatal("No upstream artifacts were found")
        sys.exit(1)

    for partner, partner_distro, full_partner_dir, repack_info in partner_dirs:
        log.info(
            "Starting repack process for partner: %s/%s" % (partner, partner_distro)
        )
        if "oem" in repack_info and options.include_oem is False:
            log.info(
                "Skipping partner: %s  - marked as OEM and --include-oem was not set"
                % partner
            )
            continue

        repack_stub_installer = repack_info.get("repack_stub_installer")
        # where everything ends up
        partner_repack_dir = repacked_builds_dir / DEFAULT_OUTPUT_DIR

        # Figure out which base builds we need to repack.
        for locale in repack_info["locales"]:
            if options.limit_locales and locale not in options.limit_locales:
                log.info("Skipping %s because it is not in limit_locales list", locale)
                continue
            source_locale = locale
            # Partner has specified a different locale to
            # use as the base for their custom locale.
            if "locale." + locale in repack_info:
                source_locale = repack_info["locale." + locale]
            for platform in repack_info["platforms"]:
                # ja-JP-mac only exists for Mac, so skip non-existent
                # platform/locale combos.
                if (source_locale == "ja" and isMac(platform)) or (
                    source_locale == "ja-JP-mac" and not isMac(platform)
                ):
                    continue
                ftp_platform = getFtpPlatform(platform)

                local_filepath = original_builds_dir / ftp_platform / locale
                local_filepath.mkdir(mode=0o755, exist_ok=True, parents=True)
                final_dir = Path(
                    str(partner_repack_dir)
                    % dict(
                        partner=partner,
                        partner_distro=partner_distro,
                        locale=locale,
                    )
                )
                if final_dir.exists():
                    rmdirRecursive(final_dir)
                final_dir.mkdir(mode=0o755, exist_ok=True, parents=True)

                # for the main repacking artifact
                file_name = getFilename(ftp_platform)
                local_filename = local_filepath / file_name

                # Check to see if this build is already on disk, i.e.
                # has already been downloaded.
                artifacts = getArtifactNames(platform, locale, repack_stub_installer)
                for artifact in artifacts:
                    local_artifact = local_filepath / Path(artifact).name
                    if local_artifact.exists():
                        log.info(f"Found {local_artifact} on disk, not downloading")
                        continue

                    if artifact not in artifact_ids:
                        log.fatal(
                            "Can't determine what taskID to retrieve %s from", artifact
                        )
                        sys.exit(1)
                    original_build_url = "%s/%s" % (
                        TASKCLUSTER_ARTIFACTS.format(taskId=artifact_ids[artifact]),
                        artifact,
                    )
                    retrieveFile(original_build_url, local_artifact)

                # Make sure we have the local file now
                if not local_filename.exists():
                    log.info(f"Error: Unable to retrieve {file_name}\n")
                    sys.exit(1)

                repackObj = repack_build[ftp_platform](
                    file_name,
                    full_partner_dir,
                    local_filepath,
                    final_dir,
                    ftp_platform,
                    repack_info,
                    locale=locale,
                    source_locale=source_locale,
                )
                repackObj.doRepack()
