# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import base64
import hashlib
import struct
import sys
import tempfile
import unittest

from unittest import mock
from urllib.parse import quote

import mozinfo

from marionette_driver import By
from marionette_driver.errors import InvalidArgumentException, NoSuchWindowException
from marionette_harness import (
    MarionetteTestCase,
    skip,
    WindowManagerMixin,
)


def inline(doc, mime="text/html;charset=utf-8"):
    return "data:{0},{1}".format(mime, quote(doc))


box = inline(
    "<body><div id='box'><p id='green' style='width: 50px; height: 50px; "
    "background: silver;'></p></div></body>"
)
input = inline("<body><input id='text-input'></input></body>")
long = inline("<body style='height: 300vh'><p style='margin-top: 100vh'>foo</p></body>")
short = inline("<body style='height: 10vh'></body>")
partially_visible = inline(
    "<body style='height: 300vh'>"
    "<div id='abs' style='position:absolute;margin-top:200px;width:20px;height:100vh;background:green'></div>"
    "</body>"
)
svg = inline(
    """
    <svg xmlns="http://www.w3.org/2000/svg" height="20" width="20">
      <rect height="20" width="20"/>
    </svg>""",
    mime="image/svg+xml",
)


class ScreenCaptureTestCase(MarionetteTestCase):
    def setUp(self):
        super(ScreenCaptureTestCase, self).setUp()

        self.maxDiff = None

        self._device_pixel_ratio = None

        # Ensure that each screenshot test runs on a blank page to avoid left
        # over elements or focus which could interfer with taking screenshots
        self.marionette.navigate("about:blank")

    @property
    def device_pixel_ratio(self):
        if self._device_pixel_ratio is None:
            self._device_pixel_ratio = self.marionette.execute_script(
                """
                return window.devicePixelRatio
                """
            )
        return self._device_pixel_ratio

    @property
    def document_element(self):
        return self.marionette.find_element(By.CSS_SELECTOR, ":root")

    @property
    def page_y_offset(self):
        return self.marionette.execute_script("return window.pageYOffset")

    @property
    def viewport_dimensions(self):
        return self.marionette.execute_script(
            "return [window.innerWidth, window.innerHeight];"
        )

    def assert_png(self, screenshot):
        """Test that screenshot is a Base64 encoded PNG file."""
        if not isinstance(screenshot, bytes):
            screenshot = bytes(screenshot, encoding="utf-8")
            image = base64.decodebytes(screenshot)
        else:
            if screenshot.startswith(b"\211PNG\r\n\032\n"):
                image = screenshot
            else:
                image = base64.decodebytes(screenshot)
        self.assertRegex(image, b"\211PNG\r\n\032\n", "Expected image to be PNG")
        return image

    def assert_formats(self, element=None):
        if element is None:
            element = self.document_element

        image_default = self.assert_png(self.marionette.screenshot(element=element))
        screenshot_base64 = self.marionette.screenshot(element=element, format="base64")
        image_base64 = self.assert_png(screenshot_base64)
        image_binary1 = self.marionette.screenshot(element=element, format="binary")
        image_binary2 = self.marionette.screenshot(element=element, format="binary")
        screenshot_hash1 = self.marionette.screenshot(element=element, format="hash")
        screenshot_hash2 = self.marionette.screenshot(element=element, format="hash")

        # Valid data should have been returned
        self.assert_png(image_base64)
        self.assert_png(image_binary1)
        self.assertEqual(image_base64, image_binary1)
        self.assertEqual(
            screenshot_hash1,
            hashlib.sha256(screenshot_base64.encode("utf-8")).hexdigest(),
        )

        # Different formats produce different data
        self.assertNotEqual(screenshot_base64, image_binary1)
        self.assertNotEqual(screenshot_base64, screenshot_hash1)
        self.assertNotEqual(image_binary1, screenshot_hash1)

        # A second capture should be identical
        self.assertEqual(image_base64, image_default)
        self.assertEqual(image_binary1, image_binary2)
        self.assertEqual(screenshot_hash1, screenshot_hash2)

    def get_element_dimensions(self, element):
        rect = element.rect
        return rect["width"], rect["height"]

    def get_image_dimensions(self, image):
        image = self.assert_png(image)
        width, height = struct.unpack(">LL", image[16:24])
        return int(width), int(height)

    def scale(self, rect):
        return (
            int(rect[0] * self.device_pixel_ratio),
            int(rect[1] * self.device_pixel_ratio),
        )


