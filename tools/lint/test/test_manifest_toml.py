# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from pathlib import Path

import mozunit
import pytest
from mozinfo.platforminfo import android_os_to_api_map

LINTER = "test-manifest-toml"
fixed = 0

ERROR = "error"
WARNING = "warning"


def test_valid(lint, paths):
    results = lint(paths("valid.toml"))
    assert len(results) == 0


def test_invalid(lint, paths):
    results = lint(paths("invalid.toml"))
    assert len(results) == 1
    assert results[0].message.startswith("The manifest is not valid TOML")
    assert results[0].level == ERROR


def test_assignment_in_preamble(lint, paths):
    # NOTE: The tomlkit parser will throw an error for this legal TOML
    # so our linter never gets to find the preamble assignment
    results = lint(paths("assignment-in-preamble.toml"))
    assert len(results) == 1
    assert (
        results[0].message
        == "The manifest is not valid TOML: 'bool' object has no attribute 'keys'"
    )
    assert results[0].level == ERROR


def test_no_default(lint, paths):
    """Test verifying [DEFAULT] section."""

    results = lint(paths("no-default.toml"))
    assert len(results) == 1
    assert results[0].message == "The manifest does not start with a [DEFAULT] section."
    assert results[0].level == WARNING


def test_no_default_fix(lint, paths, create_temp_file):
    """Test fixing missing [DEFAULT] section."""

    contents = "# this Manifest has no DEFAULT section\n"
    path = create_temp_file(contents, "no-default.toml")
    results = lint([path], fix=True)
    assert len(results) == 1
    assert results[0].message == "The manifest does not start with a [DEFAULT] section."
    assert results[0].level == WARNING
    assert fixed == 1
    expected = contents + "[DEFAULT]\n"
    assert Path(path).read_text() == expected


def test_non_double_quote_sections_fix(lint, paths, create_temp_file):
    """Test and fix sections that do not have double quotes"""

    basename = "non-double-quote-sections"
    orig, fix = paths(f"{basename}.toml", f"{basename}-fix.toml")
    original = Path(orig).read_text()
    expected = Path(fix).read_text()
    path = create_temp_file(original, f"{basename}.toml")
    results = lint([path], fix=True)
    assert len(results) == 2
    assert results[0].message == "The section name must be double quoted: [aaa.js]"
    assert results[0].level == WARNING
    assert results[0].lineno == 3
    assert results[1].message == "The section name must be double quoted: ['bbb.js']"
    assert results[1].level == WARNING
    assert results[1].lineno == 6
    assert Path(path).read_text() == expected


def test_unsorted_fix(lint, paths, create_temp_file):
    """Test and fix sections in alpha order."""

    basename = "unsorted"
    orig, fix = paths(f"{basename}.toml", f"{basename}-fix.toml")
    original = Path(orig).read_text()
    expected = Path(fix).read_text()
    path = create_temp_file(original, f"{basename}.toml")
    results = lint([path], fix=True)
    assert len(results) == 5
    i: int = 0
    assert results[i].message == "The manifest sections are not in alphabetical order."
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message == "linux condition requires display == 'x11' or 'wayland'"
    )
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message
        == 'instead of "!debug" use three conditions: "asan", "opt", "tsan"'
    )
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message == "linux condition requires display == 'x11' or 'wayland'"
    )
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message
        == "non canonical condition: variable os (rank 1) should appear before variable ccov (rank 7) in condition: ccov && os == 'linux' && os_version == '22.04'"
    )
    assert results[i].level == ERROR
    i += 1
    assert Path(path).read_text() == expected


def test_comment_section(lint, paths):
    """Test for commented sections."""

    results = lint(paths("comment-section.toml"))
    assert len(results) == 2
    assert results[0].message.startswith(
        "Use 'disabled = \"<reason>\"' to disable a test instead of a comment:"
    )
    assert results[0].level == ERROR


def test_skip_if_not_array(lint, paths):
    """Test for non-array skip-if value."""

    results = lint(paths("skip-if-not-array.toml"))
    assert len(results) == 1
    assert results[0].message.startswith("Value for conditional must be an array:")
    assert results[0].level == ERROR
    assert results[0].lineno == 3


def test_skip_if_explicit_or(lint, paths):
    """Test for explicit || in skip-if."""

    results = lint(paths("skip-if-explicit-or.toml"))
    assert len(results) == 1
    assert results[0].message.startswith(
        "Value for conditional must not include explicit ||, instead put on multiple lines:"
    )
    assert results[0].level == ERROR
    assert results[0].lineno == 4


def test_per_test_prefs(lint, paths):
    """Test that prefs in individual test sections are rejected for non-xpcshell manifests."""

    results = lint(paths("per-test-prefs.toml"))
    assert len(results) == 1
    assert (
        results[0].message
        == "'prefs' is only supported in the [DEFAULT] section for non-xpcshell manifests"
    )
    assert results[0].level == ERROR


