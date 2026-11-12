import pytest

URL = "https://cesupils.lv/"

BLOCKED_TEXT = "Sorry, you have been blocked"
BLOCKED_MSG = "Cannot test, as site is blocking us."
WORKING_CSS = "#menu-darba-1"
BROKEN_CSS = "#error-page"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL)
    if client.find_text(BLOCKED_TEXT):
        pytest.skip(BLOCKED_MSG)
    assert client.await_css(WORKING_CSS)
    assert not client.find_css(BROKEN_CSS, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL)
    if client.find_text(BLOCKED_TEXT):
        pytest.skip(BLOCKED_MSG)
    assert client.await_css(BROKEN_CSS, is_displayed=True)
    assert not client.find_css(WORKING_CSS, is_displayed=True)
