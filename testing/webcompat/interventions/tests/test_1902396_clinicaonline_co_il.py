import pytest

URL = "https://www.clinicaonline.co.il/"

FAILURE_TEXT = "יש לעבור לדפדפן אינטרנט אקספלורר 11  או לכרום"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL)
    await client.stall(3)
    assert not client.find_text(FAILURE_TEXT, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL)
    assert client.await_text(FAILURE_TEXT, is_displayed=True)
