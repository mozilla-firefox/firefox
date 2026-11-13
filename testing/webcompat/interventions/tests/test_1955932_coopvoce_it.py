import pytest
from webdriver.error import NoSuchElementException

URL = "https://www.coopvoce.it/portale/ricarica.html"
ALLOW_COOKIES_CSS = "#CybotCookiebotDialogBodyLevelButtonLevelOptinAllowAll"
RADIO_CSS = "li.nav-item.online-importo"
RADIO_SELECTED_CSS = "li.nav-item.online-importo .selected"


async def does_radiobutton_work(client):
    await client.navigate(URL)

    try:
        client.await_css(ALLOW_COOKIES_CSS, is_displayed=True, timeout=4).click()
        client.await_element_hidden(client.css(ALLOW_COOKIES_CSS))
    except NoSuchElementException:
        pass

    radio = client.await_css(RADIO_CSS, is_displayed=True)
    await client.stall(0.5)
    radio.click()
    try:
        client.await_css(RADIO_SELECTED_CSS, is_displayed=True, timeout=3)
        return True
    except NoSuchElementException:
        return False


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await does_radiobutton_work(client)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await does_radiobutton_work(client)
