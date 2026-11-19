# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import logging
from datetime import datetime, timedelta, timezone

import requests

from .comparisons import DEFAULT_COMPARISON, DEFAULT_THRESHOLD, Comparisons
from .expected_changes import find_expected_change, load_expected_changes
from .perfherder import DEFAULT_REPOSITORY, PerfherderClient, PushNotFoundError
from .utils import get_ci_revision, get_framework_id, signature_key

DEFAULT_DAYS = 7
DEFAULT_PUSH_LIMIT = None

# Minimum number of new values a suite or subtest needs to be compared
NEW_MIN_NUMS = 2

LOG = logging.getLogger(__name__)


def should_alert(suite, subtest=None):
    """Return whether Perfherder alerts on a suite value, or on one of its
    subtests when `subtest` is given.

    Suite values alert unless ``shouldAlert`` is false. Subtests alert when
    ``shouldAlert`` is true, or when it's unset and their suite has no value.
    """
    if subtest is None:
        return suite.get("shouldAlert") is not False

    subtest_should_alert = subtest.get("shouldAlert")
    if subtest_should_alert is None:
        return suite.get("value") is None
    return subtest_should_alert


def has_replicates(test):
    """Return whether a suite or subtest has enough new values to be compared."""
    return len(test.get("replicates") or []) >= NEW_MIN_NUMS


class XtremeRegressionError(Exception):
    """Raised by test harnesses when :meth:`XtremeRegression.detect` detected
    extreme changes.

    First line provides the number of regressions and the top 3 by percentage, and
    the rest of the lines provide more details on all the regressions.

    :param detections: The results of :meth:`XtremeRegression.detect` that
        are extreme, e.g. those with ``exceeds_variance`` set.
    """

    def __init__(self, detections):
        self.detections = list(detections)

        top_line_options = []
        details = []
        for detection in self.detections:
            suite, test, extra_options, application = detection.get(
                "signature_key", (None, None, (), "")
            )

            name = f"{suite}"
            if test is not None:
                name += f" {detection['test']}"
            if extra_options:
                name += f" ({', '.join(extra_options)})"
            if application:
                name += f" [{application}]"

            difference = detection.get("difference")
            if difference is not None:
                difference = f"{difference:.2f}"
            detail = f"  {name}: difference {difference}"

            top_line_option = name
            percentage = detection.get("percentage")
            if percentage is not None:
                detail += f" ({percentage:+.2f}%)"
                top_line_option += f" ({percentage:+.2f}%)"

            details.append(detail)
            top_line_options.append((
                abs(percentage) if percentage is not None else 0,
                top_line_option,
            ))

        top_metrics = [
            top_line_option
            for _, top_line_option in sorted(top_line_options, reverse=True)[:3]
        ]
        super().__init__(
            "\n".join([
                f"{len(self.detections)} extreme regression(s) detected, top "
                f"{len(top_metrics)}: " + ", ".join(top_metrics),
                *details,
            ])
        )


