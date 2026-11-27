# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/

# ALL CHANGES TO THIS FILE MUST HAVE REVIEW FROM A MARIONETTE PEER!
#
# Please refer to INSTRUCTIONS TO ADD A NEW PREFERENCE in
# remote/shared/RecommendedPreferences.sys.mjs
#
# The Marionette Python client is used out-of-tree with various builds of
# Firefox. Removing a preference from this file will cause regressions,
# so please be careful and get review from a Testing :: Marionette peer
# before you make any changes to this file.

import codecs
import io
import json
import os
import shutil
import signal
import sys
import tempfile
import time
import traceback
from copy import deepcopy

import mozversion
from mozprofile import Profile
from mozrunner import FennecEmulatorRunner, Runner

from . import errors

if sys.platform.startswith("darwin"):
    # Marionette's own processhandler is only used on MacOS for now
    from .processhandler import UNKNOWN_RETURNCODE, ProcessHandler


class GeckoInstance:
    # Force-kill instead of mozprocess' SIGTERM-first fallback,
    # which Firefox on Linux handles as a clean shutdown.
    kill_signal = getattr(signal, "SIGKILL", None)

    required_prefs = {
        # Make sure Shield doesn't hit the network.
        "app.normandy.api_url": "",
        # Disable scroll axis lock, WebDriver should be able to scroll arbitrary
        # directions.
        "apz.axis_lock.mode": 0,
        # Increase the APZ content response timeout in tests to 1 minute.
        # This is to accommodate the fact that test environments tends to be slower
        # than production environments (with the b2g emulator being the slowest of them
        # all), resulting in the production timeout value sometimes being exceeded
        # and causing false-positive test failures. See bug 1176798, bug 1177018,
        # bug 1210465.
        "apz.content_response_timeout": 60000,
        # Use zero movement tolerance before a touch is treated as a pan, so that in
        # automation touch events scroll from exactly the position they are dispatched
        # without small movements first being absorbed.
        "apz.touch_move_tolerance": "0.0",
        "apz.touch_start_tolerance": "0.0",
        # Disable extension discovery
        "browser.discovery.enabled": False,
        # Make sure error page is not shown for blank pages with 4xx or 5xx response code
        "browser.http.blank_page_with_error_response.enabled": True,
        # Disable CFR features for automated tests.
        "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features": False,
        # Don't pull sponsored Top Sites content from the network
        "browser.newtabpage.activity-stream.showSponsoredTopSites": False,
        # Disable geolocation ping (#1)
        "browser.region.network.url": "",
        # Don't pull Top Sites content from the network
        "browser.topsites.contile.enabled": False,
        # Disable translations
        "browser.translations.enable": False,
        # Disable UI tour
        "browser.uitour.enabled": False,
        # Defensively disable data reporting systems
        "datareporting.healthreport.documentServerURI": "http://%(server)s/dummy/healthreport/",
        "datareporting.healthreport.logging.consoleEnabled": False,
        "datareporting.healthreport.service.enabled": False,
        "datareporting.healthreport.service.firstRun": False,
        "datareporting.healthreport.uploadEnabled": False,
        "datareporting.usage.uploadEnabled": False,
        "telemetry.fog.test.localhost_port": -1,
        # Do not show datareporting policy notifications which can interfere with tests
        "datareporting.policy.dataSubmissionEnabled": False,
        "datareporting.policy.dataSubmissionPolicyBypassNotification": True,
        # Disable popup-blocker
        "dom.disable_open_during_load": False,
        # Enabling the support for File object creation in the content process.
        "dom.file.createInChild": True,
        # Disable delayed user input event handling
        "dom.input_events.security.minNumTicks": 0,
        # Disable delayed user input event handling
        "dom.input_events.security.minTimeElapsedInMS": 0,
        # Disable the ProcessHangMonitor
        "dom.ipc.reportProcessHangs": False,
        # No slow script dialogs
        "dom.max_chrome_script_run_time": 0,
        "dom.max_script_run_time": 0,
        # Disable navigation change rate limitation
        "dom.navigation.navigationRateLimit.count": 0,
        # Disable system permission checks for navigator.permissions.query
        "dom.permissions.testing.enabled": True,
        # DOM Push
        "dom.push.connection.enabled": False,
        # Screen Orientation API
        "dom.screenorientation.allow-lock": True,
        # Disable dialog abuse if alerts are triggered too quickly
        "dom.successive_dialog_time_limit": 0,
        # Only load extensions from the application and user profile
        # AddonManager.SCOPE_PROFILE + AddonManager.SCOPE_APPLICATION
        "extensions.autoDisableScopes": 0,
        "extensions.enabledScopes": 5,
        # Disable form autofill for extensions and credit cards
        "extensions.formautofill.addresses.enabled": False,
        "extensions.formautofill.creditCards.enabled": False,
        # Disable metadata caching for installed add-ons by default
        "extensions.getAddons.cache.enabled": False,
        # Disable intalling any distribution add-ons
        "extensions.installDistroAddons": False,
        # Turn off extension updates so they don't bother tests
        "extensions.update.enabled": False,
        "extensions.update.notifyUser": False,
        # Redirect various extension update URLs
        "extensions.blocklist.detailsURL": (
            "http://%(server)s/extensions-dummy/blocklistDetailsURL"
        ),
        "extensions.blocklist.itemURL": "http://%(server)s/extensions-dummy/blocklistItemURL",
        "extensions.hotfix.url": "http://%(server)s/extensions-dummy/hotfixURL",
        "extensions.systemAddon.update.enabled": False,
        "extensions.update.background.url": (
            "http://%(server)s/extensions-dummy/updateBackgroundURL"
        ),
        "extensions.update.url": "http://%(server)s/extensions-dummy/updateURL",
        # Make sure opening about:addons won"t hit the network
        "extensions.getAddons.discovery.api_url": "data:, ",
        "extensions.getAddons.get.url": "http://%(server)s/extensions-dummy/repositoryGetURL",
        "extensions.getAddons.search.browseURL": (
            "http://%(server)s/extensions-dummy/repositoryBrowseURL"
        ),
        # Allow the application to have focus even it runs in the background
        "focusmanager.testmode": True,
        # Disable useragent updates
        "general.useragent.updates.enabled": False,
        # Do not open system settings when geolocation is requested without OS permission
        "geo.prompt.open_system_prefs": False,
        # Disable geolocation ping (#2)
        "geo.provider.network.url": "",
        # Always use network provider for geolocation tests
        # so we bypass the OSX dialog raised by the corelocation provider
        "geo.provider.testing": True,
        # Do not scan Wifi
        "geo.wifi.scan": False,
        # Ensure webrender is on, no need for environment variables
        "gfx.webrender.all": True,
        # Disable idle-daily notifications to avoid expensive operations
        # that may cause unexpected test timeouts.
        "idle.lastDailyNotification": -1,
        # Disable Firefox accounts ping
        "identity.fxaccounts.auth.uri": "https://{server}/dummy/fxa",
        # Disable download and usage of OpenH264, and Widevine plugins
        "media.gmp-manager.updateEnabled": False,
        # Disable the GFX sanity window
        "media.sanity-test.disabled": True,
        "media.volume_scale": "0.01",
        # Allow scroll amount larger than one page on a single mouse wheel
        # event.
        "mousewheel.allow_scrolling_more_than_one_page": True,
        # Disable captive portal
        "network.captive-portal-service.enabled": False,
        # Disable connectivity service pings
        "network.connectivity-service.enabled": False,
        # Do not prompt for temporary redirects
        "network.http.prompt-temp-redirect": False,
        # Do not automatically switch between offline and online
        "network.manage-offline-status": False,
        # Make sure SNTP requests don't hit the network
        "network.sntp.pools": "%(server)s",
        # Disabled for causing marionette crashes on OSX. See bug 1882856
        "network.dns.native_https_query": False,
        # Privacy and Tracking Protection
        "privacy.trackingprotection.enabled": False,
        "privacy.trackingprotection.pbmode.enabled": False,
        # Disable recommended automation prefs in CI
        "remote.prefs.recommended": False,
        # Don't do network connections for mitm priming
        "security.certerrors.mitm.priming.enabled": False,
        # Tests don't wait for the notification button security delay
        "security.notification_enable_delay": 0,
        # Do not download intermediate certificates
        "security.remote_settings.intermediates.enabled": False,
        # Disable logging for remote settings
        "services.settings.loglevel": "off",
        # Disable the WebAuthn consents prompt
        "security.webauthn.related_origin_requests_mode": 1,
        # Ensure blocklist updates don't hit the network
        "services.settings.server": "data:,#remote-settings-dummy/v1",
        # Disable password capture, so that tests that include forms aren"t
        # influenced by the presence of the persistent doorhanger notification
        "signon.rememberSignons": False,
        # Disable alerts for credential issues
        "signon.management.page.breach-alerts.enabled": False,
        "signon.management.page.vulnerable-passwords.enabled": False,
        # Prevent starting into safe mode after application crashes
        # Do not show TOU new user modal which can interfere with tests
        "termsofuse.bypassNotification": True,
        "toolkit.startup.max_resumed_crashes": -1,
        # Disable most telemetry pings
        "toolkit.telemetry.server": "https://%(server)s/telemetry-dummy/",
        # Disable window occlusion on Windows, see Bug 1802473.
        "widget.windows.window_occlusion_tracking.enabled": False,
    }

    _appdata_tmpdir = None

    def __init__(
        self,
        host=None,
        port=None,
        bin=None,
        profile=None,
        addons=None,
        app_args=None,
        debugger_info=None,
        symbols_path=None,
        gecko_log=None,
        prefs=None,
        workspace=None,
        verbose=0,
        headless=False,
    ):
        self.runner_class = Runner
        self.app_args = app_args or []
        self.debugger_info = debugger_info
        self.runner = None
        self.symbols_path = symbols_path
        self.binary = bin

        self.marionette_host = host
        self.marionette_port = port
        self.addons = addons
        self.prefs = prefs
        self.required_prefs = deepcopy(self.required_prefs)
        if prefs:
            self.required_prefs.update(prefs)

        self._gecko_log_option = gecko_log
        self._gecko_log = None
        self.verbose = verbose
        self.headless = headless

        # keep track of errors to decide whether instance is unresponsive
        self.unresponsive_count = 0

        # Alternative to default temporary directory
        self.workspace = workspace

        # Don't use the 'profile' property here, because sub-classes could add
        # further preferences and data, which would not be included in the new
        # profile
        self._profile = profile

    @property
    def gecko_log(self):
        if self._gecko_log:
            return self._gecko_log

        path = self._gecko_log_option
        if path != "-":
            if path is None:
                path = "gecko.log"
            elif os.path.isdir(path):
                fname = f"gecko-{time.time()}.log"
                path = os.path.join(path, fname)

            path = os.path.realpath(path)
            if os.access(path, os.F_OK):
                os.remove(path)

        self._gecko_log = path
        return self._gecko_log

    @property
    def profile(self):
        return self._profile

    @profile.setter
    def profile(self, value):
        self._update_profile(value)

    def _update_profile(self, profile=None, profile_name=None):
        """Check if the profile has to be created, or replaced.

        :param profile: A Profile instance to be used.
        :param name: Profile name to be used in the path.
        """
        if self.runner and self.runner.is_running():
            raise errors.MarionetteException(
                "The current profile can only be updated "
                "when the instance is not running"
            )

        if isinstance(profile, Profile):
            # Only replace the profile if it is not the current one
            if hasattr(self, "_profile") and profile is self._profile:
                return

        else:
            profile_args = self.profile_args
            profile_path = profile

            # If a path to a profile is given then clone it
            if isinstance(profile_path, str):
                profile_args["path_from"] = profile_path
                profile_args["path_to"] = tempfile.mkdtemp(
                    suffix=f".{profile_name or os.path.basename(profile_path)}",
                    dir=self.workspace,
                )
                # The target must not exist yet
                os.rmdir(profile_args["path_to"])

                profile = Profile.clone(**profile_args)

            # Otherwise create a new profile
            else:
                profile_args["profile"] = tempfile.mkdtemp(
                    suffix=".{}".format(profile_name or "mozrunner"),
                    dir=self.workspace,
                )
                profile = Profile(**profile_args)
                profile.create_new = True

        if isinstance(self.profile, Profile):
            self.profile.cleanup()

        self._profile = profile

    def switch_profile(self, profile_name=None, clone_from=None):
        """Switch the profile by using the given name, and optionally clone it.

        Compared to :attr:`profile` this method allows to switch the profile
        by giving control over the profile name as used for the new profile. It
        also always creates a new blank profile, or as clone of an existent one.

        :param profile_name: Optional, name of the profile, which will be used
            as part of the profile path (folder name containing the profile).
        :clone_from: Optional, if specified the new profile will be cloned
            based on the given profile. This argument can be an instance of
            ``mozprofile.Profile``, or the path of the profile.
        """
        if isinstance(clone_from, Profile):
            clone_from = clone_from.profile

        self._update_profile(clone_from, profile_name=profile_name)

    @property
    def profile_args(self):
        args = {"preferences": deepcopy(self.required_prefs)}
        args["preferences"]["marionette.port"] = self.marionette_port
        args["preferences"]["marionette.defaultPrefs.port"] = self.marionette_port

        if self.prefs:
            args["preferences"].update(self.prefs)

        if self.verbose:
            level = "Trace" if self.verbose >= 2 else "Debug"
            args["preferences"]["remote.log.level"] = level

        if "-jsdebugger" in self.app_args:
            args["preferences"].update({
                "devtools.browsertoolbox.panel": "jsdebugger",
                "devtools.chrome.enabled": True,
                "devtools.debugger.prompt-connection": False,
                "devtools.debugger.remote-enabled": True,
                "devtools.testing": True,
            })

        if self.addons:
            args["addons"] = self.addons

        return args

    @classmethod
    def create(cls, app=None, *args, **kwargs):
        try:
            if not app and kwargs["bin"] is not None:
                app_id = mozversion.get_version(binary=kwargs["bin"])["application_id"]
                app = app_ids[app_id]

            instance_class = apps[app]
        except (OSError, KeyError):
            exc, val, tb = sys.exc_info()
            msg = f'Application "{app}" unknown (should be one of {list(apps.keys())})'
            raise NotImplementedError(msg).with_traceback(tb)

        return instance_class(*args, **kwargs)

    def start(self):
        self._update_profile(self.profile)
        self.runner = self.runner_class(**self._get_runner_args())

        # debugger information
        debug_args = None
        interactive = False

        if self.debugger_info:
            debug_args = [self.debugger_info.path] + self.debugger_info.args
            interactive = self.debugger_info.interactive

        self.runner.start(debug_args, interactive)

    def _get_runner_args(self):
        process_args = {
            "processOutputLine": [NullOutput()],
            "storeOutput": False,
            "universal_newlines": True,
        }

        if self.gecko_log == "-":
            if getattr(sys.stdout, "encoding") == "utf-8":
                process_args["stream"] = sys.stdout
            elif hasattr(sys.stdout, "buffer"):
                process_args["stream"] = codecs.getwriter("utf-8")(sys.stdout.buffer)
            elif isinstance(sys.stdout, io.TextIOBase):
                # If sys.stdout expects unicode strings, we can't wrap it because the
                # wrapper will write byte strings. This can happen when e.g. tests
                # replace sys.stdout with a io.StringIO().
                process_args["stream"] = sys.stdout
            else:
                process_args["stream"] = codecs.getwriter("utf-8")(sys.stdout)
        else:
            process_args["logfile"] = self.gecko_log

        env = os.environ.copy()

        # Store all required preferences for tests which need to create clean profiles.
        required_prefs_keys = list(self.required_prefs.keys())
        env["MOZ_MARIONETTE_REQUIRED_PREFS"] = json.dumps(required_prefs_keys)

        if self.headless:
            env["MOZ_HEADLESS"] = "1"
            env["DISPLAY"] = "77"  # Set a fake display.

        # environment variables needed for crashreporting
        # https://developer.mozilla.org/docs/Environment_variables_affecting_crash_reporting
        env.update({
            "MOZ_CRASHREPORTER": "1",
            "MOZ_CRASHREPORTER_NO_REPORT": "1",
            "MOZ_CRASHREPORTER_SHUTDOWN": "1",
        })

        if "MOZ_APP_DATA" not in env:
            if not self._appdata_tmpdir:
                self._appdata_tmpdir = tempfile.mkdtemp(
                    suffix=".moz-appdata", dir=self.workspace
                )
            env["MOZ_APP_DATA"] = os.path.normpath(
                os.path.join(self._appdata_tmpdir, "AppData", "Roaming")
            )
            env["MOZ_LOCAL_APP_DATA"] = os.path.normpath(
                os.path.join(self._appdata_tmpdir, "Local")
            )

        # Default to allow system access unless it is already set.
        if env.get("MOZ_REMOTE_ALLOW_SYSTEM_ACCESS") is None:
            env.update({"MOZ_REMOTE_ALLOW_SYSTEM_ACCESS": "1"})

        args = {
            "binary": self.binary,
            "profile": self.profile,
            "cmdargs": self.app_args + ["-marionette"],
            "env": env,
            "symbols_path": self.symbols_path,
            "process_args": process_args,
        }

        if sys.platform.startswith("darwin"):
            # Bug 1887666: The custom process handler class for Marionette is
            # only supported on MacOS at the moment.
            args["process_class"] = ProcessHandler

        return args

    def close(self, clean=False):
        """
        Kill the managed Gecko process.

        Depending on self.runner_class, setting `clean` to True may also kill
        the emulator process in which this instance is running.

        :param clean: If True, also perform runner cleanup.
        """
        if self.runner:
            self.runner.stop(sig=self.kill_signal)
            if clean:
                self.runner.cleanup()

        if clean:
            if isinstance(self.profile, Profile):
                self.profile.cleanup()
            self.profile = None

    def __del__(self):
        if self._appdata_tmpdir and os.path.exists(self._appdata_tmpdir):
            shutil.rmtree(self._appdata_tmpdir, ignore_errors=True)
            self._appdata_tmpdir = None

    def restart(self, prefs=None, clean=True):
        """
        Close then start the managed Gecko process.

        :param prefs: Dictionary of preference names and values.
        :param clean: If True, reset the profile before starting.
        """
        if prefs:
            self.prefs = prefs
        else:
            self.prefs = None

        self.close(clean=clean)
        self.start()

    def update_process(self, pid, timeout=None):
        """Update the process to track when the application re-launched itself"""
        if sys.platform.startswith("darwin"):
            # The new process handler is only supported on MacOS yet
            returncode = self.runner.process_handler.update_process(pid, timeout)
            if returncode not in [0, UNKNOWN_RETURNCODE]:
                raise OSError(
                    f"Old process inappropriately quit with exit code: {returncode}"
                )

        else:
            returncode = self.runner.process_handler.check_for_detached(pid)


