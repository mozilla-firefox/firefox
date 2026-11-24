# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
import time
import os
import functools
import json
from pathlib import Path
import sys
import re
import textwrap
from typing import Any, Callable, Iterable, Sequence, Tuple, Union, Optional
import urllib.request

import jinja2
import jsonschema  # type: ignore
from jsonschema import _utils  # type: ignore
import yaml

try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader  # type: ignore


def date_fromisoformat(datestr: str) -> datetime.date:
    return datetime.date.fromisoformat(datestr)


def datetime_fromisoformat(datestr: str) -> datetime.datetime:
    return datetime.datetime.fromisoformat(datestr)


TESTING_MODE = "pytest" in sys.modules


JSONType = Union[list, dict, str, int, float, None]
"""
The types supported by JSON.

This is only an approximation -- this should really be a recursive type.
"""


class DictWrapper(dict):
    pass


class _NoDatesSafeLoader(SafeLoader):
    @classmethod
    def remove_implicit_resolver(cls, tag_to_remove):
        """
        Remove implicit resolvers for a particular tag

        Takes care not to modify resolvers in super classes.

        We want to load datetimes as strings, not dates, because we
        go on to serialise as json which doesn't have the advanced types
        of yaml, and leads to incompatibilities down the track.
        """
        if "yaml_implicit_resolvers" not in cls.__dict__:
            cls.yaml_implicit_resolvers = cls.yaml_implicit_resolvers.copy()

        for first_letter, mappings in cls.yaml_implicit_resolvers.items():
            cls.yaml_implicit_resolvers[first_letter] = [
                (tag, regexp) for tag, regexp in mappings if tag != tag_to_remove
            ]


# Since we use JSON schema to validate, and JSON schema doesn't support
# datetimes, we don't want the YAML loader to give us datetimes -- just
# strings.
_NoDatesSafeLoader.remove_implicit_resolver("tag:yaml.org,2002:timestamp")


def yaml_load(stream):
    """
    Map line number to yaml nodes, and preserve the order
    of metrics as they appear in the metrics.yaml file.
    """

    class SafeLineLoader(_NoDatesSafeLoader):
        pass

    def _construct_mapping_adding_line(loader, node):
        loader.flatten_mapping(node)
        pairs = loader.construct_pairs(node)

        # Redefinition of a key might be a mistake if that key is a metric name.
        mapping = DictWrapper()
        for pair in pairs:
            if pair[0] in mapping:
                mapping.duplicate = pair[0]
            mapping[pair[0]] = pair[1]

        mapping.defined_in = {"line": node.start_mark.line}
        return mapping

    SafeLineLoader.add_constructor(
        yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _construct_mapping_adding_line
    )
    return yaml.load(stream, SafeLineLoader)


def ordered_yaml_dump(data, **kwargs):
    class OrderedDumper(yaml.Dumper):
        pass

    def _dict_representer(dumper, data):
        return dumper.represent_mapping(
            yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, data.items()
        )

    OrderedDumper.add_representer(DictWrapper, _dict_representer)
    return yaml.dump(data, Dumper=OrderedDumper, **kwargs)


def load_yaml_or_json(path: Path):
    """
    Load the content from either a .json or .yaml file, based on the filename
    extension.

    :param path: `pathlib.Path` object
    :rtype object: The tree of objects as a result of parsing the file.
    :raises ValueError: The file is neither a .json, .yml or .yaml file.
    :raises FileNotFoundError: The file does not exist.
    """
    # If in py.test, support bits of literal JSON/YAML content
    if TESTING_MODE and isinstance(path, dict):
        return yaml_load(yaml.dump(path))

    if path.suffix == ".json":
        with path.open("r", encoding="utf-8") as fd:
            return json.load(fd)
    elif path.suffix in (".yml", ".yaml", ".yamlx"):
        with path.open("r", encoding="utf-8") as fd:
            return yaml_load(fd)
    else:
        raise ValueError(f"Unknown file extension {path.suffix}")


def ensure_list(value: Any) -> Sequence[Any]:
    """
    Ensures that the value is a list. If it is anything but a list or tuple, a
    list with a single element containing only value is returned.
    """
    if not isinstance(value, (list, tuple)):
        return [value]
    return value


def to_camel_case(input: str, capitalize_first_letter: bool) -> str:
    """
    Convert the value to camelCase.

    This additionally replaces any '.' with '_'. The first letter is capitalized
    depending on `capitalize_first_letter`.
    """
    sanitized_input = input.replace(".", "_").replace("-", "_")
    # Filter out any empty token. This could happen due to leading '_' or
    # consecutive '__'.
    tokens = [s.capitalize() for s in sanitized_input.split("_") if len(s) != 0]
    # If we're not meant to capitalize the first letter, then lowercase it.
    if not capitalize_first_letter:
        tokens[0] = tokens[0].lower()
    # Finally join the tokens and capitalize.
    return "".join(tokens)


def camelize(value: str) -> str:
    """
    Convert the value to camelCase (with a lower case first letter).

    This is a thin wrapper around inflection.camelize that handles dots in
    addition to underscores.
    """
    return to_camel_case(value, False)


