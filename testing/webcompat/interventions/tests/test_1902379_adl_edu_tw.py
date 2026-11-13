import pytest

URL = "https://adl.edu.tw/HomePage/home/"

UNSUPPORTED_TEXT = "因材網建議您使用 Chrome 瀏覽器，獲得最佳使用者體驗"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL)
    await client.stall(1)
    assert client.find_text(UNSUPPORTED_TEXT, is_displayed=False)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL)
    assert client.await_text(UNSUPPORTED_TEXT, is_displayed=True)
