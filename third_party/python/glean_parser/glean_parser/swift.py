# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Outputter to generate Swift code for metrics.
"""

import enum
import json
from pathlib import Path
from typing import Any, Dict, Optional, Union

from . import __version__
from . import flows
from . import metrics
from . import pings
from . import tags
from . import util

# An (imcomplete) list of reserved keywords in Swift.
# These will be replaced in generated code by their escaped form.
SWIFT_RESERVED_NAMES = ["internal", "typealias"]


def swift_datatypes_filter(value: util.JSONType) -> str:
    """
    A Jinja2 filter that renders Swift literals.

    Based on Python's JSONEncoder, but overrides:
      - dicts to use `[key: value]`
      - sets to use `[...]`
      - enums to use the like-named Swift enum
      - Rate objects to a CommonMetricData initializer
        (for external Denominators' Numerators lists)
    """

    class SwiftEncoder(json.JSONEncoder):
        def iterencode(self, value):
            if isinstance(value, dict):
                yield "["
                first = True
                for key, subvalue in value.items():
                    if not first:
                        yield ", "
                    yield from self.iterencode(key)
                    yield ": "
                    yield from self.iterencode(subvalue)
                    first = False
                yield "]"
            elif isinstance(value, enum.Enum):
                yield ("." + util.camelize(value.name))
            elif isinstance(value, list):
                yield "["
                first = True
                for subvalue in value:
                    if not first:
                        yield ", "
                    yield from self.iterencode(subvalue)
                    first = False
                yield "]"
            elif isinstance(value, set):
                yield "["
                first = True
                for subvalue in sorted(list(value)):
                    if not first:
                        yield ", "
                    yield from self.iterencode(subvalue)
                    first = False
                yield "]"
            elif value is None:
                yield "nil"
            elif isinstance(value, metrics.Rate):
                yield "CommonMetricData("
                first = True
                for arg_name in util.common_metric_args:
                    if hasattr(value, arg_name):
                        if not first:
                            yield ", "
                        yield f"{util.camelize(arg_name)}: "
                        yield from self.iterencode(getattr(value, arg_name))
                        first = False
                yield ")"
            else:
                yield from super().iterencode(value)

    return "".join(SwiftEncoder().iterencode(value))


def type_name(obj: Union[metrics.Metric, pings.Ping]) -> str:
    """
    Returns the Swift type to use for a given metric or ping object.
    """
    generate_enums = getattr(obj, "_generate_enums", [])
    if len(generate_enums):
        generic = None
        for member, suffix in generate_enums:
            if len(getattr(obj, member)):
                generic = util.Camelize(obj.name) + suffix
            else:
                if isinstance(obj, metrics.Event):
                    generic = "NoExtras"
                else:
                    generic = "No" + suffix

        return "{}<{}>".format(class_name(obj.type), generic)

    generate_structure = getattr(obj, "_generate_structure", [])
    if len(generate_structure):
        generic = util.Camelize(obj.name) + "Object"
        return "{}<{}>".format(class_name(obj.type), generic)

    return class_name(obj.type)


def extra_type_name(typ: str) -> str:
    """
    Returns the corresponding Swift type for event's extra key types.
    """

    if typ == "boolean":
        return "Bool"
    elif typ == "string":
        return "String"
    elif typ == "quantity":
        return "Int32"
    else:
        return "UNSUPPORTED"


def structure_type_name(typ: str) -> str:
    """
    Returns the corresponding Swift type for structure items.
    """

    if typ == "boolean":
        return "Bool"
    elif typ == "string":
        return "String"
    elif typ == "number":
        return "Int64"
    else:
        return "UNSUPPORTED"


def class_name(obj_type: str) -> str:
    """
    Returns the Swift class name for a given metric or ping type.
    """
    if obj_type == "ping":
        return "Ping"
    if obj_type.startswith("labeled_"):
        obj_type = obj_type[8:]
    return util.Camelize(obj_type) + "MetricType"


def variable_name(var: str) -> str:
    """
    Returns a valid Swift variable name, escaping keywords if necessary.
    """
    if var in SWIFT_RESERVED_NAMES:
        return "`" + var + "`"
    else:
        return var


class BuildInfo:
    def __init__(self, build_date):
        self.build_date = build_date


def generate_build_date(date: Optional[str]) -> str:
    """
    Generate the build timestamp.
    """

    ts = util.build_date(date)

    data = [
        ("year", ts.year),
        ("month", ts.month),
        ("day", ts.day),
        ("hour", ts.hour),
        ("minute", ts.minute),
        ("second", ts.second),
    ]

    # The internal DatetimeMetricType API can take a `DateComponents` object,
    # which lets us easily specify the timezone.
    components = ", ".join([f"{name}: {val}" for (name, val) in data])
    return f'DateComponents(calendar: Calendar.current, timeZone: TimeZone(abbreviation: "UTC"), {components})'  # noqa


class Category:
    """
    Data struct holding information about a metric to be used in the template.
    """

    name: str
    objs: Dict[str, Union[metrics.Metric, pings.Ping, tags.Tag, flows.Flow]]
    contains_pings: bool


def output_swift(
    objs: metrics.ObjectTree, output_dir: Path, options: Optional[Dict[str, Any]] = None
) -> None:
    """
    Given a tree of objects, output Swift code to `output_dir`.

    :param objects: A tree of objects (metrics and pings) as returned from
        `parser.parse_objects`.
    :param output_dir: Path to an output directory to write to.
    :param options: options dictionary, with the following optional keys:
        - namespace: The namespace to generate metrics in
        - glean_namespace: The namespace to import Glean from
        - allow_reserved: When True, this is a Glean-internal build
        - with_buildinfo: If "true" the `GleanBuildInfo` is generated.
          Otherwise generation of that file is skipped.
          Defaults to "true".
        - build_date: If set to `0` a static unix epoch time will be used.
                      If set to a ISO8601 datetime string (e.g. `2022-01-03T17:30:00`)
                      it will use that date.
                      Other values will throw an error.
                      If not set it will use the current date & time.
    """
    if options is None:
        options = {}

    template = util.get_jinja2_template(
        "swift.jinja2",
        filters=(
            ("swift", swift_datatypes_filter),
            ("type_name", type_name),
            ("class_name", class_name),
            ("variable_name", variable_name),
            ("extra_type_name", extra_type_name),
            ("structure_type_name", structure_type_name),
        ),
    )

    namespace = options.get("namespace", "GleanMetrics")
    glean_namespace = options.get("glean_namespace", "Glean")
    with_buildinfo = options.get("with_buildinfo", "true").lower() == "true"
    build_date = options.get("build_date", None)
    build_info = None
    if with_buildinfo:
        build_date = generate_build_date(build_date)
        build_info = BuildInfo(build_date=build_date)

    filename = "Metrics.swift"
    filepath = output_dir / filename
    categories = []

    for category_key, category_val in objs.items():
        contains_pings = any(
            isinstance(obj, pings.Ping) for obj in category_val.values()
        )

        cat = Category()
        cat.name = category_key
        cat.objs = category_val
        cat.contains_pings = contains_pings

        categories.append(cat)

    with filepath.open("w", encoding="utf-8") as fd:
        fd.write(
            template.render(
                parser_version=__version__,
                categories=categories,
                common_metric_args=util.common_metric_args,
                extra_metric_args=util.extra_metric_args,
                namespace=namespace,
                glean_namespace=glean_namespace,
                allow_reserved=options.get("allow_reserved", False),
                build_info=build_info,
                flows=flows.registration_data(options.get("flows", {})),
            )
        )
        # Jinja2 squashes the final newline, so we explicitly add it
        fd.write("\n")
