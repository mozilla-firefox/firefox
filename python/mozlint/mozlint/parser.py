# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

import yaml

from .errors import LinterNotFound, LinterParseError
from .types import supported_types

GLOBAL_SUPPORT_FILES = []


class Parser:
    """Reads and validates lint configuration files."""

    required_attributes = (
        "name",
        "description",
        "type",
        "payload",
    )

    def __init__(self, root):
        self.root = root

    def __call__(self, path):
        return self.parse(path)

    def _validate(self, linter):
        relpath = os.path.relpath(linter["path"], self.root)

        missing_attrs = []
        for attr in self.required_attributes:
            if attr not in linter:
                missing_attrs.append(attr)

        if missing_attrs:
            raise LinterParseError(
                relpath,
                "Missing required attribute(s): {}".format(",".join(missing_attrs)),
            )

        if linter["type"] not in supported_types:
            raise LinterParseError(relpath, "Invalid type '{}'".format(linter["type"]))

        for attr in ("include", "exclude", "support-files", "split-ignore-files"):
            if attr not in linter:
                continue

            if not isinstance(linter[attr], list) or not all(
                isinstance(a, str) for a in linter[attr]
            ):
                raise LinterParseError(
                    relpath,
                    f"The {attr} directive must be a list of strings!",
                )
            invalid_paths = set()
            for path in linter[attr]:
                if "*" in path:
                    if attr == "include":
                        raise LinterParseError(
                            relpath,
                            "Paths in the include directive cannot "
                            f"contain globs:\n  {path}",
                        )
                    continue

                abspath = path
                if not os.path.isabs(abspath):
                    abspath = os.path.join(self.root, path)

                if not os.path.exists(abspath):
                    invalid_paths.add("  " + path)

            if invalid_paths:
                raise LinterParseError(
                    relpath,
                    "The {} directive contains the following "
                    "paths that don't exist:\n{}".format(
                        attr, "\n".join(sorted(invalid_paths))
                    ),
                )

        if "setup" in linter:
            if linter["setup"].count(":") != 1:
                raise LinterParseError(
                    relpath,
                    "The setup attribute '{!r}' must have the "
                    "form 'module:object'".format(linter["setup"]),
                )

        if "split-dirs" in linter:
            depth = linter["split-dirs"]
            if depth != "auto" and (
                isinstance(depth, bool) or not isinstance(depth, int) or depth < 1
            ):
                raise LinterParseError(
                    relpath,
                    "The split-dirs directive must be a positive integer or 'auto', "
                    f"not {depth!r}!",
                )
            if linter["type"] == "global":
                raise LinterParseError(
                    relpath, "A global linter can't have a split-dirs directive!"
                )

        if "extensions" in linter and "exclude_extensions" in linter:
            raise LinterParseError(
                relpath,
                "Can't have both 'extensions' and 'exclude_extensions'!",
            )

        for prop in ["extensions", "exclude_extensions"]:
            if prop in linter:
                linter[prop] = [e.strip(".") for e in linter[prop]]

    def parse(self, path):
        """Read a linter and return its LINTER definition.

        :param path: Path to the linter.
        :returns: List of linter definitions ([dict])
        :raises: LinterNotFound, LinterParseError
        """
        if not os.path.isfile(path):
            raise LinterNotFound(path)

        if not path.endswith(".yml"):
            raise LinterParseError(
                path, "Invalid filename, linters must end with '.yml'!"
            )

        with open(path) as fh:
            configs = list(yaml.safe_load_all(fh))

        if not configs:
            raise LinterParseError(path, "No lint definitions found!")

        linters = []
        for config in configs:
            for name, linter in config.items():
                linter["name"] = name
                linter["path"] = path
                self._validate(linter)
                linter.setdefault("support-files", []).extend(
                    GLOBAL_SUPPORT_FILES + [path]
                )
                linter.setdefault("include", ["."])
                linters.append(linter)

        return linters
