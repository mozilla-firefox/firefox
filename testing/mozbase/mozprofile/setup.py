# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from setuptools import setup

PACKAGE_NAME = "mozprofile"
PACKAGE_VERSION = "3.0.0"

deps = [
    "mozfile>=1.2",
    "mozlog>=6.0",
]

setup(
    name=PACKAGE_NAME,
    version=PACKAGE_VERSION,
    description="Library to create and modify Mozilla application profiles",
    long_description="see https://firefox-source-docs.mozilla.org/mozbase/index.html",
    classifiers=[
        "Environment :: Console",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Mozilla Public License 2.0 (MPL 2.0)",
        "Natural Language :: English",
        "Operating System :: OS Independent",
        "Programming Language :: Python",
        "Programming Language :: Python :: 3 :: Only",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "Topic :: Software Development :: Libraries :: Python Modules",
    ],
    keywords="mozilla",
    author="Mozilla Automation and Tools team",
    author_email="tools@lists.mozilla.org",
    url="https://wiki.mozilla.org/Auto-tools/Projects/Mozbase",
    license="MPL 2.0",
    packages=["mozprofile"],
    include_package_data=True,
    zip_safe=False,
    install_requires=deps,
    extras_require={"manifest": ["manifestparser >= 0.6"]},
    tests_require=["wptserve"],
    entry_points="""
      # -*- Entry points: -*-
      [console_scripts]
      mozprofile = mozprofile:cli
      view-profile = mozprofile:view_profile
      diff-profiles = mozprofile:diff_profiles
      """,
    python_requires=">=3.8",
)
