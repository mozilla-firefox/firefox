# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import enum
from pathlib import Path
import re
import sys
from typing import (
    Any,
    Callable,
    Dict,
    Generator,
    List,
    Iterable,
    Optional,
    Tuple,
    Union,
)  # noqa


from . import metrics
from . import parser
from . import pings
from . import tags
from . import util


# Yield only an error message
LintGenerator = Generator[str, None, None]

# Yield fully constructed GlinterNits
NitGenerator = Generator["GlinterNit", None, None]


class CheckType(enum.Enum):
    warning = 0
    error = 1


def _split_words(name: str) -> List[str]:
    """
    Helper function to split words on either `.` or `_`.
    """
    return re.split("[._-]", name)


def _english_list(items: List[str]) -> str:
    """
    Helper function to format a list [A, B, C] as "'A', 'B', or 'C'".
    """
    if len(items) == 0:
        return ""
    elif len(items) == 1:
        return f"'{items[0]}'"
    else:
        return "{}, or '{}'".format(
            ", ".join([f"'{x}'" for x in items[:-1]]), items[-1]
        )


def _hamming_distance(str1: str, str2: str) -> int:
    """
    Count the # of differences between strings str1 and str2,
    padding the shorter one with whitespace
    """

    diffs = 0
    if len(str1) < len(str2):
        str1, str2 = str2, str1
    len_dist = len(str1) - len(str2)
    str2 += " " * len_dist

    for ch1, ch2 in zip(str1, str2):
        if ch1 != ch2:
            diffs += 1
    return diffs


def check_common_prefix(
    category_name: str, metrics: Iterable[metrics.Metric]
) -> LintGenerator:
    """
    Check if all metrics begin with a common prefix.
    """
    metric_words = sorted([_split_words(metric.name) for metric in metrics])

    if len(metric_words) < 2:
        return

    first = metric_words[0]
    last = metric_words[-1]

    for i in range(min(len(first), len(last))):
        if first[i] != last[i]:
            break

    if i > 0:
        common_prefix = "_".join(first[:i])
        yield (
            f"Within category '{category_name}', all metrics begin with "
            f"prefix '{common_prefix}'."
            "Remove the prefixes on the metric names and (possibly) "
            "rename the category."
        )


def check_unit_in_name(
    metric: metrics.Metric, parser_config: Dict[str, Any]
) -> LintGenerator:
    """
    The metric name ends in a unit.
    """
    TIME_UNIT_ABBREV = {
        "nanosecond": "ns",
        "microsecond": "us",
        "millisecond": "ms",
        "second": "s",
        "minute": "m",
        "hour": "h",
        "day": "d",
    }

    MEMORY_UNIT_ABBREV = {
        "byte": "b",
        "kilobyte": "kb",
        "megabyte": "mb",
        "gigabyte": "gb",
    }

    name_words = _split_words(metric.name)
    unit_in_name = name_words[-1]

    time_unit = getattr(metric, "time_unit", None)
    memory_unit = getattr(metric, "memory_unit", None)

    if time_unit is not None:
        if (
            unit_in_name == TIME_UNIT_ABBREV.get(time_unit.name)
            or unit_in_name == time_unit.name
        ):
            yield (
                f"Suffix '{unit_in_name}' is redundant with time_unit "
                f"'{time_unit.name}'. Only include time_unit."
            )
        elif (
            unit_in_name in TIME_UNIT_ABBREV.keys()
            or unit_in_name in TIME_UNIT_ABBREV.values()
        ):
            yield (
                f"Suffix '{unit_in_name}' doesn't match time_unit "
                f"'{time_unit.name}'. "
                "Confirm the unit is correct and only include time_unit."
            )

    elif memory_unit is not None:
        if (
            unit_in_name == MEMORY_UNIT_ABBREV.get(memory_unit.name)
            or unit_in_name == memory_unit.name
        ):
            yield (
                f"Suffix '{unit_in_name}' is redundant with memory_unit "
                f"'{memory_unit.name}'. "
                "Only include memory_unit."
            )
        elif (
            unit_in_name in MEMORY_UNIT_ABBREV.keys()
            or unit_in_name in MEMORY_UNIT_ABBREV.values()
        ):
            yield (
                f"Suffix '{unit_in_name}' doesn't match memory_unit "
                f"{memory_unit.name}'. "
                "Confirm the unit is correct and only include memory_unit."
            )


