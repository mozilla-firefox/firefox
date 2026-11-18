# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from setuptools import find_packages, setup

entry_points = """
      [console_scripts]
      cp-creator = condprof.main:main
      cp-client = condprof.client:main
      """

setup(
    name="conditioned-profile",
    version="0.3",
    packages=find_packages(),
    description="Firefox Heavy Profile creator",
    include_package_data=True,
    data_files=[
        (
            "condprof",
            [
                "condprof/customization/default.json",
                "condprof/customization/youtube.json",
                "condprof/customization/webext.json",
            ],
        )
    ],
    zip_safe=False,
    install_requires=[],  # use requirements files
    python_requires=">=3.10",
    entry_points=entry_points,
)
