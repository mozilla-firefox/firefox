import pytest

URL = "https://demo.paw.wolffaud.io/upstage.html"

SUPPORTED_TEXT = "NEW CHAIN"
UNSUPPORTED_TEXT = "requires a modern Chrome-compatible browser"


@pytest.mark.asyncio
@pytest.mark.skip_platforms("android")
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL)
    assert client.await_text(SUPPORTED_TEXT, is_displayed=True)
    assert not client.find_text(UNSUPPORTED_TEXT)


@pytest.mark.asyncio
@pytest.mark.skip_platforms("android")
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL)
    assert client.await_text(UNSUPPORTED_TEXT)
    assert not client.find_text(SUPPORTED_TEXT, is_displayed=True)
