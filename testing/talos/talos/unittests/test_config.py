import copy
import os
import pathlib
from unittest import mock

import conftest
import mozunit
import pytest

from talos.config import (
    DEFAULTS,
    ConfigurationError,
    get_active_tests,
    get_browser_config,
    get_config,
    get_configs,
    get_test,
)
from talos.test import PageloaderTest

ORIGINAL_DEFAULTS = copy.deepcopy(DEFAULTS)


class mock_test(PageloaderTest):
    keys = [
        "tpmanifest",
        "tpcycles",
        "tppagecycles",
        "tprender",
        "tpchrome",
        "tpmozafterpaint",
        "fnbpaint",
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
        "tptimeout",
        "win_counters",
        "linux_counters",
        "mac_counters",
        "tpscrolltest",
        "xperf_counters",
        "timeout",
        "shutdown",
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
        "lower_is_better",
        "alert_threshold",
        "unit",
        "webextensions",
        "profile",
        "tpmozafterpaint",
        "url",
    ]

    tpmozafterpaint = "value"
    firstpaint = "value"
    userready = "value"
    fnbpaint = "value"


class Test_get_active_tests:
    def test_raises_exception_for_undefined_test(self):
        with pytest.raises(ConfigurationError):
            get_active_tests({"activeTests": "undefined_test"})

        with pytest.raises(ConfigurationError):
            get_active_tests({"activeTests": "  undefined_test     "})

        with pytest.raises(ConfigurationError):
            get_active_tests({"activeTests": "undef_test:undef_test2:undef_test3"})


class Test_get_test:
    global_overrides = {
        "tpmozafterpaint": "overriden",
        "firstpaint": "overriden",
        "userready": "overriden",
        "fnbpaint": "overriden",
    }

    config = {"webserver": "test_webserver"}

    def test_doesnt_override_specific_keys_unless_they_are_null(self):
        test_instance = mock_test()
        test_dict = get_test({}, self.global_overrides, [], test_instance)

        assert test_dict["tpmozafterpaint"] == "value"
        assert test_dict["firstpaint"] == "value"
        assert test_dict["userready"] == "value"
        assert test_dict["fnbpaint"] == "value"

        # nulls still get overriden
        test_instance = mock_test(
            tpmozafterpaint=None, firstpaint=None, userready=None, fnbpaint=None
        )
        test_dict = get_test({}, self.global_overrides, [], test_instance)

        assert test_dict["tpmozafterpaint"] == "overriden"
        assert test_dict["firstpaint"] == "overriden"
        assert test_dict["userready"] == "overriden"
        assert test_dict["fnbpaint"] == "overriden"

    @mock.patch("talos.config.open", create=True)
    def test_interpolate_keys(self, mock_open):
        mock_open.return_value = mock.MagicMock(readlines=lambda: [])

        test_instance = mock_test(
            url="${talos}/test_page.html", tpmanifest="${talos}/file.manifest"
        )

        test_dict = get_test(self.config, self.global_overrides, [], test_instance)
        assert test_dict["url"].startswith("http://test_webserver/")
        assert "${talos}" not in test_dict["url"]
        assert "${talos}" not in test_dict["tpmanifest"]

    def test_build_tpmanifest(self, tmpdir):
        manifest_file = tmpdir.join("file.manifest").ensure(file=True)
        test_instance = mock_test(url="test_page.html", tpmanifest=str(manifest_file))

        test_dict = get_test(self.config, self.global_overrides, [], test_instance)
        assert test_dict["tpmanifest"].endswith(".develop")

    def test_add_counters(self):
        test_instance = mock_test(
            linux_counters=None,
            mac_counters=[],
            win_counters=["counter_a"],
            xperf_counters=["counter_a", "counter_extra"],
        )

        counters = ["counter_a", "counter_b", "counter_c"]
        test_dict = get_test(
            self.config, self.global_overrides, counters, test_instance
        )

        assert test_dict["linux_counters"] == counters
        assert test_dict["mac_counters"] == counters
        assert test_dict["win_counters"] == counters
        assert set(test_dict["xperf_counters"]) == set(counters + ["counter_extra"])


class Test_get_browser_config:
    required = (
        "extensions",
        "browser_path",
        "browser_wait",
        "extra_args",
        "buildid",
        "env",
        "init_url",
        "webserver",
    )
    optional = [
        "bcontroller_config",
        "child_process",
        "debug",
        "debugger",
        "debugger_args",
        "develop",
        "e10s",
        "process",
        "framework",
        "repository",
        "sourcestamp",
        "symbols_path",
        "test_timeout",
        "xperf_path",
        "error_filename",
        "no_upload_results",
        "subtests",
        "preferences",
    ]

    def test_that_contains_title(self):
        config_no_optionals = dict.fromkeys(self.required, "")
        config_no_optionals.update(title="is_mandatory")

        browser_config = get_browser_config(config_no_optionals)
        assert browser_config["title"] == "is_mandatory"

    def test_raises_keyerror_for_missing_title(self):
        config_missing_title = dict.fromkeys(self.required, "")

        with pytest.raises(KeyError):
            get_browser_config(config_missing_title)

    def test_raises_keyerror_for_required_keys(self):
        config_missing_required = dict.fromkeys(self.required, "")
        config_missing_required.update(title="is_mandatory")
        del config_missing_required["extensions"]

        with pytest.raises(KeyError):
            get_browser_config(config_missing_required)

    def test_doesnt_raise_on_missing_optionals(self):
        config_missing_optionals = dict.fromkeys(self.required, "")
        config_missing_optionals["title"] = "is_mandatory"

        try:
            get_browser_config(config_missing_optionals)
        except KeyError:
            pytest.fail("Must not raise exception on missing optional")


