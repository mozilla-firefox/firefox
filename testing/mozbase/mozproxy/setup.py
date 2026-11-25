# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from setuptools import setup

PACKAGE_NAME = "mozproxy"
PACKAGE_VERSION = "2.0.0"

# dependencies
deps = ["redo", "mozinfo >= 0.7", "mozlog >= 6.0", "mozprocess >= 1.3.0"]

setup(
    name=PACKAGE_NAME,
    version=PACKAGE_VERSION,
    description="Proxy for playback",
    long_description="see https://firefox-source-docs.mozilla.org/mozbase/index.html",
    classifiers=[
        "Programming Language :: Python",
        "Programming Language :: Python :: 3 :: Only",
    ],
    # Get strings from http://pypi.python.org/pypi?%3Aaction=list_classifiers
    keywords="mozilla",
    author="Mozilla Automation and Tools team",
    author_email="tools@lists.mozilla.org",
    url="https://wiki.mozilla.org/Auto-tools/Projects/Mozbase",
    license="MPL",
    packages=["mozproxy"],
    install_requires=deps,
    entry_points={
        "console_scripts": [
            "mozproxy=mozproxy.driver:main",
        ],
    },
    include_package_data=True,
    zip_safe=False,
    python_requires=">=3.9",
)
