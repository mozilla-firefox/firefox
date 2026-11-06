import pytest

URL = "https://vizzy.io/export"

APP_TEXT = "Before You Export"
UNSUPPORTED_TEXT = "Chromium based browser"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL)
    assert client.await_text(APP_TEXT, is_displayed=True)
    await client.stall(5)
    assert not client.find_text(UNSUPPORTED_TEXT, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL)
    assert client.await_text(UNSUPPORTED_TEXT, is_displayed=True)
