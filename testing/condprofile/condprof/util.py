# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# This module needs to stay Python 2 and 3 compatible
#
import contextlib
import os
import platform
import shutil
import sys
import tempfile
import time
from subprocess import PIPE, Popen

import mozlog
import requests
import yaml
from requests.exceptions import ConnectionError
from requests.packages.urllib3.util.retry import Retry

from condprof import progress

TASK_CLUSTER = "MOZ_AUTOMATION" in os.environ.keys()
DOWNLOAD_TIMEOUT = 30


class ArchiveNotFound(Exception):
    pass


DEFAULT_PREFS = {
    "focusmanager.testmode": True,
    "marionette.defaultPrefs.port": 2828,
    "marionette.port": 2828,
    "remote.log.level": "Trace",
    "marionette.log.truncate": False,
    "extensions.autoDisableScopes": 10,
    "devtools.debugger.remote-enabled": True,
    "devtools.console.stdout.content": True,
    "devtools.console.stdout.chrome": True,
}

DEFAULT_CUSTOMIZATION = os.path.join(
    os.path.dirname(__file__), "customization", "default.json"
)
STRUCTLOG_PAD_SIZE = 20


class BridgeLogger:
    def __init__(self, logger):
        self.logger = logger

    def _find(self, text, *names):
        # structlog's ConsoleRenderer pads values
        for name in names:
            if name + " " * STRUCTLOG_PAD_SIZE in text:
                return True
        return False

    def _convert(self, message):
        return obfuscate(message)[1]

    def info(self, message, *args, **kw):
        if not isinstance(message, str):
            message = str(message)
        # converting Arsenic request/response struct log
        if self._find(message, "request", "response"):
            self.logger.debug(self._convert(message), *args, **kw)
        else:
            self.logger.info(self._convert(message), *args, **kw)

    def error(self, message, *args, **kw):
        self.logger.error(self._convert(message), *args, **kw)

    def warning(self, message, *args, **kw):
        self.logger.warning(self._convert(message), *args, **kw)


logger = None


def get_logger():
    global logger
    if logger is not None:
        return logger
    new_logger = mozlog.get_default_logger("condprof")
    if new_logger is None:
        new_logger = mozlog.unstructured.getLogger("condprof")

    # wrap the logger into the BridgeLogger
    new_logger = BridgeLogger(new_logger)

    try:
        # bridge for Arsenic
        from arsenic import connection
        from structlog import wrap_logger

        connection.log = wrap_logger(new_logger)
    except ImportError:
        # Arsenic is not installed for client-only usage
        pass
    logger = new_logger
    return logger


# initializing the logger right away
get_logger()


def fresh_profile(profile, customization_data):
    from mozprofile import create_profile  # NOQA

    # XXX on android we mgiht need to run it on the device?
    logger.info("Creating a fresh profile")
    new_profile = create_profile(app="firefox")
    prefs = customization_data["prefs"]
    prefs.update(DEFAULT_PREFS)
    logger.info("Setting prefs %s" % str(prefs.items()))
    new_profile.set_preferences(prefs)
    extensions = []
    for name, url in customization_data["addons"].items():
        logger.info("Downloading addon %s" % name)
        # When running on the CI, we expect the xpi files to have been
        # fetched by the firefox-addons fetch task dependency (see
        # taskcluster/kinds/fetch/browsertime.yml) and the condprof-addons
        # linter enforces the content of the archive to be unpacked into
        # a subdirectory named "firefox-addons".
        extension = download_file(url, mozfetches_subdir="firefox-addons")
        extensions.append(extension)
    logger.info("Installing addons")
    new_profile.addons.install(extensions)
    shutil.copytree(new_profile.profile, profile)
    return profile


link = "https://ftp.mozilla.org/pub/firefox/nightly/latest-mozilla-central/"


def get_firefox_download_link():
    try:
        from bs4 import BeautifulSoup
    except ImportError:
        raise ImportError("You need to run pip install beautifulsoup4")
    if platform.system() == "Darwin":
        extension = ".dmg"
    elif platform.system() == "Linux":
        arch = platform.machine()
        extension = ".linux-%s.tar.xz" % arch
    else:
        raise NotImplementedError(platform.system())

    page = requests.get(link).text
    soup = BeautifulSoup(page, "html.parser")
    for node in soup.find_all("a", href=True):
        href = node["href"]
        if href.endswith(extension):
            return "https://ftp.mozilla.org" + href

    raise Exception()


