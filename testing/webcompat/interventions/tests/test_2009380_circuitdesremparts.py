import pytest
from webdriver.error import NoSuchElementException

URL = "https://www.circuitdesremparts.com/prochaine-edition/billetterie/"
HERO_CSS = "img[src*='no-dog.jpg']"
IFRAME_CSS = "iframe[src*=ticket]"
LOADED_CSS = "#resendWrapperTop"
NO_EVENT_TEXT = "Les ventes sont suspendues par l'organisateur"


async def does_ui_appear(client):
    await client.navigate(URL, wait="none")
    client.scroll_into_view(client.await_css(HERO_CSS, is_displayed=True))
    client.apz_scroll(client.find_css("body"), dy=100)
    client.switch_to_frame(client.await_css(IFRAME_CSS))
    try:
        loaded, no_event = client.await_first_element_of([
            client.css(LOADED_CSS),
            client.text(NO_EVENT_TEXT),
        ])
        return loaded or no_event
    except NoSuchElementException:
        return False


@pytest.mark.actual_platform_required
@pytest.mark.only_firefox_versions(max=148)
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await does_ui_appear(client)


@pytest.mark.actual_platform_required
@pytest.mark.only_firefox_versions(max=148)
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await does_ui_appear(client)


@pytest.mark.actual_platform_required
@pytest.mark.only_firefox_versions(min=149)
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_works_without_intervention_now(client):
    assert await does_ui_appear(client)
