# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

from talos import filter

"""
test definitions for Talos
"""

_TESTS = {}  # internal dict of Talos test classes


def register_test():
    """Decorator to register Talos test classes"""

    def wrapper(klass):
        assert issubclass(klass, Test)
        assert klass.name() not in _TESTS

        _TESTS[klass.name()] = klass
        return klass

    return wrapper


def test_dict():
    """Return the dict of the registered test classes"""
    return _TESTS


class Test:
    """abstract base class for a Talos test case"""

    __test__ = False  # not pytest

    cycles = None  # number of cycles
    keys = []
    desktop = True
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    lower_is_better = True
    alert_threshold = 2.0
    perfherder_framework = "talos"
    subtest_alerts = False
    suite_should_alert = True
    # Default number of entries for the gecko profiler which is 128MiB.
    # This value is calculated by dividing the 128MiB of memory by 8 because
    # the profiler uses 8 bytes per entry.
    gecko_profile_entries = int(128 * 1024 * 1024 / 8)

    @classmethod
    def name(cls):
        return cls.__name__

    @classmethod
    def description(cls):
        if cls.__doc__ is None:
            return "No documentation available yet."
        else:
            doc = cls.__doc__
            description_lines = [i.strip() for i in doc.strip().splitlines()]
            return "\n".join(description_lines)

    def __init__(self, **kw):
        self.update(**kw)

    def update(self, **kw):
        self.__dict__.update(kw)

    def items(self):
        """
        returns a list of 2-tuples
        """
        retval = [("name", self.name())]
        for key in self.keys:
            value = getattr(self, key, None)
            if value is not None:
                retval.append((key, value))
        return retval

    def __str__(self):
        """string form appropriate for YAML output"""
        items = self.items()

        key, value = items.pop(0)
        lines = ["- %s: %s" % (key, value)]
        for key, value in items:
            lines.append("  %s: %s" % (key, value))
        return "\n".join(lines)


# ts-style startup tests (ts, twinopen, ts_cold, etc)
# The overall test number is calculated by excluding the max opening time
# and taking an average of the remaining numbers.
class TsBase(Test):
    """abstract base class for ts-style tests"""

    keys = [
        "url",
        "url_timestamp",
        "timeout",
        "cycles",
        "profile_path",  # The path containing the template profile. This
        # directory is copied to the temporary profile during
        # initialization of the test. If some of the files may
        # be overwritten by Firefox and need to be reinstalled
        # before each pass, use key |reinstall|
        "gecko_profile",
        "gecko_profile_interval",
        "gecko_profile_entries",
        "gecko_profile_features",
        "gecko_profile_threads",
        "gecko_profile_extra_threads",
        "gecko_profile_startup",
        "preferences",
        "xperf_counters",
        "xperf_providers",
        "xperf_user_providers",
        "xperf_stackwalk",
        "tpmozafterpaint",
        "fnbpaint",
        "tphero",
        "tpmanifest",
        "profile",
        "firstpaint",
        "userready",
        "testeventmap",
        "base_vs_ref",
        "extensions",
        "filters",
        "setup",
        "cleanup",
        "pine",
        "skip_reason",
        "webextensions",
        "webextensions_folder",
        "reinstall",  # A list of files from the profile directory that
        # should be copied to the temporary profile prior to
        # running each cycle, to avoid one cycle overwriting
        # the data used by the next another cycle (may be used
        # e.g. for sessionstore.js to ensure that all cycles
        # use the exact same sessionstore.js, rather than a
        # more recent copy).
    ]

    def __init__(self, **kw):
        super().__init__(**kw)

        # Unless set to False explicitly, all TsBase tests will have the blocklist
        # enabled by default in order to more accurately test the startup paths.
        BLOCKLIST_PREF = "extensions.blocklist.enabled"

        if not hasattr(self, "preferences"):
            self.preferences = {
                BLOCKLIST_PREF: True,
            }
        elif BLOCKLIST_PREF not in self.preferences:
            self.preferences[BLOCKLIST_PREF] = True


@register_test()
class ts_paint(TsBase):
    """
    Launches tspaint_test.html with the current timestamp in the url,
    waits for [MozAfterPaint and onLoad] to fire, then records the end
    time and calculates the time to startup.
    """

    cycles = 20
    timeout = 150
    gecko_profile_startup = True
    url = "startup_test/tspaint_test.html"
    xperf_counters = []
    win7_counters = []
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    tpmozafterpaint = True
    mainthread = False
    unit = "ms"


@register_test()
class ts_paint_webext(ts_paint):
    webextensions = "${talos}/webextensions/dummy/dummy.xpi"
    preferences = {"xpinstall.signatures.required": False}


@register_test()
class ts_paint_heavy(ts_paint):
    """
    ts_paint test ran against a heavy-user profile
    """

    profile = "simple"


