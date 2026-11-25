# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os

from setuptools import setup

try:
    here = os.path.dirname(os.path.abspath(__file__))
    description = open(os.path.join(here, "README.md")).read()
except OSError:
    description = None

PACKAGE_VERSION = "2.1.0"

deps = [
    "mozinfo >= 0.7",
    "mozfile >= 1.0",
    "requests",
]

setup(
    name="mozInstall",
    version=PACKAGE_VERSION,
    description="package for installing and uninstalling Mozilla applications",
    long_description="see https://firefox-source-docs.mozilla.org/mozbase/index.html",
    # Get strings from http://pypi.python.org/pypi?%3Aaction=list_classifiers
    classifiers=[
        "Environment :: Console",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Mozilla Public License 2.0 (MPL 2.0)",
        "Natural Language :: English",
        "Operating System :: OS Independent",
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Programming Language :: Python",
        "Programming Language :: Python :: 3 :: Only",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
    ],
    keywords="mozilla",
    author="Mozilla Automation and Tools team",
    author_email="tools@lists.mozilla.org",
    url="https://wiki.mozilla.org/Auto-tools/Projects/Mozbase",
    license="MPL 2.0",
    packages=["mozinstall"],
    include_package_data=True,
    zip_safe=False,
    install_requires=deps,
    # we have to generate two more executables for those systems that cannot run as Administrator
    # and the filename containing "install" triggers the UAC
    entry_points="""
      # -*- Entry points: -*-
      [console_scripts]
      mozinstall = mozinstall:install_cli
      mozuninstall = mozinstall:uninstall_cli
      moz_add_to_system = mozinstall:install_cli
      moz_remove_from_system = mozinstall:uninstall_cli
      """,
    python_requires=">=3.8",
)
