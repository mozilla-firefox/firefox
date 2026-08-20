import asyncio
import functools
import os
import re

import requests
from requests.packages.urllib3.util.retry import Retry

TOKEN_PATTERN = re.compile(r"\%(.*?)\%")


@functools.lru_cache
def get_tc_secret(secret_name="gha-pat", level=3, level_flag=False):
    """Returns the Taskcluster secret.

    Returns False when not running on tc
    """
    if not os.environ.get("MOZ_AUTOMATION"):
        return False
    tc_home = os.environ.get("TASKCLUSTER_PROXY_URL", "http://taskcluster")
    level = os.environ.get("MOZ_SCM_LEVEL", level)
    level_text = f"level-{level}/" if level_flag else ""
    session = requests.Session()
    retry = Retry(total=5, backoff_factor=0.1, status_forcelist=[500, 502, 503, 504])
    http_adapter = requests.adapters.HTTPAdapter(max_retries=retry)
    session.mount("https://", http_adapter)
    session.mount("http://", http_adapter)
    secrets_url = f"{tc_home}/secrets/v1/secret/project/desktop-test-ops/{level_text}{secret_name}"

    res = session.get(secrets_url, timeout=30)
    res.raise_for_status()

    return res.json()["secret"]


async def poller(
    url, condition={"status": "completed"}, result={"conclusion": "success"}
):
    """
    Poll the GHA endpoint for test condition and results.
    """
    while True:
        resp = requests.get(url=url, headers=build_headers())
        payload = resp.json()
        if payload == payload | condition:
            print(f"Condition reached: {condition}")
            ui_url = url.replace("https://api.", "https://").replace("repos/", "")
            assert payload == payload | result, (
                f"Run reached expected condition, unexpected result. See {ui_url}"
            )
            return payload
        await asyncio.sleep(60)


def build_headers():
    """Build headers for the GHA API calls"""
    return {
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {get_tc_secret().get('ghaToken')}",
        "X-GitHub-Api-Version": "2026-03-10",
    }


def issue_api_call():
    """Call POST on the GHA workflow dispatch endpoint with necessary info"""
    target_url = os.environ["INSTALLER_LINK"]
    workflow_id = os.environ["WORKFLOW_ID"]
    input_key = os.environ["INPUT_KEY"]
    branch = os.environ["BRANCH"]
    url = (
        "https://api.github.com/repos/mozilla/fx-desktop-qa-automation/"
        f"actions/workflows/{workflow_id}/dispatches"
    )
    data = {"ref": branch, "inputs": {input_key: target_url}}
    if os.environ.get("TEST_SET"):
        data["inputs"]["test_set"] = os.environ.get("TEST_SET")
    resp = requests.post(url=url, headers=build_headers(), json=data)
    print(url, data)
    print(resp.status_code, resp.reason)
    print(resp.json().get("run_url"))
    return resp.json()


if __name__ == "__main__":
    post_response = issue_api_call()
    asyncio.run(poller(post_response["run_url"]))