@register_test()
class startup_about_home_paint(ts_paint):
    """
    Tests loading about:home on startup with the about:home startup cache
    disabled, to more accurately simulate startup when the cache does not
    exist.
    """

    url = None
    cycles = 20
    timeout = 600
    extensions = ["${talos}/startup_test/startup_about_home_paint/addon"]
    tpmanifest = "${talos}/startup_test/startup_about_home_paint/startup_about_home_paint.manifest"
    preferences = {
        "browser.startup.homepage.abouthome_cache.enabled": False,
    }
    pine = False


@register_test()
class startup_about_home_paint_cached(ts_paint):
    """
    Tests loading about:home on startup with the about:home startup cache
    enabled.
    """

    url = None
    cycles = 20
    extensions = ["${talos}/startup_test/startup_about_home_paint/addon"]
    tpmanifest = "${talos}/startup_test/startup_about_home_paint/startup_about_home_paint.manifest"
    preferences = {
        "browser.startup.homepage.abouthome_cache.enabled": True,
    }
    pine = False


@register_test()
class startup_about_home_paint_realworld_webextensions(ts_paint):
    url = None
    cycles = 20
    extensions = [
        "${talos}/startup_test/startup_about_home_paint/addon",
        "${talos}/getinfooffline",
    ]
    tpmanifest = "${talos}/startup_test/startup_about_home_paint/startup_about_home_paint.manifest"
    webextensions_folder = "${talos}/webextensions"
    preferences = {
        "browser.startup.homepage.abouthome_cache.enabled": False,
    }
    pine = False


@register_test()
class sessionrestore(TsBase):
    """
    A start up test measuring the time it takes to load a sessionstore.js file.

    1. Set up Firefox to restore from a given sessionstore.js file.
    2. Launch Firefox.
    3. Measure the delta between firstPaint and sessionRestored.
    """

    extensions = ["${talos}/startup_test/sessionrestore/addon"]
    cycles = 10
    timeout = 900
    gecko_profile_startup = True
    profile_path = "${talos}/startup_test/sessionrestore/profile"
    reinstall = ["sessionstore.jsonlz4", "sessionstore.js", "sessionCheckpoints.json"]
    # Restore the session. We have to provide a URL, otherwise Talos
    # asks for a manifest URL.
    url = "about:home"
    preferences = {"browser.startup.page": 3}
    unit = "ms"
    pine = False


@register_test()
class sessionrestore_no_auto_restore(sessionrestore):
    """
    A start up test measuring the time it takes to load a sessionstore.js file.

    1. Set up Firefox to *not* restore automatically from sessionstore.js file.
    2. Launch Firefox.
    3. Measure the delta between firstPaint and sessionRestored.
    """

    timeout = 300
    preferences = {
        "browser.startup.page": 1,
        "talos.sessionrestore.norestore": True,
    }


@register_test()
class sessionrestore_many_windows(sessionrestore):
    """
    A start up test measuring the time it takes to load a sessionstore.js file.

    1. Set up Firefox to restore automatically from sessionstore.js file.
    2. Launch Firefox.
    3. Measure the delta between firstPaint and sessionRestored.
    """

    profile_path = "${talos}/startup_test/sessionrestore/profile-manywindows"


# pageloader tests(tp5, etc)

# The overall test number is determined by first calculating the median
# page load time for each page in the set (excluding the max page load
# per individual page). The max median from that set is then excluded and
# the average is taken; that becomes the number reported to the tinderbox
# waterfall.


class PageloaderTest(Test):
    """abstract base class for a Talos Pageloader test"""

    extensions = ["${talos}/pageloader"]
    tpmanifest = None  # test manifest
    tpcycles = 1  # number of time to run each page
    cycles = None
    timeout = None

    keys = [
        "tpmanifest",
        "tpcycles",
        "tppagecycles",
        "tprender",
        "tpchrome",
        "tpmozafterpaint",
        "fnbpaint",
        "tphero",
        "tploadnocache",
        "firstpaint",
        "userready",
        "testeventmap",
        "base_vs_ref",
        "mainthread",
        "resolution",
        "cycles",
        "gecko_profile",
        "gecko_profile_interval",
        "gecko_profile_entries",
        "gecko_profile_features",
        "gecko_profile_threads",
        "gecko_profile_extra_threads",
        "tptimeout",
        "win_counters",
        "linux_counters",
        "mac_counters",
        "tpscrolltest",
        "xperf_counters",
        "timeout",
        "profile_path",
        "xperf_providers",
        "xperf_user_providers",
        "xperf_stackwalk",
        "format_pagename",
        "filters",
        "preferences",
        "extensions",
        "setup",
        "cleanup",
        "pine",
        "skip_reason",
        "lower_is_better",
        "alert_threshold",
        "unit",
        "webextensions",
        "profile",
        "suite_should_alert",
        "subtest_alerts",
        "perfherder_framework",
        "pdfpaint",
        "webextensions_folder",
        "a11y",
    ]


class QuantumPageloadTest(PageloaderTest):
    """
    Base class for a Quantum Pageload test
    """

    tpcycles = 1
    tppagecycles = 25
    gecko_profile_interval = 1
    filters = filter.ignore_first.prepare(5) + filter.median.prepare()
    unit = "ms"
    lower_is_better = True
    fnbpaint = True


