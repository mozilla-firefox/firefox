# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Outputter to generate Kotlin code for metrics.
"""

import enum
import json
from pathlib import Path
from typing import Any, Dict, List, Optional, Union  # noqa

from . import __version__
from . import flows
from . import metrics
from . import pings
from . import util


def kotlin_datatypes_filter(value: util.JSONType) -> str:
    """
    A Jinja2 filter that renders Kotlin literals.

    Based on Python's JSONEncoder, but overrides:
      - lists to use listOf
      - dicts to use mapOf
      - sets to use setOf
      - enums to use the like-named Kotlin enum
      - Rate objects to a CommonMetricData initializer
        (for external Denominators' Numerators lists)
    """

    class KotlinEncoder(json.JSONEncoder):
        def iterencode(self, value):
            if isinstance(value, list):
                yield "listOf("
                first = True
                for subvalue in value:
                    if not first:
                        yield ", "
                    yield from self.iterencode(subvalue)
                    first = False
                yield ")"
            elif isinstance(value, dict):
                yield "mapOf("
                first = True
                for key, subvalue in value.items():
                    if not first:
                        yield ", "
                    yield from self.iterencode(key)
                    yield " to "
                    yield from self.iterencode(subvalue)
                    first = False
                yield ")"
            elif isinstance(value, enum.Enum):
                # UniFFI generates SCREAMING_CASE enum variants.
                yield (value.__class__.__name__ + "." + util.screaming_case(value.name))
            elif isinstance(value, set):
                yield "setOf("
                first = True
                for subvalue in sorted(list(value)):
                    if not first:
                        yield ", "
                    yield from self.iterencode(subvalue)
                    first = False
                yield ")"
            elif isinstance(value, metrics.Rate):
                yield "CommonMetricData("
                first = True
                for arg_name in util.common_metric_args:
                    if hasattr(value, arg_name):
                        if not first:
                            yield ", "
                        yield f"{util.camelize(arg_name)} = "
                        yield from self.iterencode(getattr(value, arg_name))
                        first = False
                yield ")"
            else:
                yield from super().iterencode(value)

    return "".join(KotlinEncoder().iterencode(value))


def type_name(obj: Union[metrics.Metric, pings.Ping]) -> str:
    """
    Returns the Kotlin type to use for a given metric or ping object.
    """
    generate_enums = getattr(obj, "_generate_enums", [])
    if len(generate_enums):
        generic = None
        for member, suffix in generate_enums:
            if len(getattr(obj, member)):
                if isinstance(obj, metrics.Event):
                    generic = util.Camelize(obj.name) + suffix
                else:
                    generic = util.camelize(obj.name) + suffix
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
    Returns the corresponding Kotlin type for event's extra key types.
    """

    if typ == "boolean":
        return "Boolean"
    elif typ == "string":
        return "String"
    elif typ == "quantity":
        return "Int"
    else:
        return "UNSUPPORTED"


def structure_type_name(typ: str) -> str:
    """
    Returns the corresponding Kotlin type for structure items.
    """

    if typ == "boolean":
        return "Boolean"
    elif typ == "string":
        return "String"
    elif typ == "number":
        return "Int"
    else:
        return "UNSUPPORTED"


def class_name(obj_type: str) -> str:
    """
    Returns the Kotlin class name for a given metric or ping type.
    """
    if obj_type == "ping":
        return "PingType"
    if obj_type.startswith("labeled_"):
        obj_type = obj_type[8:]
    return util.Camelize(obj_type) + "MetricType"


def generate_build_date(date: Optional[str]) -> str:
    """
    Generate the build timestamp.
    """

    ts = util.build_date(date)

    data = [
        str(ts.year),
        # In Java the first month of the year in calendars is JANUARY which is 0.
        # In Python it's 1-based
        str(ts.month - 1),
        str(ts.day),
        str(ts.hour),
        str(ts.minute),
        str(ts.second),
    ]
    components = ", ".join(data)

    # DatetimeMetricType takes a `Calendar` instance.
    return f'Calendar.getInstance(TimeZone.getTimeZone("GMT+0")).also {{ cal -> cal.set({components}) }}'  # noqa


def output_kotlin(
    objs: metrics.ObjectTree, output_dir: Path, options: Optional[Dict[str, Any]] = None
) -> None:
    """
    Given a tree of objects, output Kotlin code to `output_dir`.

    :param objects: A tree of objects (metrics and pings) as returned from
        `parser.parse_objects`.
    :param output_dir: Path to an output directory to write to.
    :param options: options dictionary, with the following optional keys:

        - `namespace`: The package namespace to declare at the top of the
          generated files. Defaults to `GleanMetrics`.
        - `glean_namespace`: The package namespace of the glean library itself.
          This is where glean objects will be imported from in the generated
          code.
        - `with_buildinfo`: If "true" a `GleanBuildInfo.kt` file is generated.
          Otherwise generation of that file is skipped.
          Defaults to "true".
        - `build_date`: If set to `0` a static unix epoch time will be used.
                        If set to a ISO8601 datetime string (e.g. `2022-01-03T17:30:00`)
                        it will use that date.
                        Other values will throw an error.
                        If not set it will use the current date & time.
    """
    if options is None:
        options = {}

    namespace = options.get("namespace", "GleanMetrics")
    glean_namespace = options.get("glean_namespace", "mozilla.components.service.glean")
    namespace_package = namespace[: namespace.rfind(".")]
    with_buildinfo = options.get("with_buildinfo", "true").lower() == "true"
    build_date = options.get("build_date", None)

    # Write out the special "build info" object
    template = util.get_jinja2_template(
        "kotlin.buildinfo.jinja2",
    )

    if with_buildinfo:
        build_date = generate_build_date(build_date)
        # This filename needs to start with "Glean" so it can never clash with a
        # metric category
        with (output_dir / "GleanBuildInfo.kt").open("w", encoding="utf-8") as fd:
            fd.write(
                template.render(
                    parser_version=__version__,
                    namespace=namespace,
                    namespace_package=namespace_package,
                    glean_namespace=glean_namespace,
                    build_date=build_date,
                )
            )
            fd.write("\n")

    # Write out the flow registration data, only if there are any flows. The
    # filename starts with "Glean" so it can never clash with a metric category.
    flow_data = flows.registration_data(options.get("flows", {}))
    if flow_data:
        flows_template = util.get_jinja2_template("kotlin.flows.jinja2")
        with (output_dir / "GleanFlows.kt").open("w", encoding="utf-8") as fd:
            fd.write(
                flows_template.render(
                    parser_version=__version__,
                    namespace=namespace,
                    glean_namespace=glean_namespace,
                    flows=flow_data,
                )
            )
            fd.write("\n")

    template = util.get_jinja2_template(
        "kotlin.jinja2",
        filters=(
            ("kotlin", kotlin_datatypes_filter),
            ("type_name", type_name),
            ("extra_type_name", extra_type_name),
            ("class_name", class_name),
            ("structure_type_name", structure_type_name),
        ),
    )

    for category_key, category_val in objs.items():
        filename = util.Camelize(category_key) + ".kt"
        filepath = output_dir / filename

        obj_types = sorted(
            list(set(class_name(obj.type) for obj in category_val.values()))
        )
        has_labeled_metrics = any(
            getattr(metric, "labeled", False) for metric in category_val.values()
        )
        has_object_metrics = any(
            isinstance(metric, metrics.Object) for metric in category_val.values()
        )

        with filepath.open("w", encoding="utf-8") as fd:
            fd.write(
                template.render(
                    parser_version=__version__,
                    category_name=category_key,
                    objs=category_val,
                    obj_types=obj_types,
                    common_metric_args=util.common_metric_args,
                    extra_metric_args=util.extra_metric_args,
                    ping_args=util.ping_args,
                    namespace=namespace,
                    has_labeled_metrics=has_labeled_metrics,
                    has_object_metrics=has_object_metrics,
                    glean_namespace=glean_namespace,
                )
            )
            # Jinja2 squashes the final newline, so we explicitly add it
            fd.write("\n")
