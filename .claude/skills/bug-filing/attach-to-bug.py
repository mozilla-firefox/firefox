#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Attach a file to a bugzilla.mozilla.org bug via the REST API.

    python3 .agents/skills/bug-filing/attach-to-bug.py --bug NNNNNN \
        --file /path/to/testcase.html \
        --summary "Testcase: ..." \
        --comment-file /path/to/comment.md

The API key is read from ~/.config/python-bugzilla/bugzillarc:

    [bugzilla.mozilla.org]
    api_key = ...

Pass --dry-run to print what would be posted without posting it. Attaching is
outward-facing and permanent, so confirm with the user before the real run.
"""

import argparse
import base64
import configparser
import json
import mimetypes
import os
import sys
import urllib.error
import urllib.request

BASE = "https://bugzilla.mozilla.org"
TIMEOUT = 60

CONTENT_TYPES = {
    ".png": "image/png",
    ".html": "text/html",
    ".svg": "image/svg+xml",
    ".txt": "text/plain",
    ".json": "application/json",
}


def api_key(path):
    config = configparser.ConfigParser()
    config.read(os.path.expanduser(path))
    try:
        key = config["bugzilla.mozilla.org"]["api_key"].strip()
    except KeyError:
        key = ""
    if not key:
        sys.exit(f"attach-to-bug: no bugzilla.mozilla.org api_key in {path}")
    return key


def bug_id(value):
    bug = int(value)
    if bug < 1:
        raise argparse.ArgumentTypeError(f"invalid bug number: {value}")
    return bug


def guess_content_type(path):
    ext = os.path.splitext(path)[1].lower()
    return CONTENT_TYPES.get(ext) or mimetypes.guess_type(path)[0] or "text/plain"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bug", type=bug_id, required=True)
    parser.add_argument("--file", required=True)
    parser.add_argument("--summary", required=True, help="the attachment's description")
    parser.add_argument(
        "--file-name",
        help="the attachment's file name on the bug (default: the file's basename)",
    )
    comment_group = parser.add_mutually_exclusive_group()
    comment_group.add_argument("--comment", help="comment body text")
    comment_group.add_argument("--comment-file", help="file holding the comment body")
    parser.add_argument(
        "--content-type",
        help="defaults by extension (.png, .html, .svg, .txt, .json), "
        "else guessed by mimetypes, else text/plain",
    )
    parser.add_argument("--is-patch", action="store_true")
    parser.add_argument("--rcfile", default="~/.config/python-bugzilla/bugzillarc")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    path = os.path.abspath(args.file)
    if not os.path.isfile(path):
        sys.exit(f"attach-to-bug: no such file: {path}")

    file_name = args.file_name or os.path.basename(path)
    if not file_name.strip() or "/" in file_name or "\\" in file_name:
        sys.exit(f"attach-to-bug: --file-name must be a bare file name: {file_name!r}")

    comment = args.comment or ""
    if args.comment_file:
        with open(args.comment_file, encoding="utf-8") as f:
            comment = f.read()

    content_type = args.content_type or guess_content_type(path)

    with open(path, "rb") as f:
        raw = f.read()

    payload = {
        "ids": [args.bug],
        "file_name": file_name,
        "summary": args.summary,
        "content_type": content_type,
        "data": base64.b64encode(raw).decode(),
        "is_patch": args.is_patch,
    }
    if comment:
        payload["comment"] = comment

    key = api_key(args.rcfile)

    if args.dry_run:
        preview = dict(payload, data=f"<{len(raw)} bytes>")
        print(json.dumps(preview, indent=1))
        return

    request = urllib.request.Request(
        f"{BASE}/rest/bug/{args.bug}/attachment",
        data=json.dumps(payload).encode(),
        headers={
            "Content-Type": "application/json",
            "X-BUGZILLA-API-KEY": key,
        },
        method="POST",
    )
    timed_out = (
        f"attach-to-bug: no response within {TIMEOUT}s; the attachment may have "
        f"been stored, so check {BASE}/show_bug.cgi?id={args.bug} before retrying"
    )
    try:
        with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
            text = response.read().decode(errors="replace")
    except urllib.error.HTTPError as e:
        sys.exit(f"attach-to-bug: HTTP {e.code}: {e.read().decode(errors='replace')}")
    except urllib.error.URLError as e:
        if isinstance(e.reason, TimeoutError):
            sys.exit(timed_out)
        sys.exit(f"attach-to-bug: cannot reach {BASE}: {e.reason}")
    except TimeoutError:
        sys.exit(timed_out)

    try:
        body = json.loads(text)
    except ValueError:
        body = None
    ids = []
    if isinstance(body, dict):
        ids = list(body.get("attachments") or body.get("ids") or [])
    if not ids:
        sys.exit(f"attach-to-bug: unexpected response: {text}")
    for attachment_id in ids:
        print(f"{BASE}/attachment.cgi?id={attachment_id}")


if __name__ == "__main__":
    main()
