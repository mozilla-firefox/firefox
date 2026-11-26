#!/usr/bin/python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import subprocess
import sys
import threading
import urllib.request
from urllib.parse import urlparse, urlunparse

# This script interposes between APT and its HTTP method. APT sends queries on
# stdin, and expect responses on stdout. We intercept those and change the
# snapshot.debian.org URLs it requests on the fly, if the equivalent URLs
# exist on deb.debian.org.

URI_HEADER = "URI: "


def url_exists(url):
    try:
        req = urllib.request.Request(url, method="HEAD")
        response = urllib.request.urlopen(req)
        return response.getcode() == 200
    except Exception:
        return False


def write_and_flush(fh, data):
    fh.write(data)
    fh.flush()


def output_handler(proc, url_mapping, lock):
    for line in proc.stdout:
        if line.startswith(URI_HEADER):
            url = line[len(URI_HEADER) :].rstrip()
            # APT expects back the original url it requested.
            with lock:
                original_url = url_mapping.get(url, None)
            if original_url:
                write_and_flush(sys.stdout, line.replace(url, original_url))
                continue
        write_and_flush(sys.stdout, line)


def main():
    proc = subprocess.Popen(
        ["/usr/lib/apt/methods/https"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True,
    )
    url_mapping = {}
    lock = threading.Lock()
    output_thread = threading.Thread(
        target=output_handler, args=(proc, url_mapping, lock), daemon=True
    )
    output_thread.start()

    while True:
        try:
            line = sys.stdin.readline()
        except KeyboardInterrupt:
            # When apt cuts the connection, we receive a KeyboardInterrupt.
            break
        if not line:
            break

        if line.startswith(URI_HEADER):
            url = line[len(URI_HEADER) :].rstrip()
            url_parts = urlparse(url)
            # For .deb packages, if we can find the file on deb.debian.org, take it
            # from there instead of snapshot.debian.org, because deb.debian.org will
            # be much faster. Hopefully, most files will be available on deb.debian.org.
            if url_parts.hostname == "snapshot.debian.org" and url_parts.path.endswith(
                ".deb"
            ):
                # The url is assumed to be of the form
                # http://snapshot.debian.org/archive/section/yymmddThhmmssZ/...
                path_parts = url_parts.path.split("/")
                # urlparse().path always starts with a / so path_parts is
                # expected to look like ["", "archive", "section", "yymmddThhmmssZ", ...]
                # we want to remove "archive" and "yymmddThhmmssZ" to create an url
                # on deb.debian.org.
                path_parts.pop(3)
                path_parts.pop(1)
                modified_url = urlunparse(
                    url_parts._replace(
                        netloc="deb.debian.org", path="/".join(path_parts)
                    )
                )
                if url_exists(modified_url):
                    with lock:
                        url_mapping[modified_url] = url
                    write_and_flush(proc.stdin, line.replace(url, modified_url))
                    continue
        write_and_flush(proc.stdin, line)

    proc.stdin.close()
    output_thread.join()


if __name__ == "__main__":
    main()