class FennecInstance(GeckoInstance):
    # Without a signal, the device runner stops the application through the
    # activity manager, which force-stops it.
    kill_signal = None

    fennec_prefs = {
        # Enable output for dump() and chrome console API
        "browser.dom.window.dump.enabled": True,
        "devtools.console.stdout.chrome": True,
        # Disable safe browsing / tracking protection updates
        "browser.safebrowsing.update.enabled": False,
        # Do not restore the last open set of tabs if the browser has crashed
        "browser.sessionstore.resume_from_crash": False,
    }

    def __init__(
        self,
        emulator_binary=None,
        avd_home=None,
        avd=None,
        adb_path=None,
        serial=None,
        connect_to_running_emulator=False,
        package_name=None,
        env=None,
        *args,
        **kwargs,
    ):
        required_prefs = deepcopy(FennecInstance.fennec_prefs)
        required_prefs.update(kwargs.get("prefs", {}))

        super().__init__(*args, **kwargs)
        self.required_prefs.update(required_prefs)

        self.runner_class = FennecEmulatorRunner
        # runner args
        self._package_name = package_name
        self.emulator_binary = emulator_binary
        self.avd_home = avd_home
        self.adb_path = adb_path
        self.avd = avd
        self.env = env
        self.serial = serial
        self.connect_to_running_emulator = connect_to_running_emulator

    @property
    def package_name(self):
        """
        Name of app to run on emulator.

        Note that FennecInstance does not use self.binary
        """
        if self._package_name is None:
            self._package_name = "org.mozilla.fennec"
            user = os.getenv("USER")
            if user:
                self._package_name += "_" + user
        return self._package_name

    def start(self):
        self._update_profile(self.profile)
        self.runner = self.runner_class(**self._get_runner_args())
        try:
            if self.connect_to_running_emulator:
                self.runner.device.connect()
            self.runner.start()
        except Exception:
            exc_cls, exc, tb = sys.exc_info()
            raise exc_cls(
                f"Error possibly due to runner or device args: {exc}"
            ).with_traceback(tb)

        # forward marionette port
        self.runner.device.device.forward(
            local=f"tcp:{self.marionette_port}",
            remote=f"tcp:{self.marionette_port}",
        )

    def _get_runner_args(self):
        process_args = {
            "processOutputLine": [NullOutput()],
            "storeOutput": False,
            "universal_newlines": True,
        }

        env = {} if self.env is None else self.env.copy()

        runner_args = {
            "app": self.package_name,
            "avd_home": self.avd_home,
            "adb_path": self.adb_path,
            "binary": self.emulator_binary,
            "env": env,
            "profile": self.profile,
            "cmdargs": ["-marionette"] + self.app_args,
            "symbols_path": self.symbols_path,
            "process_args": process_args,
            "logdir": self.workspace or os.getcwd(),
            "serial": self.serial,
        }
        if self.avd:
            runner_args["avd"] = self.avd

        return runner_args

    def close(self, clean=False):
        """
        Kill the managed Gecko process.

        If `clean` is True and the Fennec instance is running in an
        emulator managed by mozrunner, this will stop the emulator.

        :param clean: If True, also perform runner cleanup.
        """
        super().close(clean)
        if clean and self.runner and self.runner.device.connected:
            try:
                self.runner.device.device.remove_forwards(f"tcp:{self.marionette_port}")
                self.unresponsive_count = 0
            except Exception:
                self.unresponsive_count += 1
                traceback.print_exception(*sys.exc_info())


