import pytest

URL = "https://javalab.org/en/potential_energy_by_gravity_en/"
CANVAS_CSS = "#myP5Canvas"


async def can_move_car(client):
    await client.navigate(URL)

    canvas = client.await_css(CANVAS_CSS, is_displayed=True)
    coords = client.get_element_screen_position(canvas)
    coords = [coords[0] + 20, coords[1] + 20]

    # get rid of hand icon and screenshot
    await client.apz_down(coords=coords)
    coords = [coords[0] + 2, coords[1] + 2]
    await client.apz_move(coords=coords)
    await client.stall(0.025)
    await client.apz_up(coords=coords)
    await client.stall(0.5)
    pre = canvas.screenshot()

    # drag around and see if the screenshot changes
    await client.apz_down(coords=coords)
    for _ in range(40):
        coords = [coords[0] + 2, coords[1] + 2]
        await client.apz_move(coords=coords)
        await client.stall(0.025)
    await client.apz_up(coords=coords)
    post = canvas.screenshot()
    diff = client.diff_images(pre, post)
    return not client.is_one_solid_color(diff)


@pytest.mark.asyncio
@pytest.mark.skip_platforms("android")
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await can_move_car(client)


@pytest.mark.asyncio
@pytest.mark.skip_platforms("android")
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await can_move_car(client)


@pytest.mark.asyncio
@pytest.mark.only_platforms("android")
@pytest.mark.without_interventions
async def test_android_regression(client):
    assert await can_move_car(client)
