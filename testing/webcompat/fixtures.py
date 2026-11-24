# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import asyncio
import json
import math
import os
import re
import subprocess
import sys
from datetime import datetime

import pytest
import webdriver
from client import Client

try:
    import pathlib
except ImportError:
    import pathlib2 as pathlib

CB_PBM_PREF = "network.cookie.cookieBehavior.pbmode"
CB_PREF = "network.cookie.cookieBehavior"
INTERVENTIONS_PREF = "extensions.webcompat.enable_interventions"
NOTIFICATIONS_PERMISSIONS_PREF = "permissions.default.desktop-notification"
PBM_PREF = "browser.privatebrowsing.autostart"
PIP_OVERRIDES_PREF = "extensions.webcompat.enable_picture_in_picture_overrides"
SHIMS_PREF = "extensions.webcompat.enable_shims"
STRICT_ETP_PREF = "privacy.trackingprotection.enabled"
SYSTEM_ADDON_UPDATES_PREF = "extensions.systemAddon.update.enabled"
DOWNLOAD_TO_TEMP_PREF = "browser.download.start_downloads_in_tmp_dir"
DELETE_DOWNLOADS_PREF = "browser.helperApps.deleteTempFileOnExit"
PLATFORM_OVERRIDE_PREF = "extensions.webcompat.platform_override"
EME_ENABLED_PREF = "media.eme.enabled"


class WebDriver:
    def __init__(self, config):
        self.browser_binary = config.getoption("browser_binary")
        self.device_serial = config.getoption("device_serial")
        self.package_name = config.getoption("package_name")
        self.addon = config.getoption("addon")
        self.webdriver_binary = config.getoption("webdriver_binary")
        self.port = config.getoption("webdriver_port")
        self.ws_port = config.getoption("webdriver_ws_port")
        self.log_level = config.getoption("webdriver_log_level")
        self.headless = config.getoption("headless")
        self.debug = config.getoption("debug")
        self.proc = None

    def command_line_driver(self):
        raise NotImplementedError

    def capabilities(self, request, test_config):
        raise NotImplementedError

    def __enter__(self):
        assert self.proc is None
        self.proc = subprocess.Popen(self.command_line_driver())
        return self

    def __exit__(self, *args, **kwargs):
        self.proc.kill()


class FirefoxWebDriver(WebDriver):
    def command_line_driver(self):
        rv = [
            self.webdriver_binary,
            "--port",
            str(self.port),
            "--websocket-port",
            str(self.ws_port),
            "--allow-system-access",
        ]
        if self.debug:
            rv.append("-vv")
        elif self.log_level == "DEBUG":
            rv.append("-v")
        return rv

    def capabilities(self, request, test_config):
        prefs = {}

        override = request.config.getoption("platform_override")
        if override:
            prefs[PLATFORM_OVERRIDE_PREF] = override

        if "use_interventions" in test_config:
            value = test_config["use_interventions"]
            prefs[INTERVENTIONS_PREF] = value
            prefs[PIP_OVERRIDES_PREF] = value

        if "use_pbm" in test_config:
            prefs[PBM_PREF] = test_config["use_pbm"]

        if "use_shims" in test_config:
            prefs[SHIMS_PREF] = test_config["use_shims"]

        if "use_strict_etp" in test_config:
            prefs[STRICT_ETP_PREF] = test_config["use_strict_etp"]

        if test_config.get("use_big_minimum_font_size"):
            prefs["font.size.variable.x-western"] = 14
            prefs["font.minimum-size.x-western"] = 20

        if test_config.get("no_overlay_scrollbars"):
            prefs["widget.gtk.overlay-scrollbars.enabled"] = False
            prefs["widget.windows.overlay-scrollbars.enabled"] = False

        if test_config.get("enable_speechrecognition"):
            prefs["media.webspeech.recognition.enable"] = True
        elif test_config.get("disable_speechrecognition"):
            prefs["media.webspeech.recognition.enable"] = False

        if test_config.get("enable_standard_captureStream"):
            prefs["media.captureStream.enabled"] = True
        elif test_config.get("disable_standard_captureStream"):
            prefs["media.captureStream.enabled"] = False

        if test_config.get("enable_webkit_scrollbar"):
            prefs["layout.css.fake-webkit-scrollbar.enabled"] = True
        elif test_config.get("disable_webkit_scrollbar"):
            prefs["layout.css.fake-webkit-scrollbar.enabled"] = False

        if test_config.get("enable_webkit_fill_available"):
            prefs["layout.css.webkit-fill-available.enabled"] = True
        elif test_config.get("disable_webkit_fill_available"):
            prefs["layout.css.webkit-fill-available.enabled"] = False

        if test_config.get("enable_moztransform"):
            prefs["layout.css.prefixes.transforms"] = True
        elif test_config.get("disable_moztransform"):
            prefs["layout.css.prefixes.transforms"] = False

        # keep EME off, otherwise the tests try to download the CDM
        prefs[EME_ENABLED_PREF] = False

        # keep system addon updates off to prevent bug 1882562
        prefs[SYSTEM_ADDON_UPDATES_PREF] = False

        cookieBehavior = 4 if test_config.get("without_tcp") else 5
        prefs[CB_PREF] = cookieBehavior
        prefs[CB_PBM_PREF] = cookieBehavior

        prefs["webgl.allow-in-parent"] = True
        prefs["layout.css.getBoxQuads.enabled"] = True

        # prevent "allow notifications for?" popups by setting the
        # default permission for notificaitons to PERM_DENY_ACTION.
        prefs[NOTIFICATIONS_PERMISSIONS_PREF] = 2

        # if any downloads happen, put them in a temporary folder.
        prefs[DOWNLOAD_TO_TEMP_PREF] = True
        # also delete those files afterward.
        prefs[DELETE_DOWNLOADS_PREF] = True

        fx_options = {"prefs": prefs}

        if self.browser_binary:
            fx_options["binary"] = self.browser_binary
            if self.headless:
                fx_options.setdefault("args", [])
                fx_options["args"].append("--headless")

        if self.device_serial:
            fx_options["androidDeviceSerial"] = self.device_serial
            fx_options["androidPackage"] = self.package_name

        if self.addon:
            prefs["xpinstall.signatures.required"] = False
            prefs["extensions.experiments.enabled"] = True

        return {
            "pageLoadStrategy": "normal",
            "moz:firefoxOptions": fx_options,
        }