def check_category_generic(
    category_name: str, metrics: Iterable[metrics.Metric]
) -> LintGenerator:
    """
    The category name is too generic.
    """
    GENERIC_CATEGORIES = ["metrics", "events"]

    if category_name in GENERIC_CATEGORIES:
        yield (
            f"Category '{category_name}' is too generic. "
            f"Don't use {_english_list(GENERIC_CATEGORIES)} for category names"
        )


def check_bug_number(
    metric: Union[metrics.Metric, pings.Ping], parser_config: Dict[str, Any]
) -> LintGenerator:
    number_bugs = [str(bug) for bug in metric.bugs if isinstance(bug, int)]

    if len(number_bugs):
        yield (
            f"For bugs {', '.join(number_bugs)}: "
            "Bug numbers are deprecated and should be changed to full URLs. "
            f"For example, use 'http://bugzilla.mozilla.org/{number_bugs[0]}' "
            f"instead of '{number_bugs[0]}'."
        )


def check_valid_in_baseline(
    metric: metrics.Metric, parser_config: Dict[str, Any]
) -> LintGenerator:
    allow_reserved = parser_config.get("allow_reserved", False)

    if not allow_reserved and "baseline" in metric.send_in_pings:
        yield (
            "The baseline ping is Glean-internal. "
            "Remove 'baseline' from the send_in_pings array."
        )


def check_misspelled_pings(
    metric: metrics.Metric, parser_config: Dict[str, Any]
) -> LintGenerator:
    for ping in metric.send_in_pings:
        for builtin in pings.RESERVED_PING_NAMES:
            distance = _hamming_distance(ping, builtin)
            if distance == 1:
                yield f"Ping '{ping}' seems misspelled. Did you mean '{builtin}'?"


def check_tags_required(
    metric_or_ping: Union[metrics.Metric, pings.Ping], parser_config: Dict[str, Any]
) -> LintGenerator:
    if parser_config.get("require_tags", False) and not len(
        metric_or_ping.metadata.get("tags", [])
    ):
        yield "Tags are required but no tags specified"


def check_user_lifetime_expiration(
    metric: metrics.Metric, parser_config: Dict[str, Any]
) -> LintGenerator:
    if metric.lifetime == metrics.Lifetime.user and metric.expires != "never":
        yield (
            "Metrics with 'user' lifetime cannot have an expiration date. "
            "They live as long as the user profile does. "
            "Set expires to 'never'."
        )


def check_expired_date(
    metric: metrics.Metric, parser_config: Dict[str, Any]
) -> LintGenerator:
    try:
        metric.validate_expires()
    except ValueError as e:
        yield (str(e))


def check_expired_metric(
    metric: metrics.Metric, parser_config: Dict[str, Any]
) -> LintGenerator:
    if metric.is_expired():
        yield ("Metric has expired. Please consider removing it.")


def check_old_event_api(
    metric: metrics.Metric, parser_config: Dict[str, Any]
) -> LintGenerator:
    # Glean v52.0.0 removed the old events API.
    # The metrics-2-0-0 schema still supports it.
    # We want to warn about it.
    # This can go when we introduce 3-0-0

    if not isinstance(metric, metrics.Event):
        return

    if not all("type" in x for x in metric.extra_keys.values()):
        yield ("The old event API is gone. Extra keys require a type.")


def check_metric_on_events_lifetime(
    metric: metrics.Metric, parser_config: Dict[str, Any]
) -> LintGenerator:
    """A non-event metric on the Events ping only makes sense if its value
    is immutable over the life of the ping."""
    if (
        "events" in metric.send_in_pings
        and "all_pings" not in metric.send_in_pings
        and metric.type != "event"
        and metric.lifetime == metrics.Lifetime.ping
    ):
        yield (
            "Non-event metrics sent on the Events ping should not have the ping"
            " lifetime."
        )