class TestScreenCaptureContent(WindowManagerMixin, ScreenCaptureTestCase):
    def setUp(self):
        super(TestScreenCaptureContent, self).setUp()
        self.marionette.set_context("content")

    def tearDown(self):
        self.close_all_tabs()
        super(TestScreenCaptureContent, self).tearDown()

    @property
    def scroll_dimensions(self):
        return tuple(
            self.marionette.execute_script(
                """
            return [
              document.documentElement.scrollWidth,
              document.documentElement.scrollHeight
            ];
            """
            )
        )

    def test_capture_tab_already_closed(self):
        new_tab = self.open_tab()
        self.marionette.switch_to_window(new_tab)
        self.marionette.close()

        self.assertRaises(NoSuchWindowException, self.marionette.screenshot)
        self.marionette.switch_to_window(self.start_tab)

    @unittest.skipIf(mozinfo.info["bits"] == 32, "Bug 1582973 - Risk for OOM on 32bit")
    def test_capture_vertical_bounds(self):
        self.marionette.navigate(inline("<body style='margin-top: 32768px'>foo"))
        screenshot = self.marionette.screenshot()
        self.assert_png(screenshot)

    @unittest.skipIf(mozinfo.info["bits"] == 32, "Bug 1582973 - Risk for OOM on 32bit")
    def test_capture_horizontal_bounds(self):
        self.marionette.navigate(inline("<body style='margin-left: 32768px'>foo"))
        screenshot = self.marionette.screenshot()
        self.assert_png(screenshot)

    @unittest.skipIf(mozinfo.info["bits"] == 32, "Bug 1582973 - Risk for OOM on 32bit")
    def test_capture_area_bounds(self):
        self.marionette.navigate(
            inline("<body style='margin-right: 21747px; margin-top: 21747px'>foo")
        )
        screenshot = self.marionette.screenshot()
        self.assert_png(screenshot)

    def test_capture_element(self):
        self.marionette.navigate(box)
        el = self.marionette.find_element(By.TAG_NAME, "div")
        screenshot = self.marionette.screenshot(element=el)
        self.assert_png(screenshot)
        self.assertEqual(
            self.scale(self.get_element_dimensions(el)),
            self.get_image_dimensions(screenshot),
        )

    @skip("Bug 1213875")
    def test_capture_element_scrolled_into_view(self):
        self.marionette.navigate(long)
        el = self.marionette.find_element(By.TAG_NAME, "p")
        screenshot = self.marionette.screenshot(element=el)
        self.assert_png(screenshot)
        self.assertEqual(
            self.scale(self.get_element_dimensions(el)),
            self.get_image_dimensions(screenshot),
        )
        self.assertGreater(self.page_y_offset, 0)

    def test_capture_full_html_document_element(self):
        self.marionette.navigate(long)
        screenshot = self.marionette.screenshot()
        self.assert_png(screenshot)
        self.assertEqual(
            self.scale(self.scroll_dimensions), self.get_image_dimensions(screenshot)
        )

    def test_capture_full_svg_document_element(self):
        self.marionette.navigate(svg)
        screenshot = self.marionette.screenshot()
        self.assert_png(screenshot)
        self.assertEqual(
            self.scale(self.scroll_dimensions), self.get_image_dimensions(screenshot)
        )

    def test_capture_viewport(self):
        url = self.marionette.absolute_url("clicks.html")
        self.marionette.navigate(short)
        self.marionette.navigate(url)
        screenshot = self.marionette.screenshot(full=False)
        self.assert_png(screenshot)
        self.assertEqual(
            self.scale(self.viewport_dimensions), self.get_image_dimensions(screenshot)
        )

    def test_capture_viewport_after_scroll(self):
        self.marionette.navigate(long)
        before = self.marionette.screenshot()
        el = self.marionette.find_element(By.TAG_NAME, "p")
        self.marionette.execute_script(
            "arguments[0].scrollIntoView()", script_args=[el]
        )
        after = self.marionette.screenshot(full=False)
        self.assertNotEqual(before, after)
        self.assertGreater(self.page_y_offset, 0)

    def assert_readback_viewport(self, screenshot):
        # The captured region is the composited content area, which can differ
        # from the content viewport by a device pixel due to rounding.
        width, height = self.get_image_dimensions(screenshot)
        expected_width, expected_height = self.scale(self.viewport_dimensions)
        self.assertAlmostEqual(width, expected_width, delta=2)
        self.assertAlmostEqual(height, expected_height, delta=2)

    @unittest.skipIf(sys.platform.startswith("darwin"), "Not supported on MacOS")
    def test_readback_viewport_via_argument(self):
        # Opting in with readback=True reads back the compositor, without the
        # remote.screenshot.use_readback pref needing to have been enabled.
        self.marionette.navigate(short)
        screenshot = self.marionette.screenshot(full=False, readback=True)
        self.assert_readback_viewport(screenshot)

    @unittest.skipIf(sys.platform.startswith("darwin"), "Not supported on MacOS")
    def test_readback_reflects_preceding_dom_change(self):
        # A readback right after a DOM change must not capture the frame that
        # was composited before the change.
        self.marionette.navigate(inline("<body style='background: lime'>"))
        expected = self.marionette.screenshot(full=False, readback=True)
        self.marionette.navigate(inline("<body style='background: red'>"))
        self.marionette.execute_script("document.body.style.background = 'lime';")
        screenshot = self.marionette.screenshot(full=False, readback=True)
        self.assertEqual(expected, screenshot)

    @unittest.skipIf(sys.platform.startswith("darwin"), "Not supported on MacOS")
    def test_readback_viewport(self):
        self.marionette.navigate(short)
        with self.marionette.using_prefs({"remote.screenshot.use_readback": True}):
            screenshot = self.marionette.screenshot(full=False)
        self.assert_readback_viewport(screenshot)

    @unittest.skipIf(sys.platform.startswith("darwin"), "Not supported on MacOS")
    def test_readback_full_page_degrades_to_viewport(self):
        # Readback can only return composited pixels, so a full-document
        # capture degrades to the viewport instead of the scroll dimensions.
        self.marionette.navigate(long)
        with self.marionette.using_prefs({"remote.screenshot.use_readback": True}):
            screenshot = self.marionette.screenshot()
        self.assert_readback_viewport(screenshot)

    @unittest.skipIf(sys.platform.startswith("darwin"), "Not supported on MacOS")
    def test_readback_element_degrades_to_viewport(self):
        self.marionette.navigate(box)
        el = self.marionette.find_element(By.TAG_NAME, "div")
        with self.marionette.using_prefs({"remote.screenshot.use_readback": True}):
            screenshot = self.marionette.screenshot(element=el)
        self.assert_readback_viewport(screenshot)

    def test_readback_argument_ignored_for_full_document(self):
        # Readback only returns the composited viewport, so a full-document
        # capture must ignore the argument and still render the whole document
        # with drawSnapshot (the scroll dimensions, not the viewport).
        self.marionette.navigate(long)
        screenshot = self.marionette.screenshot(full=True, readback=True)
        self.assert_png(screenshot)
        self.assertEqual(
            self.scale(self.scroll_dimensions), self.get_image_dimensions(screenshot)
        )

    def test_readback_argument_ignored_for_element(self):
        # Readback captures the whole content area, so an element capture must
        # ignore the argument and still crop to the element via drawSnapshot.
        self.marionette.navigate(box)
        el = self.marionette.find_element(By.TAG_NAME, "div")
        screenshot = self.marionette.screenshot(element=el, readback=True)
        self.assert_png(screenshot)
        self.assertEqual(
            self.scale(self.get_element_dimensions(el)),
            self.get_image_dimensions(screenshot),
        )

    def test_readback_argument_invalid_type(self):
        # The readback param is a boolean, and WebDriver::TakeScreenshot should
        # raise an exception if any other type is passed (e.g. the string-value
        # "false").
        self.marionette.navigate(short)
        with self.assertRaises(InvalidArgumentException):
            self.marionette._send_message(
                "WebDriver:TakeScreenshot",
                {"full": False, "readback": "false"},
                key="value",
            )

    def test_formats(self):
        self.marionette.navigate(box)

        # Use a smaller region to speed up the test
        element = self.marionette.find_element(By.TAG_NAME, "div")
        self.assert_formats(element=element)

    def test_format_unknown(self):
        with self.assertRaises(ValueError):
            self.marionette.screenshot(format="cheese")

    def test_save_screenshot(self):
        expected = self.marionette.screenshot(format="binary")
        with tempfile.TemporaryFile("w+b") as fh:
            self.marionette.save_screenshot(fh)
            fh.flush()
            fh.seek(0)
            content = fh.read()
            self.assertEqual(expected, content)

    @unittest.skipIf(sys.platform.startswith("darwin"), "Not supported on MacOS")
    def test_save_screenshot_forwards_readback(self):
        # The save_screenshot command should honor the readback parameter
        # (checked here by watching whether the value is included in the
        # underlying call to WebDriver:TakeScreenshot).
        self.marionette.navigate(short)
        sent = []
        send_message = self.marionette._send_message

        def record(name, params=None, *args, **kwargs):
            sent.append((name, params))
            return send_message(name, params, *args, **kwargs)

        with mock.patch.object(self.marionette, "_send_message", side_effect=record):
            with tempfile.TemporaryFile("w+b") as fh:
                self.marionette.save_screenshot(fh, full=False, readback=True)
        bodies = [params for name, params in sent if name == "WebDriver:TakeScreenshot"]
        self.assertEqual(len(bodies), 1)
        self.assertIs(bodies[0]["readback"], True)

    def test_scroll_default(self):
        self.marionette.navigate(long)
        before = self.page_y_offset
        el = self.marionette.find_element(By.TAG_NAME, "p")
        self.marionette.screenshot(element=el, format="hash")
        self.assertNotEqual(before, self.page_y_offset)

    def test_scroll(self):
        self.marionette.navigate(long)
        before = self.page_y_offset
        el = self.marionette.find_element(By.TAG_NAME, "p")
        self.marionette.screenshot(element=el, format="hash", scroll=True)
        self.assertNotEqual(before, self.page_y_offset)

    def test_scroll_off(self):
        self.marionette.navigate(partially_visible)
        el = self.marionette.find_element(By.ID, "abs")
        before = self.page_y_offset
        self.marionette.screenshot(element=el, format="hash", scroll=False)
        self.assertEqual(before, self.page_y_offset)

    def test_scroll_no_element(self):
        self.marionette.navigate(long)
        before = self.page_y_offset
        self.marionette.screenshot(format="hash", scroll=True)
        self.assertEqual(before, self.page_y_offset)