@pytest.fixture(scope="session")
def should_do_2fa(request):
    return request.config.getoption("do2fa", False)


@pytest.fixture(scope="session")
def config_file(request):
    path = request.config.getoption("config")
    if not path:
        return None
    with open(path) as f:
        return json.load(f)


@pytest.fixture
def bug_number(request):
    return re.findall(r"\d+", str(request.fspath.basename))[0]


@pytest.fixture
def in_headless_mode(request, session):
    # Android cannot be headless even if we request it on the commandline.
    if session.capabilities["platformName"] == "android":
        return False


@pytest.fixture
def credentials(bug_number, config_file):
    if not config_file:
        pytest.skip(f"login info required for bug #{bug_number}")
        return None

    try:
        credentials = config_file[bug_number]
    except KeyError:
        pytest.skip(f"no login for bug #{bug_number} found")
        return

    return {"username": credentials["username"], "password": credentials["password"]}


@pytest.fixture(scope="session")
def driver(pytestconfig):
    if pytestconfig.getoption("browser") == "firefox":
        cls = FirefoxWebDriver
    else:
        assert False

    with cls(pytestconfig) as driver_instance:
        yield driver_instance


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    setattr(item, "rep_" + rep.when, rep)


@pytest.fixture(scope="function", autouse=True)
async def test_failed_check(request):
    yield
    if (
        not request.config.getoption("no_failure_screenshots")
        and request.node.rep_setup.passed
        and request.node.rep_call.failed
    ):
        session = request.node.funcargs["session"]
        file_name = f"{request.node.nodeid}_failure_{datetime.today().strftime('%Y-%m-%d_%H:%M')}.png".replace(
            "/", "_"
        ).replace("::", "__")
        dest_dir = request.config.getoption("failure_screenshots_dir")
        try:
            await take_screenshot(session, file_name, dest_dir=dest_dir)
            print("Saved failure screenshot to: ", file_name)
        except Exception as e:
            print("Error saving screenshot: ", e)


async def take_screenshot(session, file_name, dest_dir=None):
    if dest_dir:
        cwd = pathlib.Path(dest_dir)
    else:
        cwd = pathlib.Path(os.getcwd())
    path = cwd / file_name

    top = await session.bidi_session.browsing_context.get_tree()
    screenshot = await session.bidi_session.browsing_context.capture_screenshot(
        context=top[0]["context"]
    )

    with path.open("wb") as strm:
        strm.write(screenshot)

    return file_name