def check_unexpected_unit(
    metric: metrics.Metric, parser_config: Dict[str, Any]
) -> LintGenerator:
    """
    `unit` was allowed on all metrics and recently disallowed.
    We now warn about its use on all but quantity and custom distribution
    metrics.
    """
    allowed_types = [metrics.Quantity, metrics.CustomDistribution]
    if not any([isinstance(metric, ty) for ty in allowed_types]) and metric.unit:
        yield (
            "The `unit` property is only allowed for quantity "
            + "and custom distribution metrics."
        )


def check_empty_datareview(
    metric: metrics.Metric, parser_config: Dict[str, Any]
) -> LintGenerator:
    disallowed_datareview = ["", "todo"]
    data_reviews = [dr.lower() in disallowed_datareview for dr in metric.data_reviews]

    if any(data_reviews):
        yield "List of data reviews should not contain empty strings or TODO markers."


def check_redundant_ping(
    pings: pings.Ping, parser_config: Dict[str, Any]
) -> LintGenerator:
    """
    Check if the pings contains 'ping' as the prefix or suffix, or 'ping' or 'custom'
    """
    ping_words = _split_words(pings.name)

    if len(ping_words) != 0:
        ping_first_word = ping_words[0]
        ping_last_word = ping_words[-1]

        if ping_first_word == "ping":
            yield ("The prefix 'ping' is redundant.")
        elif ping_last_word == "ping":
            yield ("The suffix 'ping' is redundant.")
        elif "ping" in ping_words:
            yield ("The word 'ping' is redundant.")
        elif "custom" in ping_words:
            yield ("The word 'custom' is redundant.")


def check_unknown_ping(
    check_name: str,
    check_type: CheckType,
    all_pings: Dict[str, pings.Ping],
    metrics: Dict[str, metrics.Metric],
    parser_config: Dict[str, Any],
) -> NitGenerator:
    """
    Check that all pings in `send_in_pings` for all metrics are either a builtin ping
    or in the list of defined custom pings.
    """
    available_pings = [p for p in all_pings]

    for _, metric in metrics.items():
        if check_name in metric.no_lint:
            continue

        send_in_pings = metric.send_in_pings
        for target_ping in send_in_pings:
            if target_ping in pings.RESERVED_PING_NAMES:
                continue

            if target_ping not in available_pings:
                msg = f"Ping `{target_ping} `in `send_in_pings` is unknown."
                name = ".".join([metric.category, metric.name])
                nit = GlinterNit(
                    check_name,
                    name,
                    msg,
                    check_type,
                )
                yield nit


def check_name_too_similar(
    check_name: str,
    check_type: CheckType,
    all_pings: Dict[str, pings.Ping],
    all_metrics: Dict[str, metrics.Metric],
    parser_config: Dict[str, Any],
) -> NitGenerator:
    """
    Check that all metrics identifiers are suitably distinct.
    Require that at least n-1 of the similarly-named metrics must be no_lint'd to dismiss the lint.

    Current similarity test: the fully-qualified identifier differs solely in punctuation.
    e.g. formautofill.credit_cards and formautofill.creditcards
    """
    seen_metrics: Dict[str, metrics.Metric] = dict()

    for _, metric in all_metrics.items():
        if check_name in metric.no_lint:
            continue

        no_punc = metric.identifier().replace("_", "").replace(".", "")
        if no_punc in seen_metrics:
            msg = f"Metric `{metric.identifier()}`'s name is too similar to existing metric `{seen_metrics[no_punc].identifier()}`"
            nit = GlinterNit(check_name, metric.identifier(), msg, check_type)
            yield nit

        seen_metrics[no_punc] = metric


# The checks that operate on an entire category of metrics:
#    {NAME: (function, is_error)}
CATEGORY_CHECKS: Dict[
    str, Tuple[Callable[[str, Iterable[metrics.Metric]], LintGenerator], CheckType]
] = {
    "COMMON_PREFIX": (check_common_prefix, CheckType.error),
    "CATEGORY_GENERIC": (check_category_generic, CheckType.error),
}


