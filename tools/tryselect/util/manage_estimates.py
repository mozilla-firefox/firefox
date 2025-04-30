# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import json
import os
from datetime import datetime, timedelta

import requests

TASK_DURATION_URL = (
    "https://storage.googleapis.com/mozilla-mach-data/task_duration_history.json"
)
GRAPH_QUANTILES_URL = (
    "https://storage.googleapis.com/mozilla-mach-data/machtry_quantiles.csv"
)
from .estimates import GRAPH_QUANTILE_CACHE, TASK_DURATION_CACHE, TASK_DURATION_TAG_FILE


def check_downloaded_history(tag_file, duration_cache, quantile_cache):
    if not os.path.isfile(tag_file):
        return False

    try:
        with open(tag_file) as f:
            duration_tags = json.load(f)
        download_date = datetime.strptime(
            duration_tags.get("download_date"), "%Y-%M-%d"
        )
        if download_date < datetime.now() - timedelta(days=7):
            return False
    except (OSError, ValueError):
        return False

    if not os.path.isfile(duration_cache):
        return False
    # Check for old format version of file.
    with open(duration_cache) as f:
        data = json.load(f)
        if isinstance(data, list):
            return False
    if not os.path.isfile(quantile_cache):
        return False

    return True


def download_task_history_data(cache_dir):
    """Fetch task duration data exported from BigQuery."""
    task_duration_cache = os.path.join(cache_dir, TASK_DURATION_CACHE)
    task_duration_tag_file = os.path.join(cache_dir, TASK_DURATION_TAG_FILE)
    graph_quantile_cache = os.path.join(cache_dir, GRAPH_QUANTILE_CACHE)

    if check_downloaded_history(
        task_duration_tag_file, task_duration_cache, graph_quantile_cache
    ):
        return

    try:
        os.unlink(task_duration_tag_file)
        os.unlink(task_duration_cache)
        os.unlink(graph_quantile_cache)
    except OSError:
        print("No existing task history to clean up.")

    try:
        r = requests.get(TASK_DURATION_URL, stream=True)
        r.raise_for_status()
    except requests.exceptions.RequestException as exc:
        # This is fine, the durations just won't be in the preview window.
        print(f"Error fetching task duration cache from {TASK_DURATION_URL}: {exc}")
        return

    # The data retrieved from google storage is a newline-separated
    # list of json entries, which Python's json module can't parse.
    duration_data = list()
    for line in r.text.splitlines():
        duration_data.append(json.loads(line))

    # Reformat duration data to avoid list of dicts, as this is slow in the preview window
    duration_data = {d["name"]: d["mean_duration_seconds"] for d in duration_data}

    with open(task_duration_cache, "w") as f:
        json.dump(duration_data, f, indent=4)

    try:
        r = requests.get(GRAPH_QUANTILES_URL, stream=True)
        r.raise_for_status()
    except requests.exceptions.RequestException as exc:
        # This is fine, the percentile just won't be in the preview window.
        print(
            f"Error fetching task group percentiles from {GRAPH_QUANTILES_URL}: {exc}"
        )
        return

    with open(graph_quantile_cache, "w") as f:
        f.write(r.text)

    with open(task_duration_tag_file, "w") as f:
        json.dump({"download_date": datetime.now().strftime("%Y-%m-%d")}, f, indent=4)


def make_trimmed_taskgraph_cache(graph_cache, dep_cache, target_file=None):
    """Trim the taskgraph cache used for dependencies.

    Speeds up the fzf preview window to less human-perceptible
    ranges."""
    if not os.path.isfile(graph_cache):
        return

    target_task_set = set()
    if target_file and os.path.isfile(target_file):
        with open(target_file) as f:
            target_task_set = set(json.load(f).keys())

    with open(graph_cache) as f:
        graph = json.load(f)
    graph = {
        name: list(defn["dependencies"].values())
        for name, defn in graph.items()
        if name in target_task_set
    }
    with open(dep_cache, "w") as f:
        json.dump(graph, f, indent=4)
