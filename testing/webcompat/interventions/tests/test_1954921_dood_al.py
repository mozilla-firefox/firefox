import pytest

URL = "https://dood.al/pinktrombone/"


async def is_firefox_detected(client):
    await client.navigate(URL, wait="load")
    client.await_css("canvas")
    return client.execute_script("return window.isFirefox")


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert not await is_firefox_detected(client)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert await is_firefox_detected(client)