# The checks that operate on individual metrics:
#     {NAME: (function, is_error)}
METRIC_CHECKS: Dict[
    str, Tuple[Callable[[metrics.Metric, dict], LintGenerator], CheckType]
] = {
    "UNIT_IN_NAME": (check_unit_in_name, CheckType.error),
    "BUG_NUMBER": (check_bug_number, CheckType.error),
    "BASELINE_PING": (check_valid_in_baseline, CheckType.error),
    "MISSPELLED_PING": (check_misspelled_pings, CheckType.error),
    "TAGS_REQUIRED": (check_tags_required, CheckType.error),
    "EXPIRATION_DATE_TOO_FAR": (check_expired_date, CheckType.warning),
    "USER_LIFETIME_EXPIRATION": (check_user_lifetime_expiration, CheckType.warning),
    "EXPIRED": (check_expired_metric, CheckType.warning),
    "OLD_EVENT_API": (check_old_event_api, CheckType.warning),
    "METRIC_ON_EVENTS_LIFETIME": (check_metric_on_events_lifetime, CheckType.error),
    "UNEXPECTED_UNIT": (check_unexpected_unit, CheckType.warning),
    "EMPTY_DATAREVIEW": (check_empty_datareview, CheckType.warning),
}


# The checks that operate on individual pings:
#     {NAME: (function, is_error)}
PING_CHECKS: Dict[
    str, Tuple[Callable[[pings.Ping, dict], LintGenerator], CheckType]
] = {
    "BUG_NUMBER": (check_bug_number, CheckType.error),
    "TAGS_REQUIRED": (check_tags_required, CheckType.error),
    "REDUNDANT_PING": (check_redundant_ping, CheckType.error),
}

ALL_OBJECT_CHECKS: Dict[
    str,
    Tuple[
        Callable[
            # check name, check type, pings, metrics, config
            [str, CheckType, dict, dict, dict],
            NitGenerator,
        ],
        CheckType,
    ],
] = {
    "UNKNOWN_PING_REFERENCED": (check_unknown_ping, CheckType.error),
    "NAME_TOO_SIMILAR": (check_name_too_similar, CheckType.error),
}


class GlinterNit:
    def __init__(self, check_name: str, name: str, msg: str, check_type: CheckType):
        self.check_name = check_name
        self.name = name
        self.msg = msg
        self.check_type = check_type

    def format(self):
        return (
            f"{self.check_type.name.upper()}: {self.check_name}: "
            f"{self.name}: {self.msg}"
        )


def _lint_item_tags(
    item_name: str,
    item_type: str,
    item_tag_names: List[str],
    valid_tag_names: List[str],
) -> List[GlinterNit]:
    invalid_tags = [tag for tag in item_tag_names if tag not in valid_tag_names]
    return (
        [
            GlinterNit(
                "INVALID_TAGS",
                item_name,
                f"Invalid tags specified in {item_type}: {', '.join(invalid_tags)}",
                CheckType.error,
            )
        ]
        if len(invalid_tags)
        else []
    )


def _lint_pings(
    category: Dict[str, Union[metrics.Metric, pings.Ping, tags.Tag]],
    parser_config: Dict[str, Any],
    valid_tag_names: List[str],
) -> List[GlinterNit]:
    nits: List[GlinterNit] = []

    for ping_name, ping in sorted(list(category.items())):
        assert isinstance(ping, pings.Ping)
        for check_name, (check_func, check_type) in PING_CHECKS.items():
            new_nits = list(check_func(ping, parser_config))
            if len(new_nits):
                if check_name not in ping.no_lint:
                    nits.extend(
                        GlinterNit(
                            check_name,
                            ping_name,
                            msg,
                            check_type,
                        )
                        for msg in new_nits
                    )
        nits.extend(
            _lint_item_tags(
                ping_name,
                "ping",
                ping.metadata.get("tags", []),
                valid_tag_names,
            )
        )
    return nits


