import pytest

URL = "https://homebrewery.naturalcrit.com/"

PREVIEW_CSS = "#BrewRenderer"
WARNING_TEXT = "Built for Chrome"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL)
    client.await_css(PREVIEW_CSS, is_displayed=True)
    await client.stall(3)
    assert not client.find_text(WARNING_TEXT, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL)
    assert client.await_text(WARNING_TEXT, is_displayed=True)
