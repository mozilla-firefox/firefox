import pytest

URL = "https://app.fullcodemedical.com/Player/Player.html?case_list=true"

SUPPORTED_CSS = "[icon=home]"
UNSUPPORTED_TEXT = "Unsupported browser"


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL, wait="none")
    assert client.await_css(SUPPORTED_CSS, is_displayed=True, timeout=30)
    assert not client.find_text(UNSUPPORTED_TEXT, is_displayed=True)


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL, wait="none")
    assert client.await_text(UNSUPPORTED_TEXT, is_displayed=True, timeout=30)
    assert not client.find_css(SUPPORTED_CSS, is_displayed=True)
