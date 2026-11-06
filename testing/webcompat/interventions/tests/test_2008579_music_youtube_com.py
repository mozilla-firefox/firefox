import pytest

URL = "https://music.youtube.com/"

CAROUSEL_ITEMS_CSS = "ytmusic-carousel #items"


async def is_scrollbar_inside_carousel(client):
    await client.navigate(URL, wait="none")
    items = client.await_css(CAROUSEL_ITEMS_CSS, is_displayed=True)
    await client.stall(2)
    return client.execute_script(
        """
        const items = arguments[0];
        const carousel = items.closest("ytmusic-carousel");
        return items.getBoundingClientRect().bottom <=
               carousel.getBoundingClientRect().bottom;
      """,
        items,
    )


@pytest.mark.asyncio
@pytest.mark.with_interventions
@pytest.mark.skip_platforms("android")
async def test_enabled(client):
    assert await is_scrollbar_inside_carousel(client)


@pytest.mark.asyncio
@pytest.mark.without_interventions
@pytest.mark.skip_platforms("android")
async def test_disabled(client):
    assert not await is_scrollbar_inside_carousel(client)
