#!/usr/bin/env python3

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import datetime
import json
import os
import sys
import urllib.error
import urllib.request

DEFAULT_CREDENTIALS_FILE = "/builds/worker/gcp-credentials.json"
DEFAULT_OUTPUT_DIR = "/builds/worker/artifacts"
# Only the level-3 mozilla-central cron holds secrets:get on this. Deliberately
# not level-templated: no level-1 name exists, so a try run fails closed.
DEFAULT_SECRET = "project/bhr/aggregation-gcp-key"
# Index route this task publishes to, used to pick up the previous run's
# timeseries state so the roll-up stays incremental.
DEFAULT_STATE_INDEX = "gecko.v2.mozilla-central.latest.firefox.bhr-aggregate"
# Where a run pinned to a build date publishes, and the run-day offsets the
# daily cron's own routes need instead. A day re-run through the action is
# found by the first; a day only the cron ever produced, by the second.
BUILD_INDEX = "gecko.v2.mozilla-central.bhr-aggregate.build.{date}"
PUSHDATE_INDEX = (
    "gecko.v2.mozilla-central.pushdate.{run_day}.latest.firefox.bhr-aggregate"
)
RUN_DAY_OFFSETS = (4, 3, 5, 6)


def _env_float(name, default):
    value = os.environ.get(name)
    return default if value is None else float(value)


def _env_int(name, default):
    value = os.environ.get(name)
    return default if value is None else int(value)


def _default_date(offset_days):
    return datetime.datetime.now(datetime.UTC).date() - datetime.timedelta(
        days=offset_days
    )


def _parse_date(value):
    if value:
        return datetime.date.fromisoformat(value)
    return None


def _secret_url(secret_name, with_api_prefix=True):
    proxy = os.environ.get("TASKCLUSTER_PROXY_URL", "http://taskcluster").rstrip("/")
    prefix = "/api" if with_api_prefix else ""
    return f"{proxy}{prefix}/secrets/v1/secret/{secret_name}"


def _fetch_secret(secret_name):
    errors = []
    for with_api_prefix in (True, False):
        url = _secret_url(secret_name, with_api_prefix)
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                return json.loads(response.read().decode("utf-8"))
        except (urllib.error.URLError, urllib.error.HTTPError) as error:
            errors.append(f"{url}: {error}")
    raise RuntimeError("Could not fetch Taskcluster secret:\n" + "\n".join(errors))


def _state_url(index_route, name, with_api_prefix=True):
    proxy = os.environ.get("TASKCLUSTER_PROXY_URL", "http://taskcluster").rstrip("/")
    prefix = "/api" if with_api_prefix else ""
    return f"{proxy}{prefix}/index/v1/task/{index_route}/artifacts/public/bhr/{name}"


def _fetch_previous_state(index_route, name, path):
    """Download the last run's timeseries state next to today's artifacts.

    Returns True if state was retrieved. A miss is normal and not fatal: the
    first run has no predecessor, and build_timeseries refills any window date
    absent from state from that day's artifact, so the roll-up self-heals
    rather than failing.
    """
    errors = []
    for with_api_prefix in (True, False):
        url = _state_url(index_route, name, with_api_prefix)
        try:
            with urllib.request.urlopen(url, timeout=300) as response:
                data = response.read()
        except (urllib.error.URLError, urllib.error.HTTPError) as error:
            errors.append(f"{url}: {error}")
            continue
        with open(path, "wb") as state_file:
            state_file.write(data)
        print(f"Fetched previous timeseries state ({len(data)} bytes)", flush=True)
        return True
    print("No previous timeseries state:\n  " + "\n  ".join(errors), flush=True)
    return False


def _artifact_routes(date):
    """Index routes that might hold the daily artifact for a build date."""
    yield BUILD_INDEX.format(date=date)
    day = datetime.datetime.strptime(date, "%Y%m%d").date()
    for offset in RUN_DAY_OFFSETS:
        run_day = day + datetime.timedelta(days=offset)
        yield PUSHDATE_INDEX.format(run_day=run_day.strftime("%Y.%m.%d"))


def _fetch_indexed_artifact(date, name, path):
    """Download one day's published artifact, returning True if it was found."""
    for route in _artifact_routes(date):
        for with_api_prefix in (True, False):
            url = _state_url(route, name, with_api_prefix)
            try:
                with urllib.request.urlopen(url, timeout=900) as response:
                    data = response.read()
            except (urllib.error.URLError, urllib.error.HTTPError):
                continue
            with open(path, "wb") as artifact:
                artifact.write(data)
            print(f"Fetched {name} ({len(data)} bytes) from {route}", flush=True)
            return True
    return False


