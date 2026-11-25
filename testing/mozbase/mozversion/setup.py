# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from setuptools import setup

PACKAGE_VERSION = "2.4.0"


setup(
    name="mozversion",
    version=PACKAGE_VERSION,
    description="Library to get version information for applications",
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
    keywords="mozilla",
    author="Mozilla Automation and Testing Team",
    author_email="tools@lists.mozilla.org",
    url="https://wiki.mozilla.org/Auto-tools/Projects/Mozbase",
    license="MPL",
    packages=["mozversion"],
    include_package_data=True,
    zip_safe=False,
    install_requires=["mozlog >= 6.0"],
    entry_points="""
      # -*- Entry points: -*-
      [console_scripts]
      mozversion = mozversion:cli
      """,
    python_requires=">=3.8",
)
