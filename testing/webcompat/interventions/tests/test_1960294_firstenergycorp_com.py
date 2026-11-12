import pytest

URL = "https://www.firstenergycorp.com/"

UNSUPPORTED_TEXT = "upgrade your browser"


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client):
    await client.navigate(URL)
    assert not client.find_text(UNSUPPORTED_TEXT, is_displayed=True)
