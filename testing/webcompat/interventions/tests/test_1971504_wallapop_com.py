import pytest
from webdriver.error import (
    ElementClickInterceptedException,
    StaleElementReferenceException,
)

URL = "https://es.wallapop.com/search"
MOBILE_FILTERS_CSS = "walla-button[data-testid=single-access-filters].hydrated"
LEFT_SLIDER_CSS = "#fromSelector"


async def does_left_slider_work(client):
    await client.navigate(URL)
    client.hide_elements("#onetrust-consent-sdk,#cmpwrapper,#credential_picker_iframe")
    client.await_css(MOBILE_FILTERS_CSS, is_displayed=True).click()
    for i in range(5):
        try:
            client.await_css(
                "button",
                condition="elem.innerText.includes('Precio')",
                is_displayed=True,
            ).click()
            break
        except ElementClickInterceptedException:
            await client.stall(0.5)
        except StaleElementReferenceException:
            await client.stall(0.5)
    slider = client.await_css(LEFT_SLIDER_CSS, is_displayed=True)
    await client.stall(0.5)

    # Unfortunately, on desktop range thumbs do not react to any attempts to
    # drag them with WebDriver. However they do on Android, which is enough
    # for us to be able to test them.

    def slider_value():
        return client.execute_script("return arguments[0].value", slider)

    orig_value = slider_value()

    coords = client.get_element_screen_position(slider)
    coords = [coords[0] + 10, coords[1] + 10]
    await client.apz_down(coords=coords)
    for i in range(25):
        await client.stall(0.01)
        coords[0] += 5
        await client.apz_move(coords=coords)
    return orig_value != slider_value()


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client):
    assert await does_left_slider_work(client)