@pytest.fixture(scope="session")
def event_loop():
    return asyncio.get_event_loop_policy().new_event_loop()


@pytest.fixture(scope="function")
async def client(request, session, event_loop):
    client = Client(request, session, event_loop)
    yield client

    # force-cancel any active downloads to prevent dialogs on exit
    with client.using_context("chrome"):
        client.execute_async_script(
            """
            const done = arguments[0];
            const { Downloads } = ChromeUtils.importESModule(
              "resource://gre/modules/Downloads.sys.mjs"
            );
            Downloads.getList(Downloads.ALL).then(list => {
              list.getAll().then(downloads => {
                Promise.allSettled(downloads.map(download => [
                  list.remove(download),
                  download.finalize(true)
                ]).flat()).then(done);
              });
            });
        """
        )


def install_addon(session, addon_file_path):
    context = session.send_session_command("GET", "moz/context")
    session.send_session_command("POST", "moz/context", {"context": "chrome"})
    session.execute_async_script(
        """
        async function installAsBuiltinExtension(xpi) {
            // The built-in location requires a resource: URL that maps to a
            // jar: or file: URL.  This would typically be something bundled
            // into omni.ja but we use a temp file.
            let base = Services.io.newURI(`jar:file:${xpi.path}!/`);
            let resProto = Services.io
              .getProtocolHandler("resource")
              .QueryInterface(Ci.nsIResProtocolHandler);
            resProto.setSubstitution("ext-test", base);
            return AddonManager.installBuiltinAddon("resource://ext-test/");
        }

        const addon_file_path = arguments[0];
        const cb = arguments[1];
        const { AddonManager } = ChromeUtils.importESModule(
            "resource://gre/modules/AddonManager.sys.mjs"
        );
        const { ExtensionPermissions } = ChromeUtils.importESModule(
            "resource://gre/modules/ExtensionPermissions.sys.mjs"
        );
        const { FileUtils } = ChromeUtils.importESModule(
            "resource://gre/modules/FileUtils.sys.mjs"
        );
        const file = new FileUtils.File(arguments[0]);
        installAsBuiltinExtension(file).then(addon => {
            // also make sure the addon works in private browsing mode
            const incognitoPermission = {
                permissions: ["internal:privateBrowsingAllowed"],
                origins: [],
            };
            ExtensionPermissions.add(addon.id, incognitoPermission).then(() => {
                addon.reload().then(cb);
            });
        });
        """,
        [addon_file_path],
    )
    session.send_session_command("POST", "moz/context", {"context": context})


@pytest.fixture(scope="function")
async def session(driver, request, test_config):
    caps = driver.capabilities(request, test_config)
    caps.update({
        "acceptInsecureCerts": True,
        "webSocketUrl": True,
        "unhandledPromptBehavior": "dismiss",
    })
    caps = {"alwaysMatch": caps}
    print(caps)

    session = None
    for i in range(0, 15):
        try:
            if not session:
                session = webdriver.Session(
                    "localhost", driver.port, capabilities=caps, enable_bidi=True
                )
                session.test_config = test_config
            session.start()
            break
        except (ConnectionRefusedError, webdriver.error.TimeoutException):
            await asyncio.sleep(0.5)

    try:
        await session.bidi_session.start()
    except AttributeError:
        sys.exit("Could not start a WebDriver session; please try again")

    if driver.addon:
        install_addon(session, driver.addon)

    yield session

    await session.bidi_session.end()
    try:
        session.end()
    except webdriver.error.UnknownErrorException:
        pass


@pytest.fixture(autouse=True)
def firefox_version(session):
    raw = session.capabilities["browserVersion"]
    clean = re.findall(r"(\d+(\.\d+)?)", raw)[0][0]
    return float(clean)


@pytest.fixture(autouse=True)
def platform(request, session, test_config):
    return (
        request.config.getoption("platform_override")
        or session.capabilities["platformName"]
    )


@pytest.fixture(autouse=True)
def channel(session):
    ver = session.capabilities["browserVersion"]
    if "a" in ver:
        return "nightly"
    elif "b" in ver:
        return "beta"
    elif "esr" in ver:
        return "esr"
    return "stable"


@pytest.fixture(autouse=True)
def check_visible_scrollbars(session):
    plat = session.capabilities["platformName"]
    if plat == "android":
        return "Android does not have visible scrollbars"
    elif plat == "mac":
        cmd = ["defaults", "read", "-g", "AppleShowScrollBars"]
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        p.wait()
        if "Always" in str(p.stdout.readline()):
            return None
        return "scrollbars are not set to always be visible in MacOS system preferences"
    return None


