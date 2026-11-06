import pytest

URL = "https://tokbox.com/developer/tools/precall/"

SUPPORTED_CSS = "#start_button"
UNSUPPORTED_CSS = "#browser_unsupported_modal[open]"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL)
    client.await_css(SUPPORTED_CSS, is_displayed=True)
    assert not client.find_css(UNSUPPORTED_CSS)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL)
    client.await_css(UNSUPPORTED_CSS)
    assert not client.find_css(SUPPORTED_CSS, is_displayed=True)
