# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from setuptools import setup

PACKAGE_NAME = "manifestparser"
PACKAGE_VERSION = "2.4.0"

DEPS = [
    "mozlog >= 6.0",
    "toml >= 0.10.2",
    "tomlkit >= 0.12.3",
]
setup(
    name=PACKAGE_NAME,
    version=PACKAGE_VERSION,
    description="Library to create and manage test manifests",
    long_description="see https://firefox-source-docs.mozilla.org/mozbase/index.html",
    classifiers=[
        "Programming Language :: Python",
        "Programming Language :: Python :: 3 :: Only",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
    ],
    # Get strings from http://pypi.python.org/pypi?%3Aaction=list_classifiers
    keywords="mozilla manifests",
    author="Mozilla Automation and Testing Team",
    author_email="tools@lists.mozilla.org",
    url="https://wiki.mozilla.org/Auto-tools/Projects/Mozbase",
    license="MPL",
    zip_safe=False,
    packages=["manifestparser"],
    install_requires=DEPS,
    entry_points="""
      [console_scripts]
      manifestparser = manifestparser.cli:main
      """,
    python_requires=">=3.8",
)
