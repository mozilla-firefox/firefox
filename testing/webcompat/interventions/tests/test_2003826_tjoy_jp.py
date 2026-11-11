import pytest
from webdriver.error import NoSuchElementException

URL = "https://tjoy.jp/shinjuku_wald9#schedule-content"

DATES_CSS = ".calendar-item"
CARD_CSS = ".box-film-wapper:has(.schedule-status[onclick])"
RESERVE_CSS = ".schedule-status[onclick]"
MAP_LOADER_CSS = ".map-content .loader"
ZOOM_WRAPPER_CSS = ".js-zoom-in"
MAP_CSS = ".js-map"


# It's necessary to navigate to the seat choice screen from the movie list because of the session management.
async def does_correct_zoom(client):
    # Open and wait for the movie list to be loaded.
    await client.navigate(URL, wait="load")

    if client.find_text("403 ERROR"):
        pytest.skip("Region-locked, cannot test. Try using a VPN set to Japan.")
        return False

    # Navigate to the seat choice screen.
    # Display tomorrow's movies and reserve the seat for one of them.
    # Note that today's movies can be already closed for sale.
    dates = client.await_css(DATES_CSS, all=True)
    dates[1].click()
    tomorrows_movie_cards = client.await_css(CARD_CSS, is_displayed=True, all=True)

    # some movies don't always have any available seats, so open the first five.
    for elem in tomorrows_movie_cards[:5]:
        elem.click()

    reserve = client.await_css(RESERVE_CSS, is_displayed=True)
    # The element is overlapped by another one, so we have to use javascript to click it.
    client.execute_script("arguments[0].click()", reserve)

    # Wait for the map's loading spinner to appear and disappear
    try:
        client.await_css(MAP_LOADER_CSS, is_displayed=True)
        client.await_element_hidden(client.css(MAP_LOADER_CSS))
    except NoSuchElementException:
        pass
    await client.stall(1)

    # Zoom in to the seat map and check if it sets only 'zoom' without '-moz-transform'.
    client.soft_click(client.await_css(ZOOM_WRAPPER_CSS, is_displayed=True))
    seat_map = client.await_css(MAP_CSS)
    moz_transform = client.execute_script(
        "return getComputedStyle(arguments[0]).MozTransform", seat_map
    )
    zoom = client.execute_script("return getComputedStyle(arguments[0]).zoom", seat_map)
    return (moz_transform in {"none", ""}) and (zoom not in {"", "none"})


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await does_correct_zoom(client)


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await does_correct_zoom(client)
