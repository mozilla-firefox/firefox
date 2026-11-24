# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Downloads Heavy profiles from TaskCluster.
"""

import datetime
import functools
import os
import tarfile
from email.utils import parsedate

import requests
from mozlog import get_proxy_logger
from requests.adapters import HTTPAdapter
from xdg import xdg_config_home

LOG = get_proxy_logger()
TC_LINK = (
    "https://index.taskcluster.net/v1/task/garbage.heavyprofile/"
    "artifacts/public/today-%s.tgz"
)


class ProgressBar:
    def __init__(self, size, template="\r%d%%"):
        self.size = size
        self.current = 0
        self.tens = 0
        self.template = template

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False

    def incr(self):
        if self.current == self.size:
            return
        percent = float(self.current) / float(self.size) * 100
        tens, __ = divmod(percent, 10)
        if tens > self.tens:
            LOG.info(self.template % percent)
            self.tens = tens

        self.current += 1


def follow_redirects(url, max=3):
    location = url
    current = 0
    page = requests.head(url)
    while page.status_code == 303 and current < max:
        current += 1
        location = page.headers["Location"]
        page = requests.head(location)
    if page.status_code == 303 and current == max:
        raise ValueError("Max redirects Reached")

    last_modified = page.headers.get("Last-Modified", None)
    if last_modified is not None:
        last_modified = datetime.datetime(*parsedate(last_modified)[:6])
    return location, last_modified


def _recursive_mtime(path):
    max_mtime = os.path.getmtime(path)
    for root, dirs, files in os.walk(path):
        for element in dirs + files:
            age = os.path.getmtime(os.path.join(root, element))
            max_mtime = max(max_mtime, age)
    return max_mtime


def profile_age(profile_dir, last_modified=None):
    if last_modified is None:
        last_modified = datetime.datetime.now()

    profile_ts = _recursive_mtime(profile_dir)
    profile_ts = datetime.datetime.fromtimestamp(profile_ts)
    return (last_modified - profile_ts).days


def download_profile(name, profiles_dir=None):
    if profiles_dir is None:
        if os.environ.get("MOZ_LEGACY_HOME") == "1":
            profiles_dir = os.path.join(os.path.expanduser("~"), ".mozilla", "profiles")
        else:
            profiles_dir = os.path.join(xdg_config_home(), "mozilla", "profiles")
    profiles_dir = os.path.abspath(profiles_dir)
    if not os.path.exists(profiles_dir):
        os.makedirs(profiles_dir)

    target = os.path.join(profiles_dir, name)
    url = TC_LINK % name
    cache_dir = os.path.join(profiles_dir, ".cache")
    if not os.path.exists(cache_dir):
        os.makedirs(cache_dir)

    archive_file = os.path.join(cache_dir, f"today-{name}.tgz")

    url, last_modified = follow_redirects(url)
    if os.path.exists(target):
        age = profile_age(target, last_modified)
        if age < 7:
            # profile is not older than a week, we're good
            LOG.info(f"Local copy of {name!r} is fresh enough")
            LOG.info(f"{age} days old")
            return target

    LOG.info(f"Downloading from {url!r}")
    session = requests.Session()
    session.mount("https://", HTTPAdapter(max_retries=5))
    req = session.get(url, stream=True, timeout=20)
    req.raise_for_status()

    total_length = int(req.headers.get("content-length"))

    # XXX implement Range to resume download on disconnects
    template = "Download progress %d%%"
    with open(archive_file, "wb") as f:
        iter = req.iter_content(chunk_size=1024)
        # pylint --py3k W1619
        size = total_length / 1024 + 1
        with ProgressBar(size=size, template=template) as bar:
            for chunk in iter:
                if chunk:
                    f.write(chunk)
                bar.incr()

    LOG.info(f"Extracting profile in {target!r}")
    template = "Extraction progress %d%%"

    with tarfile.open(archive_file, "r:gz") as tar:
        LOG.info("Checking the tarball content...")
        size = len(list(tar))
        with ProgressBar(size=size, template=template) as bar:

            def _extract(self, *args, **kw):
                bar.incr()
                return self.old(*args, **kw)

            tar.old = tar.extract
            tar.extract = functools.partial(_extract, tar)
            tar.extractall(target)
    LOG.info("Profile downloaded.")
    return target