def Camelize(value: str) -> str:
    """
    Convert the value to CamelCase (with an upper case first letter).

    This is a thin wrapper around inflection.camelize that handles dots in
    addition to underscores.
    """
    return to_camel_case(value, True)


def snake_case(value: str) -> str:
    """
    Convert the value to snake_case.
    """
    return value.lower().replace(".", "_").replace("-", "_")


def camel_or_pascal_to_snake_case(value: str) -> str:
    """
    Convert Camel Case or Pascal Case to Snake Case.

    Any capitalized values beyond the first token are lowered
    and an `_` is inserted.
    """
    return re.sub(r"(?<!^)(?=[A-Z])", "_", value).lower()


def screaming_case(value: str) -> str:
    """
    Convert the value to SCREAMING_SNAKE_CASE.
    """
    return value.upper().replace(".", "_").replace("-", "_")


@functools.lru_cache()
def get_jinja2_template(
    template_name: str, filters: Iterable[Tuple[str, Callable]] = ()
):
    """
    Get a Jinja2 template that ships with glean_parser.

    The template has extra filters for camel-casing identifiers.

    :param template_name: Name of a file in ``glean_parser/templates``
    :param filters: tuple of 2-tuple. A tuple of (name, func) pairs defining
        additional filters.
    """
    env = jinja2.Environment(
        loader=jinja2.PackageLoader("glean_parser", "templates"),
        trim_blocks=True,
        lstrip_blocks=True,
    )

    env.filters["camelize"] = camelize
    env.filters["Camelize"] = Camelize
    env.filters["scream"] = screaming_case
    for filter_name, filter_func in filters:
        env.filters[filter_name] = filter_func

    return env.get_template(template_name)


def keep_value(f):
    """
    Wrap a generator so the value it returns (rather than yields), will be
    accessible on the .value attribute when the generator is exhausted.
    """

    class ValueKeepingGenerator(object):
        def __init__(self, g):
            self.g = g
            self.value = None

        def __iter__(self):
            self.value = yield from self.g

    @functools.wraps(f)
    def g(*args, **kwargs):
        return ValueKeepingGenerator(f(*args, **kwargs))

    return g


def get_null_resolver(schema):
    """
    Returns a JSON Pointer resolver that does nothing.

    This lets us handle the moz: URLs in our schemas.
    """

    class NullResolver(jsonschema.RefResolver):
        def resolve_remote(self, uri):
            if uri in self.store:
                return self.store[uri]
            if uri == "":
                return self.referrer

    return NullResolver.from_schema(schema)


def fetch_remote_url(url: str):
    """
    Fetches the contents from an HTTP url or local file path.
    """
    is_http = url.startswith("http")

    if not is_http:
        with open(url, "r", encoding="utf-8") as fd:
            return fd.read()

    contents = urllib.request.urlopen(url).read().decode("utf-8")

    return contents


_unset = _utils.Unset()


def pprint_validation_error(error) -> str:
    """
    A version of jsonschema's ValidationError __str__ method that doesn't
    include the schema fragment that failed.  This makes the error messages
    much more succinct.

    It also shows any subschemas of anyOf/allOf that failed, if any (what
    jsonschema calls "context").
    """
    essential_for_verbose = (
        error.validator,
        error.validator_value,
        error.instance,
        error.schema,
    )
    if any(m is _unset for m in essential_for_verbose):
        return textwrap.fill(error.message)

    instance = error.instance
    for path in list(error.relative_path)[::-1]:
        if isinstance(path, str):
            instance = {path: instance}
        else:
            instance = [instance]

    yaml_instance = ordered_yaml_dump(instance, width=72, default_flow_style=False)

    parts = ["```", yaml_instance.rstrip(), "```", "", textwrap.fill(error.message)]
    if error.context:
        parts.extend(
            textwrap.fill(x.message, initial_indent="    ", subsequent_indent="    ")
            for x in error.context
        )

    description = error.schema.get("description")
    if description:
        parts.extend(
            ["", "Documentation for this node:", textwrap.indent(description, "    ")]
        )

    return "\n".join(parts)


def format_error(
    filepath: Union[str, Path],
    header: str,
    content: str,
    lineno: Optional[int] = None,
) -> str:
    """
    Format a jsonshema validation error.
    """
    if isinstance(filepath, Path):
        filepath = filepath.resolve()
    else:
        filepath = "<string>"
    if lineno:
        filepath = f"{filepath}:{lineno}"
    if header:
        return f"{filepath}: {header}\n{textwrap.indent(content, '    ')}"
    else:
        return f"{filepath}:\n{textwrap.indent(content, '    ')}"


def parse_expiration_date(expires: str) -> datetime.date:
    """
    Parses the expired field date (yyyy-mm-dd) as a date.
    Raises a ValueError in case the string is not properly formatted.
    """
    try:
        return date_fromisoformat(expires)
    except (TypeError, ValueError):
        raise ValueError(
            f"Invalid expiration date '{expires}'. "
            "Must be of the form yyyy-mm-dd in UTC."
        )


