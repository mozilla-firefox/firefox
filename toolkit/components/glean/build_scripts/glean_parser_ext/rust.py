# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Outputter to generate Rust code for metrics.
"""

import enum
import json

import jinja2
from glean_parser import util
from glean_parser.metrics import CowString, Event, Rate
from util import generate_metric_ids, generate_ping_ids, get_metrics

from js import ID_BITS, ID_SIGNAL_BITS

# The list of all args to CommonMetricData.
# No particular order is required, but I have these in common_metric_data.rs
# order just to be organized.
common_metric_data_args = [
    "name",
    "category",
    "send_in_pings",
    "lifetime",
    "disabled",
    "in_session",
    "label",
]

# Every metric type, in the order tables and lookup maps are emitted.
# `private/table.rs` must know how to construct each of these.
METRIC_TYPES = [
    "boolean",
    "counter",
    "custom_distribution",
    "datetime",
    "denominator",
    "dual_labeled_counter",
    "event",
    "labeled_boolean",
    "labeled_counter",
    "labeled_custom_distribution",
    "labeled_memory_distribution",
    "labeled_quantity",
    "labeled_string",
    "labeled_timing_distribution",
    "memory_distribution",
    "numerator",
    "quantity",
    "rate",
    "string",
    "string_list",
    "text",
    "timespan",
    "timing_distribution",
    "url",
    "uuid",
]

# Labeled base types whose submetrics get a map keyed by submetric id.
SUBMETRIC_TYPES = [
    "boolean",
    "counter",
    "custom_distribution",
    "memory_distribution",
    "quantity",
    "string",
    "timing_distribution",
]


def table_rust_type(metric_type):
    """
    The Rust type stored in the table for a metric type. Events and objects
    use their runtime-typed forms; labeled metrics always use dynamic labels.
    """
    if metric_type == "event":
        return "EventMetric<NoExtraKeys>"
    if metric_type == "object":
        return "ObjectMetric<RuntimeObject>"
    if metric_type.startswith("labeled_"):
        return f"LabeledMetric<Labeled{class_name(metric_type)}, super::DynamicLabel>"
    return class_name(metric_type)


def table_type_info(metric_type):
    labeled = metric_type.startswith("labeled_")
    return {
        "name": metric_type,
        "array": metric_type.upper() + "_TABLE",
        "map": metric_type.upper() + "_MAP",
        "rust": table_rust_type(metric_type),
        "labeled": labeled,
        "base": metric_type[8:] if labeled else metric_type,
        "metrics": [],
    }


def api_ctor(obj):
    if obj.type == "event":
        return "EventRef"
    if obj.type == "object":
        return "ObjectRef"
    return "MetricRef"


def api_type(obj):
    """
    The type of the named Rust API static for a metric: a `MetricRef` to the
    table entry, or the typed `EventRef` / `ObjectRef` wrappers.
    """
    if obj.type == "event":
        return type_name(obj).replace("EventMetric<", "EventRef<", 1)
    if obj.type == "object":
        return type_name(obj).replace("ObjectMetric<", "ObjectRef<", 1)
    return f"MetricRef<{table_rust_type(obj.type)}>"


def str_slice(values):
    return "&[" + ", ".join(json.dumps(v) for v in values) + "]"


def unordered_ipc(metric):
    return metric.metadata.get("permit_non_commutative_operations_over_ipc", False)


def rust_datatypes_filter(value):
    """
    A Jinja2 filter that renders Rust literals.

    Based on Python's JSONEncoder, but overrides:
      - dicts and sets to raise an error
      - sets to vec![] (used in labels)
      - enums to become Class::Value
      - lists to vec![] (used in send_in_pings)
      - null to None
      - strings to "value".into()
      - Rate objects to a CommonMetricData initializer
        (for external Denominators' Numerators lists)
    """

    class RustEncoder(json.JSONEncoder):
        def iterencode(self, value):
            if isinstance(value, dict):
                raise ValueError(f"RustEncoder doesn't know dicts {str(value)}")
            elif isinstance(value, enum.Enum):
                yield (value.__class__.__name__ + "::" + util.Camelize(value.name))
            elif isinstance(value, set):
                yield from self.iterencode(sorted(list(value)))
            elif isinstance(value, list):
                yield "vec!["
                first = True
                for subvalue in list(value):
                    if not first:
                        yield ", "
                    yield from self.iterencode(subvalue)
                    first = False
                yield "]"
            elif value is None:
                yield "None"
            # CowString is also a 'str' but is a special case.
            # Ensure its case is handled before str's (below).
            elif isinstance(value, CowString):
                value = json.dumps(value)
                yield f"::std::borrow::Cow::from({value})"
            elif isinstance(value, str):
                yield f"{json.dumps(value)}.into()"
            # bool is a subclass of int, and JSONEncoder already renders it
            # the way Rust wants, so let it fall through to the default.
            elif isinstance(value, int) and not isinstance(value, bool):
                yield rust_int(value)
            elif isinstance(value, Rate):
                yield "CommonMetricData {"
                for arg_name in common_metric_data_args:
                    if hasattr(value, arg_name):
                        yield f"{arg_name}: "
                        yield from self.iterencode(getattr(value, arg_name))
                        yield ", "
                yield " ..Default::default()}"
            else:
                yield from super().iterencode(value)

    return "".join(RustEncoder().iterencode(value))


def ctor(obj):
    """
    Returns the scope and name of the constructor to use for a metric object.
    Necessary because LabeledMetric<T> is constructed using LabeledMetric::new
    not LabeledMetric<T>::new
    """
    suffix = "::new"
    if obj.metadata.get("permit_non_commutative_operations_over_ipc", False):
        suffix = "::with_unordered_ipc"
    if getattr(obj, "labeled", False):
        return f"LabeledMetric{suffix}"
    return f"{class_name(obj.type)}{suffix}"


def type_name(obj):
    """
    Returns the Rust type to use for a given metric or ping object.
    """

    if getattr(obj, "labeled", False):
        label_enum = "super::DynamicLabel"
        if obj.labels and len(obj.labels):
            label_enum = f"{util.Camelize(obj.name)}Label"
        return f"LabeledMetric<Labeled{class_name(obj.type)}, {label_enum}>"
    generate_enums = getattr(obj, "_generate_enums", [])  # Extra Keys? Reasons?
    if len(generate_enums):
        for name, _ in generate_enums:
            if not len(getattr(obj, name)) and isinstance(obj, Event):
                return class_name(obj.type) + "<NoExtraKeys>"
            else:
                # we always use the `extra` suffix,
                # because we only expose the new event API
                suffix = "Extra"
                return f"{class_name(obj.type)}<{util.Camelize(obj.name) + suffix}>"
    generate_structure = getattr(obj, "_generate_structure", [])
    if len(generate_structure):
        generic = util.Camelize(obj.name) + "Object"
        return f"{class_name(obj.type)}<{generic}>"
    return class_name(obj.type)


def rust_int(value: int) -> str:
    """
    Renders an integer as a Rust literal with `_` digit separators.

    Metric and ping ids run to eight digits, which clippy::unreadable_literal
    (rightly) complains about when they're written out bare.
    """

    return f"{value:_}"


def extra_type_name(typ: str) -> str:
    """
    Returns the corresponding Rust type for event's extra key types.
    """

    if typ == "boolean":
        return "bool"
    elif typ == "string":
        return "String"
    elif typ == "quantity":
        return "u32"
    else:
        return "UNSUPPORTED"


def structure_type_name(typ: str) -> str:
    """
    Returns the corresponding Rust type for structure items.
    """

    if typ == "boolean":
        return "bool"
    elif typ == "string":
        return "String"
    elif typ == "number":
        return "i64"
    else:
        return "UNSUPPORTED"


def class_name(obj_type):
    """
    Returns the Rust class name for a given metric or ping type.
    """
    if obj_type == "ping":
        return "Ping"
    if obj_type.startswith("labeled_"):
        obj_type = obj_type[8:]
    return util.Camelize(obj_type) + "Metric"


def extra_keys(allowed_extra_keys):
    """
    Returns the &'static [&'static str] ALLOWED_EXTRA_KEYS for impl ExtraKeys
    """
    return "&[" + ", ".join(map(lambda key: '"' + key + '"', allowed_extra_keys)) + "]"


def get_schedule_reverse_map(objs):
    ping_schedule_reverse_map = dict()
    if "pings" in objs:
        for ping_key, ping_val in objs["pings"].items():
            for ping_schedule in ping_val.metadata.get("ping_schedule", []):
                if ping_schedule not in ping_schedule_reverse_map:
                    ping_schedule_reverse_map[ping_schedule] = set()
                ping_schedule_reverse_map[ping_schedule].add(ping_key)

    for ping, schedules in ping_schedule_reverse_map.items():
        sorted_schedule = sorted(schedules)
        ping_schedule_reverse_map[ping] = sorted_schedule

    return ping_schedule_reverse_map


def test_get_type(metric_type):
    """
    Returns the type name returned from the `test_get_value` Rust implementation for a given metric type.
    """
    if metric_type == "boolean":
        return "bool"
    elif metric_type == "counter":
        return "i32"
    elif metric_type in {
        "custom_distribution",
        "memory_distribution",
        "timing_distribution",
    }:
        return "DistributionData"
    elif metric_type == "string":
        return "String"
    elif metric_type == "quantity":
        return "i64"
    else:
        return "UNSUPPORTED"


def output_rust(objs, output_fd, ping_names_by_app_id, options={}):
    """
    Given a tree of objects, output Rust code to the file-like object `output_fd`.

    :param objs: A tree of objects (metrics and pings) as returned from
    `parser.parse_objects`.
    :param output_fd: Writeable file to write the output to.
    :param ping_names_by_app_id: A map of app_ids to lists of ping names.
                                 Used to determine which custom pings to register.
    :param options: options dictionary, presently unused.
    """

    # Monkeypatch util.snake_case for the templates to use
    util.snake_case = lambda value: value.replace(".", "_").replace("-", "_")
    # Monkeypatch util.get_jinja2_template to find templates nearby

    def get_local_template(template_name, filters=()):
        env = jinja2.Environment(
            loader=jinja2.PackageLoader("rust", "templates"),
            trim_blocks=True,
            lstrip_blocks=True,
        )
        env.filters["camelize"] = util.camelize
        env.filters["Camelize"] = util.Camelize
        env.filters["test_get_type"] = test_get_type
        for filter_name, filter_func in filters:
            env.filters[filter_name] = filter_func
        return env.get_template(template_name)

    util.get_jinja2_template = get_local_template
    get_metric_id = generate_metric_ids(objs, options)
    get_ping_id = generate_ping_ids(objs)
    ping_schedule_reverse_map = get_schedule_reverse_map(objs)

    # Every metric goes in a per-type table, see `private/table.rs`.
    table_types = [table_type_info(t) for t in METRIC_TYPES]
    tables_by_type = {t["name"]: t for t in table_types}
    submetric_types = [
        {"map": t.upper() + "_MAP", "rust": f"Labeled{class_name(t)}"}
        for t in SUBMETRIC_TYPES
    ]
    # Object metrics keep one typed static each in their category module (see
    # the template), so the id map needs their paths.
    objects = []

    if "pings" in objs:
        template_filename = "rust_pings.jinja2"
        objs = {"pings": objs["pings"]}
    else:
        template_filename = "rust.jinja2"
        objs = get_metrics(objs)
        for category_name, category_value in objs.items():
            category_snake = util.snake_case(category_name)
            for metric in category_value.values():
                if metric.type == "object":
                    metric_snake = util.snake_case(metric.name)
                    objects.append({"path": f"{category_snake}::{metric_snake}"})
                    continue
                if metric.type not in tables_by_type:
                    raise ValueError(
                        f"No table support for metric type {metric.type} "
                        f"({category_name}.{metric.name})"
                    )
                tables_by_type[metric.type]["metrics"].append(metric)

    # Now for the modules for each category.
    template = util.get_jinja2_template(
        template_filename,
        filters=(
            ("rust", rust_datatypes_filter),
            ("snake_case", util.snake_case),
            ("type_name", type_name),
            ("extra_type_name", extra_type_name),
            ("structure_type_name", structure_type_name),
            ("ctor", ctor),
            ("api_type", api_type),
            ("api_ctor", api_ctor),
            ("extra_keys", extra_keys),
            ("str_slice", str_slice),
            ("unordered_ipc", unordered_ipc),
            ("metric_id", lambda metric: rust_int(get_metric_id(metric))),
            ("ping_id", lambda ping: rust_int(get_ping_id(ping))),
        ),
    )

    output_fd.write(
        template.render(
            all_objs=objs,
            objects=objects,
            table_types=table_types,
            submetric_types=submetric_types,
            common_metric_data_args=common_metric_data_args,
            extra_args=util.extra_args,
            submetric_bit=ID_BITS - ID_SIGNAL_BITS,
            ping_names_by_app_id=ping_names_by_app_id,
            ping_schedule_reverse_map=ping_schedule_reverse_map,
        )
    )
    output_fd.write("\n")
