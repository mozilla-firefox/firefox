# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""This routine controls which localizable files and entries are
reported and l10n-merged.
This needs to stay in sync with the copy in mobile/locales.
"""


def test(mod, path, entity=None):
    import re

    # ignore anything but mobile, which is our local repo checkout name
    if mod not in ("dom", "toolkit", "mobile", "mobile/android"):
        return "ignore"

    if mod == "toolkit":
        # keep this file list in sync with jar.mn
        if path in (
            "chrome/global/commonDialogs.properties",
            "chrome/global/intl.css",
            "toolkit/branding/brandings.ftl",
            "toolkit/global/processTypes.ftl",
            "toolkit/global/resetProfile.ftl",
        ):
            return "error"
        if re.match(r"toolkit/about/[^/]*About.ftl", path):
            # error on toolkit/about/*About.ftl
            return "error"
        if re.match(r"toolkit/about/[^/]*Mozilla.ftl", path):
            # error on toolkit/about/*Mozilla.ftl
            return "error"
        if re.match(r"toolkit/about/[^/]*Rights.ftl", path):
            # error on toolkit/about/*Rights.ftl
            return "error"
        if re.match(r"toolkit/about/[^/]*Compat.ftl", path):
            # error on toolkit/about/*Compat.ftl
            return "error"
        if re.match(r"toolkit/about/[^/]*Support.ftl", path):
            # error on toolkit/about/*Support.ftl
            return "error"
        if re.match(r"toolkit/about/[^/]*Webrtc.ftl", path):
            # error on toolkit/about/*Webrtc.ftl
            return "error"
        if re.match(r"toolkit/about/[^/]*Logging.ftl", path):
            # error on toolkit/about/*Logging.ftl
            return "error"
        return "ignore"

    if mod == "dom":
        # keep this file list in sync with jar.mn
        if path in (
            "chrome/accessibility/AccessFu.properties",
            "chrome/dom/dom.properties",
        ):
            return "error"
        return "ignore"

    return "error"
