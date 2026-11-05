# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

import mozunit
import pytest
from mozpack import path as mozpath

LINTER = "license"

pytestmark = pytest.mark.lint_config(name="license-declarations")


@pytest.fixture(scope="module")
def root():
    """The declaration fixtures live next to, not under, the header ones."""
    return mozpath.join(
        mozpath.abspath(mozpath.dirname(__file__)), "files", "license-declarations"
    )


def test_undeclared_ids_are_reported(lint, paths):
    results = lint(paths())

    reported = [(r.relpath, r.lineno) for r in results]
    assert ("bad/bad.build", 3) in reported
    assert ("bad/bad.build", 6) in reported
    assert all(r.level == "error" for r in results)
    assert "typo" in results[0].message
    assert "also-missing" in results[1].message


def test_undeclared_id_in_a_plain_assignment_is_reported(lint, paths):
    reported = [(r.relpath, r.lineno) for r in lint(paths())]

    assert ("bad/bad.build", 14) in reported


def test_invalid_spdx_expression_is_reported(lint, paths):
    results = [r for r in lint(paths("bad/bad.build")) if "SPDX" in r.message]

    assert [(r.relpath, r.lineno) for r in results] == [("bad/bad.build", 12)]
    assert "Apache2" in results[0].message


def test_every_occurrence_of_a_repeated_spdx_flag_is_checked(lint, paths):
    # The first assignment is valid, so only the second one is reported.
    results = [r for r in lint(paths("bad/repeated.build")) if "SPDX" in r.message]

    assert [(r.relpath, r.lineno) for r in results] == [("bad/repeated.build", 5)]


def test_retired_app_license_define_is_reported(lint, paths):
    results = [r for r in lint(paths()) if "about:license" in r.message]

    assert [(r.relpath, r.lineno) for r in results] == [("bad/bad.build", 16)]
    assert "APP_LICENSE_BLOCK" in results[0].message


def test_spdx_repeating_a_covering_moz_yaml_is_reported(lint, paths):
    results = [r for r in lint(paths()) if "`origin.license`" in r.message]

    assert [(r.relpath, r.lineno) for r in results] == [
        (mozpath.join("bad", "vendored", "vendored.build"), 4)
    ]
    assert "MIT" in results[0].message
    assert "subcomponent" in results[0].hint


def test_spdx_on_a_subcomponent_notice_is_accepted(lint, paths):
    assert lint(paths(mozpath.join("good", "vendored", "vendored.build"))) == []


def test_spdx_is_accepted_when_the_covering_moz_yaml_declares_no_license(lint, paths):
    assert lint(paths(mozpath.join("good", "unlicensed", "unlicensed.build"))) == []


def test_text_repeating_a_covering_moz_yaml_is_reported(lint, paths):
    results = [r for r in lint(paths()) if "`origin.license-file`" in r.message]

    assert [(r.relpath, r.lineno) for r in results] == [
        (mozpath.join("bad", "notice", "notice.build"), 3)
    ]


def test_text_naming_another_file_than_the_moz_yaml_is_accepted(lint, paths):
    assert lint(paths(mozpath.join("good", "notice", "notice.build"))) == []


def test_computed_license_id_is_reported(lint, paths):
    results = [r for r in lint(paths()) if "not a literal" in r.message]

    assert [(r.relpath, r.lineno) for r in results] == [
        ("bad/computed.build", 1),
        ("bad/computed.build", 2),
        ("bad/computed.build", 3),
    ]


def test_unparseable_file_is_reported(lint, paths):
    results = [r for r in lint(paths()) if "does not parse" in r.message]

    assert [(r.relpath, r.lineno) for r in results] == [("bad/syntax.build", 1)]


def test_unparseable_file_outside_the_linted_paths_is_not_reported(lint, paths):
    assert lint(paths("good")) == []


def test_id_declared_elsewhere_in_the_tree_is_accepted(lint, paths):
    # "assigned" is declared with a plain assignment, in a .mozbuild file.
    assert lint(paths("good/good.build")) == []


def test_license_ref_spdx_expression_is_accepted(lint, paths):
    assert lint(paths("good/assigned.mozbuild")) == []


def test_id_declared_twice_is_left_to_the_build(lint, paths):
    # mozbuild.licenses rejects it, within one moz.build and across two.
    assert lint(paths("bad/duplicate.build")) == []


def test_target_spelled_in_another_case(lint, paths):
    root = paths()[0]
    target = paths("bad/bad.build")[0]
    expected = lint([target])
    assert expected
    swapped = os.path.join(root, os.path.relpath(target, root).swapcase())
    assert lint([swapped]) == expected


if __name__ == "__main__":
    mozunit.main()