def check_exists(archive, server=None, all_types=False):
    if server is not None:
        archive = server + "/" + archive
    try:
        logger.info("Getting headers at %s" % archive)
        resp = requests.head(archive, timeout=DOWNLOAD_TIMEOUT)
    except ConnectionError:
        return False, {}

    if resp.status_code in (302, 303):
        logger.info("Redirected")
        return check_exists(resp.headers["Location"])

    # see Bug 1574854
    if (
        not all_types
        and resp.status_code == 200
        and "text/html" in resp.headers["Content-Type"]
    ):
        logger.info("Got an html page back")
        exists = False
    else:
        logger.info("Response code is %d" % resp.status_code)
        exists = resp.status_code

    return exists, resp.headers


def check_mozfetches_dir(target, mozfetches_subdir):
    logger.info("Checking for existence of: %s in MOZ_FETCHES_DIR" % target)
    fetches = os.environ.get("MOZ_FETCHES_DIR")
    if fetches is None:
        return None
    fetches_target = os.path.join(fetches, mozfetches_subdir, target)
    if not os.path.exists(fetches_target):
        return None
    logger.info("Already fetched and available in MOZ_FETCHES_DIR: %s" % fetches_target)
    return fetches_target


def download_file(url, target=None, mozfetches_subdir=None):
    if target is None:
        target = url.split("/")[-1]

    # check if the assets has been fetched through a taskgraph fetch task dependency
    # and already available in the MOZ_FETCHES_DIR passed as an additional parameter.
    if mozfetches_subdir is not None:
        filepath = check_mozfetches_dir(target, mozfetches_subdir)
        if filepath is not None:
            return filepath

    present, headers = check_exists(url)
    if not present:
        logger.info("Cannot find %r" % url)
        raise ArchiveNotFound(url)

    etag = headers.get("ETag")

    logger.info("Checking for existence of: %s" % target)
    if os.path.exists(target):
        # XXX for now, reusing downloads without checking them
        # when we don't have an .etag file
        if etag is None or not os.path.exists(target + ".etag"):
            logger.info("No existing etag downloads.")
            return target
        with open(target + ".etag") as f:
            current_etag = f.read()
        if etag == current_etag:
            logger.info("Already Downloaded.")
            # should at least check the size?
            return target
        else:
            logger.info("Changed!")
    else:
        logger.info("Could not find an existing archive.")
        # Add some debugging logs for the directory content
        try:
            archivedir = os.path.dirname(target)
            logger.info(
                "Content in cache directory %s: %s"
                % (archivedir, os.listdir(archivedir))
            )
        except Exception:
            logger.info("Failed to list cache directory contents")

    logger.info("Downloading %s" % url)
    req = requests.get(url, stream=True, timeout=DOWNLOAD_TIMEOUT)
    total_length = int(req.headers.get("content-length"))
    target_dir = os.path.dirname(target)
    if target_dir != "" and not os.path.exists(target_dir):
        logger.info("Creating dir %s" % target_dir)
        os.makedirs(target_dir)

    with open(target, "wb") as f:
        if TASK_CLUSTER:
            for chunk in req.iter_content(chunk_size=1024):
                if chunk:
                    f.write(chunk)
                    f.flush()
        else:
            iter = req.iter_content(chunk_size=1024)
            # pylint --py3k W1619
            size = total_length / 1024 + 1
            for chunk in progress.bar(iter, expected_size=size):
                if chunk:
                    f.write(chunk)
                    f.flush()

    if etag is not None:
        with open(target + ".etag", "w") as f:
            f.write(etag)

    return target


def extract_from_dmg(dmg, target):
    mount = tempfile.mkdtemp()
    cmd = "hdiutil attach -nobrowse -mountpoint %s %s"
    os.system(cmd % (mount, dmg))
    try:
        found = False
        for f in os.listdir(mount):
            if not f.endswith(".app"):
                continue
            app = os.path.join(mount, f)
            shutil.copytree(app, target)
            found = True
            break
    finally:
        os.system("hdiutil detach " + mount)
        shutil.rmtree(mount)
    if not found:
        raise OSError("No app file found in %s" % dmg)


@contextlib.contextmanager
def latest_nightly(binary=None):
    if binary is None:
        # we want to use the latest nightly
        nightly_archive = get_firefox_download_link()
        logger.info("Downloading %s" % nightly_archive)
        target = download_file(nightly_archive)
        # on macOs we just mount the DMG
        # XXX replace with extract_from_dmg
        if platform.system() == "Darwin":
            cmd = "hdiutil attach -mountpoint /Volumes/Nightly %s"
            os.system(cmd % target)
            binary = "/Volumes/Nightly/Firefox Nightly.app/Contents/MacOS/firefox"
        # on linux we unpack it
        elif platform.system() == "Linux":
            # Tar should automatically recognize the compression algo (xz/bzip2)
            cmd = "tar -xvf %s" % target
            os.system(cmd)
            binary = "firefox/firefox"

        mounted = True
    else:
        mounted = False
    try:
        yield binary
    finally:
        # XXX replace with extract_from_dmg
        if mounted:
            if platform.system() == "Darwin":
                logger.info("Unmounting Firefox")
                time.sleep(10)
                os.system("hdiutil detach /Volumes/Nightly")
            elif platform.system() == "Linux":
                # XXX we should keep it for next time
                shutil.rmtree("firefox")


