# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import copy

import filters
from base_python_support import BasePythonSupport
from logger.logger import RaptorLogger

LOG = RaptorLogger(component="perftest-nav-bench")

# Phase suffix appended to each site key in the alias.
#   alias = "<site>-load"     -> initial navigation (every site)
#   alias = "<site>-subnav"   -> warm hard-nav (bbc, ddg)
#   alias = "<site>-warm"     -> cached revisit (google-docs)
PHASES = ("load", "subnav", "warm")

# LoadLine2-style score scaling. score = SCORE_TARGET_MS / SpeedIndex_ms.
SCORE_TARGET_MS = 60000.0


def _parse_alias(alias):
    """Split '<site>-<phase>' into (site, phase). Returns (None, None) if the
    alias does not end with one of the known phase suffixes."""
    if not alias:
        return None, None
    for phase in PHASES:
        suffix = "-" + phase
        if alias.endswith(suffix):
            return alias[: -len(suffix)], phase
    return None, None


def _overall_suite_name(suite_name):
    """Name of the synthesized overall suite, given one per-alias suite name
    such as 'nav-bench-a11y.amazon-nav-load'.

    The a11y variants need their own name: Perfherder signatures are keyed on
    suite, test and options, so with a shared name their scores would land in
    the same series as the plain runs. nav-bench and nav-bench-m both keep
    'nav-bench-overall' so their existing history and alerts carry on."""
    test_name = suite_name.split(".", 1)[0]
    if test_name.endswith("-a11y"):
        return "nav-bench-a11y-overall"
    return "nav-bench-overall"


class NavBenchSupport(BasePythonSupport):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        # (site, phase) -> [SpeedIndex per cycle]
        self._speedindex = {}
        # (site, phase) -> {perfstat metric -> [value per cycle]}
        self._perfstats = {}

    def handle_result(self, bt_result, raw_result, last_result=False, **kwargs):
        """One raw_result per measure.start/stop pair, with alias set in
        raw_result['info']['alias']. visualMetrics is one item per cycle."""
        alias = raw_result.get("info", {}).get("alias")
        site, phase = _parse_alias(alias)
        if not site:
            return

        for vm in raw_result.get("visualMetrics", []):
            si = vm.get("SpeedIndex")
            if not si:
                LOG.warning(
                    f"nav-bench: {site}/{phase} has bad SpeedIndex ({si}); skipping cycle"
                )
                continue
            self._speedindex.setdefault((site, phase), []).append(int(si))

        # geckoPerfStats: one {metric: ms} dict per cycle, only with --collect-perfstats.
        for cycle in raw_result.get("geckoPerfStats", []):
            for metric, value in cycle.items():
                self._perfstats.setdefault((site, phase), {}).setdefault(
                    metric, []
                ).append(value)

    def summarize_test(self, test, suite, **kwargs):
        """One suite per measure.start/stop alias. Contains only min-si (ms)
        as a diagnostic; scores are aggregated in nav-bench-overall."""
        suite["type"] = "pageload"
        suite["lowerIsBetter"] = True
        suite["unit"] = "ms"
        if suite["subtests"] == {}:
            suite["subtests"] = []

        suite_alias = suite.get("name", "").split(".", 1)[-1]
        suite_site, suite_phase = _parse_alias(suite_alias)

        for (site, phase), si_values in sorted(self._speedindex.items()):
            if suite_site is not None and (site != suite_site or phase != suite_phase):
                continue
            if not si_values:
                LOG.warning(f"nav-bench: {site}/{phase} has no SpeedIndex samples")
                continue
            suite["subtests"].append({
                "name": f"{site}-{phase}-min-si",
                "lowerIsBetter": True,
                "alertThreshold": float(test.get("alert_threshold", 5.0)),
                "unit": "ms",
                "replicates": list(si_values),
                "value": min(si_values),
                "shouldAlert": False,
            })

        suite["subtests"].sort(key=lambda subtest: subtest["name"])

    def summarize_suites(self, suites):
        """Synthesize the overall suite (nav-bench-overall, or
        nav-bench-a11y-overall for the a11y variants): one score subtest per
        (site, phase) plus an overall geomean value."""
        if not suites:
            return

        alert_threshold = suites[0].get("alertThreshold", 5.0)
        all_subtests = []
        all_replicates = []
        for (site, phase), si_values in sorted(self._speedindex.items()):
            if not si_values:
                continue
            score_replicates = [round(SCORE_TARGET_MS / si, 3) for si in si_values]
            all_subtests.append({
                "name": f"{site}-{phase}-score",
                "lowerIsBetter": False,
                "alertThreshold": alert_threshold,
                "unit": "score",
                "replicates": score_replicates,
                "value": round(filters.geometric_mean(score_replicates), 3),
                "shouldAlert": True,
            })
            all_replicates.extend(score_replicates)

        if not all_replicates:
            return

        overall = copy.deepcopy(suites[0])
        overall["name"] = _overall_suite_name(suites[0].get("name", ""))
        overall["type"] = "pageload"
        overall["lowerIsBetter"] = False
        overall["unit"] = "score"
        overall["subtests"] = sorted(all_subtests, key=lambda s: s["name"])
        overall["value"] = round(filters.geometric_mean(all_replicates), 3)

        # PerfStats subtests (only with --collect-perfstats); skip all-zero metrics.
        for (site, phase), metrics in sorted(self._perfstats.items()):
            for metric, values in sorted(metrics.items()):
                if not any(values):
                    continue
                overall["subtests"].append({
                    "name": f"{site}-{phase}-perfstat-{metric}",
                    "lowerIsBetter": True,
                    "alertThreshold": alert_threshold,
                    "unit": "ms",
                    "replicates": list(values),
                    "value": round(filters.mean(values), 3),
                    "shouldAlert": False,
                })

        suites.insert(0, overall)