def test_missing_include(lint, paths):
    """Test for missing include"""

    results = lint(paths("missing-include.toml"))
    assert len(results) == 1
    assert "non-existent.toml' does not exist" in results[0].message
    assert results[0].level == ERROR


def test_non_idiomatic_fix(lint, paths, create_temp_file):
    """Test and fix non-idiomatic conditions."""

    basename = "non-idiomatic"
    orig, fix = paths(f"{basename}.toml", f"{basename}-fix.toml")
    original = Path(orig).read_text()
    expected = Path(fix).read_text()
    path = create_temp_file(original, f"{basename}.toml")
    results = lint([path], fix=True)
    assert len(results) == 15
    i: int = 0
    assert (
        results[i].message
        == "non canonical condition: unknown var 'bits' in comparison: bits == 64"
    )
    assert results[i].level == ERROR
    i += 1
    assert results[i].message == "using 'bits' is not idiomatic, use 'arch' instead"
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message
        == "non canonical condition: unknown var 'processor' in comparison: processor == 'aarch64'"
    )
    assert results[i].level == ERROR
    i += 1
    assert results[i].message == "using 'bits' is not idiomatic, use 'arch' instead"
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message == "using 'processor' is not idiomatic, use 'arch' instead"
    )
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message
        == "non canonical condition: unknown var 'android_version' in comparison: android_version == '36'"
    )
    assert results[i].level == ERROR
    i += 1
    assert (
        results[i].message
        == "using 'android_version' is not idiomatic, use 'os_version' instead (see testing/mozbase/mozinfo/mozinfo/platforminfo.py)"
    )
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message
        == "linux os_version == '24.04' is only supported on display == 'x11'"
    )
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message
        == "non canonical condition: unknown var 'processor' in comparison: processor == 'x86_64'"
    )
    assert results[i].level == ERROR
    i += 1
    assert (
        results[i].message == "using 'processor' is not idiomatic, use 'arch' instead"
    )
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message == "linux condition requires display == 'x11' or 'wayland'"
    )
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message
        == 'instead of "!debug" use three conditions: "asan", "opt", "tsan"'
    )
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message == "linux condition requires display == 'x11' or 'wayland'"
    )
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message
        == "use boolean variables directly instead of testing for literal values"
    )
    assert results[i].level == WARNING
    i += 1
    assert (
        results[i].message
        == "use boolean variables directly instead of testing for literal values"
    )
    assert results[i].level == WARNING
    i += 1
    # wait to disable 18.04 until the Thunderbird migration is complete
    # assert results[i].message == "linux os_version == '18.04' is no longer used"
    # assert results[i].level == WARNING
    # i += 1
    assert Path(path).read_text() == expected


def test_android_os_mismatch(lint, paths, create_temp_file):
    """Test an android_version os_version mismatch"""

    contents = (
        "[DEFAULT]\nskip-if = [\"android_version == '36' && os_version == '15'\"]"
    )
    path = create_temp_file(contents, "android_mismatch.toml")
    with pytest.raises(Exception) as e:
        _results = lint([path], fix=True)
    assert str(e.value) == "android_version == '36' conflicts with os_version == '15'"


def test_unknown_android_version(lint, paths, create_temp_file):
    """Test unknown android_version"""

    contents = "[DEFAULT]\nskip-if = [\"android_version == '999'\"]"
    path = create_temp_file(contents, "unknown_android.toml")
    with pytest.raises(Exception) as e:
        lint([path], fix=True)
    assert str(e.value) == (
        f"Unknown Android API version '999'. "
        f"Supported versions are {android_os_to_api_map.values()}."
    )


def test_invalid_combinations(lint, paths, create_temp_file):
    """Test invalid combinations of platforms and build-types"""

    skip_if = "os == 'win' && os_version == '11.26100' && arch == 'x86' && tsan"
    contents = f'[DEFAULT]\nskip-if = ["{skip_if}"]'
    path = create_temp_file(contents, "combination.toml")
    results = lint([path], fix=True)
    assert len(results) == 1
    assert results[0].message == "tsan build-type is not tested on Windows"
    assert results[0].level == ERROR

    skip_if = "os == 'linux' && os_version == '22.04' && arch == 'x86_64' && display == 'wayland' && asan"
    contents = f'[DEFAULT]\nskip-if = ["{skip_if}"]'
    path = create_temp_file(contents, "combination.toml")
    results = lint([path], fix=True)
    assert len(results) == 1
    assert results[0].message == "asan build-type is not tested on Linux 22.04"
    assert results[0].level == ERROR

    skip_if = "os == 'linux' && os_version == '22.04' && arch == 'x86_64' && display == 'wayland' && tsan"
    contents = f'[DEFAULT]\nskip-if = ["{skip_if}"]'
    path = create_temp_file(contents, "combination.toml")
    results = lint([path], fix=True)
    assert len(results) == 1
    assert results[0].message == "tsan build-type is not tested on Linux 22.04"
    assert results[0].level == ERROR


if __name__ == "__main__":
    mozunit.main()
