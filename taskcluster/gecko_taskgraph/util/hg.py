# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import logging
import subprocess

import requests
from mozbuild.util import memoize
from redo import retry

logger = logging.getLogger(__name__)

PUSHLOG_CHANGESET_TMPL = (
    "{repository}/json-pushes?version=2&changeset={revision}&tipsonly=1"
)
PUSHLOG_PUSHES_TMPL = (
    "{repository}/json-pushes/?version=2&startID={push_id_start}&endID={push_id_end}"
)


def _query_pushlog(url):
    response = retry(
        requests.get,
        attempts=5,
        sleeptime=10,
        args=(url,),
        kwargs={"timeout": 60, "headers": {"User-Agent": "TaskCluster"}},
    )

    return response.json()["pushes"]


def find_hg_revision_push_info(repository, revision):
    """Given the parameters for this action and a revision, find the
    pushlog_id of the revision."""
    url = PUSHLOG_CHANGESET_TMPL.format(repository=repository, revision=revision)

    pushes = _query_pushlog(url)

    if len(pushes) != 1:
        raise RuntimeError(
            f"Found {len(pushes)} pushlog_ids, expected 1, for {repository} revision {revision}: {pushes}"
        )

    pushid = list(pushes.keys())[0]
    return {
        "pushdate": pushes[pushid]["date"],
        "pushid": pushid,
        "user": pushes[pushid]["user"],
    }


@memoize
def get_push_data(repository, project, push_id_start, push_id_end):
    url = PUSHLOG_PUSHES_TMPL.format(
        repository=repository,
        push_id_start=push_id_start - 1,
        push_id_end=push_id_end,
    )

    try:
        pushes = _query_pushlog(url)

        return {
            push_id: pushes[str(push_id)]
            for push_id in range(push_id_start, push_id_end + 1)
        }

    # In the event of request times out, requests will raise a TimeoutError.
    except requests.exceptions.Timeout:
        logger.warning("json-pushes timeout")

    # In the event of a network problem (e.g. DNS failure, refused connection, etc),
    # requests will raise a ConnectionError.
    except requests.exceptions.ConnectionError:
        logger.warning("json-pushes connection error")

    # In the event of the rare invalid HTTP response(e.g 404, 401),
    # requests will raise an HTTPError exception
    except requests.exceptions.HTTPError:
        logger.warning("Bad Http response")

    # When we get invalid JSON (i.e. 500 error), it results in a ValueError (bug 1313426)
    except ValueError as error:
        logger.warning(f"Invalid JSON, possible server error: {error}")

    # We just print the error out as a debug message if we failed to catch the exception above
    except requests.exceptions.RequestException as error:
        logger.warning(error)

    return None


@memoize
def get_json_pushchangedfiles(repository, revision):
    url = "{}/json-pushchangedfiles/{}".format(repository.rstrip("/"), revision)
    logger.debug("Querying version control for metadata: %s", url)

    def get_pushchangedfiles():
        response = requests.get(url, timeout=60)
        return response.json()

    return retry(get_pushchangedfiles, attempts=10, sleeptime=10)


def get_hg_revision_branch(root, revision):
    """Given the parameters for a revision, find the hg_branch (aka
    relbranch) of the revision."""
    return get_hg_revision_info(root, revision, "branch")


def get_hg_revision_info(root, revision, info):
    return subprocess.check_output(
        [
            "hg",
            "identify",
            "-T",
            f"{{{info}}}",
            "--rev",
            revision,
        ],
        cwd=root,
        universal_newlines=True,
    )


# For these functions, we assume that run-task has correctly checked out the
# revision indicated by GECKO_HEAD_REF, so all that remains is to see what the
# current revision is.  Mercurial refers to that as `.`.
def get_hg_commit_message(root, rev="."):
    return subprocess.check_output(
        ["hg", "log", "-r", rev, "-T", "{desc}"], cwd=root, universal_newlines=True
    )


def calculate_head_rev(root):
    return subprocess.check_output(
        ["hg", "log", "-r", ".", "-T", "{node}"], cwd=root, universal_newlines=True
    )
