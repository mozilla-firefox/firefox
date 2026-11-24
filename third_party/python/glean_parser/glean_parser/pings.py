# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Classes for managing the description of pings.
"""

from typing import Dict, List, Optional


from . import util


RESERVED_PING_NAMES = [
    "baseline",
    "metrics",
    "events",
    "deletion-request",
    "default",
    "server-deletion-request",
    "tags",
    "pings",
    "health",
]


class Ping:
    def __init__(
        self,
        name: str,
        description: str,
        bugs: List[str],
        notification_emails: List[str],
        metadata: Optional[Dict] = None,
        data_reviews: Optional[List[str]] = None,
        uploader_capabilities: Optional[List[str]] = None,
        include_client_id: bool = False,
        send_if_empty: bool = False,
        reasons: Optional[Dict[str, str]] = None,
        defined_in: Optional[Dict] = None,
        no_lint: Optional[List[str]] = None,
        enabled: Optional[bool] = None,
        _validated: bool = False,
    ):
        # Avoid cyclical import
        from . import parser

        self.name = name
        self.description = description

        self.bugs = bugs
        self.notification_emails = notification_emails
        if metadata is None:
            metadata = {}
        self.metadata = metadata
        self.precise_timestamps = self.metadata.get("precise_timestamps", True)
        self.include_info_sections = self.metadata.get("include_info_sections", True)
        self.follows_collection_enabled = self.metadata.get(
            "follows_collection_enabled", True
        )
        if enabled is None:
            enabled = True
        self.enabled = enabled
        if not self.follows_collection_enabled:
            self.enabled = False
        self.schedules_pings: List[str] = []
        if data_reviews is None:
            data_reviews = []
        self.data_reviews = data_reviews
        if not uploader_capabilities:
            uploader_capabilities = []
        self.uploader_capabilities = uploader_capabilities
        self.include_client_id = include_client_id
        self.send_if_empty = send_if_empty
        if reasons is None:
            reasons = {}
        self.reasons = reasons
        self.defined_in = defined_in
        if no_lint is None:
            no_lint = []
        self.no_lint = no_lint

        # _validated indicates whether this ping has already been jsonschema
        # validated (but not any of the Python-level validation).
        if not _validated:
            data: Dict[str, util.JSONType] = {
                "$schema": parser.PINGS_ID,
                self.name: self._serialize_input(),
            }
            for error in parser.validate(data):
                raise ValueError(error)

    _generate_enums = [("reason_codes", "ReasonCodes")]

    @property
    def type(self) -> str:
        return "ping"

    @property
    def reason_codes(self) -> List[str]:
        return sorted(list(self.reasons.keys()))

    def serialize(self) -> Dict[str, util.JSONType]:
        """
        Serialize the metric back to JSON object model.
        """
        d = self.__dict__.copy()
        del d["name"]
        return d

    def _serialize_input(self) -> Dict[str, util.JSONType]:
        d = self.serialize()
        modified_dict = util.remove_output_params(d, "defined_in")
        modified_dict = util.remove_output_params(modified_dict, "precise_timestamps")
        modified_dict = util.remove_output_params(
            modified_dict, "include_info_sections"
        )
        modified_dict = util.remove_output_params(
            modified_dict, "follows_collection_enabled"
        )
        modified_dict = util.remove_output_params(modified_dict, "schedules_pings")
        return modified_dict

    def identifier(self) -> str:
        """
        Used for the "generated from ..." comment in the output.
        """
        return self.name