@register_test()
class twinopen(PageloaderTest):
    """
    Tests the amount of time it takes an open browser to open a new browser
    window and paint the browser chrome. This test does not include startup
    time. Multiple test windows are opened in succession.
    (Measures ctrl-n performance.)
    """

    extensions = ["${talos}/pageloader", "${talos}/tests/twinopen"]
    tpmanifest = "${talos}/tests/twinopen/twinopen.manifest"
    tppagecycles = 20
    timeout = 300
    gecko_profile_interval = 1
    tpmozafterpaint = True
    filters = filter.ignore_first.prepare(5) + filter.median.prepare()
    unit = "ms"
    preferences = {"browser.startup.homepage": "about:blank"}


@register_test()
class pdfpaint(PageloaderTest):
    """Time first-page rendering with worker rendering enabled."""

    alert_threshold = 6.0
    tpmanifest = "${talos}/tests/pdfpaint/pdfpaint.manifest"
    tppagecycles = 15
    timeout = 2000
    tptimeout = 60000
    pdfpaint = True
    unit = "ms"
    subtest_alerts = True
    preferences = {
        "pdfjs.disableWorkerRendering": False,
        "pdfjs.enableHWA": True,
    }


@register_test()
class pdfpaint_main_hwa(pdfpaint):
    """Time first-page rendering on the main thread with canvas HWA enabled."""

    preferences = {
        "pdfjs.disableWorkerRendering": True,
        "pdfjs.enableHWA": True,
    }


@register_test()
class pdfpaint_main_sw(pdfpaint):
    """Time first-page rendering on the main thread with a software canvas."""

    preferences = {
        "pdfjs.disableWorkerRendering": True,
        "pdfjs.enableHWA": False,
    }


@register_test()
class cpstartup(PageloaderTest):
    """
    Tests the amount of time it takes to start up a new content process and
    initialize it to the point where it can start processing incoming URLs
    to load.
    """

    extensions = ["${talos}/pageloader", "${talos}/tests/cpstartup/extension"]
    tpmanifest = "${talos}/tests/cpstartup/cpstartup.manifest"
    tppagecycles = 20
    timeout = 600
    tploadnocache = True
    unit = "ms"
    preferences = {
        # By default, Talos is configured to open links from
        # content in new windows. We're overriding them so that
        # they open in new tabs instead.
        # See http://kb.mozillazine.org/Browser.link.open_newwindow
        # and http://kb.mozillazine.org/Browser.link.open_newwindow.restriction
        "browser.link.open_newwindow": 3,
        "browser.link.open_newwindow.restriction": 2,
    }


@register_test()
class tabpaint(PageloaderTest):
    """
    Tests the amount of time it takes to open new tabs, triggered from
    both the parent process and the content process.
    """

    extensions = ["${talos}/tests/tabpaint", "${talos}/pageloader"]
    tpmanifest = "${talos}/tests/tabpaint/tabpaint.manifest"
    tppagecycles = 20
    timeout = 600
    tploadnocache = True
    unit = "ms"
    preferences = {
        # By default, Talos is configured to open links from
        # content in new windows. We're overriding them so that
        # they open in new tabs instead.
        # See http://kb.mozillazine.org/Browser.link.open_newwindow
        # and http://kb.mozillazine.org/Browser.link.open_newwindow.restriction
        "browser.link.open_newwindow": 3,
        "browser.link.open_newwindow.restriction": 2,
        "browser.newtab.preload": False,
    }


@register_test()
class tabswitch(PageloaderTest):
    """
    Tests the amount of time it takes to switch between tabs
    """

    extensions = ["${talos}/tests/tabswitch", "${talos}/pageloader"]
    tpmanifest = "${talos}/tests/tabswitch/tabswitch.manifest"
    tppagecycles = 5
    timeout = 1200
    tploadnocache = True
    preferences = {
        "addon.test.tabswitch.urlfile": os.path.join("${talos}", "tests", "tp5o.html"),
        "addon.test.tabswitch.webserver": "${webserver}",
        "addon.test.tabswitch.maxurls": -1,
    }
    unit = "ms"


@register_test()
class cross_origin_pageload(PageloaderTest):
    """
    Tests the amount of time it takes to load a page which
    has 20 cross origin iframes
    """

    preferences = {"dom.ipc.processPrelaunch.fission.number": 30}
    extensions = ["${talos}/pageloader"]
    tpmanifest = "${talos}/tests/cross_origin_pageload/cross_origin_pageload.manifest"
    tppagecycles = 10
    timeout = 100
    tploadnocache = True
    unit = "ms"


