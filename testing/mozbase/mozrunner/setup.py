# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from setuptools import find_packages, setup

PACKAGE_NAME = "mozrunner"
PACKAGE_VERSION = "8.5.0"

desc = """Reliable start/stop/configuration of Mozilla Applications (Firefox, Thunderbird, etc.)"""

deps = [
    "mozdevice>=4.2.0,<5",
    "mozfile>=1.2",
    "mozinfo>=0.7,<2",
    "mozlog>=6.0",
    "mozprocess>=1.3.0,<2",
    "mozprofile~=3.0",
]

EXTRAS_REQUIRE = {"crash": ["mozcrash >= 2.3.0"]}

setup(
    name=PACKAGE_NAME,
    version=PACKAGE_VERSION,
    description=desc,
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
    packages=find_packages(),
    zip_safe=False,
    install_requires=deps,
    extras_require=EXTRAS_REQUIRE,
    entry_points="""
      # -*- Entry points: -*-
      [console_scripts]
      mozrunner = mozrunner:cli
      """,
    python_requires=">=3.8",
)
