# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from setuptools import setup

PACKAGE_VERSION = "2.4.1"


setup(
    name="mozversion",
    version=PACKAGE_VERSION,
    description="Library to get version information for applications",
    long_description="see https://firefox-source-docs.mozilla.org/mozbase/index.html",
    classifiers=[
        "Programming Language :: Python",
        "Programming Language :: Python :: 3 :: Only",
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
    python_requires=">=3.9",
)