@register_test()
class tart(PageloaderTest):
    """
    Tab Animation Regression Test
    Tests tab animation on these cases:
    1. Simple: single new tab of about:blank open/close without affecting
       (shrinking/expanding) other tabs.
    2. icon: same as above with favicons and long title instead of about:blank.
    3. Newtab: newtab open with thumbnails preview - without affecting other
       tabs, with and without preload.
    4. Fade: opens a tab, then measures fadeout/fadein (tab animation without
       the overhead of opening/closing a tab).
    - Case 1 is tested with DPI scaling of 1.
    - Case 2 is tested with DPI scaling of 1.0 and 2.0.
    - Case 3 is tested with the default scaling of the test system.
    - Case 4 is tested with DPI scaling of 2.0 with the "icon" tab
      (favicon and long title).
    - Each animation produces 3 test results:
      - error: difference between the designated duration and the actual
        completion duration from the trigger.
      - half: average interval over the 2nd half of the animation.
      - all: average interval over all recorded intervals.
    """

    tpmanifest = "${talos}/tests/tart/tart.manifest"
    extensions = ["${talos}/pageloader", "${talos}/tests/tart/addon"]
    tpcycles = 1
    tppagecycles = 25
    tploadnocache = True
    tpmozafterpaint = False
    gecko_profile_interval = 10
    win_counters = linux_counters = mac_counters = None
    """
    ASAP mode
    The recording API is broken with OMTC before ~2013-11-27
    After ~2013-11-27, disabling OMTC will also implicitly disable
    OGL HW composition to disable OMTC with older firefox builds, also
    set 'layers.offmainthreadcomposition.enabled': False
    """
    preferences = {
        "layout.frame_rate": 0,
        "docshell.event_starvation_delay_hint": 1,
        "dom.send_after_paint_to_content": False,
    }
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"
    pine = False


@register_test()
class damp(PageloaderTest):
    """
    Devtools At Maximum Performance
    Tests the speed of DevTools toolbox open, close, and page reload
    for each tool, across a very simple and very complicated page.
    """

    tpmanifest = "${talos}/tests/devtools/damp.manifest"
    extensions = ["${talos}/pageloader", "${talos}/tests/devtools/addon"]
    cycles = 5
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    gecko_profile_interval = 10
    gecko_profile_extra_threads = "DOM Worker"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    preferences = {"devtools.memory.enabled": True}
    unit = "ms"
    subtest_alerts = True
    perfherder_framework = "devtools"


@register_test()
class glterrain(PageloaderTest):
    """
    Simple rotating WebGL scene with moving light source over a
    textured terrain.
    Measures average frame intervals.
    The same sequence is measured 4 times for combinations of alpha and
    antialias as canvas properties.
    Each of these 4 runs is reported as a different test name.
    """

    tpmanifest = "${talos}/tests/webgl/glterrain.manifest"
    tpcycles = 1
    tppagecycles = 25
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 10
    win_counters = linux_counters = mac_counters = None
    """ ASAP mode """
    preferences = {
        "layout.frame_rate": 0,
        "docshell.event_starvation_delay_hint": 1,
        "dom.send_after_paint_to_content": False,
    }
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "frame interval"


