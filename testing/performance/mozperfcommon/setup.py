# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from setuptools import setup

PACKAGE_VERSION = "0.1.0"

setup(
    name="mozperfcommon",
    version=PACKAGE_VERSION,
    description="Code shared between the Mozilla performance test harnesses",
    long_description="see https://firefox-source-docs.mozilla.org/testing/perfdocs/mozperfcommon.html",
    keywords="mozilla",
    author="Mozilla Performance Test Engineering Team",
    author_email="perftest@mozilla.com",
    license="MPL",
    packages=["mozperfcommon"],
    include_package_data=True,
    zip_safe=False,
    install_requires=["pyyaml", "requests"],
    python_requires=">=3.9",
)
