# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import argparse

from mach.decorators import Command, SubCommand

RAW_DESCRIPTION = """Raw description.

  first   column
  second  column
"""


class CustomParser(argparse.ArgumentParser):
    """A parser that stashes unrecognized args into extra_args and returns
    an empty remainder list, the same way MozlintParser does."""

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.add_argument("-l", "--linter", dest="linter", default=None)
        self.add_argument(
            "extra_args",
            nargs=argparse.REMAINDER,
            default=[],
        )

    def parse_known_args(self, *args, **kwargs):
        namespace, extra = super().parse_known_args(*args, **kwargs)
        namespace.extra_args = extra
        return namespace, []


def setup_custom_parser():
    return CustomParser()


@Command(
    "cmd_custom_parser",
    category="testing",
    parser=setup_custom_parser,
)
def run_custom_parser(command_context, extra_args=None, **kwargs):
    print(" ".join(extra_args or []))


def setup_raw_description_parser():
    return argparse.ArgumentParser(
        description=RAW_DESCRIPTION,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )


@Command(
    "cmd_raw_description",
    category="testing",
    description="A command whose parser keeps its description's layout.",
    parser=setup_raw_description_parser,
)
def run_raw_description(command_context, **kwargs):
    pass


@Command(
    "cmd_raw_description_main",
    category="testing",
    description="A command with a subcommand whose parser keeps its description's layout.",
)
def run_raw_description_main(command_context):
    pass


@SubCommand(
    "cmd_raw_description_main",
    "sub",
    description="A subcommand whose parser keeps its description's layout.",
    parser=setup_raw_description_parser,
)
def run_raw_description_sub(command_context, **kwargs):
    pass
