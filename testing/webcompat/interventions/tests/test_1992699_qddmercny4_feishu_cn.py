import pytest

URL = "https://qddmercny4.feishu.cn/sheets/shtcnMklYu0WsXEDUXXanrSEB2m"
SHEET_LOADED_TEXT = "Filter"
UNSUPPORTED_CSS = ".not-compatible__announce"


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL, wait="none")
    assert client.await_text(SHEET_LOADED_TEXT, is_displayed=True, timeout=60)
    await client.stall(5)
    assert not client.find_css(UNSUPPORTED_CSS, is_displayed=True)


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL, wait="none")
    assert client.await_text(SHEET_LOADED_TEXT, is_displayed=True, timeout=60)
    assert client.await_css(UNSUPPORTED_CSS, is_displayed=True, timeout=20)
