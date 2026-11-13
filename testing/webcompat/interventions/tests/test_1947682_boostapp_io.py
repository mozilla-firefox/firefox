import pytest

URL = "https://www.boostapp.io/"

UNSUPPORTED_TEXT = "Chrome"


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client):
    await client.navigate(URL)
    await client.stall(3)
    assert not client.find_text(UNSUPPORTED_TEXT, is_displayed=True)
