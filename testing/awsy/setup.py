# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from setuptools import find_packages, setup

PACKAGE_NAME = "awsy"
PACKAGE_VERSION = "0.0.1"

setup(
    name=PACKAGE_NAME,
    version=PACKAGE_VERSION,
    description="AreWeSlimYet",
    long_description="A memory testing framework for Firefox.",
    author="Mozilla Automation and Testing Team",
    author_email="tools@lists.mozilla.org",
    license="MPL 1.1/GPL 2.0/LGPL 2.1",
    packages=find_packages(),
    zip_safe=False,
    install_requires=["marionette_harness", "PyYaml"],
    python_requires=">=3.10",
    classifiers=[
        "Development Status :: 4 - Beta",
        "Environment :: Console",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Mozilla Public License 1.1 (MPL 1.1)",
        "Operating System :: OS Independent",
        "Topic :: Software Development :: Libraries :: Python Modules",
    ],
)
