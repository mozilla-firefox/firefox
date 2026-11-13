import pytest

URL = "https://portal.adtpulse.com/myhome/30.0.0-61/access/signin.jsp"
HERO_CSS = "#username"
UNSUPPORTED_TEXT = "will not work properly on your operating system and browser"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL, wait="none")
    client.await_css(HERO_CSS, is_displayed=True, timeout=30)
    await client.stall(5)
    assert not client.find_text(UNSUPPORTED_TEXT, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL, wait="none")
    client.await_css(HERO_CSS, is_displayed=True, timeout=30)
    assert client.await_text(UNSUPPORTED_TEXT, is_displayed=True)