def parse_expiration_version(expires: str) -> int:
    """
    Parses the expired field version string as an integer.
    Raises a ValueError in case the string does not contain a valid
    positive integer.
    """
    try:
        if isinstance(expires, int):
            version_number = int(expires)
            if version_number > 0:
                return version_number
        # Fall-through: if it's not an integer or is not greater than zero,
        # raise an error.
        raise ValueError()
    except ValueError:
        raise ValueError(
            f"Invalid expiration version '{expires}'. Must be a positive integer."
        )


def now_epoch():
    try:
        epoch_ts = int(os.environ.get("SOURCE_DATE_EPOCH", time.time()))
    except ValueError:
        raise ValueError("Invalid SOURCE_DATE_EPOCH")

    return datetime.datetime.fromtimestamp(epoch_ts, tz=datetime.timezone.utc)


def is_expired(expires: str, major_version: Optional[int] = None) -> bool:
    """
    Parses the `expires` field in a metric or ping and returns whether
    the object should be considered expired.
    """
    if expires == "never":
        return False
    elif expires == "expired":
        return True
    elif major_version is not None:
        return parse_expiration_version(expires) <= major_version
    else:
        date = parse_expiration_date(expires)
        return date <= now_epoch().date()


def validate_expires(expires: str, major_version: Optional[int] = None) -> None:
    """
    If expiration by major version is enabled, raises a ValueError in
    case `expires` is not a positive integer.
    Otherwise raises a ValueError in case the `expires` is not ISO8601
    parseable, or in case the date is more than 730 days (~2 years) in
    the future.
    """
    if expires in ("never", "expired"):
        return

    if major_version is not None:
        parse_expiration_version(expires)
        # Don't need to keep parsing dates if expiration by version
        # is enabled. We don't allow mixing dates and versions for a
        # single product.
        return

    date = parse_expiration_date(expires)
    max_date = datetime.datetime.now() + datetime.timedelta(days=730)
    if date > max_date.date():
        raise ValueError(
            f"'{expires}' is more than 730 days (~2 years) in the future.",
            "Please make sure this is intentional.",
            "You can supress this warning by adding EXPIRATION_DATE_TOO_FAR to no_lint",
            "See: https://mozilla.github.io/glean_parser/metrics-yaml.html#no_lint",
        )


def build_date(date: Optional[str]) -> datetime.datetime:
    """
    Generate the build timestamp.

    If `date` is set to `0` a static unix epoch time will be used.
    If `date` it is set to a ISO8601 datetime string (e.g. `2022-01-03T17:30:00`)
    it will use that date.
    Note that any timezone offset will be ignored and UTC will be used.
    Otherwise it will throw an error.

    If `date` is `None` it will use the current date & time.
    """

    if date is not None:
        date = str(date)
        if date == "0":
            ts = datetime.datetime(1970, 1, 1, 0, 0, 0)
        else:
            ts = datetime_fromisoformat(date).replace(tzinfo=datetime.timezone.utc)
    else:
        ts = datetime.datetime.now(datetime.timezone.utc)

    return ts


def report_validation_errors(all_objects):
    """
    Report any validation errors found to the console.

    Returns the number of errors reported.
    """
    found_errors = 0
    for error in all_objects:
        found_errors += 1
        print("=" * 78, file=sys.stderr)
        print(error, file=sys.stderr)
    return found_errors


def remove_output_params(d, output_params):
    """
    Remove output-only params, such as "defined_in",
    in order to validate the output against the input schema.
    """
    modified_dict = {}
    for key, value in d.items():
        if key is not output_params:
            modified_dict[key] = value
    return modified_dict


# Names of  parameters to pass to all metrics constructors constructors.
common_metric_args = [
    "category",
    "name",
    "send_in_pings",
    "lifetime",
    "disabled",
    "in_session",
]


# Names of parameters that only apply to some of the metrics types.
# **CAUTION**: This list needs to be in the order the Swift & Rust type constructors
# expects them. (The other language bindings don't care about the order).
extra_metric_args = [
    "time_unit",
    "memory_unit",
    "allowed_extra_keys",
    "reason_codes",
    "range_min",
    "range_max",
    "bucket_count",
    "histogram_type",
    "numerators",
]


# This includes only things that the language bindings care about, not things
# that are metadata-only or are resolved into other parameters at parse time.
# **CAUTION**: This list needs to be in the order the Swift & Rust type constructors
# expects them. (The other language bindings don't care about the order). The
# `test_order_of_fields` test checks that the generated code is valid.
# **DO NOT CHANGE THE ORDER OR ADD NEW FIELDS IN THE MIDDLE**
metric_args = common_metric_args + extra_metric_args


# Names of ping parameters to pass to constructors.
ping_args = [
    "name",
    "include_client_id",
    "send_if_empty",
    "precise_timestamps",
    "include_info_sections",
    "enabled",
    "schedules_pings",
    "reason_codes",
    "follows_collection_enabled",
    "uploader_capabilities",
]


# Names of parameters to pass to both metric and ping constructors (no duplicates).
extra_args = metric_args + [v for v in ping_args if v not in metric_args]
