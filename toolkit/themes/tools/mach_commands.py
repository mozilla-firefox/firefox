# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import importlib
import os
import sys

from mach.decorators import Command, SubCommand

HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    if HERE not in sys.path:
        sys.path.insert(0, HERE)
    return importlib.import_module(name)


def parser(name):
    return lambda: load(name).get_parser()


@Command(
    "theme",
    category="misc",
    description="Inspect design tokens, contrast, high contrast mode colors and screenshots of the front end.",
)
def theme(command_context):
    """
    Example:
      # The winning value of a token in every environment:
      $ ./mach buildtokens
      $ ./mach theme resolve-token toolkit/themes/shared/design-system/dist --effective --button-text-color
    """
    command_context._sub_mach(["help", "theme"])
    return 1


def token_parser():
    ap = argparse.ArgumentParser(
        description=load("token_resolve").__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("args", nargs=argparse.REMAINDER, default=[])
    return ap


@SubCommand(
    "theme",
    "resolve-token",
    description="Resolve design tokens from the generated token sheets.",
    parser=token_parser,
)
def tokens(command_context, args):
    return load("token_resolve").main(["./mach theme resolve-token", *args])


@SubCommand(
    "theme",
    "contrast",
    description="Check the contrast of text, borders and outlines against the background behind them, across states.",
    parser=parser("contrast"),
    virtualenv_name="wpt",
)
def contrast(command_context, **kwargs):
    module = load("contrast")
    return module.run(
        module.get_parser("./mach theme contrast"), argparse.Namespace(**kwargs)
    )


@SubCommand(
    "theme",
    "check-hcm-colors",
    description="Check that text, icons, borders and outlines use the system colors that go with their background in high contrast mode, across states.",
    parser=parser("pair_walk"),
    virtualenv_name="wpt",
)
def pairs(command_context, **kwargs):
    module = load("pair_walk")
    return module.run(
        module.get_parser("./mach theme check-hcm-colors"),
        argparse.Namespace(**kwargs),
    )


@SubCommand(
    "theme",
    "pixel-diff",
    description="Count the pixels that differ between two captures.",
    parser=parser("pixel_diff"),
    virtualenv_name="wpt",
)
def pixel_diff(command_context, **kwargs):
    return load("pixel_diff").run(argparse.Namespace(**kwargs))
