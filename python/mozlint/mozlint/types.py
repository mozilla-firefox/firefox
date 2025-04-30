# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import re
import sys
from abc import ABCMeta, abstractmethod

from mozlog import commandline, get_default_logger, structuredlog
from mozlog.reader import LogHandler
from mozpack.files import FileFinder

from . import result
from .pathutils import expand_exclusions, filterpaths, findobject


class BaseType:
    """Abstract base class for all types of linters."""

    __metaclass__ = ABCMeta
    batch = False

    def __call__(self, paths, config, **lintargs):
        """Run linter defined by `config` against `paths` with `lintargs`.

        :param paths: Paths to lint. Can be a file or directory.
        :param config: Linter config the paths are being linted against.
        :param lintargs: External arguments to the linter not defined in
                         the definition, but passed in by a consumer.
        :returns: A list of :class:`~result.Issue` objects.
        """
        orig_paths = None
        log = lintargs["log"]

        if lintargs.get("use_filters", True):
            # If we're linting a file passed via stdin, the actual file on disk
            # is temporary. But for filtering purposes we want to use the true
            # path passed in via `--stdin-filename` to accurately determine
            # whether it was supposed to be linted or not. Therefore we
            # overwrite paths with stdin_filename, and then restore back to the
            # temporary one later.
            if lintargs.get("stdin_filename"):
                orig_paths = paths[:]
                paths = [lintargs["stdin_filename"]]

            paths, exclude = filterpaths(
                lintargs["root"],
                paths,
                config["include"],
                config.get("exclude", []),
                config.get("extensions", []),
                config.get("exclude_extensions", []),
            )
            config["exclude"] = exclude
        elif config.get("exclude"):
            del config["exclude"]

        if not paths:
            return {"results": [], "fixed": 0}

        if orig_paths:
            paths = orig_paths

        log.debug(
            "Passing the following paths:\n{paths}".format(
                paths="  \n".join(paths),
            )
        )

        if self.batch:
            return self._lint(paths, config, **lintargs)

        errors = []

        try:
            for p in paths:
                result = self._lint(p, config, **lintargs)
                if result:
                    errors.extend(result)
        except KeyboardInterrupt:
            pass
        return errors

    def _lint_dir(self, path, config, **lintargs):
        if not config.get("extensions"):
            patterns = ["**"]
        else:
            patterns = [f"**/*.{e}" for e in config["extensions"]]

        exclude = [os.path.relpath(e, path) for e in config.get("exclude", [])]
        finder = FileFinder(path, ignore=exclude)

        errors = []
        for pattern in patterns:
            for p, f in finder.find(pattern):
                errors.extend(self._lint(os.path.join(path, p), config, **lintargs))
        return errors

    @abstractmethod
    def _lint(self, path, config, **lintargs):
        pass


class LineType(BaseType):
    """Abstract base class for linter types that check each line individually.

    Subclasses of this linter type will read each file and check the provided
    payload against each line one by one.
    """

    __metaclass__ = ABCMeta

    @abstractmethod
    def condition(payload, line, config):
        pass

    def _lint(self, path, config, **lintargs):
        if os.path.isdir(path):
            return self._lint_dir(path, config, **lintargs)

        payload = config["payload"]
        errors = []
        with open(path, errors="replace") as fh:
            for i, line in enumerate(fh):
                if self.condition(payload, line, config):
                    errors.append(result.from_config(config, path=path, lineno=i + 1))

        return errors


class StringType(LineType):
    """Linter type that checks whether a substring is found."""

    def condition(self, payload, line, config):
        return payload in line


class RegexType(LineType):
    """Linter type that checks whether a regex match is found."""

    def condition(self, payload, line, config):
        flags = 0
        if config.get("ignore-case"):
            flags |= re.IGNORECASE

        return re.search(payload, line, flags)


class ExternalType(BaseType):
    """Linter type that runs an external function.

    The function is responsible for properly formatting the results
    into a list of :class:`~result.Issue` objects.
    """

    batch = True

    def _lint(self, files, config, **lintargs):
        func = findobject(config["payload"])
        return func(files, config, **lintargs)


class ExternalFileType(ExternalType):
    batch = False


class GlobalType(ExternalType):
    """Linter type that runs an external global linting function just once.

    The function is responsible for properly formatting the results
    into a list of :class:`~result.Issue` objects.
    """

    batch = True

    def _lint(self, files, config, **lintargs):
        # Global lints are expensive to invoke.  Try to avoid running
        # them based on extensions and exclusions.
        try:
            next(expand_exclusions(files, config, lintargs["root"]))
        except StopIteration:
            return []

        func = findobject(config["payload"])
        return func(config, **lintargs)


class LintHandler(LogHandler):
    def __init__(self, config):
        self.config = config
        self.results = []

    def lint(self, data):
        self.results.append(result.from_config(self.config, **data))


class StructuredLogType(BaseType):
    batch = True

    def _lint(self, files, config, **lintargs):
        handler = LintHandler(config)
        logger = config.get("logger")
        if logger is None:
            logger = get_default_logger()
        if logger is None:
            logger = structuredlog.StructuredLogger(config["name"])
            commandline.setup_logging(logger, {}, {"mach": sys.stdout})
        logger.add_handler(handler)

        func = findobject(config["payload"])
        try:
            func(files, config, logger, **lintargs)
        except KeyboardInterrupt:
            pass
        return handler.results


supported_types = {
    "string": StringType(),
    "regex": RegexType(),
    "external": ExternalType(),
    "external-file": ExternalFileType(),
    "global": GlobalType(),
    "structured_log": StructuredLogType(),
}
"""Mapping of type string to an associated instance."""
