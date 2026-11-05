# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import annotations

import functools
import io
import mimetypes
import os
import sys
from concurrent import futures
from pprint import pprint
from typing import TYPE_CHECKING

import boto3
import botocore
import requests

if TYPE_CHECKING:
    from mozpack.files import BaseFile

# Each upload is a small file that mostly waits on the request round trip, so
# many requests in flight keep the upload short on workers far from the bucket.
UPLOAD_THREADS = 128


@functools.cache
def create_aws_session():
    """
    This function creates an aws session that is
    shared between upload and delete both.
    """
    region = "us-west-2"
    level = os.environ.get("MOZ_SCM_LEVEL", "1")
    bucket = {
        "1": "gecko-docs.mozilla.org-l1",
        "2": "gecko-docs.mozilla.org-l2",
        "3": "gecko-docs.mozilla.org",
    }[level]
    secrets_url = "http://taskcluster/secrets/v1/secret/"
    secrets_url += f"project/releng/gecko/build/level-{level}/gecko-docs-upload"

    # Get the credentials from the TC secrets service.  Note that these
    # differ per SCM level
    if "TASK_ID" in os.environ:
        print("Using AWS credentials from the secrets service")
        session = requests.Session()
        res = session.get(secrets_url)
        res.raise_for_status()
        secret = res.json()["secret"]
        session = boto3.session.Session(
            aws_access_key_id=secret["AWS_ACCESS_KEY_ID"],
            aws_secret_access_key=secret["AWS_SECRET_ACCESS_KEY"],
            region_name=region,
        )
    else:
        print("Trying to use your AWS credentials..")
        session = boto3.session.Session(region_name=region)

    # The upload threads and the prune task in s3_upload share this client.
    s3 = session.client(
        "s3", config=botocore.client.Config(max_pool_connections=UPLOAD_THREADS + 1)
    )

    return s3, bucket


@functools.cache
def get_s3_keys(s3, bucket, include_prefix=None, exclude_prefix=None):
    # When exclude_prefix is set, enumerate one level at a time with a
    # delimiter and skip that subtree, descending into the other prefixes.
    # Listing the whole bucket is prohibitively slow because every build
    # leaves an immutable per-build copy under main/.
    kwargs = {"Bucket": bucket}
    if include_prefix is not None:
        kwargs["Prefix"] = include_prefix
    if exclude_prefix is not None:
        _, sep, rest = exclude_prefix.partition("/")
        assert sep and not rest, "exclude_prefix must be a single top-level prefix"
        kwargs["Delimiter"] = "/"
    all_keys = []
    while True:
        response = s3.list_objects_v2(**kwargs)
        for obj in response.get("Contents", []):
            all_keys.append(obj["Key"])
        for common in response.get("CommonPrefixes", []):
            if common["Prefix"] != exclude_prefix:
                all_keys.extend(
                    get_s3_keys(s3, bucket, include_prefix=common["Prefix"])
                )

        try:
            kwargs["ContinuationToken"] = response["NextContinuationToken"]
        except KeyError:
            break

    return all_keys


def s3_set_redirects(redirects):
    s3, bucket = create_aws_session()

    configuration = {"IndexDocument": {"Suffix": "index.html"}, "RoutingRules": []}

    for path, redirect in redirects.items():
        rule = {
            "Condition": {"KeyPrefixEquals": path},
            "Redirect": {"ReplaceKeyPrefixWith": redirect},
        }
        if os.environ.get("MOZ_SCM_LEVEL") == "3":
            rule["Redirect"]["HostName"] = "firefox-source-docs.mozilla.org"

        configuration["RoutingRules"].append(rule)

    s3.put_bucket_website(
        Bucket=bucket,
        WebsiteConfiguration=configuration,
    )


def s3_delete_missing(files, key_prefix=None):
    """Delete files in the S3 bucket.

    Delete files on the S3 bucket that doesn't match the files
    given as the param. If the key_prefix is not specified, missing
    files that has main/ as a prefix will be removed. Otherwise, it
    will remove files with the same prefix as key_prefix.
    """
    s3, bucket = create_aws_session()
    if key_prefix:
        files_on_server = get_s3_keys(s3, bucket, include_prefix=key_prefix)
    else:
        files_on_server = get_s3_keys(s3, bucket, exclude_prefix="main/")
    files = [key_prefix + "/" + path if key_prefix else path for path, f in files]
    files_to_delete = [path for path in files_on_server if path not in files]

    query_size = 1000
    while files_to_delete:
        keys_to_remove = [{"Key": key} for key in files_to_delete[:query_size]]
        response = s3.delete_objects(
            Bucket=bucket,
            Delete={
                "Objects": keys_to_remove,
            },  # NOQA
        )
        pprint(response, indent=2)
        files_to_delete = files_to_delete[query_size:]


def s3_upload(files: list[tuple[str, BaseFile]], key_prefix: str | None = None) -> None:
    """Upload files to an S3 bucket.

    Keys in the bucket correspond to source filenames. If ``key_prefix`` is
    defined, key names will be ``<key_prefix>/<path>``.

    Pruning of stale keys (s3_delete_missing) runs concurrently with the
    uploads instead of after them. Listing the bucket is network-bound and
    slow, but it is independent of the uploads: uploads only ever write keys
    that are present in ``files``, while the prune only ever removes keys that
    are *not* in ``files``, so the two operate on disjoint key sets and their
    relative ordering doesn't affect the result.
    """
    s3, bucket = create_aws_session()

    def upload(f, path, bucket, key, extra_args):
        # Need to flush to avoid buffering/interleaving from multiple threads.
        sys.stdout.write(f"uploading {path} to {key}\n")
        sys.stdout.flush()
        # The file types returned by mozpack behave like file objects. But
        # they don't accept an argument to read(). So we wrap in a BytesIO.
        s3.upload_fileobj(io.BytesIO(f.read()), bucket, key, ExtraArgs=extra_args)

    fs = []
    # One extra worker for the prune task on top of the upload workers.
    with futures.ThreadPoolExecutor(UPLOAD_THREADS + 1) as e:
        # Kick off the (slow) prune first so its bucket listing overlaps with
        # the uploads rather than blocking after them.
        delete_future = e.submit(s3_delete_missing, files, key_prefix)

        for path, f in files:
            content_type, content_encoding = mimetypes.guess_type(path)
            extra_args = {}
            if content_type:
                if content_type.startswith("text/"):
                    content_type += '; charset="utf-8"'
                extra_args["ContentType"] = content_type
            if content_encoding:
                extra_args["ContentEncoding"] = content_encoding

            if key_prefix:
                key = f"{key_prefix}/{path}"
            else:
                key = path

            fs.append(e.submit(upload, f, path, bucket, key, extra_args))

    # Need to do this to catch any exceptions.
    delete_future.result()
    for f in fs:
        f.result()
