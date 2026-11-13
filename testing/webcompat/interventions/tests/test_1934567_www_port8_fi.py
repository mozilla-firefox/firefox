import pytest

URL = "https://www.port8.fi/bokning/"


async def check_if_scrolling_works(client):
    await client.navigate(URL)
    body = client.await_css("body")
    expected_pos = client.execute_script("return window.scrollY")
    client.apz_scroll(body, dy=100000)
    await client.stall(0.2)
    final_pos = client.execute_script("return window.scrollY")
    return expected_pos != final_pos


@pytest.mark.only_platforms("android")
@pytest.mark.actual_platform_required
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client, in_headless_mode):
    assert await check_if_scrolling_works(client)


@pytest.mark.only_platforms("android")
@pytest.mark.actual_platform_required
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client, in_headless_mode):
    assert not await check_if_scrolling_works(client)
