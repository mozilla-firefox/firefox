import pytest

URL = "https://de.pons.com/%C3%BCbersetzung-2/englisch-deutsch/panacea"

HERO_TEXT = "PONS Pur"
UNSUPPORTED_TEXT = "Bitte verwende einen alternativen Browser"


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client):
    await client.navigate(URL)
    assert client.await_text(HERO_TEXT, is_displayed=True)
    await client.stall(1)
    assert not client.find_text(UNSUPPORTED_TEXT, is_displayed=True)
