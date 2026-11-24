# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from urllib.parse import quote

from marionette_driver import By, errors

from marionette_harness import (
    MarionetteTestCase,
    WindowManagerMixin,
)


def inline(doc):
    return "data:text/html;charset=utf-8,{}".format(quote(doc))


# The <a> element in the following HTML is not interactable because it
# is hidden by an overlay when scrolled into the top of the viewport.
# It should be interactable when scrolled in at the bottom of the
# viewport.
fixed_overlay = inline(
    """
<style>
* { margin: 0; padding: 0; }
body { height: 300vh }
div, a { display: block }
div {
  background-color: pink;
  position: fixed;
  width: 100%;
  height: 40px;
  top: 0;
}
a {
  margin-top: 1000px;
}
</style>

<div>overlay</div>
<a href=#>link</a>

<script>
window.clicked = false;

let link = document.querySelector("a");
link.addEventListener("click", () => window.clicked = true);
</script>
"""
)


obscured_overlay = inline(
    """
<style>
* { margin: 0; padding: 0; }
body { height: 100vh }
#overlay {
  background-color: pink;
  position: absolute;
  width: 100%;
  height: 100%;
}
</style>

<div id=overlay></div>
<a id=obscured href=#>link</a>

<script>
window.clicked = false;

let link = document.querySelector("#obscured");
link.addEventListener("click", () => window.clicked = true);
</script>
"""
)


class ClickBaseTestCase(WindowManagerMixin, MarionetteTestCase):
    def setUp(self):
        super(ClickBaseTestCase, self).setUp()

        # Always use a blank new tab for an empty history
        self.new_tab = self.open_tab()
        self.marionette.switch_to_window(self.new_tab)

    def tearDown(self):
        self.close_all_tabs()

    def test_click(self):
        self.marionette.navigate(
            inline(
                """
            <button>click me</button>
            <script>
              window.clicks = 0;
              let button = document.querySelector("button");
              button.addEventListener("click", () => window.clicks++);
            </script>
        """
            )
        )
        button = self.marionette.find_element(By.TAG_NAME, "button")
        button.click()
        self.assertEqual(
            1, self.marionette.execute_script("return window.clicks", sandbox=None)
        )

    def test_click_number_link(self):
        test_html = self.marionette.absolute_url("clicks.html")
        self.marionette.navigate(test_html)
        self.marionette.find_element(By.LINK_TEXT, "333333").click()
        self.marionette.find_element(By.ID, "testDiv")
        self.assertEqual(self.marionette.title, "Marionette Test")

    def test_clicking_an_element_that_is_not_displayed_raises(self):
        self.marionette.navigate(
            inline(
                """
            <p hidden>foo</p>
        """
            )
        )

        with self.assertRaises(errors.ElementNotInteractableException):
            self.marionette.find_element(By.TAG_NAME, "p").click()

    def test_clicking_on_a_multiline_link(self):
        test_html = self.marionette.absolute_url("clicks.html")
        self.marionette.navigate(test_html)
        self.marionette.find_element(By.ID, "overflowLink").click()
        self.marionette.find_element(By.ID, "testDiv")
        self.assertEqual(self.marionette.title, "Marionette Test")

    def test_click_mathml(self):
        self.marionette.navigate(
            inline(
                """
            <math><mtext id="target">click me</mtext></math>
            <script>
              window.clicks = 0;
              let mtext = document.getElementById("target");
              mtext.addEventListener("click", () => window.clicks++);
            </script>
        """
            )
        )
        mtext = self.marionette.find_element(By.ID, "target")
        mtext.click()
        self.assertEqual(
            1, self.marionette.execute_script("return window.clicks", sandbox=None)
        )

    def test_scroll_into_view_near_end(self):
        self.marionette.navigate(fixed_overlay)
        link = self.marionette.find_element(By.TAG_NAME, "a")
        link.click()
        self.assertTrue(
            self.marionette.execute_script("return window.clicked", sandbox=None)
        )

    def test_inclusive_descendant(self):
        self.marionette.navigate(
            inline(
                """
            <select multiple>
              <option>first
              <option>second
              <option>third
             </select>"""
            )
        )
        select = self.marionette.find_element(By.TAG_NAME, "select")

        # This tests that the pointer-interactability test does not
        # cause an ElementClickInterceptedException.
        #
        # At a <select multiple>'s in-view centre point, you might
        # find a fully rendered <option>.  Marionette should test that
        # the paint tree at this point _contains_ <option>, not that the
        # first element of the paint tree is _equal_ to <select>.
        select.click()

        # Bug 1413821 - Click does not select an option on Android
        if self.marionette.session_capabilities["browserName"] != "fennec":
            self.assertNotEqual(select.get_property("selectedIndex"), -1)

    def test_container_is_select(self):
        self.marionette.navigate(
            inline(
                """
            <select>
              <option>foo</option>
            </select>"""
            )
        )
        option = self.marionette.find_element(By.TAG_NAME, "option")
        option.click()
        self.assertTrue(option.get_property("selected"))

    def test_container_is_button(self):
        self.marionette.navigate(
            inline(
                """
          <button onclick="window.clicked = true;">
            <span><em>foo</em></span>
          </button>"""
            )
        )
        span = self.marionette.find_element(By.TAG_NAME, "span")
        span.click()
        self.assertTrue(
            self.marionette.execute_script("return window.clicked", sandbox=None)
        )

    def test_container_element_outside_view(self):
        self.marionette.navigate(
            inline(
                """
            <select style="margin-top: 100vh">
              <option>foo</option>
            </select>"""
            )
        )
        option = self.marionette.find_element(By.TAG_NAME, "option")
        option.click()
        self.assertTrue(option.get_property("selected"))

    def test_table_tr(self):
        self.marionette.navigate(
            inline(
                """
          <table>
            <tr><td onclick="window.clicked = true;">
              foo
            </td></tr>
          </table>"""
            )
        )
        tr = self.marionette.find_element(By.TAG_NAME, "tr")
        tr.click()
        self.assertTrue(
            self.marionette.execute_script("return window.clicked", sandbox=None)
        )


