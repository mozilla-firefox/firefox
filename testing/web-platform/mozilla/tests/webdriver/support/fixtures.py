import os
import subprocess
from copy import deepcopy

import pytest
import pytest_asyncio
import tests.support.fixtures as global_fixtures
from mozdevice import ADBDeviceFactory
from tests.support import defaults

from .chrome_handler import using_chrome_handler
from .context import using_context
from .helpers import (
    Browser,
    Geckodriver,
    create_custom_profile,
    get_profile_folder,
    read_user_preferences,
)

_geckodriver_state = {"current": None}


def pytest_configure(config):
    config.addinivalue_line(
        "markers",
        "geckodriver(allow_system_access=False, force_new=False): "
        "Configure the geckodriver instance for the test",
    )


@pytest.fixture(scope="module")
def browser(configuration, firefox_options):
    """Start a Firefox instance without using geckodriver.

    geckodriver will automatically use the --remote-allow-hosts and
    --remote.allow.origins command line arguments.

    Starting Firefox without geckodriver allows to set those command line arguments
    as needed. The fixture method returns the browser instance that should be used
    to connect to a RemoteAgent supported protocol (WebDriver BiDi).
    """
    current_browser = None

    def _browser(
        clone_profile=True,
        extra_args=None,
        extra_prefs=None,
        use_bidi=False,
        use_marionette=False,
    ):
        nonlocal current_browser

        webdriver_args = configuration["webdriver"]["args"]
        log_level = None
        truncate_enabled = True

        if "-vvv" in webdriver_args:
            log_level = "Trace"
            truncate_enabled = False
        elif "-vv" in webdriver_args:
            log_level = "Trace"
        elif "-v" in webdriver_args:
            log_level = "Debug"

        # If the requested preferences and arguments match the ones for the
        # already started firefox, we can reuse the current firefox instance,
        # return the instance immediately.
        if current_browser:
            if (
                current_browser.extra_args == extra_args
                and current_browser.extra_prefs == extra_prefs
                and current_browser.is_running
                and current_browser.use_bidi == use_bidi
                and current_browser.use_marionette == use_marionette
                and current_browser.log_level == log_level
                and current_browser.truncate_enabled == truncate_enabled
            ):
                return current_browser

            # Otherwise, if firefox is already started, terminate it because we need
            # to create a new instance for the provided preferences.
            current_browser.quit()

        binary = configuration["browser"]["binary"]
        env = configuration["browser"]["env"]

        profile_path = get_profile_folder(firefox_options)
        default_prefs = read_user_preferences(profile_path)
        profile = create_custom_profile(
            profile_path, default_prefs, clone=clone_profile
        )

        current_browser = Browser(
            binary,
            profile,
            extra_args=extra_args,
            extra_prefs=extra_prefs,
            env=env,
            log_level=log_level,
            truncate_enabled=truncate_enabled,
            use_bidi=use_bidi,
            use_marionette=use_marionette,
        )
        current_browser.start()
        return current_browser

    yield _browser

    # Stop firefox at the end of the test module.
    if current_browser is not None:
        current_browser.quit()
        current_browser = None


@pytest.fixture
def default_chrome_handler(current_session):
    manifest_path = os.path.join(
        os.path.abspath(os.path.dirname(__file__)), "chrome-assets", "chrome.manifest"
    )
    entries = [["content", "marionette-chrome", "chrome/"]]

    with using_chrome_handler(current_session, manifest_path, entries):
        yield "chrome://marionette-chrome/content/"


@pytest.fixture
def default_preferences(profile_folder):
    return read_user_preferences(profile_folder)


