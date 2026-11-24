# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
High-level interface for translating `metrics.yaml` into other formats.
"""

from pathlib import Path
import os
import shutil
import tempfile
from typing import Any, Callable, Dict, Iterable, List, Optional

from . import flows
from . import lint
from . import parser
from . import go_server
from . import javascript
from . import javascript_server
from . import python_server
from . import kotlin
from . import markdown
from . import metrics
from . import ruby_server
from . import rust
from . import rust_server
from . import rust_sym
from . import swift
from . import util


class Outputter:
    """
    Class to define an output format.

    Each outputter in the table has the following member values:

    - output_func: the main function of the outputter, the one which
      does the actual translation.

    - clear_patterns: A list of glob patterns to clear in the directory before
      writing new results to it.
    """

    def __init__(
        self,
        output_func: Callable[[metrics.ObjectTree, Path, Dict[str, Any]], None],
        clear_patterns: Optional[List[str]] = None,
    ):
        if clear_patterns is None:
            clear_patterns = []

        self.output_func = output_func
        self.clear_patterns = clear_patterns


OUTPUTTERS = {
    "go_server": Outputter(go_server.output_go_logger, []),
    "javascript": Outputter(javascript.output_javascript, []),
    "typescript": Outputter(javascript.output_typescript, []),
    "javascript_server": Outputter(javascript_server.output_javascript, []),
    "typescript_server": Outputter(javascript_server.output_typescript, []),
    "python_server": Outputter(python_server.output_python, []),
    "ruby_server": Outputter(ruby_server.output_ruby, []),
    "kotlin": Outputter(kotlin.output_kotlin, ["*.kt"]),
    "markdown": Outputter(markdown.output_markdown, []),
    "swift": Outputter(swift.output_swift, ["*.swift"]),
    "rust": Outputter(rust.output_rust, []),
    "rust_server": Outputter(rust_server.output_rust, []),
    "rust_sym": Outputter(rust_sym.output_rust, []),
}


def transform_metrics(objects):
    """
    Transform the object model from one that represents the YAML definitions
    to one that reflects the type specifics needed by code generators.

    e.g. This will transform a `rate` to be a `numerator` if its denominator is
    external.
    """
    counters = {}
    numerators_by_denominator: Dict[str, Any] = {}
    for category_name, category_val in objects.items():
        if category_name == "tags":
            continue
        for metric in category_val.values():
            fqmn = metric.identifier()
            if getattr(metric, "type", None) == "counter":
                counters[fqmn] = metric
            denominator_name = getattr(metric, "denominator_metric", None)
            if denominator_name:
                metric.type = "numerator"
                numerators_by_denominator.setdefault(denominator_name, [])
                numerators_by_denominator[denominator_name].append(metric)

    for denominator_name, numerators in numerators_by_denominator.items():
        if denominator_name not in counters:
            raise ValueError(
                f"No `counter` named {denominator_name} found to be used as"
                "denominator for {numerators}",
            )
        counters[denominator_name].__class__ = metrics.Denominator
        counters[denominator_name].type = "denominator"
        counters[denominator_name].numerators = numerators


def translate_metrics(
    input_filepaths: Iterable[Path],
    output_dir: Path,
    translation_func: Callable[[metrics.ObjectTree, Path, Dict[str, Any]], None],
    clear_patterns: Optional[List[str]] = None,
    options: Optional[Dict[str, Any]] = None,
    parser_config: Optional[Dict[str, Any]] = None,
):
    """
    Translate the files in `input_filepaths` by running the metrics through a
    translation function and writing the results in `output_dir`.

    :param input_filepaths: list of paths to input metrics.yaml files
    :param output_dir: the path to the output directory
    :param translation_func: the function that actually performs the translation.
        It is passed the following arguments:

            - metrics_objects: The tree of metrics as pings as returned by
              `parser.parse_objects`.
            - output_dir: The path to the output directory.
            - options: A dictionary of output format-specific options.

        Examples of translation functions are in `kotlin.py` and `swift.py`.
    :param clear_patterns: a list of glob patterns of files to clear before
        generating the output files. By default, no files will be cleared (i.e.
        the directory should be left alone).
    :param options: dictionary of options. The available options are backend
        format specific. These are passed to `translation_func` unchanged,
        except that if any `flows.yaml` files define flows, they are removed
        from the object tree and passed as the `flows` option instead, a
        dictionary of flow name to `flows.Flow`.
    :param parser_config: A dictionary of options that change parsing behavior.
        See `parser.parse_metrics` for more info.
    """
    if clear_patterns is None:
        clear_patterns = []

    if options is None:
        options = {}

    if parser_config is None:
        parser_config = {}

    input_filepaths = util.ensure_list(input_filepaths)

    allow_missing_files = parser_config.get("allow_missing_files", False)
    if not input_filepaths and not allow_missing_files:
        print("❌ No metric files specified. ", end="")
        print("Use `--allow-missing-files` to not treat this as an error.")
        return 1

    if lint.glinter(input_filepaths, parser_config):
        return 1

    all_objects = parser.parse_objects(input_filepaths, parser_config)

    if util.report_validation_errors(all_objects):
        return 1

    # allow_reserved is also relevant to the translators, so copy it there
    if parser_config.get("allow_reserved"):
        options["allow_reserved"] = True

    # We don't render tags anywhere yet.
    all_objects.value.pop("tags", None)

    # Flows are not metrics, so they are taken out of the tree and handed to the
    # outputters that emit them (registration data, docs) through the `flows`
    # option, present only when there are flows. The rest never see them.
    found_flows = flows.flows_in(all_objects.value)
    if found_flows:
        all_objects.value.pop("flows")
        options = dict(options, flows=found_flows)

    # Apply additional general transformations to all metrics
    transform_metrics(all_objects.value)

    # Write everything out to a temporary directory, and then move it to the
    # real directory, for transactional integrity.
    with tempfile.TemporaryDirectory() as tempdir:
        tempdir_path = Path(tempdir)
        translation_func(all_objects.value, tempdir_path, options)

        if output_dir.is_file():
            output_dir.unlink()
        elif output_dir.is_dir() and len(clear_patterns):
            for clear_pattern in clear_patterns:
                for filepath in output_dir.glob(clear_pattern):
                    filepath.unlink()
            if len(list(output_dir.iterdir())):
                print(f"Extra contents found in '{output_dir}'.")

        # We can't use shutil.copytree alone if the directory already exists.
        # However, if it doesn't exist, make sure to create one otherwise
        # shutil.copy will fail.
        os.makedirs(str(output_dir), exist_ok=True)
        for filename in tempdir_path.glob("*"):
            shutil.copy(str(filename), str(output_dir))

    return 0


def translate(
    input_filepaths: Iterable[Path],
    output_format: str,
    output_dir: Path,
    options: Optional[Dict[str, Any]] = None,
    parser_config: Optional[Dict[str, Any]] = None,
):
    """
    Translate the files in `input_filepaths` to the given `output_format` and
    put the results in `output_dir`.

    :param input_filepaths: list of paths to input metrics.yaml files
    :param output_format: the name of the output format
    :param output_dir: the path to the output directory
    :param options: dictionary of options. The available options are backend
        format specific.
    :param parser_config: A dictionary of options that change parsing behavior.
        See `parser.parse_metrics` for more info.
    """
    if options is None:
        options = {}

    if parser_config is None:
        parser_config = {}

    format_desc = OUTPUTTERS.get(output_format, None)

    if format_desc is None:
        raise ValueError(f"Unknown output format '{output_format}'")

    return translate_metrics(
        input_filepaths,
        output_dir,
        format_desc.output_func,
        format_desc.clear_patterns,
        options,
        parser_config,
    )
