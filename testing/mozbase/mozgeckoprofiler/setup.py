# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from setuptools import setup

PACKAGE_NAME = "mozgeckoprofiler"
PACKAGE_VERSION = "1.0.0"

setup(
    name=PACKAGE_NAME,
    version=PACKAGE_VERSION,
    description="Library to generate and view performance data in the Firefox Profiler",
    long_description="see https://firefox-source-docs.mozilla.org/mozbase/mozgeckoprofiler.html",
    classifiers=[
        "Programming Language :: Python",
        "Programming Language :: Python :: 3 :: Only",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "License :: OSI Approved :: Mozilla Public License 2.0 (MPL 2.0)",
    ],
    keywords="mozilla",
    author="Mozilla Automation and Tools team",
    author_email="tools@lists.mozilla.org",
    url="https://wiki.mozilla.org/Auto-tools/Projects/Mozbase",
    license="MPL",
    packages=["mozgeckoprofiler"],
    include_package_data=True,
    zip_safe=False,
    install_requires=[],
    tests_require=[],
    python_requires=">=3.8",
)