class XtremeRegression:
    """Compares perfherder-data blobs with the historical data of a repository,
    see :meth:`detect`.

    :param platform: Treeherder machine platform the data was produced on, e.g.
        ``windows11-64-24h2-shippable``.
    :param repository: Treeherder repository to compare with, defaults to autoland.
    :param days: Number of days of history to use.
    :param push_limit: Maximum number of pushes with data, the latest ones in the
        `days` of history, to compare with. None, the default, to use all of
        them from the previous `days` number of days.
    :param revision: Revision the data was produced on. Only data pushed before
        this revision is used. Defaults to the revision being tested in CI, see
        :func:`~mozperfcommon.utils.get_ci_revision`. Outside of CI, the most
        recent data is used.
    :param revision_repository: Repository `revision` was pushed to, e.g.
        ``try``. Defaults to `repository`.
    :param replicates: If True, the history contains every replicate of each
        past run. If False, it contains one value per past run. Runs without
        replicates always contribute their single value. The new values are
        always the replicates.

    Regressions listed in the in-tree expected changes, see
    :func:`~mozperfcommon.expected_changes.load_expected_changes`, are not
    checked against the variance.
    """

    def __init__(
        self,
        platform,
        repository=DEFAULT_REPOSITORY,
        days=DEFAULT_DAYS,
        push_limit=DEFAULT_PUSH_LIMIT,
        revision=None,
        revision_repository=None,
        replicates=True,
    ):
        self.platform = platform
        self.days = days
        self.push_limit = push_limit
        self.revision = revision if revision is not None else get_ci_revision()
        self.revision_repository = revision_repository
        self.replicates = replicates

        self.expected_changes = load_expected_changes()
        self.client = PerfherderClient(repository=repository)

        # Set by detect.
        self._start_date = None
        self._end_date = None
        self._signatures = {}
        self._application = None
        self._framework_id = None

    def detect(
        self,
        perfherder_data,
        comparison=DEFAULT_COMPARISON,
        threshold=DEFAULT_THRESHOLD,
        raise_for_regressions=False,
    ):
        """Compare a PERFHERDER_DATA blob with the historical data.

        :param perfherder_data: Dict following performance-artifact-schema.json.
        :param comparison: Name of the comparison to do with the historical
            values, see :class:`~mozperfcommon.comparisons.Comparisons`. Currently,
            only mean and median are available.
        :param threshold: Threshold of the variance check of the comparison,
            e.g. the number of historical standard deviations the difference
            must exceed for the ``mean`` comparison.
        :param raise_for_regressions: Raise an :class:`XtremeRegressionError`
            for the extreme regressions detected, see
            :meth:`raise_for_regressions`.
        :returns: A list with one dict per suite and subtest value with
            replicates, e.g. with the default ``mean`` comparison::

                [
                    {
                        "suite": "speedometer3",
                        "test": "Charts-chartjs/total",  # None for the suite
                        "value": 53.26,
                        "new_count": 50,  # Number of new values (replicates)
                        "count": 2400,  # Number of historical values
                        "signature_id": 5257402,
                        # (suite, test, extra options, application)
                        "signature_key": (
                            "speedometer3",
                            "Charts-chartjs/total",
                            ("fission", "webrender"),
                            "firefox",
                        ),
                        "expected_change": None,  # Bug of the expected change
                        # Fields returned by the comparison.
                        "new_mean": 53.26,
                        "historical_mean": 35.73,
                        "historical_stdev": 1.31,
                        "difference": 17.53,
                        "percentage": 49.06,
                        "exceeds_variance": True,
                        # Number of new values ignored as outliers
                        "new_outliers": 0,
                    },
                    ...,
                ]

            Suite and subtest values without replicates are not compared,
            since a single value says nothing about the variance. Values that
            don't alert in Perfherder are not compared either.

            The comparison fields are None when there's no history, including
            when the push of the revision or the signatures can't be retrieved
            from Treeherder.
        """
        compare = Comparisons.get(comparison)
        framework_id = get_framework_id(perfherder_data["framework"]["name"])
        self._application = perfherder_data.get("application", {}).get("name")

        try:
            if self.revision:
                self._end_date = self.client.get_push_time(
                    self.revision, self.revision_repository
                )
            else:
                self._end_date = datetime.now(timezone.utc)
            self._start_date = self._end_date - timedelta(days=self.days)
            self._get_signatures(framework_id)
        except (requests.RequestException, ValueError, PushNotFoundError) as e:
            LOG.warning(f"Failed to get the Perfherder history, skipping it: {e}")
            self._end_date = datetime.now(timezone.utc)
            self._start_date = self._end_date - timedelta(days=self.days)
            self._signatures = {}
            self._framework_id = None

        results = []
        for suite in perfherder_data["suites"]:
            subtests = [
                subtest
                for subtest in suite["subtests"]
                if should_alert(suite, subtest) and has_replicates(subtest)
            ]
            tests = [
                (self._signature_key(suite, subtest["name"]), subtest)
                for subtest in subtests
            ]
            if "value" in suite and has_replicates(suite) and should_alert(suite):
                tests.insert(0, (self._signature_key(suite, None), suite))
            if not tests:
                continue

            history = self._get_suite_history(suite, subtests)
            for test_key, test in tests:
                historical_values, signature_id = history.get(test_key, ([], None))
                results.append(
                    self._compare_test(
                        test_key,
                        test,
                        historical_values,
                        signature_id,
                        compare,
                        threshold,
                    )
                )

        if raise_for_regressions:
            # Behind a flag since some harnesses may want to control
            # when the error is thrown and it may come after the detections.
            self.raise_for_regressions(results)

        return results

    def raise_for_regressions(self, results):
        """Helper for raising on extreme regressions."""
        detections = [result for result in results if result["exceeds_variance"]]
        if detections:
            raise XtremeRegressionError(detections)

    def _compare_test(
        self,
        test_key,
        test,
        historical_values,
        signature_id,
        compare,
        threshold,
    ):
        """Runs the comparison between two groups (including checking if it's expected.)"""
        suite_name, test_name, _, _ = test_key

        value = test["value"]
        new_values = test["replicates"]

        # TODO: Don't call compare on expected changes. Keeping for now
        # for monitoring purposes.
        expected_change = find_expected_change(
            self.expected_changes,
            test_key,
            self.platform,
            self._end_date.date(),
        )
        results = compare(
            new_values,
            historical_values,
            threshold=None if expected_change else threshold,
            lower_is_better=test.get("lowerIsBetter", True),
        )

        return {
            "suite": suite_name,
            "test": test_name,
            "value": value,
            "new_count": len(new_values),
            "count": len(historical_values),
            "signature_id": signature_id,
            "signature_key": test_key,
            "expected_change": expected_change["bug"] if expected_change else None,
            **results,
        }

    def _get_signatures(self, framework_id):
        """Gets all the signatures and reformats them into a dict.

        Used for making accessing the signature easier and as a cache for
        signatures so we don't retrieve them again for the same framework. Platform
        should always be constant so it's not considered here.
        """
        if self._signatures and framework_id == self._framework_id:
            return self._signatures

        self._signatures = {}
        self._framework_id = framework_id

        signatures = self.client.get_signatures(
            framework_id, self.platform, self._start_date, subtests=False
        )

        for signature in signatures.values():
            key = signature_key(
                signature["suite"],
                signature.get("test"),
                signature.get("extra_options"),
                signature.get("application"),
            )
            self._signatures[key] = signature

        return self._signatures

    def _signature_key(self, suite, test):
        """Helper to get a signature key from a suite and test pairing."""
        return signature_key(
            suite["name"], test, suite.get("extraOptions"), self._application
        )

    def _get_summaries(self, **kwargs):
        """Returns the performance data for the given parameters over a date range."""
        try:
            return self.client.get_summary(
                self._start_date,
                self._end_date,
                replicates=self.replicates,
                **kwargs,
            )
        except (requests.RequestException, ValueError) as e:
            LOG.warning(f"Failed to get the Perfherder summary for {kwargs}: {e}")
            return []

    def _get_signature_history(self, signature_id):
        """Return all the info needed for a comparison of a specific signature.

        This handles the push limit and returns the values, and the ids of the jobs.
        """
        # There is at most one summary for a single signature, but perfherder returns a list
        # so we pick the first element from it.
        summaries = self._get_summaries(signature_id=signature_id, all_data=True)
        if not summaries:
            return [], set()
        summary = summaries[0]

        pushes = sorted({
            (point["push_timestamp"], point["push_id"]) for point in summary["data"]
        })
        if self.push_limit is not None:
            pushes = pushes[-self.push_limit :]

        push_ids = {push_id for _, push_id in pushes}
        points = [point for point in summary["data"] if point["push_id"] in push_ids]

        return (
            [point["value"] for point in points],
            {point["job_id"] for point in points},
        )

    def _get_suite_history(self, suite, subtests):
        """Get perf history of a suite and the given subtests, by signature key."""
        history = {}

        suite_key = self._signature_key(suite, None)
        suite_signature = self._signatures.get(suite_key)
        if suite_signature:
            values, job_ids = self._get_signature_history(suite_signature["id"])
            history[suite_key] = (values, suite_signature["id"])

            if subtests:
                # Only pick subtest data that has the same job IDs as the suite
                for summary in self._get_summaries(
                    parent_signature_id=suite_signature["id"]
                ):
                    history[self._signature_key(suite, summary["test"])] = (
                        [
                            value
                            for value, job_id in zip(
                                summary["values"], summary["job_ids"]
                            )
                            if job_id in job_ids
                        ],
                        summary["signature_id"],
                    )

        # Subtests reported without a parent signature have their own.
        for subtest in subtests:
            key = self._signature_key(suite, subtest["name"])
            if key not in history:
                signature = self._signatures.get(key)
                if signature:
                    values, _ = self._get_signature_history(signature["id"])
                    history[key] = (values, signature["id"])

        return history
