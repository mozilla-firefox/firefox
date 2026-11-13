import pytest
from webdriver import NoSuchElementException, WebDriverException

URL = "https://www.autotrader.ca/lst?adtype=Dealer&atype=C&cy=CA&damaged_listing=exclude&desc=0&hprc=True&inMarket=advancedSearch&lat=49.1848258972168&loc=v2p5p4&lon=-121.93928527832031&offer=U&prv=British%20Columbia&prx=250&rcp=20&rcs=0&search_id=hf3of1x3pk&showcpo=1&size=20&sort=price&srt=4&sts=New&wcp=False&zip=V2P5P4%20Chilliwack%2C%20BC&zipr=250"

COOKIES_CSS = "#cookie-banner"
FILTER_BUTTON_CSS = "button[class^=SearchMaskFilterTags_mobileFilter]"
MAKE_FILTER_CSS = "[aria-controls=make-model-filter-details]"
MAKE_FILTER_OPTS_CSS = "#select-make-container"


async def are_filter_options_onscreen(client):
    await client.navigate(URL)

    try:
        client.remove_element(client.await_css(COOKIES_CSS, timeout=4))
    except NoSuchElementException:
        pass

    # try clicking the button a few times until it hides itself
    btn = client.await_css(FILTER_BUTTON_CSS, is_displayed=True)
    for i in range(5):
        try:
            btn.click()
            await client.stall(1)
        except WebDriverException:
            break

    # scroll to one of the filters and "open" it
    flt = client.await_css(MAKE_FILTER_CSS, is_displayed=True)
    client.scroll_into_view(flt)
    flt.click()

    # check whether the container of the filter is off-screen
    opts = client.await_css(MAKE_FILTER_OPTS_CSS, is_displayed=True)
    return client.execute_script(
        """
      return arguments[0].getBoundingClientRect().right > window.innerWidth;
    """,
        opts,
    )


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client):
    assert not await are_filter_options_onscreen(client)
