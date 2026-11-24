# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Outputter to generate Markdown documentation for metrics.
"""

from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union
from urllib.parse import urlsplit, parse_qs


from . import __version__
from . import flows
from . import metrics
from . import pings
from . import util
from collections import defaultdict


def extra_info(obj: Union[metrics.Metric, pings.Ping]) -> List[Tuple[str, str]]:
    """
    Returns a list of string to string tuples with extra information for the type
    (e.g. extra keys for events) or an empty list if nothing is available.
    """
    extra_info = []

    if isinstance(obj, metrics.Event):
        for key in obj.allowed_extra_keys:
            extra_info.append((key, obj.extra_keys[key]["description"]))

    if isinstance(obj, metrics.Labeled) and obj.ordered_labels is not None:
        for label in obj.ordered_labels:
            extra_info.append((label, None))

    if isinstance(obj, metrics.Quantity):
        extra_info.append(("unit", obj.unit))

    return extra_info


def ping_desc(
    ping_name: str, custom_pings_cache: Optional[Dict[str, pings.Ping]] = None
) -> str:
    """
    Return a text description of the ping. If a custom_pings_cache
    is available, look in there for non-reserved ping names description.
    """
    desc = ""

    if ping_name in pings.RESERVED_PING_NAMES:
        desc = (
            "This is a built-in ping that is assembled out of the box by the Glean SDK."
        )

    elif ping_name == "all-pings":
        desc = "These metrics are sent in every ping."
    elif custom_pings_cache is not None and ping_name in custom_pings_cache:
        desc = custom_pings_cache[ping_name].description

    return desc


def metrics_docs(obj_name: str) -> str:
    """
    Return a link to the documentation entry for the Glean SDK metric of the
    requested type.
    """
    # We need to fixup labeled stuff, as types are singular and docs refer
    # to them as plural.
    fixedup_name = obj_name
    if obj_name.startswith("labeled_"):
        fixedup_name += "s"

    return f"https://mozilla.github.io/glean/book/user/metrics/{fixedup_name}.html"


def ping_docs(ping_name: str) -> str:
    """
    Return a link to the documentation entry for the requested Glean SDK
    built-in ping.
    """
    if ping_name not in pings.RESERVED_PING_NAMES:
        return ""

    return f"https://mozilla.github.io/glean/book/user/pings/{ping_name}.html"


def if_empty(
    ping_name: str, custom_pings_cache: Optional[Dict[str, pings.Ping]] = None
) -> bool:
    if custom_pings_cache is not None and ping_name in custom_pings_cache:
        return custom_pings_cache[ping_name].send_if_empty
    else:
        return False


def ping_reasons(
    ping_name: str, custom_pings_cache: Dict[str, pings.Ping]
) -> Dict[str, str]:
    """
    Returns the reasons dictionary for the ping.
    """
    if ping_name == "all-pings":
        return {}
    elif ping_name in custom_pings_cache:
        return custom_pings_cache[ping_name].reasons

    return {}


def ping_data_reviews(
    ping_name: str, custom_pings_cache: Optional[Dict[str, pings.Ping]] = None
) -> Optional[List[str]]:
    if custom_pings_cache is not None and ping_name in custom_pings_cache:
        return custom_pings_cache[ping_name].data_reviews
    else:
        return None


def ping_review_title(data_url: str, index: int) -> str:
    """
    Return a title for a data review in human readable form.

    :param data_url: A url for data review.
    :param index: Position of the data review on list (e.g: 1, 2, 3...).
    """
    url_object = urlsplit(data_url)

    # Bugzilla urls like `https://bugzilla.mozilla.org/show_bug.cgi?id=1581647`
    query = url_object.query
    params = parse_qs(query)

    # GitHub urls like `https://github.com/mozilla-mobile/fenix/pull/1707`
    path = url_object.path
    short_url = path[1:].replace("/pull/", "#")

    if params and params["id"]:
        return f"Bug {params['id'][0]}"
    elif url_object.netloc == "github.com":
        return short_url

    return f"Review {index}"


def ping_bugs(
    ping_name: str, custom_pings_cache: Optional[Dict[str, pings.Ping]] = None
) -> Optional[List[str]]:
    if custom_pings_cache is not None and ping_name in custom_pings_cache:
        return custom_pings_cache[ping_name].bugs
    else:
        return None


def ping_include_client_id(
    ping_name: str, custom_pings_cache: Optional[Dict[str, pings.Ping]] = None
) -> bool:
    if custom_pings_cache is not None and ping_name in custom_pings_cache:
        return custom_pings_cache[ping_name].include_client_id
    else:
        return False


def data_sensitivity_numbers(
    data_sensitivity: Optional[List[metrics.DataSensitivity]],
) -> str:
    if data_sensitivity is None:
        return "unknown"
    else:
        return ", ".join(str(x.value) for x in data_sensitivity)


def timeout_desc(timeout_ms: Optional[int]) -> str:
    """
    A readable description of a flow's `auto_close_timeout`.
    """
    if timeout_ms is None:
        return "none: an instance stays open until it exits or its session ends"
    for unit, ms in (("day", 86400000), ("hour", 3600000), ("minute", 60000)):
        if timeout_ms % ms == 0:
            count = timeout_ms // ms
            return f"{count} {unit}{'s' if count != 1 else ''}"
    count = timeout_ms // 1000
    return f"{count} second{'s' if count != 1 else ''}"


def output_markdown(
    objs: metrics.ObjectTree, output_dir: Path, options: Optional[Dict[str, Any]] = None
) -> None:
    """
    Given a tree of objects, output Markdown docs to `output_dir`.

    This produces a single `metrics.md`. The file contains a table of
    contents and a section for each ping metrics are collected for.

    :param objects: A tree of objects (metrics and pings) as returned from
        `parser.parse_objects`.
    :param output_dir: Path to an output directory to write to.
    :param options: options dictionary, with the following optional key:
        - `project_title`: The projects title.
    """
    if options is None:
        options = {}

    # Build a dictionary that associates pings with their metrics.
    #
    # {
    #  "baseline": [
    #    { ... metric data ... },
    #    ...
    #  ],
    #  "metrics": [
    #    { ... metric data ... },
    #    ...
    #  ],
    #  ...
    # }
    #
    # This also builds a dictionary of custom pings, if available.
    custom_pings_cache: Dict[str, pings.Ping] = defaultdict()
    metrics_by_pings: Dict[str, List[metrics.Metric]] = defaultdict(list)
    for _category_key, category_val in objs.items():
        for obj in category_val.values():
            # Filter out custom pings. We will need them for extracting
            # the description
            if isinstance(obj, pings.Ping):
                custom_pings_cache[obj.name] = obj
                # Pings that have `send_if_empty` set to true,
                # might not have any metrics. They need to at least have an
                # empty array of metrics to show up on the template.
                if obj.send_if_empty and not metrics_by_pings[obj.name]:
                    metrics_by_pings[obj.name] = []

            # If this is an internal Glean metric, and we don't
            # want docs for it.
            if isinstance(obj, metrics.Metric) and not obj.is_internal_metric():
                # If we get here, obj is definitely a metric we want
                # docs for.
                for ping_name in obj.send_in_pings:
                    metrics_by_pings[ping_name].append(obj)

    # Sort the metrics by their identifier, to make them show up nicely
    # in the docs and to make generated docs reproducible.
    for ping_name in metrics_by_pings:
        metrics_by_pings[ping_name] = sorted(
            metrics_by_pings[ping_name], key=lambda x: x.identifier()
        )

    # The flows each event belongs to, so the event's row can link to them.
    flow_data = flows.registration_data(options.get("flows", {}))
    role_labels = {"Start": "entry", "End": "exit", "Member": "member"}
    flows_by_event: Dict[str, List[Tuple[str, str]]] = defaultdict(list)
    for flow in flow_data:
        for event in flow["events"]:
            flows_by_event[event["identifier"]].append(
                (flow["name"], role_labels[event["role"]])
            )

    project_title = options.get("project_title", "this project")
    introduction_extra = options.get("introduction_extra")

    template = util.get_jinja2_template(
        "markdown.jinja2",
        filters=(
            ("extra_info", extra_info),
            ("metrics_docs", metrics_docs),
            ("ping_desc", lambda x: ping_desc(x, custom_pings_cache)),
            ("ping_send_if_empty", lambda x: if_empty(x, custom_pings_cache)),
            ("ping_docs", ping_docs),
            ("ping_reasons", lambda x: ping_reasons(x, custom_pings_cache)),
            ("ping_data_reviews", lambda x: ping_data_reviews(x, custom_pings_cache)),
            ("ping_review_title", ping_review_title),
            ("ping_bugs", lambda x: ping_bugs(x, custom_pings_cache)),
            (
                "ping_include_client_id",
                lambda x: ping_include_client_id(x, custom_pings_cache),
            ),
            ("data_sensitivity_numbers", data_sensitivity_numbers),
            ("event_flows", lambda x: flows_by_event.get(x.identifier(), [])),
            ("timeout_desc", timeout_desc),
        ),
    )

    filename = "metrics.md"
    filepath = output_dir / filename

    with filepath.open("w", encoding="utf-8") as fd:
        fd.write(
            template.render(
                parser_version=__version__,
                metrics_by_pings=metrics_by_pings,
                flows=flow_data,
                project_title=project_title,
                introduction_extra=introduction_extra,
            )
        )
        # Jinja2 squashes the final newline, so we explicitly add it
        fd.write("\n")