@register_test()
class glvideo(PageloaderTest):
    """
    WebGL video texture update with 1080p video.
    Measures mean tick time across 100 ticks.
    (each tick is texImage2D(<video>)+setTimeout(0))
    """

    tpmanifest = "${talos}/tests/webgl/glvideo.manifest"
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class canvas2dvideo(PageloaderTest):
    """
    Canvas2D video texture update with 1080p video.
    Measures mean tick time across 100 ticks.
    (each tick is drawImage(<video>)+setTimeout(0))
    """

    tpmanifest = "${talos}/tests/canvas2d/canvas2dvideo.manifest"
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class offscreencanvas_webcodecs_main_webgl_h264(PageloaderTest):
    """
    OffscreenCanvas WebGL video texture update on the main thread with WebCodecs and 1080p H264 video.
    Measures mean frame time across 100 frames.
    (decodes each frame and performs texImage2D(<videoFrame>))
    """

    tpmanifest = "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_webgl_h264.manifest"
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class offscreencanvas_webcodecs_main_webgl_vp9(PageloaderTest):
    """
    OffscreenCanvas WebGL video texture update on the main thread with WebCodecs and 1080p VP9 video.
    Measures mean frame time across 100 frames.
    (decodes each frame and performs texImage2D(<videoFrame>))
    """

    tpmanifest = "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_webgl_vp9.manifest"
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class offscreencanvas_webcodecs_main_webgl_av1(PageloaderTest):
    """
    OffscreenCanvas WebGL video texture update on the main thread with WebCodecs and 1080p AV1 video.
    Measures mean frame time across 100 frames.
    (decodes each frame and performs texImage2D(<videoFrame>))
    """

    tpmanifest = "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_webgl_av1.manifest"
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class offscreencanvas_webcodecs_worker_webgl_h264(PageloaderTest):
    """
    OffscreenCanvas WebGL video texture update on a DOM worker thread with WebCodecs and 1080p H264 video.
    Measures mean frame time across 100 frames.
    (decodes each frame and performs texImage2D(<videoFrame>))
    """

    tpmanifest = "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_webgl_h264.manifest"
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "DOM Worker,CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class offscreencanvas_webcodecs_worker_webgl_vp9(PageloaderTest):
    """
    OffscreenCanvas WebGL video texture update on a DOM worker thread with WebCodecs and 1080p VP9 video.
    Measures mean frame time across 100 frames.
    (decodes each frame and performs texImage2D(<videoFrame>))
    """

    tpmanifest = "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_webgl_vp9.manifest"
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "DOM Worker,CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class offscreencanvas_webcodecs_worker_webgl_av1(PageloaderTest):
    """
    OffscreenCanvas WebGL video texture update on a DOM worker thread with WebCodecs and 1080p AV1 video.
    Measures mean frame time across 100 frames.
    (decodes each frame and performs texImage2D(<videoFrame>))
    """

    tpmanifest = "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_webgl_av1.manifest"
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "DOM Worker,CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class offscreencanvas_webcodecs_main_2d_h264(PageloaderTest):
    """
    OffscreenCanvas 2D video texture update on the main thread with WebCodecs and 1080p H264 video.
    Measures mean frame time across 100 frames.
    (decodes each frame and performs drawImage(<videoFrame>))
    """

    tpmanifest = (
        "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_2d_h264.manifest"
    )
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class offscreencanvas_webcodecs_main_2d_vp9(PageloaderTest):
    """
    OffscreenCanvas 2D video texture update on the main thread with WebCodecs and 1080p VP9 video.
    Measures mean frame time across 100 frames.
    (decodes each frame and performs drawImage(<videoFrame>))
    """

    tpmanifest = (
        "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_2d_vp9.manifest"
    )
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class offscreencanvas_webcodecs_main_2d_av1(PageloaderTest):
    """
    OffscreenCanvas 2D video texture update on the main thread with WebCodecs and 1080p AV1 video.
    Measures mean frame time across 100 frames.
    (decodes each frame and performs drawImage(<videoFrame>))
    """

    tpmanifest = (
        "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_2d_av1.manifest"
    )
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class offscreencanvas_webcodecs_worker_2d_h264(PageloaderTest):
    """
    OffscreenCanvas 2D video texture update on a DOM worker thread with WebCodecs and 1080p H264 video.
    Measures mean frame time across 100 frames.
    (decodes each frame and performs drawImage(<videoFrame>))
    """

    tpmanifest = "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_2d_h264.manifest"
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "DOM Worker,CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class offscreencanvas_webcodecs_worker_2d_vp9(PageloaderTest):
    """
    OffscreenCanvas 2D video texture update on a DOM worker thread with WebCodecs and 1080p VP9 video.
    Measures mean frame time across 100 frames.
    (decodes each frame and performs drawImage(<videoFrame>))
    """

    tpmanifest = "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_2d_vp9.manifest"
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "DOM Worker,CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class offscreencanvas_webcodecs_worker_2d_av1(PageloaderTest):
    """
    OffscreenCanvas 2D video texture update on a DOM worker thread with WebCodecs and 1080p AV1 video.
    Measures mean frame time across 100 frames.
    (decodes each frame and performs drawImage(<videoFrame>))
    """

    tpmanifest = "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_2d_av1.manifest"
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    gecko_profile_extra_threads = "DOM Worker,CanvasRenderer,MediaSupervisor"
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms"


@register_test()
class tp5n(PageloaderTest):
    """
    Tests the time it takes Firefox to load the tp5 web page test set.

    The tp5 is an updated web page test set to 100 pages from April 8th, 2011.
    Effort was made for the pages to no longer be splash screens/login
    pages/home pages but to be pages that better reflect the actual content
    of the site in question.
    """

    resolution = 20
    multidomain = True
    tpmanifest = "${talos}/fis/tp5n/tp5n.manifest"
    tpcycles = 1
    tppagecycles = 1
    cycles = 1
    tpmozafterpaint = True
    tptimeout = 10000
    mainthread = True
    win_counters = []
    linux_counters = []
    mac_counters = []
    xperf_counters = [
        "main_startup_fileio",
        "main_startup_netio",
        "main_normal_fileio",
        "main_normal_netio",
        "nonmain_startup_fileio",
        "nonmain_normal_fileio",
        "nonmain_normal_netio",
        "mainthread_readcount",
        "mainthread_readbytes",
        "mainthread_writecount",
        "mainthread_writebytes",
        "time_to_session_store_window_restored_ms",
    ]
    xperf_providers = [
        "PROC_THREAD",
        "LOADER",
        "HARD_FAULTS",
        "FILENAME",
        "FILE_IO",
        "FILE_IO_INIT",
    ]
    xperf_user_providers = ["Mozilla Generic Provider", "Microsoft-Windows-TCPIP"]
    xperf_stackwalk = ["FileCreate", "FileRead", "FileWrite", "FileFlush", "FileClose"]
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    timeout = 1800
    setup = "${talos}/xtalos/start_xperf.py -c ${talos}/bcontroller.json"
    cleanup = "${talos}/xtalos/parse_xperf.py -c ${talos}/bcontroller.json"
    preferences = {
        "extensions.enabledScopes": 5,
        "talos.logfile": "browser_output.txt",
    }
    unit = "ms"


