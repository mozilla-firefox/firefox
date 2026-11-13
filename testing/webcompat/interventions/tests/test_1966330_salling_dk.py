import pytest
from webdriver.error import (
    ElementClickInterceptedException,
    WebDriverException,
)

URL = "https://salling.dk/herre/toej/c-11905/"
COOKIES_CSS = "button[class*=accept]"
COOKIES_OVERLAY_CSS = "#coiOverlay"
FILTERS_CSS = "button:has(svg.icon--filter)"
BRANDS_CSS = ".filter:nth-of-type(3):has(.accordion)"


async def are_filters_onscreen(client):
    client.set_screen_size(767, 500)
    await client.navigate(URL, wait="none")
    client.hide_elements("#usercentrics-cmp-ui")
    for _ in range(20):
        try:
            await client.stall(0.25)
            client.await_css(FILTERS_CSS, is_displayed=True).click()
        except (WebDriverException, ElementClickInterceptedException):
            continue
        if client.find_css(BRANDS_CSS, is_displayed=True):
            break
    for _ in range(20):
        try:
            client.await_css(BRANDS_CSS, is_displayed=True).click()
            break
        except (WebDriverException, ElementClickInterceptedException):
            await client.stall(0.1)
    await client.stall(0.5)
    return client.execute_script(
        "return arguments[0].getBoundingClientRect().top >= 0",
        client.await_css(BRANDS_CSS, is_displayed=True),
    )


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await are_filters_onscreen(client)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await are_filters_onscreen(client)
