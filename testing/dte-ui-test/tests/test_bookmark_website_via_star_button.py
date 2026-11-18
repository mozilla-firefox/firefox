import pytest
from auto_fxui.browser_object import Navigation, PanelUi
from auto_fxui.page_object import GenericPage
from selenium.webdriver import Firefox


@pytest.fixture()
def add_to_prefs_list():
    return [("browser.toolbars.bookmarks.visibility", "always")]


@pytest.fixture()
def test_case():
    return "2084539"


BOOKMARK_URL = "https://www.mozilla.org/"
BOOKMARK_TITLE = "Internet for people"


def test_bookmark_website_via_star(driver: Firefox):
    """
    C2084539: Verify that the Websites can be bookmarked via star-shaped button
    """
    # Instantiate objects
    nav = Navigation(driver)
    panel = PanelUi(driver)
    page = GenericPage(driver, url=BOOKMARK_URL)

    # Bookmark using star button and verify star turned blue
    page.open()
    nav.add_bookmark_via_star_icon()
    nav.verify_star_button_is_blue()

    # Verify bookmark appears in Hamburger Menu, Bookmarks section
    panel.open_bookmarks_panel_from_hamburger_menu()
    panel.verify_bookmark_exists_in_hamburger_menu(BOOKMARK_TITLE)
