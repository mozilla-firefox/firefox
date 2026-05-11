# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

config = {
    "default_actions": [
        "get-secrets",
        "build",
    ],
    "vcs_share_base": os.path.join("y:", os.sep, "hg-shared"),
    "max_build_output_timeout": 60 * 80,
    "env": {
        "HG_SHARE_BASE_DIR": os.path.join("y:", os.sep, "hg-shared"),
        "MOZBUILD_STATE_PATH": os.path.join(os.getcwd(), ".mozbuild"),
        "MOZ_CRASHREPORTER_NO_REPORT": "1",
        "MOZ_OBJDIR": "%(abs_obj_dir)s",
        "TINDERBOX_OUTPUT": "1",
        "TOOLTOOL_CACHE": "c:/builds/tooltool_cache",
        "TOOLTOOL_HOME": "/c/builds",
        "MSYSTEM": "MINGW32",
        "WORKSPACE": "%(base_work_dir)s",
    },
    "upload_env": {
        "UPLOAD_PATH": os.path.join(os.getcwd(), "public", "build"),
    },
    "secret_files": [
        {
            "filename": "sb-gapi.data",
            "secret_name": "project/releng/gecko/build/level-%(scm-level)s/sb-gapi.data",
            "min_scm_level": 1,
        },
        {
            "filename": "mozilla-desktop-geoloc-api.key",
            "secret_name": "project/releng/gecko/build/level-%(scm-level)s/mozilla-desktop-geoloc-api.key",
            "min_scm_level": 2,
            "default": "try-build-has-no-secrets",
        },
    ],
}
