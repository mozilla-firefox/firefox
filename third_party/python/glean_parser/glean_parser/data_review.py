# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Produce skeleton Data Review Requests.
"""

from pathlib import Path
from typing import Sequence
import re


from . import flows
from . import parser
from . import util


def generate(
    bug: str,
    metrics_files: Sequence[Path],
) -> int:
    """
    Commandline helper for Data Review Request template generation.

    :param bug: pattern to match in metrics' bug_numbers lists.
    :param metrics_files: List of Path objects to load metrics from.
    :return: Non-zero if there were any errors.
    """

    metrics_files = util.ensure_list(metrics_files)

    # Accept any value of expires.
    parser_options = {
        "allow_reserved": True,
        "custom_is_expired": lambda expires: False,
        "custom_validate_expires": lambda expires: True,
    }
    all_objects = parser.parse_objects(metrics_files, parser_options)

    if util.report_validation_errors(all_objects):
        return 1

    # I tried [\W\Z] but it complained. So `|` it is.
    reobj = re.compile(f"\\W{bug}\\W|\\W{bug}$")
    durations = set()
    responsible_emails = set()
    filtered_metrics = list()
    for category_key, metrics in all_objects.value.items():
        if flows.flows_in({category_key: metrics}):
            continue
        for metric in metrics.values():
            if not any([len(reobj.findall(bug)) == 1 for bug in metric.bugs]):
                continue

            filtered_metrics.append(metric)

            durations.add(metric.expires)

            if metric.expires == "never":
                responsible_emails.update(metric.notification_emails)

    if len(filtered_metrics) == 0:
        print(f"I'm sorry, I couldn't find metrics matching the bug number {bug}.")
        return 1

    template = util.get_jinja2_template(
        "data_review.jinja2",
        filters=(("snake_case", util.snake_case),),
    )

    print(
        template.render(
            metrics=filtered_metrics,
            durations=durations,
            responsible_emails=responsible_emails,
        )
    )

    return 0
