# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# ATTENTION: Changes to this file will need to be reflected in probe-scraper[1].
# This should happen automatically once a day.
# If something is unclear or data is not showing up in time
# you will need to file a bug in Data Platform and Tools :: General.
#
# [1] https://github.com/mozilla/probe-scraper

# Metrics that are sent by Gecko and everyone using Gecko
# (Firefox Desktop, Firefox for Android, Focus for Android, etc.).
# Order is lexicographical, enforced by t/c/glean/tests/pytest/test_yaml_indices.py
gecko_metrics = [
    "accessible/metrics.yaml",
    "browser/base/content/metrics.yaml",
    "devtools/client/accessibility/metrics.yaml",
    "devtools/client/framework/metrics.yaml",
    "devtools/client/inspector/metrics.yaml",
    "devtools/client/responsive/metrics.yaml",
    "devtools/client/shared/metrics.yaml",
    "devtools/client/webconsole/metrics.yaml",
    "devtools/shared/heapsnapshot/metrics.yaml",
    "docshell/base/metrics.yaml",
    "dom/base/use_counter_metrics.yaml",
    "dom/cache/metrics.yaml",
    "dom/canvas/metrics.yaml",
    "dom/geolocation/metrics.yaml",
    "dom/indexedDB/metrics.yaml",
    "dom/localstorage/metrics.yaml",
    "dom/media/eme/metrics.yaml",
    "dom/media/hls/metrics.yaml",
    "dom/media/metrics.yaml",
    "dom/media/mp4/metrics.yaml",
    "dom/media/webrtc/metrics.yaml",
    "dom/media/webspeech/metrics.yaml",
    "dom/metrics.yaml",
    "dom/notification/metrics.yaml",
    "dom/performance/metrics.yaml",
    "dom/push/metrics.yaml",
    "dom/quota/metrics.yaml",
    "dom/security/metrics.yaml",
    "dom/serviceworkers/metrics.yaml",
    "dom/webauthn/metrics.yaml",
    "dom/workers/metrics.yaml",
    "extensions/permissions/metrics.yaml",
    "gfx/metrics.yaml",
    "image/decoders/metrics.yaml",
    "intl/locale/metrics.yaml",
    "ipc/metrics.yaml",
    "js/src/metrics.yaml",
    "js/xpconnect/metrics.yaml",
    "layout/base/metrics.yaml",
    "mobile/shared/modules/geckoview/metrics.yaml",
    "modules/libjar/metrics.yaml",
    "modules/libpref/metrics.yaml",
    "netwerk/cache2/metrics.yaml",
    "netwerk/dns/metrics.yaml",
    "netwerk/metrics.yaml",
    "netwerk/protocol/http/metrics.yaml",
    "netwerk/protocol/websocket/metrics.yaml",
    "parser/htmlparser/metrics.yaml",
    "security/certverifier/metrics.yaml",
    "security/ct/metrics.yaml",
    "security/manager/ssl/metrics.yaml",
    "security/sandbox/metrics.yaml",
    "services/settings/metrics.yaml",
    "services/sync/modules/metrics.yaml",
    "startupcache/metrics.yaml",
    "storage/metrics.yaml",
    "toolkit/components/aboutprocesses/metrics.yaml",
    "toolkit/components/antitracking/bouncetrackingprotection/metrics.yaml",
    "toolkit/components/antitracking/metrics.yaml",
    "toolkit/components/backgroundhangmonitor/metrics.yaml",
    "toolkit/components/captchadetection/metrics.yaml",
    "toolkit/components/cleardata/metrics.yaml",
    "toolkit/components/doh/metrics.yaml",
    "toolkit/components/downloads/metrics.yaml",
    "toolkit/components/enterprisepolicies/metrics.yaml",
    "toolkit/components/extensions/metrics.yaml",
    "toolkit/components/formautofill/metrics.yaml",
    "toolkit/components/gecko-trace/generated-metrics.yaml",
    "toolkit/components/glean/metrics.yaml",
    "toolkit/components/ipprotection/metrics.yaml",
    "toolkit/components/mediasniffer/metrics.yaml",
    "toolkit/components/messaging-system/metrics.yaml",
    "toolkit/components/ml/metrics.yaml",
    "toolkit/components/normandy/metrics.yaml",
    "toolkit/components/pageextractor/metrics.yaml",
    "toolkit/components/passwordmgr/metrics.yaml",
    "toolkit/components/pdfjs/metrics.yaml",
    "toolkit/components/printing/metrics.yaml",
    "toolkit/components/processtools/metrics.yaml",
    "toolkit/components/reader/metrics.yaml",
    "toolkit/components/reputationservice/metrics.yaml",
    "toolkit/components/resistfingerprinting/metrics.yaml",
    "toolkit/components/satchel/metrics.yaml",
    "toolkit/components/startup/metrics.yaml",
    "toolkit/components/thumbnails/metrics.yaml",
    "toolkit/components/translations/metrics.yaml",
    "toolkit/components/url-classifier/metrics.yaml",
    "toolkit/content/metrics.yaml",
    "toolkit/content/widgets/metrics.yaml",
    "toolkit/modules/gecko_metrics.yaml",
    "toolkit/mozapps/extensions/metrics.yaml",
    "toolkit/mozapps/pushnotificationhelper/metrics.yaml",
    "toolkit/mozapps/update/metrics.yaml",
    "toolkit/profile/metrics.yaml",
    "toolkit/xre/metrics.yaml",
    "widget/metrics.yaml",
    "xpcom/metrics.yaml",
]

