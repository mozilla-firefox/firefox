import pytest

URL = "https://eternalslots.com/games/new-games"
FIRST_GAME_CSS = ".gameGrid .AsyncImage-image[src]"
PLAY_FOR_FUN_CSS = "#BtnUserPlayNow"
IFRAME_CSS = "iframe[src*=gameLauncher]"
UNSUPPORTED_CSS = ".unsupported-device-box"
SUPPORTED_CSS = "#game_main"


async def get_to_page(client):
    await client.navigate(URL)
    if client.find_text("Connection timed out"):
        pytest.skip("Site appears to be down right now")
        return
    client.soft_click(client.await_css(FIRST_GAME_CSS, is_displayed=True))
    client.soft_click(client.await_css(PLAY_FOR_FUN_CSS, is_displayed=True))
    client.switch_to_frame(client.await_css(IFRAME_CSS))


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await get_to_page(client)
    assert client.await_css(SUPPORTED_CSS, timeout=30)


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await get_to_page(client)
    assert client.await_css(UNSUPPORTED_CSS, timeout=30)
