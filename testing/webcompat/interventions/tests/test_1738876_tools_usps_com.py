import pytest

URL = "https://tools.usps.com/rcas.htm"
LOADING_OVERLAY_CSS = ".modalLoading.initLoader"

# The breakage is a race which only happens on some loads, so try several times.
TRIES = 10


async def page_gets_stuck_loading(client):
    for _ in range(TRIES):
        await client.navigate(URL, wait="load")
        await client.stall(3)
        overlay = client.find_css(LOADING_OVERLAY_CSS)
        if overlay and client.is_displayed(overlay):
            return True
    return False


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert not await page_gets_stuck_loading(client)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert await page_gets_stuck_loading(client)
