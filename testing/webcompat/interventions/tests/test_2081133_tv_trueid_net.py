import pytest

URL = "https://tv.trueid.net/th-th/live/t-sports-7-sd"
VIDEO_CSS = "video-js"
INCOMPATIBLE_TEXT = "Google Chrome"
VPN_TEXT = "unavailable in this country"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.make_preload_script("delete navigator.__proto__.webdriver")
    await client.navigate(URL)
    video, vpn = client.await_first_element_of(
        [client.css(VIDEO_CSS), client.text(VPN_TEXT)], timeout=30
    )
    if vpn:
        pytest.skip("Region-locked, cannot test. Try using a VPN set to Thailand.")
    assert video
    assert not client.find_text(INCOMPATIBLE_TEXT)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL)
    assert client.await_text(INCOMPATIBLE_TEXT, timeout=30)
