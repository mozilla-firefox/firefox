# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# The values correspond to entries at
# https://tools.taskcluster.net/index/artifacts/#gecko.v2.mozilla-central.latest/gecko.v2.mozilla-central.latest
JOB_CHOICES = {
    "android-arm-opt",
    "android-arm-debug",
    "android-x86_64-opt",
    "android-x86_64-debug",
    "android-aarch64-opt",
    "android-aarch64-debug",
    "android-arm-appservices-opt",
    "android-x86_64-appservices-opt",
    "android-aarch64-appservices-opt",
    "linux64-opt",
    "linux64-debug",
    "linux64-aarch64-opt",
    "linux64-aarch64-debug",
    "macosx64-opt",
    "macosx64-debug",
    "macosx64-aarch64-opt",
    "macosx64-aarch64-debug",
    "win32-opt",
    "win32-debug",
    "win64-opt",
    "win64-debug",
    "win64-aarch64-opt",
    "win64-aarch64-debug",
}

GECKO_NIGHTLY_TREES = [
    "mozilla-central",
    "integration/autoland",
]

# The list below should be updated when we have new ESRs.
GECKO_ESR_TREES = [
    "releases/mozilla-esr153",
    "releases/mozilla-esr140",
    "releases/mozilla-esr115",
]
