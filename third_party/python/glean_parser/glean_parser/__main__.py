# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Console script for glean_parser."""

import datetime
import io
from pathlib import Path
import sys

import click
import json


import glean_parser


from . import data_review as mod_data_review
from . import lint
from . import translate as mod_translate
from . import validate_ping
from . import translation_options


CONTEXT_SETTINGS = dict(help_option_names=["-h", "--help"])


@click.command(context_settings=CONTEXT_SETTINGS)
@click.argument(
    "input",
    type=click.Path(exists=False, dir_okay=False, file_okay=True, readable=True),
    nargs=-1,
)
@click.option(
    "--output",
    "-o",
    type=click.Path(dir_okay=True, file_okay=False, writable=True),
    nargs=1,
    required=True,
)
@click.option(
    "--format",
    "-f",
    type=click.Choice(list(mod_translate.OUTPUTTERS.keys())),
    required=True,
)
@click.option(
    "--option",
    "-s",
    help="Backend-specific option. Must be of the form key=value.\
 Pass 'help' for valid options",
    type=str,
    multiple=True,
    required=False,
    is_eager=True,
    callback=translation_options.translate_options,
)
@click.option(
    "--allow-reserved",
    is_flag=True,
    help=(
        "If provided, allow the use of reserved fields. "
        "Should only be set when building the Glean library itself."
    ),
)
@click.option(
    "--allow-missing-files",
    is_flag=True,
    help=("Do not treat missing input files as an error."),
)
@click.option(
    "--require-tags",
    is_flag=True,
    help=("Require tags to be specified for metrics and pings."),
)
@click.option(
    "--expire-by-version",
    help="Expire metrics by version, with the provided major version.",
    type=click.INT,
    required=False,
)
def translate(
    input,
    format,
    output,
    option,
    allow_reserved,
    allow_missing_files,
    require_tags,
    expire_by_version,
):
    """
    Translate metrics.yaml and pings.yaml files to other formats.
    """
    option_dict = {}
    for opt in option:
        key, val = opt.split("=", 1)
        option_dict[key] = val

    sys.exit(
        mod_translate.translate(
            [Path(x) for x in input],
            format,
            Path(output),
            option_dict,
            {
                "allow_reserved": allow_reserved,
                "allow_missing_files": allow_missing_files,
                "require_tags": require_tags,
                "expire_by_version": expire_by_version,
            },
        )
    )


@click.command()
@click.option(
    "--schema",
    "-s",
    type=str,
    nargs=1,
    required=True,
    help=("HTTP url or file path to Glean ping schema. If remote, will cache to disk."),
)
def check(schema):
    """
    Validate the contents of a Glean ping.

    The ping contents are read from stdin, and the validation errors are
    written to stdout.
    """
    sys.exit(
        validate_ping.validate_ping(
            io.TextIOWrapper(sys.stdin.buffer, encoding="utf-8"),
            io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8"),
            schema_url=schema,
        )
    )


@click.command()
@click.argument(
    "input",
    type=click.Path(exists=True, dir_okay=False, file_okay=True, readable=True),
    nargs=-1,
)
@click.option(
    "--allow-reserved",
    is_flag=True,
    help=(
        "If provided, allow the use of reserved fields. "
        "Should only be set when building the Glean library itself."
    ),
)
@click.option(
    "--allow-missing-files",
    is_flag=True,
    help=("Do not treat missing input files as an error."),
)
@click.option(
    "--require-tags",
    is_flag=True,
    help=("Require tags to be specified for metrics and pings."),
)
@click.option(
    "--expire-by-version",
    help="Expire metrics by version, with the provided major version.",
    type=click.INT,
    required=False,
)
def glinter(
    input, allow_reserved, allow_missing_files, require_tags, expire_by_version
):
    """
    Runs a linter over the metrics.
    """
    sys.exit(
        lint.glinter(
            [Path(x) for x in input],
            {
                "allow_reserved": allow_reserved,
                "allow_missing_files": allow_missing_files,
                "require_tags": require_tags,
                "expire_by_version": expire_by_version,
            },
        )
    )


@click.command()
@click.argument(
    "input",
    type=click.Path(exists=True, dir_okay=False, file_okay=True, readable=True),
    nargs=-1,
)
@click.option(
    "--allow-reserved",
    is_flag=True,
    help=(
        "If provided, allow the use of reserved fields. "
        "Should only be set when building the Glean library itself."
    ),
)
@click.option(
    "--allow-missing-files",
    is_flag=True,
    help=("Do not treat missing input files as an error."),
)
@click.option(
    "--require-tags",
    is_flag=True,
    help=("Require tags to be specified for metrics and pings."),
)
def dump(input, allow_reserved, allow_missing_files, require_tags):
    """
    Dump the list of metrics/pings as JSON to stdout.
    """

    results = glean_parser.parser.parse_objects(
        [Path(x) for x in input],
        {
            "allow_reserved": allow_reserved,
            "allow_missing_files": allow_missing_files,
            "require_tags": require_tags,
        },
    )
    errs = list(results)
    assert len(errs) == 0

    metrics = {
        metric.identifier(): metric.serialize()
        for category, probes in results.value.items()
        if not glean_parser.flows.flows_in({category: probes})
        for probe_name, metric in probes.items()
    }

    def date_serializer(o):
        if isinstance(o, datetime.datetime):
            return o.isoformat()

    print(
        json.dumps(
            metrics,
            sort_keys=True,
            indent=2,
            separators=(",", ": "),
            default=date_serializer,
        )
    )


@click.command()
@click.argument("bug", type=str)
@click.argument(
    "metrics_files",
    type=click.Path(exists=True, dir_okay=False, file_okay=True, readable=True),
    nargs=-1,
)
def data_review_request(bug, metrics_files):
    """
    Generate a skeleton Data Review Request for all metrics in METRICS_FILES
    whose bug_numbers fields contain the provided BUG string.

    For example, providing "1694739" matches
    "https://bugzilla.mozilla.org/show_bug.cgi?id=1694739".
    To ensure substrings don't match, the provided bug string will match only
    if it is bounded by non-word characters.

    Prints to stdout.
    """
    sys.exit(mod_data_review.generate(bug, [Path(x) for x in metrics_files]))


@click.group()
@click.version_option(glean_parser.__version__, prog_name="glean_parser")
def main(args=None):
    """Command line utility for glean_parser."""
    pass


main.add_command(translate)
main.add_command(check)
main.add_command(glinter)
main.add_command(dump)
main.add_command(data_review_request, "data-review")


def main_wrapper(args=None):
    """
    A simple wrapper around click's `main` to display the glean_parser version
    when there is an error.
    """
    try:
        main(args=args)
    except SystemExit as e:
        if e.code != 0:
            print(
                f"ERROR running glean_parser v{glean_parser.__version__}",
                file=sys.stderr,
            )
        raise


if __name__ == "__main__":
    main_wrapper()  # pragma: no cover