@register_test()
class tp5o(PageloaderTest):
    """
    Derived from the tp5n pageset, this is the 49 most reliable webpages.
    """

    tpcycles = 1
    tppagecycles = 25
    cycles = 1
    tpmozafterpaint = True
    tptimeout = 5000
    mainthread = False
    multidomain = True
    tpmanifest = "${talos}/fis/tp5n/tp5o.manifest"
    win_counters = ["% Processor Time"]
    linux_counters = ["XRes"]
    mac_counters = []
    gecko_profile_interval = 2
    filters = filter.ignore_first.prepare(5) + filter.median.prepare()
    timeout = 1800
    unit = "ms"


@register_test()
class tp5o_webext(tp5o):
    webextensions = "${talos}/webextensions/dummy/dummy.xpi"
    preferences = {"xpinstall.signatures.required": False}


@register_test()
class tp5o_scroll(PageloaderTest):
    """
    Tests scroll (like tscrollx does, including ASAP) but on the tp5o pageset.
    """

    tpmanifest = "${talos}/tests/tp5n/tp5o.manifest"
    tpcycles = 1
    tppagecycles = 12
    gecko_profile_interval = 2
    tpscrolltest = True
    """ASAP mode"""
    tpmozafterpaint = False
    preferences = {
        "layout.frame_rate": 0,
        "docshell.event_starvation_delay_hint": 1,
        "dom.send_after_paint_to_content": True,
        "apz.paint_skipping.enabled": False,
        "layout.css.scroll-snap.spring-constant": "'10'",
        "layout.css.scroll-behavior.same-physics-as-user-input": False,
        "toolkit.framesRecording.bufferSize": 10000,
    }
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "1/FPS"


@register_test()
class tp5o_scroll_paint_skip(tp5o_scroll):
    """
    Tests scroll with paint skip (like tscrollx does, including ASAP) but on the tp5o pageset.
    """

    preferences = {
        "layout.frame_rate": 0,
        "docshell.event_starvation_delay_hint": 1,
        "dom.send_after_paint_to_content": True,
        "apz.paint_skipping.enabled": True,
        "layout.css.scroll-snap.spring-constant": "'10'",
        "layout.css.scroll-behavior.same-physics-as-user-input": False,
        "toolkit.framesRecording.bufferSize": 10000,
    }


@register_test()
class v8_7(PageloaderTest):
    """
    This is the V8 (version 7) javascript benchmark taken verbatim and
    slightly modified to fit into our pageloader extension and talos harness.

    The previous version of this test is V8 version 5 which was run on
    selective branches and operating systems.
    """

    tpmanifest = "${talos}/tests/v8_7/v8.manifest"
    gecko_profile_interval = 1
    tpcycles = 1
    resolution = 20
    tpmozafterpaint = False
    preferences = {"dom.send_after_paint_to_content": False}
    filters = filter.v8_subtest.prepare()
    unit = "score"
    lower_is_better = False


@register_test()
class kraken(PageloaderTest):
    """
    This is the Kraken javascript benchmark taken verbatim and slightly
    modified to fit into our pageloader extension and talos harness.
    """

    tpmanifest = "${talos}/tests/kraken/kraken.manifest"
    tpcycles = 1
    tppagecycles = 1
    gecko_profile_interval = 1
    tpmozafterpaint = False
    tpchrome = False
    preferences = {"dom.send_after_paint_to_content": False}
    filters = filter.mean.prepare()
    unit = "score"


@register_test()
class basic_compositor_video(PageloaderTest):
    """
    Video test
    """

    tpmanifest = "${talos}/tests/video/video.manifest"
    tpcycles = 1
    tppagecycles = 12
    tpchrome = False
    timeout = 10000
    gecko_profile_interval = 1
    preferences = {
        "full-screen-api.allow-trusted-requests-only": False,
        "layers.acceleration.force-enabled": False,
        "layers.acceleration.disabled": True,
        "gfx.webrender.software": True,
        "layout.frame_rate": 0,
        "docshell.event_starvation_delay_hint": 1,
        "full-screen-api.warning.timeout": 500,
        "media.ruin-av-sync.enabled": True,
    }
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    unit = "ms/frame"
    lower_is_better = True


class dromaeo(PageloaderTest):
    """abstract base class for dramaeo tests"""

    filters = filter.dromaeo.prepare()
    lower_is_better = False
    alert_threshold = 5.0
    tpchrome = False


@register_test()
class dromaeo_css(dromaeo):
    """
    Dromaeo suite of tests for JavaScript performance testing.
    See the Dromaeo wiki (https://wiki.mozilla.org/Dromaeo)
    for more information.

    Each page in the manifest is part of the dromaemo css benchmark.
    """

    gecko_profile_interval = 2
    tpmanifest = "${talos}/tests/dromaeo/css.manifest"
    unit = "score"


