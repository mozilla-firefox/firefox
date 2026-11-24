# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Drive Firefox's AI features from Python.

``mozai`` hands Python code a live Firefox and a set of commands for driving the
AI features inside it. This makes it easy for the Python ecosystem, which has
rich ML tooling, to integrate with the Gecko AI Runtime.

Requires Nightly or a local build. Other channels do not ship the driver.

See ``browser/components/ai/driver/python/README.md`` for installing it and a
usage example.
"""

from __future__ import annotations

import logging
import subprocess
from importlib import resources
from pathlib import Path
from typing import Any

from selenium import webdriver

__all__ = ["AIDriver", "DriverVersionError"]

logger = logging.getLogger("mozai")

# Bumped in lockstep with AI_DRIVER_API_VERSION in the in-tree AIDriver.sys.mjs. A build
# reporting a different major version has removed or reshaped commands this client calls.
SUPPORTED_API_VERSION = 1


class DriverVersionError(RuntimeError):
    """The Firefox build speaks a different driver API version than this package."""


def _load_bootstrap_js() -> str:
    """Return the contents of bootstrap.js bundled with the package."""
    return resources.files(__package__).joinpath("bootstrap.js").read_text()


class AIDriver:
    """High-level helper for driving Firefox's AI features via Selenium."""

    driver: webdriver.Firefox
    bootstrap_js: str

    def __init__(
        self,
        headless: bool = False,
        firefox_bin: Path | None = None,
        log_firefox: bool = False,
        prefs: dict[str, Any] | None = None,
        check_version: bool = True,
    ) -> None:
        if not logger.handlers:
            logging.basicConfig(level=logging.INFO, format="[%(name)s] %(message)s")
        logger.setLevel(logging.INFO)

        self.driver = AIDriver.setup_driver(headless, firefox_bin, log_firefox, prefs)
        self.bootstrap_js = _load_bootstrap_js()
        self._commands: set[str] | None = None

        if check_version:
            self._check_version()

    @staticmethod
    def setup_driver(
        headless: bool,
        firefox_bin: Path | None,
        log_firefox: bool,
        prefs: dict[str, Any] | None = None,
    ) -> webdriver.Firefox:
        options = webdriver.FirefoxOptions()
        if headless:
            options.add_argument("-headless")

        if firefox_bin:
            if not firefox_bin.exists():
                raise FileNotFoundError(f"Firefox binary not found: {firefox_bin}")
            options.binary_location = str(firefox_bin)

        service_args = ["--allow-system-access"]
        if log_firefox:
            service_args.append("--log-no-truncate")
        service = webdriver.FirefoxService(
            log_output=subprocess.STDOUT if log_firefox else None,
            service_args=service_args,
        )

        defaults = {
            "browser.ml.enable": True,
            "browser.ml.logLevel": "All",
            "browser.smartwindow.enabled": True,
        }
        if prefs:
            defaults.update(prefs)

        for pref, value in defaults.items():
            options.set_preference(pref, value)

        return webdriver.Firefox(options=options, service=service)

    def _check_version(self) -> None:
        """Fail early and legibly when the build's command surface has moved."""
        described = self.describe()
        api_version = described["apiVersion"]
        self._commands = set(described["commands"])

        if api_version != SUPPORTED_API_VERSION:
            raise DriverVersionError(
                f"This Firefox build uses AI driver API version {api_version}, but "
                f"mozai supports version {SUPPORTED_API_VERSION}. Update Firefox if it "
                "is older than this package, or `pip install -U mozai` if it is newer."
            )

    def _send(self, command: str, *args: Any) -> Any:
        """Forward a command to the in-tree binding layer."""
        if self._commands is not None and command not in self._commands:
            raise DriverVersionError(
                f"This Firefox build does not support {command!r}. It supports: "
                f"{', '.join(sorted(self._commands))}."
            )

        with self.driver.context(self.driver.CONTEXT_CHROME):
            response = self.driver.execute_async_script(
                self.bootstrap_js, command, *args
            )

        response_name = response and response.get("name")
        if response_name == "success":
            return response["result"]

        if response_name == "error":
            error_details = response.get("error") or {}
            message = error_details.get("message") or response.get("error")
            raise RuntimeError(f"{command} failed: {message}")

        raise RuntimeError(f"{command} failed with no response: {response}")

    def _send_after_navigation(self, url: str, command: str, *args: Any) -> Any:
        logger.info("Loading page for %s -> %s", command, url)
        self.driver.get(url)
        self._send("wait_for_page_ready")
        return self._send(command, *args)

    def describe(self) -> dict[str, Any]:
        """Return the build's ``{apiVersion, commands}``, bypassing the command check."""
        with self.driver.context(self.driver.CONTEXT_CHROME):
            response = self.driver.execute_async_script(self.bootstrap_js, "describe")
        if not response or response.get("name") != "success":
            error_details = (response or {}).get("error") or {}
            raise RuntimeError(
                f"describe failed: {error_details.get('message') or response}"
            )
        return response["result"]

    def supports(self, command: str) -> bool:
        """Whether the build this driver is attached to exposes ``command``."""
        if self._commands is None:
            self._commands = set(self.describe()["commands"])
        return command in self._commands

    def get_page_text(
        self, url: str, options: dict[str, Any] | None = None
    ) -> dict[str, Any]:
        """Navigate to a page and extract its text with the PageExtractor."""
        return self._send_after_navigation(url, "get_page_text", options or {})

    def get_page_metadata(self, url: str) -> dict[str, Any]:
        """Navigate to a page and return its PageExtractor metadata."""
        return self._send_after_navigation(url, "get_page_metadata")

    def get_headless_page_text(
        self,
        url: str,
        options: dict[str, Any] | None = None,
        anonymous_fetch: bool = False,
    ) -> dict[str, Any]:
        """Extract text from a page loaded in a hidden browser."""
        return self._send("get_headless_page_text", url, options or {}, anonymous_fetch)

    def init_conversation(self) -> dict[str, Any]:
        """Start a new Smart Window conversation and make it the current one."""
        return self._send("init_conversation")

    def quit(self) -> None:
        """Exit Firefox at the end of a session."""
        self.driver.quit()
