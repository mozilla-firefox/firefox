# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Utility functions for Talos"""

import os
import platform
import re
import string
import time
import urllib.parse
from sys import stdout

try:
    from mozlog import get_proxy_logger

    LOG = get_proxy_logger()
except ModuleNotFoundError:
    LOG = stdout

# directory of this file for use with interpolatePath()
here = os.path.dirname(os.path.realpath(__file__))


class Timer:
    def __init__(self):
        self._start_time = 0
        self.start()

    def start(self):
        self._start_time = time.time()

    def elapsed(self):
        seconds = time.time() - self._start_time
        return time.strftime("%H:%M:%S", time.gmtime(seconds))


class TalosError(Exception):
    "Errors found while running the talos harness."


class TalosRegression(Exception):
    """When a regression is detected at runtime, report it properly
    Currently this is a simple definition so we can detect the class type
    """


class TalosCrash(Exception):
    """Exception type where we want to report a crash and stay
    compatible with tbpl while allowing us to continue on.

    https://bugzilla.mozilla.org/show_bug.cgi?id=829734
    """


def interpolate(template, **kwargs):
    """
    Use string.Template to substitute variables in a string.

    The placeholder ${talos} is always defined and will be replaced by the
    folder containing this file (global variable 'here').

    You can add placeholders using kwargs.
    """
    kwargs.setdefault("talos", here)
    return string.Template(template).safe_substitute(**kwargs)


def findall(string, token):
    """find all occurences in a string"""
    return [m.start() for m in re.finditer(re.escape(token), string)]


def tokenize(string, start, end):
    """
    tokenize a string by start + end tokens,
    returns parts and position of last token
    """
    assert end not in start, f"End token '{end}' is contained in start token '{start}'"
    assert start not in end, f"Start token '{start}' is contained in end token '{end}'"
    _start = findall(string, start)
    _end = findall(string, end)
    if not _start and not _end:
        return [], -1
    assert len(_start), f"Could not find start token: '{start}'"
    assert len(_end), f"Could not find end token: '{end}'"
    assert len(_start) == len(_end), (
        f"Unmatched number of tokens found: '{start}' ({len(_start)}) vs '{end}' ({len(_end)})"
    )
    for i in range(len(_start)):
        assert _end[i] > _start[i], (
            f"End token '{end}' occurs before start token '{start}'"
        )
    parts = []
    for i in range(len(_start)):
        parts.append(string[_start[i] + len(start) : _end[i]])
    return parts, _end[-1]


def urlsplit(url, default_scheme="file"):
    """front-end to urlparse.urlsplit"""

    if "://" not in url:
        url = f"{default_scheme}://{url}"

    if url.startswith("file://"):
        # file:// URLs do not play nice with windows
        # https://bugzilla.mozilla.org/show_bug.cgi?id=793875
        return ["file", "", url[len("file://") :], "", ""]

    # split the URL and return a list
    return [i for i in urllib.parse.urlsplit(url)]


def parse_pref(value):
    """parse a preference value from a string"""
    from mozprofile.prefs import Preferences

    return Preferences.cast(value)


def GenerateBrowserCommandLine(
    browser_path, extra_args, profile_dir, url, profiling_info=None
):
    # TODO: allow for spaces in file names on Windows
    command_args = [browser_path.strip()]

    if platform.system() == "Darwin":
        command_args.extend(["-foreground"])

    if isinstance(extra_args, list):
        command_args.extend(extra_args)

    elif extra_args.strip():
        command_args.extend([extra_args])

    command_args.extend(["-profile", profile_dir])

    if profiling_info:
        # pageloader tests use a tpmanifest browser pref instead of passing in a manifest url
        # profiling info is handled differently for pageloader vs non-pageloader (startup) tests
        # for pageloader the profiling info was mirrored already in an env var; so here just
        # need to setup profiling info for startup / non-pageloader tests
        if url is not None:
            # for non-pageloader/non-manifest tests the profiling info is added to the test url
            if url.find("?") != -1:
                url += "&" + urllib.parse.urlencode(profiling_info)
            else:
                url += "?" + urllib.parse.urlencode(profiling_info)

    # if there's a url i.e. startup test / non-manifest test, add it to the cmd line args
    if url is not None:
        command_args.extend(url.split(" "))

    return command_args


def run_in_debug_mode(browser_config):
    if (
        browser_config.get("debug")
        or browser_config.get("debugger")
        or browser_config.get("debugg_args")
    ):
        return True
    return False