class TestLegacyClick(ClickBaseTestCase):
    """Uses legacy Selenium element displayedness checks."""

    def setUp(self):
        super(TestLegacyClick, self).setUp()

        self.marionette.delete_session()
        self.marionette.start_session({"moz:webdriverClick": False})


class TestClick(ClickBaseTestCase):
    """Uses WebDriver specification compatible element interactability checks."""

    def setUp(self):
        super(TestClick, self).setUp()

        self.marionette.delete_session()
        self.marionette.start_session({"moz:webdriverClick": True})

    def test_click_element_obscured_by_absolute_positioned_element(self):
        self.marionette.navigate(obscured_overlay)
        overlay = self.marionette.find_element(By.ID, "overlay")
        obscured = self.marionette.find_element(By.ID, "obscured")

        overlay.click()
        with self.assertRaises(errors.ElementClickInterceptedException):
            obscured.click()

    def test_centre_outside_viewport_vertically(self):
        self.marionette.navigate(
            inline(
                """
            <style>
            * { margin: 0; padding: 0; }
            div {
             display: block;
             position: absolute;
             background-color: blue;
             width: 200px;
             height: 200px;

             /* move centre point off viewport vertically */
             top: -105px;
            }
            </style>

            <div onclick="window.clicked = true;"></div>"""
            )
        )

        self.marionette.find_element(By.TAG_NAME, "div").click()
        self.assertTrue(
            self.marionette.execute_script("return window.clicked", sandbox=None)
        )

    def test_centre_outside_viewport_horizontally(self):
        self.marionette.navigate(
            inline(
                """
            <style>
            * { margin: 0; padding: 0; }
            div {
             display: block;
             position: absolute;
             background-color: blue;
             width: 200px;
             height: 200px;

             /* move centre point off viewport horizontally */
             left: -105px;
            }
            </style>

            <div onclick="window.clicked = true;"></div>"""
            )
        )

        self.marionette.find_element(By.TAG_NAME, "div").click()
        self.assertTrue(
            self.marionette.execute_script("return window.clicked", sandbox=None)
        )

    def test_centre_outside_viewport(self):
        self.marionette.navigate(
            inline(
                """
            <style>
            * { margin: 0; padding: 0; }
            div {
             display: block;
             position: absolute;
             background-color: blue;
             width: 200px;
             height: 200px;

             /* move centre point off viewport */
             left: -105px;
             top: -105px;
            }
            </style>

            <div onclick="window.clicked = true;"></div>"""
            )
        )

        self.marionette.find_element(By.TAG_NAME, "div").click()
        self.assertTrue(
            self.marionette.execute_script("return window.clicked", sandbox=None)
        )

    def test_css_transforms(self):
        self.marionette.navigate(
            inline(
                """
            <style>
            * { margin: 0; padding: 0; }
            div {
             display: block;
             background-color: blue;
             width: 200px;
             height: 200px;

             transform: translateX(-105px);
            }
            </style>

            <div onclick="window.clicked = true;"></div>"""
            )
        )

        self.marionette.find_element(By.TAG_NAME, "div").click()
        self.assertTrue(
            self.marionette.execute_script("return window.clicked", sandbox=None)
        )

    def test_input_file(self):
        self.marionette.navigate(inline("<input type=file>"))
        with self.assertRaises(errors.InvalidArgumentException):
            self.marionette.find_element(By.TAG_NAME, "input").click()

    def test_obscured_element(self):
        self.marionette.navigate(obscured_overlay)
        overlay = self.marionette.find_element(By.ID, "overlay")
        obscured = self.marionette.find_element(By.ID, "obscured")

        overlay.click()
        with self.assertRaises(errors.ElementClickInterceptedException):
            obscured.click()
        self.assertFalse(
            self.marionette.execute_script("return window.clicked", sandbox=None)
        )

    def test_pointer_events_none(self):
        self.marionette.navigate(
            inline(
                """
            <button style="pointer-events: none">click me</button>
            <script>
              window.clicked = false;
              let button = document.querySelector("button");
              button.addEventListener("click", () => window.clicked = true);
            </script>
        """
            )
        )
        button = self.marionette.find_element(By.TAG_NAME, "button")
        self.assertEqual("none", button.value_of_css_property("pointer-events"))

        with self.assertRaisesRegex(
            errors.ElementClickInterceptedException,
            "does not have pointer events enabled",
        ):
            button.click()
        self.assertFalse(
            self.marionette.execute_script("return window.clicked", sandbox=None)
        )

    def test_prevent_default(self):
        self.marionette.navigate(
            inline(
                """
            <button>click me</button>
            <script>
              let button = document.querySelector("button");
              button.addEventListener("click", event => event.preventDefault());
            </script>
        """
            )
        )
        button = self.marionette.find_element(By.TAG_NAME, "button")
        # should not time out
        button.click()

    def test_stop_propagation(self):
        self.marionette.navigate(
            inline(
                """
            <button>click me</button>
            <script>
              let button = document.querySelector("button");
              button.addEventListener("click", event => event.stopPropagation());
            </script>
        """
            )
        )
        button = self.marionette.find_element(By.TAG_NAME, "button")
        # should not time out
        button.click()

    def test_stop_immediate_propagation(self):
        self.marionette.navigate(
            inline(
                """
            <button>click me</button>
            <script>
              let button = document.querySelector("button");
              button.addEventListener("click", event => event.stopImmediatePropagation());
            </script>
        """
            )
        )
        button = self.marionette.find_element(By.TAG_NAME, "button")
        # should not time out
        button.click()


