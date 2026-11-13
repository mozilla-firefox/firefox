import pytest

URL = "https://www.centerparcs.ie/"

UNSUPPORTED_TEXT = "not support the current browser"


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client):
    await client.navigate(URL)
    await client.stall(2)
    assert not client.find_text(UNSUPPORTED_TEXT, is_displayed=True)