@pytest.fixture(autouse=True)
def need_visible_scrollbars(bug_number, check_visible_scrollbars, request, session):
    if request.node.get_closest_marker("need_visible_scrollbars"):
        if (
            request.node.get_closest_marker("need_visible_scrollbars")
            and check_visible_scrollbars
        ):
            pytest.skip(f"Bug #{bug_number} skipped: {check_visible_scrollbars}")


@pytest.fixture(autouse=True)
def only_firefox_versions(bug_number, firefox_version, request):
    if request.node.get_closest_marker("only_firefox_versions"):
        kwargs = request.node.get_closest_marker("only_firefox_versions").kwargs

        min = float(kwargs["min"]) if "min" in kwargs else 0.0
        if firefox_version < min:
            pytest.skip(
                f"Bug #{bug_number} skipped on this Firefox version ({firefox_version} < {min})"
            ) @ pytest.fixture(autouse=True)

        if "max" in kwargs:
            max = kwargs["max"]

            # if we don't care about the minor version, ignore it
            bad = False
            if isinstance(max, float):
                bad = firefox_version > max
            else:
                bad = math.floor(firefox_version) > max

            if bad:
                pytest.skip(
                    f"Bug #{bug_number} skipped on this Firefox version ({firefox_version} > {max})"
                ) @ pytest.fixture(autouse=True)


@pytest.fixture(autouse=True)
def only_platforms(bug_number, platform, request, session):
    is_fenix = "org.mozilla.fenix" in session.capabilities.get("moz:profile", "")
    is_gve = "org.mozilla.geckoview_example" in session.capabilities.get(
        "moz:profile", ""
    )
    actualPlatform = session.capabilities["platformName"]
    actualPlatformRequired = request.node.get_closest_marker("actual_platform_required")
    if actualPlatformRequired and request.config.getoption("platform_override"):
        pytest.skip(
            f"Bug #{bug_number} skipped; needs to be run on the actual platform, won't work while overriding"
        )
    is_desktop = platform in ["linux", "mac", "windows"]
    if request.node.get_closest_marker("only_platforms"):
        plats = request.node.get_closest_marker("only_platforms").args
        for only in plats:
            if (
                only == platform
                or (only == "android" and not is_desktop)
                or (only == "desktop" and is_desktop)
                or (only == "fenix" and is_fenix)
                or (only == "gve" and is_gve)
            ):
                if actualPlatform == platform or not actualPlatformRequired:
                    return
        pytest.skip(
            f"Bug #{bug_number} skipped on platform ({platform}, test only for {' or '.join(plats)})"
        )


@pytest.fixture(autouse=True)
def skip_platforms(bug_number, platform, request, session):
    is_fenix = "org.mozilla.fenix" in session.capabilities.get("moz:profile", "")
    is_gve = "org.mozilla.geckoview_example" in session.capabilities.get(
        "moz:profile", ""
    )
    is_desktop = platform in ["linux", "mac", "windows"]
    if request.node.get_closest_marker("skip_platforms"):
        plats = request.node.get_closest_marker("skip_platforms").args
        for skipped in plats:
            if (
                skipped == platform
                or (skipped == "android" and not is_desktop)
                or (skipped == "desktop" and is_desktop)
                or (skipped == "fenix" and is_fenix)
                or (skipped == "gve" and is_gve)
            ):
                pytest.skip(
                    f"Bug #{bug_number} skipped on platform ({platform}, test skipped for {' and '.join(plats)})"
                )


@pytest.fixture(autouse=True)
def only_channels(bug_number, channel, request, session):
    if request.node.get_closest_marker("only_channels"):
        channels = request.node.get_closest_marker("only_channels").args
        for only in channels:
            if only == channel:
                return
        pytest.skip(
            f"Bug #{bug_number} skipped on channel ({channel}, test only for {' or '.join(channels)})"
        )


@pytest.fixture(autouse=True)
def skip_channels(bug_number, channel, request, session):
    if request.node.get_closest_marker("skip_channels"):
        channels = request.node.get_closest_marker("skip_channels").args
        for skipped in channels:
            if skipped == channel:
                pytest.skip(
                    f"Bug #{bug_number} skipped on channel ({channel}, test skipped for {' and '.join(channels)})"
                )