def write_yml_file(yml_file, yml_data):
    logger.info("writing %s to %s" % (yml_data, yml_file))
    try:
        with open(yml_file, "w") as outfile:
            yaml.dump(yml_data, outfile, default_flow_style=False)
    except Exception:
        logger.error("failed to write yaml file", exc_info=True)


def get_version(firefox):
    p = Popen([firefox, "--version"], stdin=PIPE, stdout=PIPE, stderr=PIPE)
    output, __ = p.communicate()
    first_line = output.strip().split(b"\n")[0]
    res = first_line.split()[-1]
    return res.decode("utf-8")


def get_current_platform():
    """Returns a combination of system and arch info that matches TC standards.

    e.g. macosx64, win32, linux64, etc..
    """
    arch = sys.maxsize == 2**63 - 1 and "64" or "32"
    plat = platform.system().lower()
    if plat == "windows":
        plat = "win"
    elif plat == "darwin":
        plat = "macosx"
    return plat + arch


class BaseEnv:
    def __init__(self, profile, firefox, geckodriver, archive, device_name):
        self.profile = profile
        self.firefox = firefox
        self.geckodriver = geckodriver
        if profile is None:
            self.profile = os.path.join(tempfile.mkdtemp(), "profile")
        else:
            self.profile = profile
        self.archive = archive
        self.device_name = device_name

    @property
    def target_platform(self):
        return self.get_target_platform()

    def get_target_platform(self):
        raise NotImplementedError()

    def get_device(self, *args, **kw):
        raise NotImplementedError()

    @contextlib.contextmanager
    def get_browser(self, path):
        raise NotImplementedError()

    def get_browser_args(self, headless):
        raise NotImplementedError()

    def prepare(self, logfile):
        pass

    def check_session(self, session):
        pass

    def dump_logs(self):
        pass

    def get_browser_version(self):
        raise NotImplementedError()

    def get_geckodriver(self, log_file):
        raise NotImplementedError()

    def collect_profile(self):
        pass

    def stop_browser(self):
        pass


_URL = (
    "{0}/secrets/v1/secret/project"
    "{1}releng{1}gecko{1}build{1}level-{2}{1}conditioned-profiles"
)
_DEFAULT_SERVER = "https://firefox-ci-tc.services.mozilla.com"


def get_tc_secret():
    if not TASK_CLUSTER:
        raise OSError("Not running in Taskcluster")
    session = requests.Session()
    retry = Retry(total=5, backoff_factor=0.1, status_forcelist=[500, 502, 503, 504])
    http_adapter = requests.adapters.HTTPAdapter(max_retries=retry)
    session.mount("https://", http_adapter)
    session.mount("http://", http_adapter)
    secrets_url = _URL.format(
        os.environ.get("TASKCLUSTER_PROXY_URL", _DEFAULT_SERVER),
        "%2F",
        os.environ.get("MOZ_SCM_LEVEL", "1"),
    )
    res = session.get(secrets_url, timeout=DOWNLOAD_TIMEOUT)
    res.raise_for_status()
    return res.json()["secret"]


_CACHED = {}


def obfuscate(text):
    if "CONDPROF_RUNNER" not in os.environ:
        return True, text
    username, password = get_credentials()
    if username is None:
        return False, text
    if username not in text and password not in text:
        return False, text
    text = text.replace(password, "<PASSWORD>")
    text = text.replace(username, "<USERNAME>")
    return True, text


def obfuscate_file(path):
    if "CONDPROF_RUNNER" not in os.environ:
        return
    with open(path) as f:
        data = f.read()
    hit, data = obfuscate(data)
    if not hit:
        return
    with open(path, "w") as f:
        f.write(data)


def get_credentials():
    if "creds" in _CACHED:
        return _CACHED["creds"]
    password = os.environ.get("FXA_PASSWORD")
    username = os.environ.get("FXA_USERNAME")
    if username is None or password is None:
        if not TASK_CLUSTER:
            return None, None
        secret = get_tc_secret()
        password = secret.get("password", None)
        username = secret.get("username", None)
    _CACHED["creds"] = username, password
    return username, password