class Test_get_config:
    @classmethod
    def setup_class(cls):
        cls.argv = "--suite other-e10s --mainthread -e /some/random/path".split()
        cls.argv_unprovided_tests = "-e /some/random/path".split()
        cls.argv_unknown_suite = (
            "--suite random-unknown-suite -e /some/random/path".split()
        )
        cls.argv_overrides_defaults = """
        --suite other-e10s
        --executablePath /some/random/path
        --cycles 20
        --gecko-profile
        --gecko-profile-interval 1000
        --gecko-profile-entries 1000
        --mainthread
        --tpcycles 20
        --mozAfterPaint
        --firstPaint
        --firstNonBlankPaint
        --userReady
        --tppagecycles 20
        """.split()

        cls.argv_ts_paint = "--activeTests ts_paint -e /some/random/path".split()
        cls.argv_ts_paint_webext = (
            "--activeTests ts_paint_webext -e /some/random/path".split()
        )
        cls.argv_ts_paint_heavy = (
            "--activeTests ts_paint_heavy -e /some/random/path".split()
        )
        cls.argv_sessionrestore = (
            "--activeTests sessionrestore -e /some/random/path".split()
        )
        cls.argv_sessionrestore_no_auto_restore = (
            "--activeTests sessionrestore_no_auto_restore -e /some/random/path".split()
        )
        cls.argv_sessionrestore_many_windows = (
            "--activeTests sessionrestore_many_windows -e /some/random/path".split()
        )
        cls.argv_tresize = "--activeTests tresize -e /some/random/path".split()
        cls.argv_cpstartup = "--activeTests cpstartup -e /some/random/path".split()
        cls.argv_tabpaint = "--activeTests tabpaint -e /some/random/path".split()
        cls.argv_tabswitch = "--activeTests tabswitch -e /some/random/path".split()
        cls.argv_tart = "--activeTests tart -e /some/random/path".split()
        cls.argv_damp = "--activeTests damp -e /some/random/path".split()
        cls.argv_glterrain = "--activeTests glterrain -e /some/random/path".split()
        cls.argv_glvideo = "--activeTests glvideo -e /some/random/path".split()
        cls.argv_canvas2dvideo = (
            "--activeTests canvas2dvideo -e /some/random/path".split()
        )
        cls.argv_offscreencanvas_webcodecs_main_webgl_h264 = "--activeTests offscreencanvas_webcodecs_main_webgl_h264 -e /some/random/path".split()
        cls.argv_offscreencanvas_webcodecs_main_webgl_vp9 = "--activeTests offscreencanvas_webcodecs_main_webgl_vp9 -e /some/random/path".split()
        cls.argv_offscreencanvas_webcodecs_main_webgl_av1 = "--activeTests offscreencanvas_webcodecs_main_webgl_av1 -e /some/random/path".split()
        cls.argv_offscreencanvas_webcodecs_worker_webgl_h264 = "--activeTests offscreencanvas_webcodecs_worker_webgl_h264 -e /some/random/path".split()
        cls.argv_offscreencanvas_webcodecs_worker_webgl_vp9 = "--activeTests offscreencanvas_webcodecs_worker_webgl_vp9 -e /some/random/path".split()
        cls.argv_offscreencanvas_webcodecs_worker_webgl_av1 = "--activeTests offscreencanvas_webcodecs_worker_webgl_av1 -e /some/random/path".split()
        cls.argv_offscreencanvas_webcodecs_main_2d_h264 = "--activeTests offscreencanvas_webcodecs_main_2d_h264 -e /some/random/path".split()
        cls.argv_offscreencanvas_webcodecs_main_2d_vp9 = "--activeTests offscreencanvas_webcodecs_main_2d_vp9 -e /some/random/path".split()
        cls.argv_offscreencanvas_webcodecs_main_2d_av1 = "--activeTests offscreencanvas_webcodecs_main_2d_av1 -e /some/random/path".split()
        cls.argv_offscreencanvas_webcodecs_worker_2d_h264 = "--activeTests offscreencanvas_webcodecs_worker_2d_h264 -e /some/random/path".split()
        cls.argv_offscreencanvas_webcodecs_worker_2d_vp9 = "--activeTests offscreencanvas_webcodecs_worker_2d_vp9 -e /some/random/path".split()
        cls.argv_offscreencanvas_webcodecs_worker_2d_av1 = "--activeTests offscreencanvas_webcodecs_worker_2d_av1 -e /some/random/path".split()
        cls.argv_tp5n = "--activeTests tp5n -e /some/random/path".split()
        cls.argv_tp5o = "--activeTests tp5o -e /some/random/path".split()
        cls.argv_tp5o_webext = "--activeTests tp5o_webext -e /some/random/path".split()
        cls.argv_tp5o_scroll = "--activeTests tp5o_scroll -e /some/random/path".split()
        cls.argv_tp5o_scroll_paint_skip = (
            "--activeTests tp5o_scroll_paint_skip -e /some/random/path".split()
        )
        cls.argv_v8_7 = "--activeTests v8_7 -e /some/random/path".split()
        cls.argv_kraken = "--activeTests kraken -e /some/random/path".split()
        cls.argv_basic_compositor_video = (
            "--activeTests basic_compositor_video -e /some/random/path".split()
        )
        cls.argv_dromaeo_css = "--activeTests dromaeo_css -e /some/random/path".split()
        cls.argv_dromaeo_dom = "--activeTests dromaeo_dom -e /some/random/path".split()
        cls.argv_tsvgm = "--activeTests tsvgm -e /some/random/path".split()
        cls.argv_tsvgx = "--activeTests tsvgx -e /some/random/path".split()
        cls.argv_tsvg_static = "--activeTests tsvg_static -e /some/random/path".split()
        cls.argv_tsvgr_opacity = (
            "--activeTests tsvgr_opacity -e /some/random/path".split()
        )
        cls.argv_tscrollx = "--activeTests tscrollx -e /some/random/path".split()
        cls.argv_tscrollx_paint_skip = (
            "--activeTests tscrollx_paint_skip -e /some/random/path".split()
        )
        cls.argv_a11yr = "--activeTests a11yr -e /some/random/path".split()
        cls.argv_perf_reftest = (
            "--activeTests perf_reftest -e /some/random/path".split()
        )
        cls.argv_perf_reftest_singletons = (
            "--activeTests perf_reftest_singletons -e /some/random/path".split()
        )
        cls.argv_pdfpaint = "--activeTests pdfpaint -e /some/random/path".split()

    @classmethod
    def teardown_class(cls):
        conftest.remove_develop_files()

    def test_correctly_overrides_test_valus(self):
        config = get_config(self.argv)
        assert bool(config) is True

        # no null values
        null_keys = [key for key, val in config.items() if val is None]
        assert len(null_keys) == 0

        # expected keys are there
        assert config["browser_path"] == "/some/random/path"
        assert config["suite"] == "other-e10s"
        assert config["mainthread"] is True

        # default values overriden
        config = get_config(self.argv_overrides_defaults)
        assert config["basetest"] == ORIGINAL_DEFAULTS["basetest"]

    def test_config_has_tests(self):
        config = get_config(self.argv)
        assert len(config["tests"]) > 0

    def test_global_variable_isnt_modified(self):
        get_config(self.argv)
        assert ORIGINAL_DEFAULTS == DEFAULTS

    def test_raises_except_if_unprovided_tests_on_cli(self):
        with pytest.raises(ConfigurationError):
            get_config(self.argv_unprovided_tests)

        with pytest.raises(ConfigurationError):
            get_config(self.argv_unknown_suite)

    def test_ts_paint_has_expected_attributes(self):
        config = get_config(self.argv_ts_paint)
        test_config = config["tests"][0]

        assert test_config["name"] == "ts_paint"
        assert test_config["cycles"] == 20
        assert test_config["timeout"] == 150
        assert test_config["gecko_profile_startup"] is True
        assert test_config["gecko_profile_entries"] == 10000000
        assert (
            test_config["url"] != "startup_test/tspaint_test.html"
        )  # interpolation was done
        assert test_config["xperf_counters"] == []
        # TODO: these don't work; is this a bug?
        # assert test_config['win7_counters'] == []
        assert test_config["filters"] is not None
        assert test_config["tpmozafterpaint"] is True
        # assert test_config['mainthread'] is False
        # assert test_config['unit'] == 'ms'

    def test_ts_paint_webext_has_expected_attributes(self):
        config = get_config(self.argv_ts_paint_webext)
        test_config = config["tests"][0]

        assert test_config["name"] == "ts_paint_webext"
        assert test_config["cycles"] == 20
        assert test_config["timeout"] == 150
        assert test_config["gecko_profile_startup"] is True
        assert test_config["gecko_profile_entries"] == 10000000
        assert (
            test_config["url"] != "startup_test/tspaint_test.html"
        )  # interpolation was done
        assert test_config["xperf_counters"] == []
        # TODO: these don't work; is this a bug?
        # assert test_config['win7_counters'] == []
        assert test_config["filters"] is not None
        assert test_config["tpmozafterpaint"] is True
        # assert test_config['mainthread'] is False
        # assert test_config['unit'] == 'ms'
        # TODO: this isn't overriden
        # assert test_config['webextensions'] != '${talos}/webextensions/dummy/dummy-signed.xpi'
        assert test_config["preferences"] == {"xpinstall.signatures.required": False}

    def test_ts_paint_heavy_has_expected_attributes(self):
        config = get_config(self.argv_ts_paint_heavy)
        test_config = config["tests"][0]

        assert test_config["name"] == "ts_paint_heavy"
        assert test_config["cycles"] == 20
        assert test_config["timeout"] == 150
        assert test_config["gecko_profile_startup"] is True
        assert test_config["gecko_profile_entries"] == 10000000
        assert (
            test_config["url"] != "startup_test/tspaint_test.html"
        )  # interpolation was done
        assert test_config["xperf_counters"] == []
        # TODO: this doesn't work; is this a bug?
        # assert test_config['win7_counters'] == []
        assert test_config["filters"] is not None
        assert test_config["tpmozafterpaint"] is True
        # assert test_config['mainthread'] is False
        # assert test_config['unit'] == 'ms'
        assert test_config["profile"] == "simple"

    def test_sessionrestore_has_expected_attributes(self):
        config = get_config(self.argv_sessionrestore)
        test_config = config["tests"][0]

        assert test_config["name"] == "sessionrestore"
        assert test_config["cycles"] == 10
        assert test_config["timeout"] == 900
        assert test_config["gecko_profile_startup"] is True
        assert test_config["gecko_profile_entries"] == 10000000
        assert test_config["reinstall"] == [
            "sessionstore.jsonlz4",
            "sessionstore.js",
            "sessionCheckpoints.json",
        ]
        assert test_config["url"] == "about:home"
        assert test_config["preferences"] == {"browser.startup.page": 3}
        # assert test_config['unit'] == 'ms'

    def test_sesssionrestore_no_auto_restore_has_expected_attributes(self):
        config = get_config(self.argv_sessionrestore_no_auto_restore)
        test_config = config["tests"][0]

        assert test_config["name"] == "sessionrestore_no_auto_restore"
        assert test_config["cycles"] == 10
        assert test_config["timeout"] == 900
        assert test_config["gecko_profile_startup"] is True
        assert test_config["gecko_profile_entries"] == 10000000
        assert test_config["reinstall"] == [
            "sessionstore.jsonlz4",
            "sessionstore.js",
            "sessionCheckpoints.json",
        ]
        assert test_config["url"] == "about:home"
        assert test_config["preferences"] == {"browser.startup.page": 1}
        # assert test_config['unit'] == 'ms'

    def test_sessionrestore_many_windows_has_expected_attributes(self):
        config = get_config(self.argv_sessionrestore_many_windows)
        test_config = config["tests"][0]

        assert test_config["name"] == "sessionrestore_many_windows"
        assert test_config["cycles"] == 10
        assert test_config["timeout"] == 900
        assert test_config["gecko_profile_startup"] is True
        assert test_config["gecko_profile_entries"] == 10000000
        assert test_config["reinstall"] == [
            "sessionstore.jsonlz4",
            "sessionstore.js",
            "sessionCheckpoints.json",
        ]
        assert test_config["url"] == "about:home"
        assert test_config["preferences"] == {"browser.startup.page": 3}
        # assert test_config['unit'] == 'ms'

    def test_tresize_has_expected_attributes(self):
        config = get_config(self.argv_tresize)
        test_config = config["tests"][0]

        assert test_config["name"] == "tresize"
        assert test_config["cycles"] == 20
        assert (
            test_config["url"] != "startup_test/tresize/addon/content/tresize-test.html"
        )
        assert test_config["timeout"] == 150
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 1000000
        assert test_config["tpmozafterpaint"] is True
        assert test_config["filters"] is not None
        # assert test_config['unit'] == 'ms'

    def test_cpstartup_has_expected_attributes(self):
        config = get_config(self.argv_cpstartup)
        test_config = config["tests"][0]

        assert test_config["name"] == "cpstartup"
        assert test_config["tpcycles"] == 1
        assert (
            test_config["tpmanifest"] != "${talos}/tests/cpstartup/cpstartup.manifest"
        )
        assert test_config["tppagecycles"] == 20
        assert test_config["gecko_profile_entries"] == 1000000
        assert test_config["tploadnocache"] is True
        assert test_config["unit"] == "ms"
        assert test_config["preferences"] == {
            "addon.test.cpstartup.webserver": "${webserver}",
            "browser.link.open_newwindow": 3,
            "browser.link.open_newwindow.restriction": 2,
        }

    def test_tabpaint_has_expected_attributes(self):
        config = get_config(self.argv_tabpaint)
        test_config = config["tests"][0]

        assert test_config["name"] == "tabpaint"
        assert test_config["tpcycles"] == 1
        assert test_config["tpmanifest"] != "${talos}/tests/tabpaint/tabpaint.manifest"
        assert test_config["tppagecycles"] == 20
        assert test_config["gecko_profile_entries"] == 1000000
        assert test_config["tploadnocache"] is True
        assert test_config["unit"] == "ms"
        assert test_config["preferences"] == {
            "browser.link.open_newwindow": 3,
            "browser.link.open_newwindow.restriction": 2,
        }

    def test_tabswitch_has_expected_attributes(self):
        config = get_config(self.argv_tabswitch)
        test_config = config["tests"][0]

        assert test_config["name"] == "tabswitch"
        assert test_config["tpcycles"] == 1
        assert (
            test_config["tpmanifest"] != "${talos}/tests/tabswitch/tabswitch.manifest"
        )
        assert test_config["tppagecycles"] == 5
        assert test_config["gecko_profile_entries"] == 5000000
        assert test_config["tploadnocache"] is True
        assert test_config["preferences"] == {
            "addon.test.tabswitch.urlfile": os.path.join(
                "${talos}", "tests", "tp5o.html"
            ),
            "addon.test.tabswitch.webserver": "${webserver}",
            "addon.test.tabswitch.maxurls": -1,
        }
        assert test_config["unit"] == "ms"

    def test_tart_has_expected_attributes(self):
        config = get_config(self.argv_tart)
        test_config = config["tests"][0]

        assert test_config["name"] == "tart"
        assert test_config["tpmanifest"] != "${talos}/tests/tart/tart.manifest"
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 25
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["gecko_profile_interval"] == 10
        assert test_config["gecko_profile_entries"] == 1000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["preferences"] == {
            "layout.frame_rate": 0,
            "docshell.event_starvation_delay_hint": 1,
            "dom.send_after_paint_to_content": False,
        }
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_damp_has_expected_attributes(self):
        config = get_config(self.argv_damp)
        test_config = config["tests"][0]

        assert test_config["name"] == "damp"
        assert test_config["tpmanifest"] != "${talos}/tests/devtools/damp.manifest"
        assert test_config["cycles"] == 5
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["gecko_profile_interval"] == 10
        assert test_config["gecko_profile_entries"] == 1000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["preferences"] == {"devtools.memory.enabled": True}
        assert test_config["unit"] == "ms"

    def test_glterrain_has_expected_attributes(self):
        config = get_config(self.argv_glterrain)
        test_config = config["tests"][0]

        assert test_config["name"] == "glterrain"
        assert test_config["tpmanifest"] != "${talos}/tests/webgl/glterrain.manifest"
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 25
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 10
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["preferences"] == {
            "layout.frame_rate": 0,
            "docshell.event_starvation_delay_hint": 1,
            "dom.send_after_paint_to_content": False,
        }
        assert test_config["filters"] is not None
        assert test_config["unit"] == "frame interval"

    def test_glvideo_has_expected_attributes(self):
        config = get_config(self.argv_glvideo)
        test_config = config["tests"][0]

        assert test_config["name"] == "glvideo"
        assert test_config["tpmanifest"] != "${talos}/tests/webgl/glvideo.manifest"
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_canvas2dvideo_has_expected_attributes(self):
        config = get_config(self.argv_canvas2dvideo)
        test_config = config["tests"][0]

        assert test_config["name"] == "canvas2dvideo"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/canvas2d/canvas2dvideo.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_offscreencanvas_webcodecs_main_webgl_h264_has_expected_attributes(self):
        config = get_config(self.argv_offscreencanvas_webcodecs_main_webgl_h264)
        test_config = config["tests"][0]

        assert test_config["name"] == "offscreencanvas_webcodecs_main_webgl_h264"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_webgl_h264.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_offscreencanvas_webcodecs_main_webgl_vp9_has_expected_attributes(self):
        config = get_config(self.argv_offscreencanvas_webcodecs_main_webgl_vp9)
        test_config = config["tests"][0]

        assert test_config["name"] == "offscreencanvas_webcodecs_main_webgl_vp9"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_webgl_vp9.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_offscreencanvas_webcodecs_main_webgl_av1_has_expected_attributes(self):
        config = get_config(self.argv_offscreencanvas_webcodecs_main_webgl_av1)
        test_config = config["tests"][0]

        assert test_config["name"] == "offscreencanvas_webcodecs_main_webgl_av1"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_webgl_av1.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_offscreencanvas_webcodecs_worker_webgl_h264_has_expected_attributes(self):
        config = get_config(self.argv_offscreencanvas_webcodecs_worker_webgl_h264)
        test_config = config["tests"][0]

        assert test_config["name"] == "offscreencanvas_webcodecs_worker_webgl_h264"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_webgl_h264.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_offscreencanvas_webcodecs_worker_webgl_vp9_has_expected_attributes(self):
        config = get_config(self.argv_offscreencanvas_webcodecs_worker_webgl_vp9)
        test_config = config["tests"][0]

        assert test_config["name"] == "offscreencanvas_webcodecs_worker_webgl_vp9"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_webgl_vp9.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_offscreencanvas_webcodecs_worker_webgl_av1_has_expected_attributes(self):
        config = get_config(self.argv_offscreencanvas_webcodecs_worker_webgl_av1)
        test_config = config["tests"][0]

        assert test_config["name"] == "offscreencanvas_webcodecs_worker_webgl_av1"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_webgl_av1.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_offscreencanvas_webcodecs_main_2d_h264_has_expected_attributes(self):
        config = get_config(self.argv_offscreencanvas_webcodecs_main_2d_h264)
        test_config = config["tests"][0]

        assert test_config["name"] == "offscreencanvas_webcodecs_main_2d_h264"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_2d_h264.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_offscreencanvas_webcodecs_main_2d_vp9_has_expected_attributes(self):
        config = get_config(self.argv_offscreencanvas_webcodecs_main_2d_vp9)
        test_config = config["tests"][0]

        assert test_config["name"] == "offscreencanvas_webcodecs_main_2d_vp9"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_2d_vp9.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_offscreencanvas_webcodecs_main_2d_av1_has_expected_attributes(self):
        config = get_config(self.argv_offscreencanvas_webcodecs_main_2d_av1)
        test_config = config["tests"][0]

        assert test_config["name"] == "offscreencanvas_webcodecs_main_2d_av1"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_2d_av1.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_offscreencanvas_webcodecs_worker_2d_h264_has_expected_attributes(self):
        config = get_config(self.argv_offscreencanvas_webcodecs_worker_2d_h264)
        test_config = config["tests"][0]

        assert test_config["name"] == "offscreencanvas_webcodecs_worker_2d_h264"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_2d_h264.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_offscreencanvas_webcodecs_worker_2d_vp9_has_expected_attributes(self):
        config = get_config(self.argv_offscreencanvas_webcodecs_worker_2d_vp9)
        test_config = config["tests"][0]

        assert test_config["name"] == "offscreencanvas_webcodecs_worker_2d_vp9"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_2d_vp9.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_offscreencanvas_webcodecs_worker_2d_av1_has_expected_attributes(self):
        config = get_config(self.argv_offscreencanvas_webcodecs_worker_2d_av1)
        test_config = config["tests"][0]

        assert test_config["name"] == "offscreencanvas_webcodecs_worker_2d_av1"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_2d_av1.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 5
        assert test_config["tploadnocache"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert "win_counters" not in test_config
        assert "linux_counters" not in test_config
        assert "mac_counters" not in test_config
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    @mock.patch("talos.config.build_manifest", conftest.patched_build_manifest)
    def test_tp5n_has_expected_attributes(self):
        config = get_config(self.argv_tp5n)
        test_config = config["tests"][0]

        assert test_config["name"] == "tp5n"
        assert test_config["resolution"] == 20
        assert test_config["tpmanifest"] != "${talos}/tests/tp5n/tp5n.manifest"
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 1
        assert test_config["cycles"] == 1
        assert test_config["tpmozafterpaint"] is True
        assert test_config["tptimeout"] == 5000
        assert test_config["mainthread"] is True
        assert test_config["win_counters"] == []
        assert test_config["linux_counters"] == []
        assert test_config["mac_counters"] == []
        assert test_config["xperf_counters"] == [
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
        ]
        assert test_config["xperf_providers"] == [
            "PROC_THREAD",
            "LOADER",
            "HARD_FAULTS",
            "FILENAME",
            "FILE_IO",
            "FILE_IO_INIT",
        ]
        assert test_config["xperf_user_providers"] == [
            "Mozilla Generic Provider",
            "Microsoft-Windows-TCPIP",
        ]
        assert test_config["xperf_stackwalk"] == [
            "FileCreate",
            "FileRead",
            "FileWrite",
            "FileFlush",
            "FileClose",
        ]
        assert test_config["filters"] is not None
        assert test_config["timeout"] == 1800
        assert test_config["preferences"] == {
            "extensions.enabledScopes": "",
            "talos.logfile": "browser_output.txt",
        }
        assert test_config["unit"] == "ms"

    @mock.patch("talos.config.build_manifest", conftest.patched_build_manifest)
    def test_tp5o_has_expected_attributes(self):
        config = get_config(self.argv_tp5o)
        test_config = config["tests"][0]

        assert test_config["name"] == "tp5o"
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 25
        assert test_config["cycles"] == 1
        assert test_config["tpmozafterpaint"] is True
        assert test_config["tptimeout"] == 5000
        assert test_config["mainthread"] is False
        assert test_config["tpmanifest"] != "${talos}/tests/tp5n/tp5o.manifest"
        assert test_config["win_counters"] == ["% Processor Time"]
        assert test_config["linux_counters"] == ["XRes"]
        assert test_config["mac_counters"] == []
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 4000000
        assert test_config["filters"] is not None
        assert test_config["timeout"] == 1800
        assert test_config["unit"] == "ms"

    @mock.patch("talos.config.build_manifest", conftest.patched_build_manifest)
    def test_tp5o_webext_has_expected_attributes(self):
        config = get_config(self.argv_tp5o_webext)
        test_config = config["tests"][0]

        assert test_config["name"] == "tp5o_webext"
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 25
        assert test_config["cycles"] == 1
        assert test_config["tpmozafterpaint"] is True
        assert test_config["tptimeout"] == 5000
        assert test_config["mainthread"] is False
        assert test_config["tpmanifest"] != "${talos}/tests/tp5n/tp5o.manifest"
        assert test_config["win_counters"] == ["% Processor Time"]
        assert test_config["linux_counters"] == ["XRes"]
        assert test_config["mac_counters"] == []
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 4000000
        assert test_config["filters"] is not None
        assert test_config["timeout"] == 1800
        assert test_config["unit"] == "ms"
        assert test_config["webextensions"] == "${talos}/webextensions/dummy/dummy.xpi"
        assert test_config["preferences"] == {"xpinstall.signatures.required": False}

    @mock.patch("talos.config.build_manifest", conftest.patched_build_manifest)
    def test_tp5o_scroll_has_expected_attributes(self):
        config = get_config(self.argv_tp5o_scroll)
        test_config = config["tests"][0]

        assert test_config["name"] == "tp5o_scroll"
        assert test_config["tpmanifest"] != "${talos}/tests/tp5n/tp5o.manifest"
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 12
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 2000000
        assert test_config["tpscrolltest"] is True
        assert test_config["tpmozafterpaint"] is False
        assert test_config["preferences"] == {
            "layout.frame_rate": 0,
            "docshell.event_starvation_delay_hint": 1,
            "dom.send_after_paint_to_content": False,
            "layout.css.scroll-snap.spring-constant": "'10'",
            "layout.css.scroll-behavior.same-physics-as-user-input": False,
            "toolkit.framesRecording.bufferSize": 10000,
        }
        assert test_config["filters"] is not None
        assert test_config["unit"] == "1/FPS"

    def test_v8_7_has_expected_attributes(self):
        config = get_config(self.argv_v8_7)
        test_config = config["tests"][0]

        assert test_config["name"] == "v8_7"
        assert test_config["tpmanifest"] != "${talos}/tests/v8_7/v8.manifest"
        assert test_config["gecko_profile_interval"] == 1
        assert test_config["gecko_profile_entries"] == 1000000
        assert test_config["tpcycles"] == 1
        assert test_config["resolution"] == 20
        assert test_config["tpmozafterpaint"] is False
        assert test_config["preferences"] == {"dom.send_after_paint_to_content": False}
        assert test_config["filters"] is not None
        assert test_config["unit"] == "score"
        assert test_config["lower_is_better"] is False

    def test_kraken_has_expected_attributes(self):
        config = get_config(self.argv_kraken)
        test_config = config["tests"][0]

        assert test_config["name"] == "kraken"
        assert test_config["tpmanifest"] != "${talos}/tests/kraken/kraken.manifest"
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 1
        assert test_config["gecko_profile_interval"] == 1
        assert test_config["gecko_profile_entries"] == 5000000
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["preferences"] == {"dom.send_after_paint_to_content": False}
        assert test_config["filters"] is not None
        assert test_config["unit"] == "score"

    def test_basic_compositor_video_has_expected_attributes(self):
        config = get_config(self.argv_basic_compositor_video)
        test_config = config["tests"][0]

        assert test_config["name"] == "basic_compositor_video"
        assert test_config["tpmanifest"] != "${talos}/tests/video/video.manifest"
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 12
        assert test_config["tpchrome"] is False
        assert test_config["timeout"] == 10000
        assert test_config["gecko_profile_interval"] == 1
        assert test_config["gecko_profile_entries"] == 2000000
        assert test_config["preferences"] == {
            "full-screen-api.allow-trusted-requests-only": False,
            "layers.acceleration.force-enabled": False,
            "layers.acceleration.disabled": True,
            "layout.frame_rate": 0,
            "docshell.event_starvation_delay_hint": 1,
            "full-screen-api.warning.timeout": 500,
            "media.ruin-av-sync.enabled": True,
        }
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms/frame"
        assert test_config["lower_is_better"] is True

    def test_dromaeo_css_has_expected_attributes(self):
        config = get_config(self.argv_dromaeo_css)
        test_config = config["tests"][0]

        assert test_config["name"] == "dromaeo_css"
        assert test_config["tpcycles"] == 1
        assert test_config["filters"] is not None
        assert test_config["lower_is_better"] is False
        assert test_config["alert_threshold"] == 5.0
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 10000000
        assert test_config["tpmanifest"] != "${talos}/tests/dromaeo/css.manifest"
        assert test_config["unit"] == "score"

    def test_dromaeo_dom_has_expected_attributes(self):
        config = get_config(self.argv_dromaeo_dom)
        test_config = config["tests"][0]

        assert test_config["name"] == "dromaeo_dom"
        assert test_config["tpcycles"] == 1
        assert test_config["filters"] is not None
        assert test_config["lower_is_better"] is False
        assert test_config["alert_threshold"] == 5.0
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 2
        assert test_config["gecko_profile_entries"] == 10000000
        assert test_config["tpmanifest"] != "${talos}/tests/dromaeo/dom.manifest"
        assert test_config["unit"] == "score"

    def test_tsvgm_has_expected_attributes(self):
        config = get_config(self.argv_tsvgm)
        test_config = config["tests"][0]

        assert test_config["name"] == "tsvgm"
        assert test_config["tpmanifest"] != "${talos}/tests/svgx/svgm.manifest"
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 7
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 10
        assert test_config["gecko_profile_entries"] == 1000000
        assert test_config["preferences"] == {
            "layout.frame_rate": 0,
            "docshell.event_starvation_delay_hint": 1,
            "dom.send_after_paint_to_content": False,
        }
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_tsvgx_has_expected_attributes(self):
        config = get_config(self.argv_tsvgx)
        test_config = config["tests"][0]

        assert test_config["name"] == "tsvgx"
        assert test_config["tpmanifest"] != "${talos}/tests/svgx/svgx.manifest"
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 25
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 10
        assert test_config["gecko_profile_entries"] == 1000000
        assert test_config["preferences"] == {
            "layout.frame_rate": 0,
            "docshell.event_starvation_delay_hint": 1,
            "dom.send_after_paint_to_content": False,
        }
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_tsvg_static_has_expected_attributes(self):
        config = get_config(self.argv_tsvg_static)
        test_config = config["tests"][0]

        assert test_config["name"] == "tsvg_static"
        assert (
            test_config["tpmanifest"] != "${talos}/tests/svg_static/svg_static.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 25
        assert test_config["tpmozafterpaint"] is True
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 1
        assert test_config["gecko_profile_entries"] == 10000000
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_tsvgr_opacity_has_expected_attributes(self):
        config = get_config(self.argv_tsvgr_opacity)
        test_config = config["tests"][0]

        assert test_config["name"] == "tsvgr_opacity"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/svg_opacity/svg_opacity.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 25
        assert test_config["tpmozafterpaint"] is True
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 1
        assert test_config["gecko_profile_entries"] == 10000000
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_tscrollx_has_expected_attributes(self):
        config = get_config(self.argv_tscrollx)
        test_config = config["tests"][0]

        assert test_config["name"] == "tscrollx"
        assert test_config["tpmanifest"] != "${talos}/tests/scroll/scroll.manifest"
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 25
        assert test_config["tpmozafterpaint"] is False
        assert test_config["tpchrome"] is False
        assert test_config["gecko_profile_interval"] == 1
        assert test_config["gecko_profile_entries"] == 1000000
        assert test_config["preferences"] == {
            "layout.frame_rate": 0,
            "docshell.event_starvation_delay_hint": 1,
            "dom.send_after_paint_to_content": False,
            "layout.css.scroll-snap.spring-constant": "'10'",
            "layout.css.scroll-behavior.same-physics-as-user-input": False,
            "toolkit.framesRecording.bufferSize": 10000,
        }
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"

    def test_a11yr_has_expect_attributes(self):
        config = get_config(self.argv_a11yr)
        test_config = config["tests"][0]

        assert test_config["name"] == "a11yr"
        assert test_config["tpmanifest"] != "${talos}/tests/a11y/a11y.manifest"
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 25
        assert test_config["tpmozafterpaint"] is True
        assert test_config["tpchrome"] is False
        assert test_config["preferences"] == {"dom.send_after_paint_to_content": False}
        assert test_config["unit"] == "ms"
        assert test_config["alert_threshold"] == 5.0

    def test_perf_reftest_has_expected_attributes(self):
        config = get_config(self.argv_perf_reftest)
        test_config = config["tests"][0]

        assert test_config["name"] == "perf_reftest"
        assert test_config["base_vs_ref"] is True
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/perf-reftest/perf_reftest.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 10
        assert test_config["tptimeout"] == 30000
        assert test_config["gecko_profile_interval"] == 1
        assert test_config["gecko_profile_entries"] == 2000000
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"
        assert test_config["lower_is_better"] is True
        assert test_config["alert_threshold"] == 5.0

    def test_perf_reftest_singletons_has_expected_attributes(self):
        config = get_config(self.argv_perf_reftest_singletons)
        test_config = config["tests"][0]

        assert test_config["name"] == "perf_reftest_singletons"
        assert (
            test_config["tpmanifest"]
            != "${talos}/tests/perf-reftest-singletons/perf_reftest_singletons.manifest"
        )
        assert test_config["tpcycles"] == 1
        assert test_config["tppagecycles"] == 15
        assert test_config["tptimeout"] == 30000
        assert test_config["gecko_profile_interval"] == 1
        assert test_config["gecko_profile_entries"] == 2000000
        assert test_config["filters"] is not None
        assert test_config["unit"] == "ms"
        assert test_config["lower_is_better"] is True
        assert test_config["alert_threshold"] == 5.0


# The tests in the Test_get_config class don't currently run, so these
# pdfpaint tests live outside of it for now. See bug 1888132.
@mock.patch("pathlib.Path.unlink", new=mock.MagicMock())
@mock.patch("pathlib.Path.symlink_to", new=mock.MagicMock())
def test_pdfpaint_has_expected_attributes_no_chunk(pdfpaint_dir_info):
    pdfpaint_dir, pdf_count = pdfpaint_dir_info

    Test_get_config.setup_class()
    with mock.patch.dict(
        os.environ, {"MOZ_FETCHES_DIR": "", "MOZBUILD_PATH": str(pdfpaint_dir)}
    ):
        config = get_config(Test_get_config.argv_pdfpaint)

    test_config = config["tests"][0]

    assert test_config["name"] == "pdfpaint"
    assert test_config["tpmanifest"] != "${talos}/tests/pdfpaint/pdfpaint.manifest"

    manifest_content = pathlib.Path(test_config["tpmanifest"]).read_text()
    manifest_lines = manifest_content.split("\n")
    assert len([line for line in manifest_lines if line]) == pdf_count

    assert test_config["tpcycles"] == 1
    assert test_config["tppagecycles"] == 15
    assert test_config["tptimeout"] == 60000
    assert test_config["gecko_profile_entries"] == 16777216
    assert test_config["filters"] is not None
    assert test_config["unit"] == "ms"
    assert test_config["lower_is_better"] is True
    assert test_config["alert_threshold"] == 6.0


@mock.patch("pathlib.Path.unlink", new=mock.MagicMock())
@mock.patch("pathlib.Path.symlink_to", new=mock.MagicMock())
def test_pdfpaint_has_expected_attributes_with_chunk(pdfpaint_dir_info):
    pdfpaint_dir, _ = pdfpaint_dir_info

    Test_get_config.setup_class()
    args = Test_get_config.argv_pdfpaint + ["--pdfPaintChunk", "1"]
    with mock.patch.dict(
        os.environ,
        {"MOZ_FETCHES_DIR": str(pdfpaint_dir), "MOZBUILD_PATH": ""},
    ):
        config = get_config(args)

    test_config = config["tests"][0]

    assert test_config["name"] == "pdfpaint"
    assert test_config["tpmanifest"] != "${talos}/tests/pdfpaint/pdfpaint.manifest"

    manifest_content = pathlib.Path(test_config["tpmanifest"]).read_text()
    manifest_lines = manifest_content.split("\n")
    assert len([line for line in manifest_lines if line]) == 100

    assert test_config["tpcycles"] == 1
    assert test_config["tppagecycles"] == 15
    assert test_config["tptimeout"] == 60000
    assert test_config["gecko_profile_entries"] == 16777216
    assert test_config["filters"] is not None
    assert test_config["unit"] == "ms"
    assert test_config["lower_is_better"] is True
    assert test_config["alert_threshold"] == 6.0


def test_pdfpaint_fails_on_bad_chunk(pdfpaint_dir_info):
    pdfpaint_dir, _ = pdfpaint_dir_info

    Test_get_config.setup_class()
    args = Test_get_config.argv_pdfpaint + ["--pdfPaintChunk", "10"]
    with pytest.raises(ConfigurationError):
        with mock.patch.dict(
            os.environ,
            {"MOZ_FETCHES_DIR": str(pdfpaint_dir), "MOZBUILD_PATH": ""},
        ):
            get_config(args)


@mock.patch("pathlib.Path.unlink", new=mock.MagicMock())
@mock.patch("pathlib.Path.symlink_to", new=mock.MagicMock())
def test_pdfpaint_with_pdf_name(pdfpaint_dir_info):
    pdfpaint_dir, _ = pdfpaint_dir_info

    Test_get_config.setup_class()
    args = Test_get_config.argv_pdfpaint + ["--pdfPaintName", "1"]
    with mock.patch.dict(
        os.environ,
        {"MOZ_FETCHES_DIR": str(pdfpaint_dir), "MOZBUILD_PATH": ""},
    ):
        config = get_config(args)

    test_config = config["tests"][0]

    assert test_config["name"] == "pdfpaint"
    assert test_config["tpmanifest"] != "${talos}/tests/pdfpaint/pdfpaint.manifest"

    manifest_content = pathlib.Path(test_config["tpmanifest"]).read_text()
    manifest_lines = manifest_content.split("\n")
    assert len([line for line in manifest_lines if line]) == 1
    assert manifest_lines[0].split("/")[-1] == "1"

    assert test_config["tpcycles"] == 1
    assert test_config["tppagecycles"] == 15


@mock.patch("talos.config.get_browser_config")
@mock.patch("talos.config.get_config")
def test_get_configs(get_config_mock, get_browser_config_mock):
    # unpacks in right order
    get_config_mock.return_value = "first"
    get_browser_config_mock.return_value = "second"

    first, second = get_configs()
    assert (first, second) == ("first", "second")


if __name__ == "__main__":
    mozunit.main()
