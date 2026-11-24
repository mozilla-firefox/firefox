# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Classes for each of the high-level metric types.
"""

import enum
from typing import Any, Dict, List, Optional, Type, Union  # noqa


from . import flows
from . import pings
from . import tags
from . import util


# Important: if the values are ever changing here, make sure
# to also fix mozilla/glean. Otherwise language bindings may
# break there.
class Lifetime(enum.Enum):
    ping = 0
    application = 1
    user = 2


class DataSensitivity(enum.Enum):
    technical = 1
    interaction = 2
    stored_content = 3
    web_activity = 3  # Old, deprecated name
    highly_sensitive = 4


class Metric:
    typename: str = "ERROR"
    glean_internal_metric_cat: str = "glean.internal.metrics"
    metric_types: Dict[str, Any] = {}
    default_store_names: List[str] = ["metrics"]

    def __init__(
        self,
        type: str,
        category: str,
        name: str,
        bugs: List[str],
        description: str,
        notification_emails: List[str],
        expires: Any,
        metadata: Optional[Dict] = None,
        data_reviews: Optional[List[str]] = None,
        version: int = 0,
        disabled: bool = False,
        lifetime: str = "ping",
        send_in_pings: Optional[List[str]] = None,
        unit: Optional[str] = None,
        gecko_datapoint: str = "",
        no_lint: Optional[List[str]] = None,
        data_sensitivity: Optional[List[str]] = None,
        defined_in: Optional[Dict] = None,
        telemetry_mirror: Optional[str] = None,
        in_session: bool = False,
        _config: Optional[Dict[str, Any]] = None,
        _validated: bool = False,
    ):
        # Avoid cyclical import
        from . import parser

        self.type = type
        self.category = category
        self.name = name
        self.bugs = bugs
        self.description = description
        self.notification_emails = notification_emails
        self.expires = expires
        if metadata is None:
            metadata = {}
        self.metadata = metadata
        if data_reviews is None:
            data_reviews = []
        self.data_reviews = data_reviews
        self.version = version
        self.disabled = disabled
        self.lifetime = getattr(Lifetime, lifetime)
        if send_in_pings is None:
            send_in_pings = ["default"]
        self.send_in_pings = send_in_pings
        self.unit = unit
        self.gecko_datapoint = gecko_datapoint
        if no_lint is None:
            no_lint = []
        self.no_lint = no_lint
        if data_sensitivity is not None:
            self.data_sensitivity = [
                getattr(DataSensitivity, x) for x in data_sensitivity
            ]
        self.defined_in = defined_in
        if telemetry_mirror is not None:
            self.telemetry_mirror = telemetry_mirror
        if not hasattr(self, "in_session"):
            self.in_session = in_session

        # _validated indicates whether this metric has already been jsonschema
        # validated (but not any of the Python-level validation).
        if not _validated:
            data = {
                "$schema": parser.METRICS_ID,
                self.category: {self.name: self._serialize_input()},
            }  # type: Dict[str, util.JSONType]
            for error in parser.validate(data):
                raise ValueError(error)

        # Store the config, but only after validation.
        if _config is None:
            _config = {}
        self._config = _config

        # Metrics in the special category "glean.internal.metrics" need to have
        # an empty category string when identifying the metrics in the ping.
        if self.category == Metric.glean_internal_metric_cat:
            self.category = ""

    def __init_subclass__(cls, **kwargs):
        # Create a mapping of all of the subclasses of this class
        if cls not in Metric.metric_types and hasattr(cls, "typename"):
            Metric.metric_types[cls.typename] = cls
        super().__init_subclass__(**kwargs)

    @classmethod
    def make_metric(
        cls,
        category: str,
        name: str,
        metric_info: Dict[str, util.JSONType],
        config: Optional[Dict[str, Any]] = None,
        validated: bool = False,
    ):
        """
        Given a metric_info dictionary from metrics.yaml, return a metric
        instance.

        :param: category The category the metric lives in
        :param: name The name of the metric
        :param: metric_info A dictionary of the remaining metric parameters
        :param: config A dictionary containing commandline configuration
            parameters
        :param: validated True if the metric has already gone through
            jsonschema validation
        :return: A new Metric instance.
        """
        if config is None:
            config = {}

        metric_type = metric_info["type"]
        if not isinstance(metric_type, str):
            raise TypeError(f"Unknown metric type {metric_type}")
        return cls.metric_types[metric_type](
            category=category,
            name=name,
            defined_in=getattr(metric_info, "defined_in", None),
            _validated=validated,
            _config=config,
            **metric_info,
        )

    def serialize(
        self, rename_fields: Optional[Dict[str, str]] = None
    ) -> Dict[str, util.JSONType]:
        """
        Serialize the metric back to JSON object model.
        """
        d = self.__dict__.copy()
        # Convert enum fields back to strings
        for key, val in d.items():
            if isinstance(val, enum.Enum):
                d[key] = d[key].name
            if isinstance(val, set):
                d[key] = sorted(list(val))
            if isinstance(val, list) and len(val) and isinstance(val[0], enum.Enum):
                d[key] = [x.name for x in val]

        if rename_fields is not None:
            for key, val in rename_fields.items():
                if key in d:
                    d[val] = d[key]
                    del d[key]

        del d["name"]
        del d["category"]
        if not d["unit"]:
            d.pop("unit")
        d.pop("_config", None)
        d.pop("_generate_enums", None)
        d.pop("_generate_structure", None)
        return d

    def _serialize_input(self) -> Dict[str, util.JSONType]:
        d = self.serialize()
        modified_dict = util.remove_output_params(d, "defined_in")
        return modified_dict

    def identifier(self) -> str:
        """
        Create an identifier unique for this metric.
        Generally, category.name; however, Glean internal
        metrics only use name.
        """
        if not self.category:
            return self.name
        return ".".join((self.category, self.name))

    def is_disabled(self) -> bool:
        return self.disabled or self.is_expired()

    def is_expired(self) -> bool:
        def default_handler(expires) -> bool:
            return util.is_expired(expires, self._config.get("expire_by_version"))

        return self._config.get("custom_is_expired", default_handler)(self.expires)

    def validate_expires(self):
        def default_handler(expires):
            return util.validate_expires(expires, self._config.get("expire_by_version"))

        return self._config.get("custom_validate_expires", default_handler)(
            self.expires
        )

    def is_internal_metric(self) -> bool:
        return self.category in (Metric.glean_internal_metric_cat, "")


class Boolean(Metric):
    typename = "boolean"


class String(Metric):
    typename = "string"


class StringList(Metric):
    typename = "string_list"


class Counter(Metric):
    typename = "counter"


class Quantity(Metric):
    typename = "quantity"


class TimeUnit(enum.Enum):
    nanosecond = 0
    microsecond = 1
    millisecond = 2
    second = 3
    minute = 4
    hour = 5
    day = 6


class TimeBase(Metric):
    def __init__(self, *args, **kwargs):
        self.time_unit = getattr(TimeUnit, kwargs.pop("time_unit", "millisecond"))
        super().__init__(*args, **kwargs)


class Timespan(TimeBase):
    typename = "timespan"


class TimingDistribution(TimeBase):
    typename = "timing_distribution"

    def __init__(self, *args, **kwargs):
        self.time_unit = getattr(TimeUnit, kwargs.pop("time_unit", "nanosecond"))
        Metric.__init__(self, *args, **kwargs)


class MemoryUnit(enum.Enum):
    byte = 0
    kilobyte = 1
    megabyte = 2
    gigabyte = 3


class MemoryDistribution(Metric):
    typename = "memory_distribution"

    def __init__(self, *args, **kwargs):
        self.memory_unit = getattr(MemoryUnit, kwargs.pop("memory_unit", "byte"))
        super().__init__(*args, **kwargs)


class HistogramType(enum.Enum):
    linear = 0
    exponential = 1


class CustomDistribution(Metric):
    typename = "custom_distribution"

    def __init__(self, *args, **kwargs):
        self.range_min = kwargs.pop("range_min", 1)
        self.range_max = kwargs.pop("range_max")
        self.bucket_count = kwargs.pop("bucket_count")
        self.histogram_type = getattr(
            HistogramType, kwargs.pop("histogram_type", "exponential")
        )
        super().__init__(*args, **kwargs)


class Datetime(TimeBase):
    typename = "datetime"


class Event(Metric):
    typename = "event"

    default_store_names = ["events"]

    def __init__(self, *args, **kwargs):
        self.extra_keys = kwargs.pop("extra_keys", {})
        self.in_session = kwargs.pop("in_session", True)
        self.validate_extra_keys(self.extra_keys, kwargs.get("_config", {}))
        super().__init__(*args, **kwargs)
        self._generate_enums = [("allowed_extra_keys_with_types", "Extra")]

    @property
    def allowed_extra_keys(self):
        # Sort keys so that output is deterministic
        return sorted(list(self.extra_keys.keys()))

    @property
    def allowed_extra_keys_with_types(self):
        # Sort keys so that output is deterministic
        return sorted(
            [(k, v.get("type", "string")) for (k, v) in self.extra_keys.items()],
            key=lambda x: x[0],
        )

    @staticmethod
    def validate_extra_keys(extra_keys: Dict[str, str], config: Dict[str, Any]) -> None:
        if not config.get("allow_reserved") and any(
            k.startswith("glean.") for k in extra_keys.keys()
        ):
            raise ValueError(
                "Extra keys beginning with 'glean.' are reserved for "
                "Glean internal use."
            )


class Uuid(Metric):
    typename = "uuid"


class Url(Metric):
    typename = "url"


class Jwe(Metric):
    typename = "jwe"

    def __init__(self, *args, **kwargs):
        raise ValueError(
            "JWE support was removed. "
            "If you require this send an email to glean-team@mozilla.com."
        )


class CowString(str):
    """
    Wrapper class for strings that should be represented
    as a `Cow<'static, str>` in Rust,
    or `String` in other target languages.

    This wraps `str`, so unless `CowString` is specifically
    handled it acts (and serializes)
    as a string.
    """

    def __init__(self, val: str):
        self.inner: str = val

    def __eq__(self, other):
        return self.inner == other.inner

    def __hash__(self):
        return self.inner.__hash__()

    def __lt__(self, other):
        return self.inner.__lt__(other.inner)


class Labeled(Metric):
    labeled = True

    def __init__(self, *args, **kwargs):
        labels = kwargs.pop("labels", None)
        if labels is not None:
            self.ordered_labels = labels
            self.labels = set([CowString(label) for label in labels])
        else:
            self.ordered_labels = None
            self.labels = None
        super().__init__(*args, **kwargs)

    def serialize(
        self, rename_fields: Optional[Dict[str, str]] = None
    ) -> Dict[str, util.JSONType]:
        """
        Serialize the metric back to JSON object model.
        """
        d = super().serialize(rename_fields=rename_fields)
        d["labels"] = self.ordered_labels
        del d["ordered_labels"]
        return d


class LabeledBoolean(Labeled, Boolean):
    typename = "labeled_boolean"


class LabeledString(Labeled, String):
    typename = "labeled_string"


class LabeledCounter(Labeled, Counter):
    typename = "labeled_counter"


class LabeledCustomDistribution(Labeled, CustomDistribution):
    typename = "labeled_custom_distribution"


class LabeledMemoryDistribution(Labeled, MemoryDistribution):
    typename = "labeled_memory_distribution"


class LabeledTimingDistribution(Labeled, TimingDistribution):
    typename = "labeled_timing_distribution"


class LabeledQuantity(Labeled, Quantity):
    typename = "labeled_quantity"


class Rate(Metric):
    typename = "rate"

    def __init__(self, *args, **kwargs):
        self.denominator_metric = kwargs.pop("denominator_metric", None)
        super().__init__(*args, **kwargs)


class Denominator(Counter):
    typename = "denominator"
    # A denominator is a counter with an additional list of numerators.
    numerators: List[Rate] = []


class Text(Metric):
    typename = "text"


class Object(Metric):
    typename = "object"

    def __init__(self, *args, **kwargs):
        structure = kwargs.pop("structure", None)
        if not structure:
            raise ValueError("`object` is missing required parameter `structure`")

        self._generate_structure = self.validate_structure(structure)
        super().__init__(*args, **kwargs)

    ALLOWED_TOPLEVEL = {"type", "properties", "items", "description", "oneOf"}
    ALLOWED_TYPES = ["object", "array", "number", "string", "boolean"]
    ALLOWED_SUBTYPES = ["number", "string", "boolean"]
    ALLOWED_ONEOF_FIELDS = {"description", "type"}

    @staticmethod
    def _validate_substructure(structure):
        extra = set(structure.keys()) - Object.ALLOWED_TOPLEVEL
        if extra:
            extra = ", ".join(extra)
            allowed = ", ".join(Object.ALLOWED_TOPLEVEL)
            raise ValueError(
                f"Found additional fields: {extra}. Only allowed: {allowed}"
            )

        if "oneOf" in structure:
            subtypes = structure.pop("oneOf")
            structure["type"] = "oneof"
            structure["subtypes"] = []
            if not subtypes:
                raise ValueError("List of types required.")

            for typ in subtypes:
                extra = set(typ.keys()) - Object.ALLOWED_ONEOF_FIELDS
                if extra:
                    extra = ", ".join(extra)
                    allowed = ", ".join(Object.ALLOWED_ONEOF_FIELDS)
                    raise ValueError(
                        f"Found additional fields: {extra}. Only allowed: {allowed}"
                    )

                ty = typ.get("type")
                if not ty:
                    raise ValueError("element of `oneOf` list must contain a type")

                if ty not in Object.ALLOWED_SUBTYPES:
                    raise ValueError(
                        f"invalid `type` in `oneOf` list. found: {ty}, only allowed: {Object.ALLOWED_SUBTYPES}"
                    )

                structure["subtypes"].append(ty)
            return structure

        if "type" not in structure:
            raise ValueError(
                f"missing `type` in object structure. Allowed: {Object.ALLOWED_TYPES}"
            )
        if structure["type"] not in Object.ALLOWED_TYPES:
            raise ValueError(
                "invalid `type` in object structure. found: {}, allowed: {}".format(
                    structure["type"], Object.ALLOWED_TYPES
                )
            )

        if structure["type"] == "object":
            if "items" in structure:
                raise ValueError("`items` not allowed in object structure")

            if "properties" not in structure:
                raise ValueError("`properties` missing for type `object`")

            for key in structure["properties"]:
                value = structure["properties"][key]
                structure["properties"][key] = Object._validate_substructure(value)

        if structure["type"] == "array":
            if "properties" in structure:
                raise ValueError("`properties` not allowed in array structure")

            if "items" not in structure:
                raise ValueError("`items` missing for type `array`")

            value = structure["items"]
            structure["items"] = Object._validate_substructure(value)

        return structure

    @staticmethod
    def validate_structure(structure):
        if None:
            raise ValueError("`structure` needed for object metric.")

        # Different from `ALLOWED_TYPES`:
        # We _require_ the root type to be an object or array.
        allowed_types = ["object", "array"]
        if "type" not in structure:
            raise ValueError(
                f"missing `type` in object structure. Allowed: {allowed_types}"
            )
        if structure["type"] not in allowed_types:
            raise ValueError(
                "invalid `type` in object structure. found: {}, allowed: {}".format(
                    structure["type"], allowed_types
                )
            )

        structure = Object._validate_substructure(structure)
        return structure


class DualLabeledCounter(Metric):
    typename = "dual_labeled_counter"
    dual_labeled = True

    def __init__(self, *args, **kwargs):
        dual_labels = kwargs.pop("dual_labels", None)
        if not dual_labels:
            raise ValueError(
                "`dual_labeled_counter` is missing required parameter `dual_labels`"
            )
        k = dual_labels.get("key", None)
        if not k:
            raise ValueError("`dual_labels` is missing required parameter `key`")
        c = dual_labels.get("category", None)
        if not c:
            raise ValueError("`dual_labels` is missing required parameter `categories`")
        keys = k.get("labels", None)
        if keys is not None:
            if not isinstance(keys, list) or not all(isinstance(k, str) for k in keys):
                raise ValueError("key `labels` must be a list of strings")
            self.ordered_keys = keys
            self.keys = set([CowString(key) for key in keys])
        else:
            self.ordered_keys = None
            self.keys = None
        categories = c.get("labels", None)
        if categories is not None:
            if not isinstance(categories, list) or not all(
                isinstance(c, str) for c in categories
            ):
                raise ValueError("category `labels` must be a list of strings")
            self.ordered_categories = categories
            self.categories = set([CowString(category) for category in categories])
        else:
            self.ordered_categories = None
            self.categories = None
        super().__init__(*args, **kwargs)

    def serialize(
        self, rename_fields: Optional[Dict[str, str]] = None
    ) -> Dict[str, util.JSONType]:
        """
        Serialize the metric back to JSON object model.
        """
        d = super().serialize(rename_fields=rename_fields)
        d["keys"] = self.ordered_keys
        d["categories"] = self.ordered_categories
        del d["ordered_keys"]
        del d["ordered_categories"]
        return d


ObjectTree = Dict[str, Dict[str, Union[Metric, pings.Ping, tags.Tag, flows.Flow]]]
