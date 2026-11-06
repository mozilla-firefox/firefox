import pytest

URL = "https://www.tiktok.com/@argenby/video/7377415282358324481"

MENU_BUTTON_CSS = "[data-e2e='more-menu-icon']"
NOT_INTERESTED_CSS = "[data-e2e='more-menu-popover_not-interested']"
FLOATING_PLAYER_CSS = "[data-e2e='more-menu-popover_mini-player']"


async def is_floating_player_offered(client):
    await client.navigate(URL, wait="none")
    button = client.await_css(MENU_BUTTON_CSS)
    await client.stall(2)
    client.execute_script(
        """
      const button = arguments[0];
      for (const type of ["mouseover", "mouseenter", "mousemove"]) {
        button.dispatchEvent(new MouseEvent(type, { bubbles: true }));
      }
      button.click();
    """,
        button,
    )
    client.await_css(NOT_INTERESTED_CSS)
    return client.find_css(FLOATING_PLAYER_CSS)


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await is_floating_player_offered(client)


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await is_floating_player_offered(client)
