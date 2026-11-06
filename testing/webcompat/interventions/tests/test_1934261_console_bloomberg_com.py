import pytest

URL = "https://console.bloomberg.com/"
LOADED_CSS = ".notification-bar--fixed"
UNSUPPORTED_TEXT = "For best Enterprise Console experience use"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL)
    client.await_css(LOADED_CSS)
    await client.stall(3)
    assert not client.find_text(UNSUPPORTED_TEXT, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL)
    assert client.await_text(UNSUPPORTED_TEXT, is_displayed=True)