@pytest.fixture
def new_chrome_window(current_session):
    opened_chrome_windows = []

    def _new_chrome_window(url, focus=True):
        # Bug 1944570: Replace with BiDi once scripts can be evaluated
        # in the parent process.
        with using_context(current_session, "chrome"):
            new_window = current_session.execute_async_script(
                """
                  const { NavigableManager } = ChromeUtils.importESModule(
                    "chrome://remote/content/shared/NavigableManager.sys.mjs"
                  );

                  let [url, focus, resolve] = arguments;

                  function waitForEvent(target, type, args) {
                    return new Promise(resolve => {
                      let params = Object.assign({once: true}, args);
                      target.addEventListener(type, event => {
                        dump(`** Received DOM event ${event.type} for ${event.target}\n`);
                        resolve();
                      }, params);
                    });
                  }

                  function waitForFocus(win) {
                    return Promise.all([
                      waitForEvent(win, "activate"),
                      waitForEvent(win, "focus", {capture: true}),
                    ]);
                  }

                  const isLoaded = window =>
                    window?.document.readyState === "complete" &&
                    !window?.document.isUncommittedInitialDocument;

                  (async function() {
                    // Open a window, wait for it to receive focus
                    let newWindow = window.openDialog(url, null, "chrome,centerscreen");
                    let focused = waitForFocus(newWindow);

                    newWindow.focus();
                    await focused;

                    // The new window shouldn't get focused. As such set the
                    // focus back to the opening window.
                    if (!focus && Services.focus.activeWindow != window) {
                      let focused = waitForFocus(window);
                      window.focus();
                      await focused;
                    }

                    // Wait for the new window to be finished loading
                    if (isLoaded(newWindow)) {
                      resolve(newWindow);
                    } else {
                      const onLoad = () => {
                        if (isLoaded(newWindow)) {
                          newWindow.removeEventListener("load", onLoad);
                          resolve(newWindow);
                        } else {
                          dump(`** Target window not loaded yet.  Waiting for the next "load" event\n`);
                        }
                      };
                      newWindow.addEventListener("load", onLoad);
                    }
                  })();
                """,
                args=[url, focus],
            )

            # Append opened chrome window to automatic closing on teardown
            opened_chrome_windows.append(new_window)
            return new_window

    yield _new_chrome_window

    with using_context(current_session, "chrome"):
        for win in opened_chrome_windows:
            try:
                current_session.window_handle = win.id
                current_session.execute_script("arguments[0].close()", args=[win])
            except Exception:
                pass

    current_session.window_handle = current_session.handles[0]


@pytest.fixture(name="create_custom_profile")
def fixture_create_custom_profile(default_preferences, profile_folder):
    profile = None

    def _create_custom_profile(clone=True):
        profile = create_custom_profile(
            profile_folder, default_preferences, clone=clone
        )

        return profile

    yield _create_custom_profile

    # if profile is not None:
    if profile:
        profile.cleanup()


@pytest.fixture(scope="session")
def firefox_options(configuration):
    return configuration["capabilities"]["moz:firefoxOptions"]


@pytest.fixture(scope="module")
def geckodriver(configuration, firefox_options):
    """Start a geckodriver instance directly."""

    custom_profile = None

    def _geckodriver(
        config=None,
        hostname=None,
        extra_args=None,
        extra_env=None,
        popen_kwargs=None,
        profile=None,
        force_new=False,
    ):
        nonlocal custom_profile

        if config is None:
            config = deepcopy(configuration)

        if extra_args is None:
            extra_args = []

        if extra_env is None:
            extra_env = {}

        # When --profile-root is used, geckodriver manages its own profile.
        uses_profile_root = "--profile-root" in extra_args

        # Use a cloned profile to avoid conflicts with the harness Firefox.
        if not uses_profile_root and profile is None:
            if custom_profile is None:
                profile_path = get_profile_folder(firefox_options)
                default_prefs = read_user_preferences(profile_path)
                custom_profile = create_custom_profile(
                    profile_path, default_prefs, clone=True
                )
            profile = custom_profile

        # Reuse the running geckodriver if the configuration matches.
        current = _geckodriver_state["current"]
        if current is not None:
            if (
                not force_new
                and current.extra_args == extra_args
                and current.extra_env == extra_env
                and current.hostname == (hostname or config["host"])
                and current.proc
                and current.proc.poll() is None
                and (
                    uses_profile_root
                    or (
                        custom_profile is not None
                        and profile.profile == custom_profile.profile
                    )
                )
            ):
                return current

            current.kill()

        if not uses_profile_root:
            fx_options = config["capabilities"].setdefault("moz:firefoxOptions", {})
            fx_args = fx_options.setdefault("args", [])
            if "--profile" in fx_args:
                index = fx_args.index("--profile")
                fx_args[index + 1] = profile.profile
            else:
                fx_args.extend(["--profile", profile.profile])

        # End any active harness session to free up ports and resources
        # before starting the custom geckodriver instance.
        harness_session = global_fixtures.get_current_session()
        if harness_session is not None:
            harness_session.end()
            global_fixtures.set_current_session(None)

        current = Geckodriver(config, hostname, extra_args, extra_env, popen_kwargs)
        current.start()
        _geckodriver_state["current"] = current

        return current

    yield _geckodriver

    current = _geckodriver_state["current"]
    if current is not None:
        current.kill()
        _geckodriver_state["current"] = None

    if custom_profile is not None:
        custom_profile.cleanup()


