import pytest

URL = "https://www.zoopla.co.uk/for-sale/map/property/?baths_min=2&beds_max=5&beds_min=3&category=residential&floor_area_units=sq_feet&is_auction=false&is_retirement_home=false&is_shared_ownership=false&new_homes=include&price_max=250000&price_min=160000&property_sub_type=bungalow&property_sub_type=detached&property_sub_type=terraced&property_sub_type=semi_detached&property_sub_type=flats&q=Uk&radius=0&search_source=refine&tenure=freehold&user_alert_id=30905409&map_app=true&polyenc=kvv%7EH_s%7ESbyx%2540tlzHymf%2540pytAwpx%2540%7E%2560l%2540ybHu_N_%257CBmuhEhaY%257D%257Cx%2540xfxAsknAbii%2540f%7EvA%2560pa%2540%2560%257CwFezkAn%257DnDipw%2540hrV"
COOKIES_CSS = "#usercentrics-cmp-ui"
DRAW_BUTTON_CSS = "button[data-loading=false]"
SEARCHBAR_CSS = "[class*=SearchBar]"


async def can_draw(client):
    await client.make_preload_script("delete navigator.__proto__.webdriver")
    await client.navigate(URL)
    client.hide_elements(COOKIES_CSS)

    client.await_css(
        DRAW_BUTTON_CSS, condition="elem.innerText.includes('Draw')", is_displayed=True
    ).click()

    client.await_css(
        DRAW_BUTTON_CSS,
        condition="elem.innerText.includes('Draw from scratch')",
        is_displayed=True,
    ).click()

    coords = [200, 200]
    await client.apz_down(coords=coords)
    for _ in range(0, 10):
        coords = [coords[0] + 5, coords[1] + 5]
        await client.apz_move(coords=coords)
    await client.apz_up(coords=coords)

    # a searchbar appears if the page let us draw
    await client.stall(1)
    return client.find_css(SEARCHBAR_CSS, is_displayed=True)


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await can_draw(client)


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await can_draw(client)
