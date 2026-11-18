# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from setuptools import setup

PACKAGE_NAME = "mozperftest"
PACKAGE_VERSION = "0.2"

deps = [
    "regex",
    "jsonschema",
    "attr",
    "mozlog >= 6.0",
    "mozdevice >= 4.0.0",
    "mozproxy",
    "mozinfo",
    "mozfile",
    "mozperftest-tools",
]

setup(
    name=PACKAGE_NAME,
    version=PACKAGE_VERSION,
    description="Mozilla's mach perftest command",
    classifiers=["Programming Language :: Python :: 3 :: Only"],
    python_requires=">=3.10",
    # Get strings from http://pypi.python.org/pypi?%3Aaction=list_classifiers
    keywords="",
    author="Mozilla Performance Test Engineering Team",
    author_email="tools@lists.mozilla.org",
    url="https://hg.mozilla.org/mozilla-central/file/tip/python/mozperftest",
    license="MPL",
    packages=["mozperftest"],
    include_package_data=True,
    zip_safe=False,
    install_requires=deps,
)