@register_test()
class dromaeo_dom(dromaeo):
    """
    Dromaeo suite of tests for JavaScript performance testing.
    See the Dromaeo wiki (https://wiki.mozilla.org/Dromaeo)
    for more information.

    Each page in the manifest is part of the dromaemo dom benchmark.
    """

    gecko_profile_interval = 2
    tpmanifest = "${talos}/tests/dromaeo/dom.manifest"
    unit = "score"


@register_test()
class tresize(PageloaderTest):
    """
    This test does some resize thing.
    """

    tpmanifest = "${talos}/tests/tresize/tresize.manifest"
    extensions = ["${talos}/pageloader", "${talos}/tests/tresize/addon"]
    tppagecycles = 20
    timeout = 900
    gecko_profile_interval = 2
    tpmozafterpaint = True
    filters = filter.ignore_first.prepare(5) + filter.median.prepare()
    unit = "ms"


@register_test()
class tsvgm(PageloaderTest):
    """
    An svg-only number that measures SVG rendering performance
    for dynamic content only.
    """

    tpmanifest = "${talos}/tests/svgx/svgm.manifest"
    tpcycles = 1
    tppagecycles = 7
    tpmozafterpaint = False
    tpchrome = False
    gecko_profile_interval = 10
    """ASAP mode"""
    preferences = {
        "layout.frame_rate": 0,
        "docshell.event_starvation_delay_hint": 1,
        "dom.send_after_paint_to_content": False,
    }
    filters = filter.ignore_first.prepare(2) + filter.median.prepare()
    unit = "ms"


@register_test()
class tsvgx(PageloaderTest):
    """
    An svg-only number that measures SVG rendering performance
    for dynamic content only.
    """

    tpmanifest = "${talos}/tests/svgx/svgx.manifest"
    tpcycles = 1
    tppagecycles = 25
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 10
    """ASAP mode"""
    preferences = {
        "layout.frame_rate": 0,
        "docshell.event_starvation_delay_hint": 1,
        "dom.send_after_paint_to_content": False,
        "places.history.enabled": False,
    }
    filters = filter.ignore_first.prepare(5) + filter.median.prepare()
    unit = "ms"


@register_test()
class tsvg_static(PageloaderTest):
    """
    An svg-only number that measures SVG rendering performance
    for static content only.
    """

    tpmanifest = "${talos}/tests/svg_static/svg_static.manifest"
    tpcycles = 1
    tppagecycles = 25
    tpmozafterpaint = True
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 1
    filters = filter.ignore_first.prepare(5) + filter.median.prepare()
    unit = "ms"


@register_test()
class tsvgr_opacity(PageloaderTest):
    """
    An svg-only number that measures SVG rendering performance.
    """

    tpmanifest = "${talos}/tests/svg_opacity/svg_opacity.manifest"
    tpcycles = 1
    tppagecycles = 25
    tpmozafterpaint = True
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 1
    filters = filter.ignore_first.prepare(5) + filter.median.prepare()
    unit = "ms"


@register_test()
class tscrollx(PageloaderTest):
    """
    This test does some scrolly thing.
    """

    tpmanifest = "${talos}/tests/scroll/scroll.manifest"
    tpcycles = 1
    tppagecycles = 25
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 1
    """ ASAP mode """
    preferences = {
        "layout.frame_rate": 0,
        "docshell.event_starvation_delay_hint": 1,
        "dom.send_after_paint_to_content": True,
        "apz.paint_skipping.enabled": False,
        "layout.css.scroll-snap.spring-constant": "'10'",
        "layout.css.scroll-behavior.same-physics-as-user-input": False,
        "toolkit.framesRecording.bufferSize": 10000,
    }
    filters = filter.ignore_first.prepare(5) + filter.median.prepare()
    unit = "ms"
    pine = False


@register_test()
class tscrollx_paint_skip(tscrollx):
    """
    This test does some scrolly thing with paint skip.
    """

    preferences = {
        "layout.frame_rate": 0,
        "docshell.event_starvation_delay_hint": 1,
        "dom.send_after_paint_to_content": True,
        "apz.paint_skipping.enabled": True,
        "layout.css.scroll-snap.spring-constant": "'10'",
        "layout.css.scroll-behavior.same-physics-as-user-input": False,
        "toolkit.framesRecording.bufferSize": 10000,
    }


@register_test()
class a11yr(PageloaderTest):
    """
    This test ensures basic a11y tables and permutations do not cause
    performance regressions.
    """

    tpmanifest = "${talos}/tests/a11y/a11y.manifest"
    tpcycles = 1
    tppagecycles = 25
    tpmozafterpaint = True
    tpchrome = False
    timeout = 600
    preferences = {"dom.send_after_paint_to_content": False}
    unit = "ms"
    alert_threshold = 5.0
    a11y = True


class WebkitBenchmark(PageloaderTest):
    tpcycles = 1
    tppagecycles = 5
    tpmozafterpaint = False
    tpchrome = False
    format_pagename = False
    lower_is_better = False
    unit = "score"