# Metrics that are sent by Firefox Desktop
# Order is lexicographical, enforced by t/c/glean/tests/pytest/test_yaml_indices.py
firefox_desktop_metrics = [
    "browser/actors/metrics.yaml",
    "browser/components/aiwindow/metrics.yaml",
    "browser/components/asrouter/metrics.yaml",
    "browser/components/attribution/metrics.yaml",
    "browser/components/backup/metrics.yaml",
    "browser/components/contextualidentity/metrics.yaml",
    "browser/components/controlcenter/metrics.yaml",
    "browser/components/customizableui/metrics.yaml",
    "browser/components/customkeys/metrics.yaml",
    "browser/components/downloads/metrics.yaml",
    "browser/components/extensions/metrics.yaml",
    "browser/components/firefoxview/metrics.yaml",
    "browser/components/genai/metrics.yaml",
    "browser/components/ipprotection/metrics.yaml",
    "browser/components/metrics.yaml",
    "browser/components/migration/metrics.yaml",
    "browser/components/miniwindow/metrics.yaml",
    "browser/components/newtab/metrics.yaml",
    "browser/components/places/metrics.yaml",
    "browser/components/preferences/metrics.yaml",
    "browser/components/privatebrowsing/metrics.yaml",
    "browser/components/profiles/metrics.yaml",
    "browser/components/protections/metrics.yaml",
    "browser/components/referrals/metrics.yaml",
    "browser/components/screenshots/metrics.yaml",
    "browser/components/search/metrics.yaml",
    "browser/components/sessionstore/metrics.yaml",
    "browser/components/sharing/metrics.yaml",
    "browser/components/shell/metrics.yaml",
    "browser/components/sidebar/metrics.yaml",
    "browser/components/tabbrowser/metrics.yaml",
    "browser/components/tabnotes/metrics.yaml",
    "browser/components/taskbartabs/metrics.yaml",
    "browser/components/textrecognition/metrics.yaml",
    "browser/components/urlbar/metrics.yaml",
    "browser/extensions/search-detection/metrics.yaml",
    "browser/modules/metrics.yaml",
    "dom/media/platforms/wmf/metrics.yaml",
    "services/fxaccounts/metrics.yaml",
    "toolkit/components/contentanalysis/metrics.yaml",
    "toolkit/components/contentrelevancy/metrics.yaml",
    "toolkit/components/crashes/metrics.yaml",
    "toolkit/components/nimbus/metrics.yaml",
    "toolkit/components/pictureinpicture/metrics.yaml",
    "toolkit/components/places/metrics.yaml",
    "toolkit/components/reportbrokensite/metrics.yaml",
    "toolkit/components/satchel/megalist/metrics.yaml",
    "toolkit/components/search/metrics.yaml",
    "toolkit/components/telemetry/metrics.yaml",
    "toolkit/modules/metrics.yaml",
    "toolkit/mozapps/update/shared_metrics.yaml",
    "widget/cocoa/metrics.yaml",
    "widget/gtk/metrics.yaml",
    "widget/windows/metrics.yaml",
]

# Metrics that are sent by the Firefox Desktop Background Update Task
# Order is lexicographical, enforced by t/c/glean/tests/pytest/test_yaml_indices.py
background_update_metrics = [
    "toolkit/components/crashes/metrics.yaml",
    "toolkit/mozapps/update/background_task_metrics.yaml",
    "toolkit/mozapps/update/shared_metrics.yaml",
]

# Metrics that are sent by the Firefox Desktop Background Tasks
# Order is lexicographical, enforced by t/c/glean/tests/pytest/test_yaml_indices.py
background_tasks_metrics = [
    "browser/components/metrics.yaml",
    "toolkit/components/backgroundtasks/metrics.yaml",
    "toolkit/components/crashes/metrics.yaml",
    "toolkit/mozapps/defaultagent/metrics.yaml",
]

