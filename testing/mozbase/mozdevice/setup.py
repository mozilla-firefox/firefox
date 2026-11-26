# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from setuptools import setup

PACKAGE_NAME = "mozdevice"
PACKAGE_VERSION = "4.4.0"

deps = ["mozlog >= 6.0", "mozprocess >= 1.3.0"]

setup(
    name=PACKAGE_NAME,
    version=PACKAGE_VERSION,
    description="Mozilla-authored device management",
    long_description="see https://firefox-source-docs.mozilla.org/mozbase/index.html",
    classifiers=[
        "Programming Language :: Python",
        "Programming Language :: Python :: 3 :: Only",
    ],
    # Get strings from http://pypi.python.org/pypi?%3Aaction=list_classifiers
    keywords="",
    author="Mozilla Automation and Testing Team",
    author_email="tools@lists.mozilla.org",
    url="https://wiki.mozilla.org/Auto-tools/Projects/Mozbase",
    license="MPL",
    packages=["mozdevice"],
    include_package_data=True,
    zip_safe=False,
    install_requires=deps,
    entry_points="""
      # -*- Entry points: -*-
      """,
    python_requires=">=3.9",
)
