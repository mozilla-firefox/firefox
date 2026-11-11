import pytest

URL = "https://www.gracobaby.ca/en_CA/All%20In%20Ones/SAP_2184499.html"


async def check_can_scroll(client):
    # The site only has the problem if the window size is narrow enough.
    client.set_screen_size(400, 800)
    await client.navigate(URL)
    await client.stall(2)

    # body.scrollTop changes if wk-fill-available is on, otherwise window.scrollY does.
    old_sy = client.execute_script("return window.scrollY")
    old_st = client.execute_script("return document.body.scrollTop")
    client.apz_scroll(client.await_css("body"), dy=100)

    # We need to wait for window.scrollY to be updated, but we
    # can't rely on an event being fired to detect when it's done.
    await client.stall(2)
    return old_sy != client.execute_script(
        "return window.scrollY"
    ) or old_st != client.execute_script("return document.body.scrollTop")


@pytest.mark.enable_webkit_fill_available
@pytest.mark.actual_platform_required
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await check_can_scroll(client)


@pytest.mark.disable_webkit_fill_available
@pytest.mark.actual_platform_required
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled2(client):
    assert await check_can_scroll(client)


@pytest.mark.disable_webkit_fill_available
@pytest.mark.actual_platform_required
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await check_can_scroll(client)