class TestClickNavigation(WindowManagerMixin, MarionetteTestCase):
    def setUp(self):
        super(TestClickNavigation, self).setUp()

        # Always use a blank new tab for an empty history
        self.new_tab = self.open_tab()
        self.marionette.switch_to_window(self.new_tab)

        self.test_page = self.marionette.absolute_url("clicks.html")
        self.marionette.navigate(self.test_page)

    def tearDown(self):
        self.close_all_tabs()

    def close_notification(self):
        try:
            with self.marionette.using_context("chrome"):
                elem = self.marionette.find_element(
                    By.CSS_SELECTOR,
                    "#notification-popup popupnotification .popup-notification-closebutton",
                )
                elem.click()
        except errors.NoSuchElementException:
            pass

    def test_click_link_page_load(self):
        self.marionette.find_element(By.LINK_TEXT, "333333").click()
        self.assertNotEqual(self.marionette.get_url(), self.test_page)
        self.assertEqual(self.marionette.title, "Marionette Test")

    def test_click_link_anchor(self):
        self.marionette.find_element(By.ID, "anchor").click()
        self.assertEqual(self.marionette.get_url(), "{}#".format(self.test_page))

    def test_click_link_install_addon(self):
        try:
            self.marionette.find_element(By.ID, "install-addon").click()
            self.assertEqual(self.marionette.get_url(), self.test_page)
        finally:
            self.close_notification()

    def test_click_no_link(self):
        self.marionette.find_element(By.ID, "links").click()
        self.assertEqual(self.marionette.get_url(), self.test_page)

    def test_click_option_navigate(self):
        self.marionette.find_element(By.ID, "option").click()
        self.marionette.find_element(By.ID, "delay")

    def test_click_remoteness_change(self):
        self.marionette.navigate("about:robots")
        self.marionette.navigate(self.test_page)
        self.marionette.find_element(By.ID, "anchor")

        self.marionette.navigate("about:robots")
        with self.assertRaises(errors.NoSuchElementException):
            self.marionette.find_element(By.ID, "anchor")

        self.marionette.go_back()
        self.marionette.find_element(By.ID, "anchor")

        self.marionette.find_element(By.ID, "history-back").click()
        with self.assertRaises(errors.NoSuchElementException):
            self.marionette.find_element(By.ID, "anchor")

    def test_wait_after_click_disabled_click_no_link_navigate(self):
        self.marionette.timeout.page_load = 5
        with self.marionette.using_prefs({
            "marionette.navigate-after-click.enabled": False
        }):
            self.marionette.find_element(By.ID, "links").click()
        self.assertEqual(self.marionette.get_url(), self.test_page)

    def test_wait_after_click_disabled_click_beforeunload_no_unload_navigate(self):
        self.marionette.timeout.page_load = 10
        no_content_url = self.marionette.absolute_url("clicks.html?pipe=status(204)")
        page = inline(
            f"""<button onclick="location.href='{no_content_url}'">204</button>"""
        )
        self.marionette.navigate(page)

        with self.marionette.using_prefs({
            "marionette.navigate-after-click.enabled": False
        }):
            self.marionette.find_element(By.TAG_NAME, "button").click()
        self.assertEqual(self.marionette.get_url(), page)


class TestClickCloseContext(WindowManagerMixin, MarionetteTestCase):
    def setUp(self):
        super(TestClickCloseContext, self).setUp()

        self.test_page = self.marionette.absolute_url("clicks.html")

    def tearDown(self):
        self.close_all_tabs()

        super(TestClickCloseContext, self).tearDown()

    def test_click_close_tab(self):
        new_tab = self.open_tab()
        self.marionette.switch_to_window(new_tab)

        self.marionette.navigate(self.test_page)
        self.marionette.find_element(By.ID, "close-window").click()

    def test_click_close_window(self):
        new_tab = self.open_window()
        self.marionette.switch_to_window(new_tab)

        self.marionette.navigate(self.test_page)
        self.marionette.find_element(By.ID, "close-window").click()
