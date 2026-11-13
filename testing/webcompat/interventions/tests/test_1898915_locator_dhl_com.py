import pytest

URL = "https://locator.dhl.com/results"

FAILURE_TEXT1 = "Browser not supported"
FAILURE_TEXT2 = "we recommend using the latest version of the following browsers"


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL)
    await client.stall(3)
    assert not client.find_text(FAILURE_TEXT1, is_displayed=True)
    assert not client.find_text(FAILURE_TEXT2, is_displayed=True)


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL)
    assert client.await_text(FAILURE_TEXT1, is_displayed=True)
    assert client.await_text(FAILURE_TEXT2, is_displayed=True)