@pytest.fixture
def is_process_alive(configuration):
    """Check if a process is still running.

    On Android the process is looked up on the device instead of the host.
    """

    android_device = None
    if configuration["target_platform"] == "android":
        android = configuration["android"]
        android_device = ADBDeviceFactory(
            device=android["device_serial"], adb=android["adb_binary"]
        )

    def _is_process_alive(pid):
        if android_device is not None:
            return android_device.is_dir(f"/proc/{pid}")

        if os.name == "nt":
            try:
                output = subprocess.check_output(
                    ["tasklist", "/FI", f"PID eq {pid}"], text=True
                )
                return str(pid) in output
            except subprocess.CalledProcessError:
                return False

        try:
            os.kill(pid, 0)
            return True
        except OSError:
            return False

    return _is_process_alive


@pytest.fixture
def profile_folder(firefox_options):
    return get_profile_folder(firefox_options)


@pytest_asyncio.fixture
async def bidi_session(request, geckodriver):
    """Override bidi_session to use the geckodriver fixture.

    Reads the ``geckodriver`` marker for extra arguments and the
    ``capabilities`` marker for custom session capabilities.
    """
    extra_args, force_new = _get_geckodriver_marker_args(request)
    capabilities = _get_capabilities_marker_args(request)

    driver = geckodriver(extra_args=extra_args, force_new=force_new)

    bidi_capabilities = {"webSocketUrl": True}
    if capabilities is not None:
        bidi_capabilities.update(capabilities)
    driver.new_session(capabilities=bidi_capabilities)

    bidi_session = driver.session.bidi_session

    # Clear any stale transport from a previous event loop (e.g. from a test
    # that used the geckodriver fixture directly to start a bidi session).
    bidi_session.transport = None
    await bidi_session.start()

    if driver.session.capabilities.get("setWindowRect"):
        driver.session.window.size = defaults.WINDOW_SIZE
        driver.session.window.position = defaults.WINDOW_POSITION

    yield bidi_session

    if bidi_session.transport is not None:
        await bidi_session.transport.end()
        bidi_session.transport = None


@pytest.fixture
def session(request, configuration, geckodriver):
    """Override session to use the geckodriver fixture."""
    extra_args, force_new = _get_geckodriver_marker_args(request)
    capabilities = _get_capabilities_marker_args(request)

    driver = geckodriver(extra_args=extra_args, force_new=force_new)

    session_capabilities = {"webSocketUrl": False}
    if capabilities is not None:
        session_capabilities.update(capabilities)
    driver.new_session(capabilities=session_capabilities)

    session = driver.session

    if session.capabilities.get("setWindowRect"):
        session.window.size = defaults.WINDOW_SIZE
        session.window.position = defaults.WINDOW_POSITION

    multiplier = configuration["timeout_multiplier"]
    session.timeouts.implicit = defaults.IMPLICIT_WAIT_TIMEOUT * multiplier
    session.timeouts.page_load = defaults.PAGE_LOAD_TIMEOUT * multiplier
    session.timeouts.script = defaults.SCRIPT_TIMEOUT * multiplier

    yield session


@pytest.fixture
def current_session():
    """Override current_session to return the active geckodriver session."""
    assert _geckodriver_state["current"] is not None, (
        "current_session requires an active geckodriver session. "
        "Use the bidi_session or session fixture first."
    )
    return _geckodriver_state["current"].session


def _get_capabilities_marker_args(request):
    """Read the capabilities marker and return extra capabilities, or None."""
    capabilities_marker = request.node.get_closest_marker("capabilities")
    if capabilities_marker and capabilities_marker.args:
        return capabilities_marker.args[0]

    return None


def _get_geckodriver_marker_args(request):
    """Read the geckodriver marker for extra args and force_new."""
    extra_args = []
    force_new = False

    geckodriver_marker = request.node.get_closest_marker("geckodriver")
    if geckodriver_marker:
        if geckodriver_marker.kwargs.get("allow_system_access"):
            extra_args.append("--allow-system-access")
        force_new = geckodriver_marker.kwargs.get("force_new", False)

    return extra_args, force_new
