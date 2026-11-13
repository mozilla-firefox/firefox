import pytest

URL = "https://ace7.acecombat.jp"

HERO_CSS = "body.videoLoaded"


async def is_mousewheel_scrolling_too_fast(client):
    await client.navigate(URL, wait="none")
    hero = client.await_css(HERO_CSS, is_displayed=True, timeout=60)
    await client.send_apz_scroll_gesture(-100, element=hero, offset=[200, 200])
    await client.stall(2)
    after = client.execute_script("return window.scrollY")
    return after > 400


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert not await is_mousewheel_scrolling_too_fast(client)


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert await is_mousewheel_scrolling_too_fast(client)
