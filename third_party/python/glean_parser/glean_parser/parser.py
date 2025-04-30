# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Code for parsing metrics.yaml files.
"""

import functools
from pathlib import Path
import textwrap
from typing import Any, cast, Dict, Generator, Iterable, Optional, Set, Tuple, Union

import jsonschema  # type: ignore
from jsonschema.exceptions import ValidationError  # type: ignore

from .metrics import Metric, ObjectTree
from .pings import Ping, RESERVED_PING_NAMES
from .tags import Tag
from . import util
from .util import DictWrapper


ROOT_DIR = Path(__file__).parent
SCHEMAS_DIR = ROOT_DIR / "schemas"

METRICS_ID = "moz://mozilla.org/schemas/glean/metrics/2-0-0"
PINGS_ID = "moz://mozilla.org/schemas/glean/pings/2-0-0"
TAGS_ID = "moz://mozilla.org/schemas/glean/tags/1-0-0"


def _update_validator(validator):
    """
    Adds some custom validators to the jsonschema validator that produce
    nicer error messages.
    """

    def required(validator, required, instance, schema):
        if not validator.is_type(instance, "object"):
            return
        missing_properties = set(
            property for property in required if property not in instance
        )
        if len(missing_properties):
            missing_properties = sorted(list(missing_properties))
            yield ValidationError(
                f"Missing required properties: {', '.join(missing_properties)}"
            )

    validator.VALIDATORS["required"] = required


def _load_file(
    filepath: Path, parser_config: Dict[str, Any]
) -> Generator[str, None, Tuple[Dict[str, util.JSONType], Optional[str]]]:
    """
    Load a metrics.yaml or pings.yaml format file.

    If the `filepath` does not exist, raises `FileNotFoundError`, unless
    `parser_config["allow_missing_files"]` is `True`.
    """
    try:
        content = util.load_yaml_or_json(filepath)
    except FileNotFoundError:
        if not parser_config.get("allow_missing_files", False):
            raise
        else:
            return {}, None
    except Exception as e:
        yield util.format_error(filepath, "", textwrap.fill(str(e)))
        return {}, None

    if content is None:
        yield util.format_error(filepath, "", f"'{filepath}' file can not be empty.")
        return {}, None

    if not isinstance(content, dict):
        return {}, None

    if content == {}:
        return {}, None

    schema_key = content.get("$schema")
    if not isinstance(schema_key, str):
        raise TypeError(f"Invalid schema key {schema_key}")

    filetype: Optional[str] = None
    try:
        filetype = schema_key.split("/")[-2]
    except IndexError:
        filetype = None

    if filetype not in ("metrics", "pings", "tags"):
        filetype = None

    for error in validate(content, filepath):
        content = {}
        yield error

    return content, filetype


@functools.lru_cache(maxsize=1)
def _load_schemas() -> Dict[str, Tuple[Any, Any]]:
    """
    Load all of the known schemas from disk, and put them in a map based on the
    schema's $id.
    """
    schemas = {}
    for schema_path in SCHEMAS_DIR.glob("*.yaml"):
        schema = util.load_yaml_or_json(schema_path)
        resolver = util.get_null_resolver(schema)
        validator_class = jsonschema.validators.validator_for(schema)
        _update_validator(validator_class)
        validator_class.check_schema(schema)
        validator = validator_class(schema, resolver=resolver)
        schemas[schema["$id"]] = (schema, validator)
    return schemas


def _get_schema(
    schema_id: str, filepath: Union[str, Path] = "<input>"
) -> Tuple[Any, Any]:
    """
    Get the schema for the given schema $id.
    """
    schemas = _load_schemas()
    if schema_id not in schemas:
        raise ValueError(
            util.format_error(
                filepath,
                "",
                f"$schema key must be one of {', '.join(schemas.keys())}",
            )
        )
    return schemas[schema_id]


def _get_schema_for_content(
    content: Dict[str, util.JSONType], filepath: Union[str, Path]
) -> Tuple[Any, Any]:
    """
    Get the appropriate schema for the given JSON content.
    """
    schema_url = content.get("$schema")
    if not isinstance(schema_url, str):
        raise TypeError("Invalid $schema type {schema_url}")
    return _get_schema(schema_url, filepath)


def validate(
    content: Dict[str, util.JSONType], filepath: Union[str, Path] = "<input>"
) -> Generator[str, None, None]:
    """
    Validate the given content against the appropriate schema.
    """
    try:
        schema, validator = _get_schema_for_content(content, filepath)
    except ValueError as e:
        yield str(e)
    else:
        yield from (
            util.format_error(filepath, "", util.pprint_validation_error(e))
            for e in validator.iter_errors(content)
        )


def _instantiate_metrics(
    all_objects: ObjectTree,
    sources: Dict[Any, Path],
    content: Dict[str, util.JSONType],
    filepath: Path,
    config: Dict[str, Any],
) -> Generator[str, None, None]:
    """
    Load a list of metrics.yaml files, convert the JSON information into Metric
    objects, and merge them into a single tree.
    """
    global_no_lint = content.get("no_lint", [])
    global_tags = content.get("$tags", [])
    assert isinstance(global_tags, list)

    for category_key, category_val in sorted(content.items()):
        if category_key.startswith("$"):
            continue
        if category_key == "no_lint":
            continue
        if not config.get("allow_reserved") and category_key.split(".")[0] == "glean":
            if category_key not in ("glean.attribution", "glean.distribution"):
                yield util.format_error(
                    filepath,
                    f"For category '{category_key}'",
                    "Categories beginning with 'glean' are reserved for "
                    "Glean internal use.",
                )
                continue
        all_objects.setdefault(category_key, DictWrapper())

        if not isinstance(category_val, dict):
            raise TypeError(f"Invalid content for {category_key}")

        for metric_key, metric_val in sorted(category_val.items()):
            if (
                not config.get("allow_reserved")
                and category_key in ("glean.attribution", "glean.distribution")
                and metric_key != "ext"
            ):
                yield util.format_error(
                    filepath,
                    f"For {category_key}.{metric_key}",
                    f"May only use semi-reserved category {category_key} with metric name 'ext'",
                    metric_val.defined_in["line"],
                )
                continue
            try:
                metric_obj = Metric.make_metric(
                    category_key, metric_key, metric_val, validated=True, config=config
                )
            except Exception as e:
                yield util.format_error(
                    filepath,
                    f"On instance {category_key}.{metric_key}",
                    str(e),
                    metric_val.defined_in["line"],
                )
                metric_obj = None
            else:
                if not config.get("allow_reserved"):
                    if "all-pings" in metric_obj.send_in_pings:
                        yield util.format_error(
                            filepath,
                            f"On instance {category_key}.{metric_key}",
                            'Only internal metrics may specify "all-pings" '
                            'in "send_in_pings"',
                            metric_val.defined_in["line"],
                        )
                        metric_obj = None
                    elif (
                        metric_obj.identifier()
                        in ("glean.attribution.ext", "glean.distribution.ext")
                        and metric_obj.type != "object"
                    ):
                        yield util.format_error(
                            filepath,
                            f"On instance {category_key}.{metric_key}",
                            "Extended attribution/distribution metrics must be of type 'object'",
                            metric_val.defined_in["line"],
                        )
                        metric_obj = None

            if metric_obj is not None:
                metric_obj.no_lint = sorted(set(metric_obj.no_lint + global_no_lint))
                if len(global_tags):
                    metric_obj.metadata["tags"] = sorted(
                        set(metric_obj.metadata.get("tags", []) + global_tags)
                    )

                if isinstance(filepath, Path):
                    metric_obj.defined_in["filepath"] = str(filepath)

            already_seen = sources.get((category_key, metric_key))
            if already_seen is not None:
                # We've seen this metric name already
                yield util.format_error(
                    filepath,
                    "",
                    (
                        f"Duplicate metric name '{category_key}.{metric_key}' "
                        f"already defined in '{already_seen}'"
                    ),
                    metric_obj.defined_in["line"],
                )
            else:
                all_objects[category_key][metric_key] = metric_obj
                sources[(category_key, metric_key)] = filepath


def _instantiate_pings(
    all_objects: ObjectTree,
    sources: Dict[Any, Path],
    content: Dict[str, util.JSONType],
    filepath: Path,
    config: Dict[str, Any],
) -> Generator[str, None, None]:
    """
    Load a list of pings.yaml files, convert the JSON information into Ping
    objects.
    """
    global_no_lint = content.get("no_lint", [])
    assert isinstance(global_no_lint, list)
    ping_schedule_reverse_map: Dict[str, Set[str]] = dict()

    for ping_key, ping_val in sorted(content.items()):
        if ping_key.startswith("$"):
            continue
        if ping_key == "no_lint":
            continue
        if not config.get("allow_reserved"):
            if ping_key in RESERVED_PING_NAMES:
                yield util.format_error(
                    filepath,
                    f"For ping '{ping_key}'",
                    f"Ping uses a reserved name ({RESERVED_PING_NAMES})",
                )
                continue
        if not isinstance(ping_val, dict):
            raise TypeError(f"Invalid content for ping {ping_key}")
        ping_val["name"] = ping_key

        if "metadata" in ping_val and "ping_schedule" in ping_val["metadata"]:
            if ping_key in ping_val["metadata"]["ping_schedule"]:
                yield util.format_error(
                    filepath,
                    f"For ping '{ping_key}'",
                    "ping_schedule contains its own ping name",
                )
                continue
            for ping_schedule in ping_val["metadata"]["ping_schedule"]:
                if ping_schedule not in ping_schedule_reverse_map:
                    ping_schedule_reverse_map[ping_schedule] = set()
                ping_schedule_reverse_map[ping_schedule].add(ping_key)

        try:
            ping_obj = Ping(
                defined_in=getattr(ping_val, "defined_in", None),
                _validated=True,
                **ping_val,
            )
        except Exception as e:
            yield util.format_error(filepath, f"On instance '{ping_key}'", str(e))
            continue

        if ping_obj is not None:
            ping_obj.no_lint = sorted(set(ping_obj.no_lint + global_no_lint))

        if isinstance(filepath, Path) and ping_obj.defined_in is not None:
            ping_obj.defined_in["filepath"] = str(filepath)

        already_seen = sources.get(ping_key)
        if already_seen is not None:
            # We've seen this ping name already
            yield util.format_error(
                filepath,
                "",
                f"Duplicate ping name '{ping_key}' "
                f"already defined in '{already_seen}'",
            )
        else:
            all_objects.setdefault("pings", {})[ping_key] = ping_obj
            sources[ping_key] = filepath

    for scheduler, scheduled in ping_schedule_reverse_map.items():
        if scheduler in all_objects["pings"] and isinstance(
            all_objects["pings"][scheduler], Ping
        ):
            scheduler_obj: Ping = cast(Ping, all_objects["pings"][scheduler])
            scheduler_obj.schedules_pings = sorted(list(scheduled))


def _instantiate_tags(
    all_objects: ObjectTree,
    sources: Dict[Any, Path],
    content: Dict[str, util.JSONType],
    filepath: Path,
    config: Dict[str, Any],
) -> Generator[str, None, None]:
    """
    Load a list of tags.yaml files, convert the JSON information into Tag
    objects.
    """
    global_no_lint = content.get("no_lint", [])
    assert isinstance(global_no_lint, list)

    for tag_key, tag_val in sorted(content.items()):
        if tag_key.startswith("$"):
            continue
        if tag_key == "no_lint":
            continue
        if not isinstance(tag_val, dict):
            raise TypeError(f"Invalid content for tag {tag_key}")
        tag_val["name"] = tag_key
        try:
            tag_obj = Tag(
                defined_in=getattr(tag_val, "defined_in", None),
                _validated=True,
                **tag_val,
            )
        except Exception as e:
            yield util.format_error(filepath, f"On instance '{tag_key}'", str(e))
            continue

        if tag_obj is not None:
            tag_obj.no_lint = sorted(set(tag_obj.no_lint + global_no_lint))

            if isinstance(filepath, Path) and tag_obj.defined_in is not None:
                tag_obj.defined_in["filepath"] = str(filepath)

        already_seen = sources.get(tag_key)
        if already_seen is not None:
            # We've seen this tag name already
            yield util.format_error(
                filepath,
                "",
                f"Duplicate tag name '{tag_key}' "
                f"already defined in '{already_seen}'",
            )
        else:
            all_objects.setdefault("tags", {})[tag_key] = tag_obj
            sources[tag_key] = filepath


def _preprocess_objects(objs: ObjectTree, config: Dict[str, Any]) -> ObjectTree:
    """
    Preprocess the object tree to better set defaults.
    """
    for category in objs.values():
        for obj in category.values():
            if not isinstance(obj, Metric):
                continue

            if not config.get("do_not_disable_expired", False) and hasattr(
                obj, "is_disabled"
            ):
                obj.disabled = obj.is_disabled()

            if hasattr(obj, "send_in_pings"):
                if "default" in obj.send_in_pings:
                    obj.send_in_pings = obj.default_store_names + [
                        x for x in obj.send_in_pings if x != "default"
                    ]
                obj.send_in_pings = sorted(list(set(obj.send_in_pings)))
    return objs


@util.keep_value
def parse_objects(
    filepaths: Iterable[Path], config: Optional[Dict[str, Any]] = None
) -> Generator[str, None, ObjectTree]:
    """
    Parse one or more metrics.yaml and/or pings.yaml files, returning a tree of
    `metrics.Metric`, `pings.Ping`, and `tags.Tag` instances.

    The result is a generator over any errors.  If there are no errors, the
    actual metrics can be obtained from `result.value`.  For example::

      result = metrics.parse_metrics(filepaths)
      for err in result:
          print(err)
      all_metrics = result.value

    The result value is a dictionary of category names to categories, where
    each category is a dictionary from metric name to `metrics.Metric`
    instances.  There are also the special categories `pings` and `tags`
    containing all of the `pings.Ping` and `tags.Tag` instances, respectively.

    :param filepaths: list of Path objects to metrics.yaml, pings.yaml, and/or
        tags.yaml files
    :param config: A dictionary of options that change parsing behavior.
        Supported keys are:

        - `allow_reserved`: Allow values reserved for internal Glean use.
        - `do_not_disable_expired`: Don't mark expired metrics as disabled.
          This is useful when you want to retain the original "disabled"
          value from the `metrics.yaml`, rather than having it overridden when
          the metric expires.
        - `allow_missing_files`: Do not raise a `FileNotFoundError` if any of
          the input `filepaths` do not exist.
        - `interesting`: Contains an array of interesting metrics/ping files.
          Probes not included in these files will be marked as disabled.
    """
    if config is None:
        config = {}

    all_objects: ObjectTree = DictWrapper()
    sources: Dict[Any, Path] = {}
    filepaths = util.ensure_list(filepaths)
    for filepath in filepaths:
        content, filetype = yield from _load_file(filepath, config)
        if filetype == "metrics":
            yield from _instantiate_metrics(
                all_objects, sources, content, filepath, config
            )
        elif filetype == "pings":
            yield from _instantiate_pings(
                all_objects, sources, content, filepath, config
            )
        elif filetype == "tags":
            yield from _instantiate_tags(
                all_objects, sources, content, filepath, config
            )

    if config.get("interesting"):
        # We're configured to disable probes not included in the interesting list.
        filepaths = util.ensure_list(config.get("interesting"))
        interesting_metrics_dict: Dict[str, Dict[str, Any]] = dict()
        interesting_metrics_dict.setdefault("metrics", DictWrapper())
        interesting_metrics_dict.setdefault("pings", DictWrapper())
        for filepath in filepaths:
            content, filetype = yield from _load_file(filepath, config)

            if not isinstance(content, dict):
                raise TypeError(f"Invalid content for {filepath}")

            for category_key, category_val in sorted(content.items()):
                if category_key.startswith("$"):
                    continue

                interesting_metrics_dict.setdefault(category_key, DictWrapper())

                if not isinstance(category_val, dict):
                    raise TypeError(f"Invalid category_val for {category_key}")

                for metric_key, metric_val in sorted(category_val.items()):
                    interesting_metrics_dict[category_key][metric_key] = metric_val

        for category_key, category_val in all_objects.items():
            if category_key == "tags":
                continue

            for metric_key, metric_val in sorted(category_val.items()):
                category_dict = interesting_metrics_dict.get(category_key, {})
                if metric_key not in category_dict:
                    obj = all_objects[category_key][metric_key]
                    if hasattr(obj, "disabled"):
                        obj.disabled = True

    return _preprocess_objects(all_objects, config)
