import pytest

URL = "https://www.sigadoc.mt.gov.br/"

HERO_CSS = "#username"
UNSUPPORTED_TEXT = "Google Chrome"
UNSUPPORTED_CSS = "#isChrome"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL, wait="none")
    client.await_css(HERO_CSS, is_displayed=True, timeout=30)
    await client.stall(3)
    assert not client.find_css(UNSUPPORTED_CSS, is_displayed=True)
    assert not client.find_text(UNSUPPORTED_TEXT, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL, wait="none")
    client.await_css(HERO_CSS, is_displayed=True, timeout=30)
    assert client.await_css(UNSUPPORTED_CSS, is_displayed=True)
    assert client.await_text(UNSUPPORTED_TEXT, is_displayed=True)
