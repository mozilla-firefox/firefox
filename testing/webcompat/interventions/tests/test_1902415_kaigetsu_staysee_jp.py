import pytest

URL = "https://kaigetsu.staysee.jp/login"

FAILURE_TEXT = "Google Chrome"
FORBIDDEN_TEXT = "403 Forbidden"


async def visit_site(client):
    await client.navigate(URL)
    if client.find_text(FORBIDDEN_TEXT):
        pytest.skip("Region-locked, cannot test. Try using a VPN set to Japan.")


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await visit_site(client)
    await client.stall(3)
    assert not client.find_text(FAILURE_TEXT, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await visit_site(client)
    assert client.await_text(FAILURE_TEXT, is_displayed=True)
