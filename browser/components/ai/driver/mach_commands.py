# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Hook up AIDriver commands for ergonomic running as a CLI via `./mach ai`.

This makes it easy to resolve the mozai import and access its internals.
"""

import json
from pathlib import Path

from mach.decorators import Command, CommandArgument, SubCommand


def _run(command_context, headless, firefox_bin, call):
    """Drive `call` against a Firefox binary and print its result as JSON.

    The `mozai` import is deferred to here so that selenium is only pulled in when a
    Selenium-driving subcommand actually runs (inside the activated "ai" virtualenv).
    """
    from mozai import AIDriver

    binary = (
        Path(firefox_bin)
        if firefox_bin
        else Path(command_context.get_binary_path("app"))
    )

    driver = AIDriver(headless=headless, firefox_bin=binary)
    try:
        print(json.dumps(call(driver), indent=2, ensure_ascii=False))
    finally:
        driver.quit()
    return 0


@Command(
    "ai",
    category="misc",
    description="Drive Firefox's AI features from Python via Selenium.",
)
def ai(command_context):
    """Drive Firefox's AI features from Python via Selenium.

    Each subcommand launches Firefox, runs a single command against it, prints
    the result as JSON, and quits. It needs no `pip install`, so it is the
    quickest way to exercise a command against your local build.

    Commands that hand back a handle for use in later calls are not available
    here, because the browser is gone by the time a subcommand returns. Reach
    those through the `mozai` package, which holds one driver across calls.
    """
    print("Run `./mach ai --help` for the available subcommands.")
    return 0


@SubCommand(
    "ai",
    "describe",
    description="Print the API version and commands this build supports.",
    virtualenv_name="ai",
)
@CommandArgument("--headless", action="store_true", help="Run Firefox headless.")
@CommandArgument(
    "--firefox-bin",
    default=None,
    help="Path to the Firefox binary (defaults to the local build).",
)
def describe(command_context, headless=False, firefox_bin=None):
    """Print the API version and the commands this build supports.

    The command surface lives in the browser rather than in the driver, so the
    answer depends on the Firefox being driven, not on this checkout.
    """
    return _run(command_context, headless, firefox_bin, lambda d: d.describe())


@SubCommand(
    "ai",
    "get-page-text",
    description="Load a URL and print the extracted page text.",
    virtualenv_name="ai",
)
@CommandArgument("url", help="The URL to load and extract.")
@CommandArgument("--headless", action="store_true", help="Run Firefox headless.")
@CommandArgument(
    "--firefox-bin",
    default=None,
    help="Path to the Firefox binary (defaults to the local build).",
)
def get_page_text(command_context, url, headless=False, firefox_bin=None):
    """Load a URL in a tab and print the text PageExtractor extracts from it."""
    return _run(command_context, headless, firefox_bin, lambda d: d.get_page_text(url))


@SubCommand(
    "ai",
    "get-page-metadata",
    description="Load a URL and print the extracted page metadata.",
    virtualenv_name="ai",
)
@CommandArgument("url", help="The URL to load and extract.")
@CommandArgument("--headless", action="store_true", help="Run Firefox headless.")
@CommandArgument(
    "--firefox-bin",
    default=None,
    help="Path to the Firefox binary (defaults to the local build).",
)
def get_page_metadata(command_context, url, headless=False, firefox_bin=None):
    """Load a URL in a tab and print PageExtractor's metadata for it.

    Covers the structured data types found on the page, its word count and
    language, and whether it is readerable or gated.
    """
    return _run(
        command_context, headless, firefox_bin, lambda d: d.get_page_metadata(url)
    )


@SubCommand(
    "ai",
    "get-headless-text",
    description="Extract text from a URL loaded in a hidden browser.",
    virtualenv_name="ai",
)
@CommandArgument("url", help="The URL to load and extract.")
@CommandArgument("--headless", action="store_true", help="Run Firefox headless.")
@CommandArgument(
    "--anonymous-fetch",
    action="store_true",
    help="Fetch the page without cookies or site data.",
)
@CommandArgument(
    "--firefox-bin",
    default=None,
    help="Path to the Firefox binary (defaults to the local build).",
)
def get_headless_text(
    command_context, url, headless=False, anonymous_fetch=False, firefox_bin=None
):
    """Extract text from a URL loaded in a hidden browser."""
    return _run(
        command_context,
        headless,
        firefox_bin,
        lambda d: d.get_headless_page_text(url, anonymous_fetch=anonymous_fetch),
    )
