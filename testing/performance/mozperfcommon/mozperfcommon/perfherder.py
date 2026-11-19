# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from datetime import datetime, timezone

import requests

from .utils import format_date

TREEHERDER_URL = "https://treeherder.mozilla.org"
DEFAULT_REPOSITORY = "autoland"
USER_AGENT = "mozperfcommon"


class PushNotFoundError(Exception):
    """Raised when a push is not found for a specific revision."""


class PerfherderClient:
    """Helper client for getting performance data from Perfherder.

    :param server_url: Root URL of the Treeherder instance.
    :param repository: Treeherder repository (project) to query.
    :param timeout: Timeout in seconds for each request.
    """

    def __init__(
        self,
        server_url=TREEHERDER_URL,
        repository=DEFAULT_REPOSITORY,
        timeout=30,
    ):
        self.server_url = server_url.rstrip("/")
        self.repository = repository
        self.timeout = timeout
        self.session = requests.Session()
        self.session.headers.update({
            "User-Agent": USER_AGENT,
            "Accept": "application/json",
        })

    def _get(self, path, params=None):
        response = self.session.get(
            f"{self.server_url}/api/{path}", params=params, timeout=self.timeout
        )
        response.raise_for_status()
        return response.json()

    def get_push_time(self, revision, repository=None):
        """Return the time, as a UTC datetime, at which `revision` was pushed to
        `repository` (defaults to the client's repository)."""
        repository = repository or self.repository
        pushes = self._get(
            f"project/{repository}/push/", params={"revision": revision}
        )["results"]
        if not pushes:
            raise PushNotFoundError(
                f"No {repository} push found for revision {revision}"
            )
        return datetime.fromtimestamp(pushes[0]["push_timestamp"], timezone.utc)

    def get_signatures(self, framework_id, platform, start_date, subtests=True):
        """Return the signatures, keyed by id, that received data on `platform`
        since `start_date`. Signatures that have a parent are only included when
        `subtests` is True."""
        return self._get(
            f"project/{self.repository}/performance/signatures/",
            params={
                "framework": framework_id,
                "platform": platform,
                "start_date": format_date(start_date),
                "subtests": int(subtests),
            },
        )

    def get_summary(
        self,
        start_date,
        end_date,
        signature_id=None,
        parent_signature_id=None,
        replicates=True,
        all_data=False,
    ):
        """Return the summaries of a signature, or of all the subtests of a parent
        signature, with the values pushed after `start_date` and before
        `end_date`.

        Each summary has the values in ``values``. With `replicates`, those are
        the replicates of each data point, or the data point itself when it has
        none. With `all_data`, which only applies to `signature_id`, the values
        are instead in ``data``, along with the push they come from.
        """
        if signature_id is None and parent_signature_id is None:
            raise ValueError("Either signature_id or parent_signature_id is required")
        if all_data and parent_signature_id is not None:
            raise ValueError("all_data only applies to signature_id")

        params = {
            "repository": self.repository,
            "startday": format_date(start_date),
            "endday": format_date(end_date),
            "replicates": str(replicates).lower(),
        }
        if signature_id is not None:
            params["signature"] = signature_id
        if parent_signature_id is not None:
            params["parent_signature"] = parent_signature_id
        if all_data:
            params["all_data"] = "true"
        return self._get("performance/summary/", params=params)