class DesktopInstance(GeckoInstance):
    desktop_prefs = {
        # Disable Firefox old build background check
        "app.update.checkInstallTime": False,
        # Disable automatically upgrading Firefox
        #
        # Note: Possible update tests could reset or flip the value to allow
        # updates to be downloaded and applied.
        "app.update.disabledForTesting": True,
        # !!! For backward compatibility up to Firefox 64. Only remove
        # when this Firefox version is no longer supported by the client !!!
        "app.update.auto": False,
        # Disable the profile backup service.
        "browser.backup.enabled": False,
        # Enable output for dump() and chrome console API
        "browser.dom.window.dump.enabled": True,
        "devtools.console.stdout.chrome": True,
        # Don't open the downloads panel every time a download begins.
        # The first download ever run in a new profile will still open the panel,
        # but because "browser.download.panel.shown" is set to true,
        # this preference is going to act as the first download already happened.
        "browser.download.focusPanelOnOpen": False,
        # Indicate that the download panel has been shown once so that whichever
        # download test runs first doesn"t show the popup inconsistently
        "browser.download.panel.shown": True,
        # Do not show the EULA notification which can interfer with tests
        "browser.EULA.override": True,
        # Disable all machine learning features by default
        "browser.ml.enable": False,
        # Do not initialize any activitystream features
        "browser.newtabpage.activity-stream.testing.shouldInitializeFeeds": False,
        # Do not render or load any New Tab widget
        "browser.newtabpage.activity-stream.widgets.enabled": False,
        # Background thumbnails in particular cause grief, and disabling thumbnails
        # in general can"t hurt - we re-enable them when tests need them
        "browser.pagethumbnails.capturing_disabled": True,
        # Do not show the preonboarding modal/splash which can interfere with tests
        "browser.preonboarding.enabled": False,
        # Disable safe browsing / tracking protection updates
        "browser.safebrowsing.update.enabled": False,
        # Disable updates to search engines
        "browser.search.update": False,
        # Do not restore the last open set of tabs if the browser has crashed
        "browser.sessionstore.resume_from_crash": False,
        # Don't check for the default web browser during startup
        "browser.shell.checkDefaultBrowser": False,
        # Disable session restore infobar
        "browser.startup.couldRestoreSession.count": -1,
        # Needed for branded builds to prevent opening a second tab on startup
        "browser.startup.homepage_override.mstone": "ignore",
        # Start with a blank page by default
        "browser.startup.page": 0,
        # Unload the previously selected tab immediately
        "browser.tabs.remote.unloadDelayMs": 0,
        # Don't unload tabs when available memory is running low
        "browser.tabs.unloadOnLowMemory": False,
        # Do not warn when closing all open tabs
        "browser.tabs.warnOnClose": False,
        # Do not warn when closing all other open tabs
        "browser.tabs.warnOnCloseOtherTabs": False,
        # Do not warn when multiple tabs will be opened
        "browser.tabs.warnOnOpen": False,
        # Don't show the Bookmarks Toolbar on any tab (the above pref that
        # disables the New Tab Page ends up showing the toolbar on about:blank).
        "browser.toolbars.bookmarks.visibility": "never",
        # Disable the UI tour
        "browser.uitour.enabled": False,
        # Turn off Merino suggestions in the location bar so as not to trigger network
        # connections.
        "browser.urlbar.merino.endpointURL": "",
        "browser.urlbar.merino.ohttpConfigURL": "",
        "browser.urlbar.merino.ohttpRelayURL": "",
        # Turn off search suggestions in the location bar so as not to trigger network
        # connections.
        "browser.urlbar.suggest.searches": False,
        # Don't warn when exiting the browser
        "browser.warnOnQuit": False,
        # Disable the QoS manager on MacOS and the priority manager on all other
        # platforms to not cause stalled processes in background tabs when the
        # overall CPU load on the machine is high.
        #
        # TODO: Should be considered to get removed once bug 1960741 is fixed.
        "threads.lower_mainthread_priority_in_background.enabled": False,
        "dom.ipc.processPriorityManager.enabled": False,
        # Turn off semantic history search as it triggers network connections to
        # download ML models.
        "places.semanticHistory.featureGate": False,
        # Disable first-run welcome page
        "startup.homepage_welcome_url": "about:blank",
        "startup.homepage_welcome_url.additional": "",
    }

    def __init__(self, *args, **kwargs):
        required_prefs = deepcopy(DesktopInstance.desktop_prefs)
        required_prefs.update(kwargs.get("prefs", {}))

        super().__init__(*args, **kwargs)
        self.required_prefs.update(required_prefs)


class ThunderbirdInstance(GeckoInstance):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        try:
            # Copied alongside in the test archive
            from .thunderbirdinstance import thunderbird_prefs
        except ImportError:
            try:
                # Directly from the source tree
                here = os.path.dirname(__file__)
                sys.path.append(
                    os.path.join(here, "../../../../comm/testing/marionette")
                )
                from thunderbirdinstance import thunderbird_prefs
            except ImportError:
                thunderbird_prefs = {}
        self.required_prefs.update(thunderbird_prefs)


class NullOutput:
    def __call__(self, line):
        pass


apps = {
    "fennec": FennecInstance,
    "fxdesktop": DesktopInstance,
    "thunderbird": ThunderbirdInstance,
}

app_ids = {
    "{aa3c5121-dab2-40e2-81ca-7ea25febc110}": "fennec",
    "{ec8030f7-c20a-464f-9b0e-13a3a9e97384}": "fxdesktop",
    "{3550f703-e582-4d05-9a08-453d09bdfdc6}": "thunderbird",
}
