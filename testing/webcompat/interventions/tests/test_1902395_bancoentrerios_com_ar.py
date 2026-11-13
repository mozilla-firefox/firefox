import pytest

URL = "https://www.bancoentrerios.com.ar/"

BLOCKED_TEXT = "navegador"


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client):
    await client.navigate(URL)
    await client.stall(3)
    assert not client.find_text(BLOCKED_TEXT, is_displayed=True)
