import pytest

URL = "https://sales-crowd.jp/"

FAILURE_CSS = "#google-install"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL)
    await client.stall(3)
    assert not client.find_css(FAILURE_CSS, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL)
    assert client.await_css(FAILURE_CSS, is_displayed=True)