# Test metrics
# Order is lexicographical, enforced by t/c/glean/tests/pytest/test_yaml_indices.py
test_metrics = [
    "toolkit/components/glean/tests/test_metrics.yaml",
    "toolkit/components/telemetry/tests/test_metrics.yaml",
]

# The list of all Glean metrics.yaml files, relative to the top src dir.
# ONLY TO BE MODIFIED BY FOG PEERS!
metrics_yamls = sorted(
    set(
        gecko_metrics
        + firefox_desktop_metrics
        + background_update_metrics
        + background_tasks_metrics
        + test_metrics
    )
)

# Pings that are sent by Gecko and everyone using Gecko
# (Firefox Desktop, Firefox for Android, Focus for Android, etc.).
# Order is lexicographical, enforced by t/c/glean/tests/pytest/test_yaml_indices.py
gecko_pings = [
    "dom/pings.yaml",
    "netwerk/pings.yaml",
    "storage/pings.yaml",
    "toolkit/components/antitracking/bouncetrackingprotection/pings.yaml",
    "toolkit/components/backgroundhangmonitor/pings.yaml",
    "toolkit/components/captchadetection/pings.yaml",
    "toolkit/components/gecko-trace/pings.yaml",
    "toolkit/components/glean/pings.yaml",
    "toolkit/components/pageextractor/pings.yaml",
    "toolkit/components/resistfingerprinting/pings.yaml",
    "toolkit/mozapps/extensions/pings.yaml",
]

# Pings that are sent by Firefox Desktop.
# Order is lexicographical, enforced by t/c/glean/tests/pytest/test_yaml_indices.py
firefox_desktop_pings = [
    "browser/components/asrouter/pings.yaml",
    "browser/components/backup/pings.yaml",
    "browser/components/newtab/pings.yaml",
    "browser/components/profiles/pings.yaml",
    "browser/components/referrals/pings.yaml",
    "browser/components/search/pings.yaml",
    "browser/components/urlbar/pings.yaml",
    "browser/modules/pings.yaml",
    "services/fxaccounts/pings.yaml",
    "services/sync/pings.yaml",
    "toolkit/components/nimbus/pings.yaml",
    "toolkit/components/reportbrokensite/pings.yaml",
    "toolkit/components/telemetry/pings.yaml",
    "toolkit/modules/pings.yaml",
    "toolkit/mozapps/update/shared_pings.yaml",
    "toolkit/profile/pings.yaml",
]

# Pings that are sent by the Firefox Desktop Background Update Task
# Order is lexicographical, enforced by t/c/glean/tests/pytest/test_yaml_indices.py
background_update_pings = [
    "toolkit/mozapps/update/pings.yaml",
    "toolkit/mozapps/update/shared_pings.yaml",
]

# Pings that are sent by the Firefox Desktop Background Tasks
# Order is lexicographical, enforced by t/c/glean/tests/pytest/test_yaml_indices.py
background_tasks_pings = [
    "toolkit/components/backgroundtasks/pings.yaml",
    "toolkit/components/nimbus/pings.yaml",
    "toolkit/mozapps/defaultagent/pings.yaml",
]

# Test pings
# Order is lexicographical, enforced by t/c/glean/tests/pytest/test_yaml_indices.py
test_pings = [
    "toolkit/components/glean/tests/test_pings.yaml",
]

# Map of app ids to lists of pings files for that app.
# Necessary to ensure different apps don't store data for unsent pings.
# Use the app id conjugation passed to initializeFOG (dotted.case).
pings_by_app_or_lib_id = {
    "firefox.desktop": gecko_pings + firefox_desktop_pings + test_pings,
    "firefox.desktop.background.update": gecko_pings
    + background_update_pings
    + test_pings,
    "firefox.desktop.background.tasks": gecko_pings + background_tasks_pings,
    # Not an _actual_ app ID in use,
    # but needed to differentiate ping registration in geckoview builds
    "gecko": gecko_pings + test_pings,
}

# The list of all Glean pings.yaml files, relative to the top src dir.
# ONLY TO BE MODIFIED BY FOG PEERS!
pings_yamls = sorted(
    set(
        gecko_pings
        + firefox_desktop_pings
        + background_update_pings
        + background_tasks_pings
        + test_pings
    )
)

# The list of tags that are allowed in the above to files, and their
# descriptions. Currently we restrict to a set scraped from bugzilla
# (via `./mach update-glean-tags`)
# Order is lexicographical, enforced by t/c/glean/tests/pytest/test_yaml_indices.py
tags_yamls = [
    "toolkit/components/glean/tags.yaml",
]