def _lint_all_objects(
    objects: Dict[str, Dict[str, Union[metrics.Metric, pings.Ping, tags.Tag]]],
    parser_config: Dict[str, Any],
) -> List[GlinterNit]:
    nits: List[GlinterNit] = []

    pings = objects.get("pings")
    if not pings:
        return []

    metrics = objects.get("all_metrics")
    if not metrics:
        return []

    for check_name, (check_func, check_type) in ALL_OBJECT_CHECKS.items():
        new_nits = list(
            check_func(check_name, check_type, pings, metrics, parser_config)
        )
        nits.extend(new_nits)

    return nits


def lint_metrics(
    objs: metrics.ObjectTree,
    parser_config: Optional[Dict[str, Any]] = None,
    file=sys.stderr,
) -> List[GlinterNit]:
    """
    Performs glinter checks on a set of metrics objects.

    :param objs: Tree of metric objects, as returns by `parser.parse_objects`.
    :param file: The stream to write errors to.
    :returns: List of nits.
    """
    if parser_config is None:
        parser_config = {}

    nits: List[GlinterNit] = []
    valid_tag_names = [tag for tag in objs.get("tags", [])]

    nits.extend(_lint_all_objects(objs, parser_config))

    for category_name, category in sorted(list(objs.items())):
        if category_name == "pings":
            nits.extend(_lint_pings(category, parser_config, valid_tag_names))
            continue

        if category_name == "tags":
            # currently we have no linting for tags
            continue

        # Make sure the category has only Metrics, not Pings or Tags
        category_metrics = dict(
            (name, metric)
            for (name, metric) in category.items()
            if isinstance(metric, metrics.Metric)
        )

        for cat_check_name, (cat_check_func, check_type) in CATEGORY_CHECKS.items():
            if any(
                cat_check_name in metric.no_lint for metric in category_metrics.values()
            ):
                continue
            nits.extend(
                GlinterNit(cat_check_name, category_name, msg, check_type)
                for msg in cat_check_func(category_name, category_metrics.values())
            )

        for _metric_name, metric in sorted(list(category_metrics.items())):
            for check_name, (check_func, check_type) in METRIC_CHECKS.items():
                new_nits = list(check_func(metric, parser_config))
                if len(new_nits):
                    if check_name not in metric.no_lint:
                        nits.extend(
                            GlinterNit(
                                check_name,
                                ".".join([metric.category, metric.name]),
                                msg,
                                check_type,
                            )
                            for msg in new_nits
                        )

            # also check that tags for metric are valid
            nits.extend(
                _lint_item_tags(
                    ".".join([metric.category, metric.name]),
                    "metric",
                    metric.metadata.get("tags", []),
                    valid_tag_names,
                )
            )

    if len(nits):
        print("Sorry, Glean found some glinter nits:", file=file)
        for nit in nits:
            print(nit.format(), file=file)
        print("", file=file)
        print("Please fix the above nits to continue.", file=file)
        print(
            "To disable a check, add a `no_lint` parameter "
            "with a list of check names to disable.\n"
            "This parameter can appear with each individual metric, or at the "
            "top-level to affect the entire file.",
            file=file,
        )

    return nits


def glinter(
    input_filepaths: Iterable[Path],
    parser_config: Optional[Dict[str, Any]] = None,
    file=sys.stderr,
) -> int:
    """
    Commandline helper for glinter.

    :param input_filepaths: List of Path objects to load metrics from.
    :param parser_config: Parser configuration object, passed to
      `parser.parse_objects`.
    :param file: The stream to write the errors to.
    :return: Non-zero if there were any glinter errors.
    """
    if parser_config is None:
        parser_config = {}

    errors = 0

    objs = parser.parse_objects(input_filepaths, parser_config)
    errors += util.report_validation_errors(objs)

    nits = lint_metrics(objs.value, parser_config=parser_config, file=file)
    errors += len([nit for nit in nits if nit.check_type == CheckType.error])

    if errors == 0:
        print("✨ Your metrics are Glean! ✨", file=file)
        return 0

    print(f"❌ Found {errors} errors.")

    return 1
