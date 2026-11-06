import pytest

URL = "https://app.showit.com/"

LOGIN_CSS = "form#showit-login"
UNSUPPORTED_TEXT = "made for Google Chrome"


@pytest.mark.asyncio
@pytest.mark.skip_platforms("android")
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL, wait="none")
    assert client.await_css(LOGIN_CSS, is_displayed=True)
    assert not client.find_text(UNSUPPORTED_TEXT, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.skip_platforms("android")
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL, wait="none")
    assert client.await_text(UNSUPPORTED_TEXT, is_displayed=True)
    assert not client.find_css(LOGIN_CSS, is_displayed=True)
