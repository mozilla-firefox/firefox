# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Classes for each of the high-level flow types.
"""

from typing import Any, Dict, List, Mapping, Optional, Set

from . import util


# Milliseconds per `auto_close_timeout` unit.
TIMEOUT_UNIT_MS = {
    "s": 1000,
    "m": 60 * 1000,
    "h": 60 * 60 * 1000,
    "d": 24 * 60 * 60 * 1000,
}

# The roles an event can play in a flow, in the order they are listed in
# `flows.yaml`.
ROLES = ("starts_on", "ends_on", "includes")


class Flow:
    def __init__(
        self,
        name: str,
        description: str,
        bugs: List[str],
        data_reviews: List[str],
        notification_emails: List[str],
        starts_on: List[str],
        ends_on: Optional[List[str]] = None,
        includes: Optional[List[str]] = None,
        auto_close_timeout: Optional[str] = None,
        disabled: bool = False,
        defined_in: Optional[Dict] = None,
        no_lint: Optional[List[str]] = None,
        _validated: bool = False,
    ):
        # Avoid cyclical import
        from . import parser

        self.name = name
        self.description = description
        self.bugs = bugs
        self.data_reviews = data_reviews
        self.notification_emails = notification_emails
        self.starts_on = starts_on
        self.ends_on = ends_on if ends_on is not None else []
        self.includes = includes if includes is not None else []
        self.auto_close_timeout = auto_close_timeout
        self.disabled = disabled
        self.defined_in = defined_in
        if no_lint is None:
            no_lint = []
        self.no_lint = no_lint

        # The events this flow names that are not defined in any of the files
        # given to the parser, and are assumed to be defined elsewhere. Filled
        # in once every file has been parsed.
        self.unresolved: Set[str] = set()

        # _validated indicates whether this flow has already been jsonschema
        # validated (but not any of the Python-level validation).
        if not _validated:
            data: Dict[str, util.JSONType] = {
                "$schema": parser.FLOWS_ID,
                self.name: self._serialize_input(),
            }
            for error in parser.validate(data):
                raise ValueError(error)

        if not self.starts_on:
            raise ValueError("A flow must have at least one event in `starts_on`.")

        # An event may play at most one role in a flow.
        seen: Dict[str, str] = {}
        for role in ROLES:
            for event in getattr(self, role):
                if event in seen:
                    raise ValueError(
                        f"Event '{event}' is in both `{seen[event]}` and `{role}`. "
                        "An event may appear in at most one of a flow's lists."
                    )
                seen[event] = role

    @property
    def type(self) -> str:
        return "flow"

    @property
    def auto_close_timeout_ms(self) -> Optional[int]:
        """
        The `auto_close_timeout`, in milliseconds, or `None` if unset.
        """
        if self.auto_close_timeout is None:
            return None
        value, unit = self.auto_close_timeout[:-1], self.auto_close_timeout[-1]
        return int(value) * TIMEOUT_UNIT_MS[unit]

    def events(self) -> Dict[str, str]:
        """
        Every event this flow names, mapped to its role: `starts_on`,
        `ends_on`, or `includes`.
        """
        return {event: role for role in ROLES for event in getattr(self, role)}

    def _serialize_input(self) -> Dict[str, util.JSONType]:
        d = self.serialize()
        modified_dict = util.remove_output_params(d, "defined_in")
        modified_dict = util.remove_output_params(modified_dict, "unresolved")
        return modified_dict

    def serialize(self) -> Dict[str, util.JSONType]:
        """
        Serialize the flow back to JSON object model.
        """
        d = self.__dict__.copy()
        del d["name"]
        d["unresolved"] = sorted(self.unresolved)
        if d["auto_close_timeout"] is None:
            del d["auto_close_timeout"]
        return d


def flows_in(objs: Mapping[str, Any]) -> Dict[str, "Flow"]:
    """
    Returns the flows in a parsed object tree, keyed by name.

    Flows are stored under the `flows` key, which is also a valid metric
    category name. The key holds flows only if it holds `Flow` objects; the
    parser rejects a tree that mixes the two.
    """
    candidate = objs.get("flows")
    if not candidate:
        return {}
    if not all(isinstance(obj, Flow) for obj in candidate.values()):
        return {}
    return candidate


# The SDK's name for each role in a flow.
_REGISTRATION_ROLES = {"starts_on": "Start", "ends_on": "End", "includes": "Member"}


def registration_data(flows: Mapping[str, "Flow"]) -> List[Dict[str, Any]]:
    """
    The flow registration data the Glean SDK loads at initialization, in a
    form the code generators can render directly.

    Flows are emitted as authored, flow to events, in name order. Each event
    carries its role and whether it was resolved; the SDK builds the reverse
    index itself.
    """
    return [
        {
            "name": flow.name,
            "description": flow.description,
            "events": [
                {
                    "identifier": event,
                    "role": _REGISTRATION_ROLES[role],
                    "unresolved": event in flow.unresolved,
                }
                for event, role in sorted(flow.events().items())
            ],
            "auto_close_timeout_ms": flow.auto_close_timeout_ms,
            "disabled": flow.disabled,
        }
        for _, flow in sorted(flows.items())
    ]