def _write_gcp_credentials(secret_name, secret_key, path):
    secret = _fetch_secret(secret_name)["secret"][secret_key]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as credentials:
        if isinstance(secret, str):
            credentials.write(secret)
        else:
            json.dump(secret, credentials)
    os.chmod(path, 0o600)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--date", default=os.environ.get("BHR_AGGREGATE_DATE"))
    parser.add_argument(
        "--end-date",
        default=os.environ.get("BHR_AGGREGATE_END_DATE"),
        help=(
            "Last build date of the window, inclusive. One BigQuery scan covers "
            "the whole window, which is far cheaper than a scan per day, and "
            "each day is still written as its own artifact. Defaults to --date."
        ),
    )
    parser.add_argument(
        "--date-offset-days",
        type=int,
        default=_env_int("BHR_AGGREGATE_DATE_OFFSET_DAYS", 4),
    )
    parser.add_argument(
        "--sample-size",
        type=float,
        default=_env_float("BHR_AGGREGATE_SAMPLE_SIZE", 0.5),
    )
    parser.add_argument(
        "--billing-project",
        default=os.environ.get("BHR_AGGREGATE_BILLING_PROJECT", "mozdata"),
    )
    parser.add_argument(
        "--output-dir",
        default=os.environ.get("BHR_AGGREGATE_OUTPUT_DIR", DEFAULT_OUTPUT_DIR),
    )
    parser.add_argument(
        "--output-tag",
        default=os.environ.get("BHR_AGGREGATE_OUTPUT_TAG", "main"),
    )
    parser.add_argument(
        "--thread-filter",
        default=os.environ.get("BHR_AGGREGATE_THREAD_FILTER", "Gecko"),
    )
    parser.add_argument(
        "--credentials-file",
        default=os.environ.get(
            "GOOGLE_APPLICATION_CREDENTIALS", DEFAULT_CREDENTIALS_FILE
        ),
    )
    parser.add_argument(
        "--gcp-secret",
        default=os.environ.get("BHR_GCP_SECRET", DEFAULT_SECRET),
    )
    parser.add_argument(
        "--gcp-secret-key",
        default=os.environ.get("BHR_GCP_SECRET_KEY", "serviceAccount"),
    )
    parser.add_argument(
        "--skip-timeseries",
        action="store_true",
        default=bool(os.environ.get("BHR_SKIP_TIMESERIES")),
        help="Only aggregate the day; do not update the timeseries roll-up.",
    )
    parser.add_argument(
        "--timeseries-window-days",
        type=int,
        default=_env_int("BHR_TIMESERIES_WINDOW_DAYS", 365),
    )
    parser.add_argument(
        "--timeseries-top-count",
        type=int,
        default=_env_int("BHR_TIMESERIES_TOP_COUNT", 500),
    )
    parser.add_argument(
        "--timeseries-refill-dates",
        default=os.environ.get("BHR_TIMESERIES_REFILL_DATES", ""),
        help=(
            "Comma-separated YYYYMMDD build dates to recompute in the roll-up. "
            "State keeps whatever a day's first run produced, so a backfilled "
            "or re-run day only reaches the timeseries when named here."
        ),
    )
    parser.add_argument(
        "--timeseries-state-index",
        default=os.environ.get("BHR_TIMESERIES_STATE_INDEX", DEFAULT_STATE_INDEX),
    )
    parser.add_argument(
        "topsrcdir",
        nargs="?",
        default=os.environ.get("GECKO_PATH", os.getcwd()),
    )
    args = parser.parse_args()

    if not 0 < args.sample_size <= 1:
        raise ValueError(f"--sample-size must be in (0, 1], got {args.sample_size}")

    build_date = _parse_date(args.date) or _default_date(args.date_offset_days)
    end_date = _parse_date(args.end_date) or build_date
    if end_date < build_date:
        raise ValueError(f"--end-date {end_date} is before --date {build_date}")

    if not os.path.exists(args.credentials_file):
        if not args.gcp_secret:
            raise RuntimeError(
                "No GCP credentials file or Taskcluster secret configured"
            )
        _write_gcp_credentials(
            args.gcp_secret, args.gcp_secret_key, args.credentials_file
        )
    os.environ["GOOGLE_APPLICATION_CREDENTIALS"] = args.credentials_file

    aggregation_dir = os.path.join(
        args.topsrcdir,
        "toolkit",
        "components",
        "backgroundhangmonitor",
        "aggregation",
    )
    sys.path.insert(0, aggregation_dir)
    import bhr_collection

    window = (
        f"{build_date:%Y-%m-%d}"
        if end_date == build_date
        else f"{build_date:%Y-%m-%d}..{end_date:%Y-%m-%d}"
    )
    print(
        f"Running BHR aggregation for build date {window} "
        f"at sample-size {args.sample_size}",
        flush=True,
    )
    bhr_collection.aggregate(
        date=build_date,
        end_date=end_date,
        sample_size=args.sample_size,
        billing_project=args.billing_project,
        output_dir=args.output_dir,
        output_tag=args.output_tag,
        config_overrides={"thread_filter": args.thread_filter},
        # Only a run pinned to a date, a backfill or its retry, reads an earlier
        # scan; the cron has to see the pings that arrived since.
        reuse=args.date is not None,
    )

    if args.skip_timeseries:
        return

    import bhr_timeseries

    state_name = f"hangs_timeseries_{args.output_tag}_state.json.gz"
    _fetch_previous_state(
        args.timeseries_state_index,
        state_name,
        os.path.join(args.output_dir, state_name),
    )

    # Days being recomputed need their artifacts alongside today's, since the
    # roll-up reads them from disk. Anything that cannot be fetched is dropped
    # rather than failing the run: its state entry then simply stays as it was.
    refill_dates = [
        d.strip() for d in args.timeseries_refill_dates.split(",") if d.strip()
    ]
    fetched = []
    for date in refill_dates:
        name = f"hangs_{args.output_tag}_{date}.json"
        if _fetch_indexed_artifact(date, name, os.path.join(args.output_dir, name)):
            fetched.append(date)
        else:
            print(f"No published artifact for {date}; leaving its state", flush=True)

    # Beyond those, the day just written is the only new input; every earlier
    # day in the window is already summarized in the state fetched above.
    print(
        f"Rolling up the last {args.timeseries_window_days} days"
        + (f", recomputing {len(fetched)} of them" if fetched else ""),
        flush=True,
    )
    bhr_timeseries.build_timeseries(
        input_dir=args.output_dir,
        output_dir=args.output_dir,
        output_tag=args.output_tag,
        end_date=end_date,
        window_days=args.timeseries_window_days,
        top_count=args.timeseries_top_count,
        refill_dates=fetched,
    )


if __name__ == "__main__":
    main()
