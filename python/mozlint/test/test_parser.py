# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

import mozunit
import pytest

from mozlint.errors import LinterNotFound, LinterParseError
from mozlint.parser import Parser

here = os.path.abspath(os.path.dirname(__file__))


@pytest.fixture(scope="module")
def parse(lintdir):
    parser = Parser(here)

    def _parse(name):
        path = os.path.join(lintdir, name)
        return parser(path)

    return _parse


def test_parse_valid_linter(parse):
    lintobj = parse("string.yml")
    assert isinstance(lintobj, list)
    assert len(lintobj) == 1

    lintobj = lintobj[0]
    assert isinstance(lintobj, dict)
    assert "name" in lintobj
    assert "description" in lintobj
    assert "type" in lintobj
    assert "payload" in lintobj
    assert "extensions" in lintobj
    assert "include" in lintobj
    assert lintobj["include"] == ["."]
    assert set(lintobj["extensions"]) == set(["js"])


def test_parser_valid_multiple(parse):
    lintobj = parse("multiple.yml")
    assert isinstance(lintobj, list)
    assert len(lintobj) == 2

    assert lintobj[0]["name"] == "StringLinter"
    assert lintobj[1]["name"] == "RegexLinter"


@pytest.mark.parametrize(
    "linter",
    [
        "invalid_type.yml",
        "invalid_extension.ym",
        "invalid_include.yml",
        "invalid_include_with_glob.yml",
        "invalid_exclude.yml",
        "invalid_support_files.yml",
        "invalid_ext_and_exclude_ext.yml",
        "missing_attrs.yml",
        "missing_definition.yml",
        "non_existing_include.yml",
        "non_existing_exclude.yml",
        "non_existing_support_files.yml",
        "non_existing_split_ignore_files.yml",
    ],
)
def test_parse_invalid_linter(parse, linter):
    with pytest.raises(LinterParseError):
        parse(linter)


@pytest.mark.parametrize(
    "linter,message",
    [
        ("invalid_split_dirs.yml", "must be a positive integer or 'auto', not '2'"),
        ("invalid_split_dirs_global.yml", "global linter can't have a split-dirs"),
    ],
)
def test_parse_invalid_split_dirs(parse, linter, message):
    with pytest.raises(LinterParseError, match=message):
        parse(linter)


def test_parse_split_dirs_auto(parse):
    assert parse("split_dirs_auto.yml")[0]["split-dirs"] == "auto"


def test_parse_non_existent_linter(parse):
    with pytest.raises(LinterNotFound):
        parse("missing_file.lint")


if __name__ == "__main__":
    mozunit.main()