@register_test()
class stylebench(WebkitBenchmark):
    # StyleBench benchmark used by many browser vendors (from webkit)
    tpmanifest = "${talos}/tests/stylebench/stylebench.manifest"


@register_test()
class motionmark_animometer(WebkitBenchmark):
    # MotionMark benchmark used by many browser vendors (from webkit)
    tpmanifest = "${talos}/tests/motionmark/animometer.manifest"


@register_test()
class motionmark_webgl(WebkitBenchmark):
    # MotionMark benchmark used by many browser vendors (from webkit)
    tpmanifest = "${talos}/tests/motionmark/webgl.manifest"
    unit = "fps"
    timeout = 600


@register_test()
class ARES6(WebkitBenchmark):
    # ARES-6 benchmark used by many browser vendors (from webkit)
    tpmanifest = "${talos}/tests/ares6/ares6.manifest"
    tppagecycles = 1
    lower_is_better = True


@register_test()
class motionmark_htmlsuite(WebkitBenchmark):
    # MotionMark benchmark used by many browser vendors (from webkit)
    tpmanifest = "${talos}/tests/motionmark/htmlsuite.manifest"


@register_test()
class JetStream(WebkitBenchmark):
    # JetStream benchmark used by many browser vendors (from webkit)
    tpmanifest = "${talos}/tests/jetstream/jetstream.manifest"
    tppagecycles = 1


@register_test()
class perf_reftest(PageloaderTest):
    """
    Style perf-reftest a set of tests where the result is the difference of base vs ref pages
    """

    base_vs_ref = (
        True  # compare the two test pages with eachother and report comparison
    )
    tpmanifest = "${talos}/tests/perf-reftest/perf_reftest.manifest"
    tpcycles = 1
    tppagecycles = 10
    tptimeout = 30000
    gecko_profile_interval = 1
    filters = filter.ignore_first.prepare(5) + filter.median.prepare()
    unit = "ms"
    lower_is_better = True
    alert_threshold = 5.0
    subtest_alerts = True


@register_test()
class perf_reftest_singletons(PageloaderTest):
    """
    Style perf-reftests run as individual tests
    """

    tpmanifest = (
        "${talos}/tests/perf-reftest-singletons/perf_reftest_singletons.manifest"
    )
    tpcycles = 1
    tppagecycles = 15
    tptimeout = 30000
    gecko_profile_interval = 1
    filters = filter.ignore_first.prepare(5) + filter.median.prepare()
    unit = "ms"
    lower_is_better = True
    alert_threshold = 5.0
    subtest_alerts = True
    suite_should_alert = False


@register_test()
class displaylist_mutate(PageloaderTest):
    """
    Test modifying single items in a large display list. Measure transaction speed
    to the compositor.
    """

    tpmanifest = "${talos}/tests/layout/displaylist_mutate.manifest"
    tpcycles = 1
    tppagecycles = 5
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    """ASAP mode"""
    preferences = {
        "layout.frame_rate": 0,
        "docshell.event_starvation_delay_hint": 1,
        "dom.send_after_paint_to_content": False,
    }
    unit = "ms"


@register_test()
class rasterflood_svg(PageloaderTest):
    """
    Test modifying single items in a large display list. Measure transaction speed
    to the compositor.
    """

    tpmanifest = "${talos}/tests/gfx/rasterflood_svg.manifest"
    tpcycles = 1
    tppagecycles = 10
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    """ASAP mode"""
    preferences = {
        "layout.frame_rate": 0,
        "docshell.event_starvation_delay_hint": 1,
        "dom.send_after_paint_to_content": False,
    }
    unit = "ms"


@register_test()
class rasterflood_gradient(PageloaderTest):
    """
    Test expensive rasterization while the main thread is busy.
    """

    tpmanifest = "${talos}/tests/gfx/rasterflood_gradient.manifest"
    tpcycles = 1
    tppagecycles = 10
    tploadnocache = True
    tpmozafterpaint = False
    tpchrome = False
    timeout = 600
    gecko_profile_interval = 2
    win_counters = linux_counters = mac_counters = None
    filters = filter.ignore_first.prepare(1) + filter.median.prepare()
    """ASAP mode"""
    preferences = {
        "layout.frame_rate": 0,
        "docshell.event_starvation_delay_hint": 1,
        "dom.send_after_paint_to_content": False,
    }
    lower_is_better = False
    unit = "score"


@register_test()
class about_preferences_basic(PageloaderTest):
    """
    Base class for about_preferences test
    """

    tpmanifest = "${talos}/tests/about-preferences/about_preferences_basic.manifest"
    # this test uses 'about:blank' as a dummy page (see manifest) so that the pages
    # that just change url categories (i.e. about:preferences#search) will get a load event
    # also any of the url category pages cannot have more than one tppagecycle
    tpcycles = 25
    tppagecycles = 1
    gecko_profile_interval = 1
    filters = filter.ignore_first.prepare(5) + filter.median.prepare()
    unit = "ms"
    lower_is_better = True
    fnbpaint = True
    pine = False
