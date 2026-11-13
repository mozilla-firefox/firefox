import pytest

URL = "https://www.shazam.com/song/1678276312/a-feeling"

CONTAINER_CSS = "[class*=MedialistInner_overflowable]"


async def is_scrollbar_visible(client):
    await client.navigate(URL)
    container = client.await_css(CONTAINER_CSS)
    return client.execute_script(
        """
      const container = arguments[0];
      return Math.round(container.getBoundingClientRect().height) != container.clientHeight;
    """,
        container,
    )


@pytest.mark.enable_webkit_scrollbar
@pytest.mark.skip_platforms("android")
@pytest.mark.need_visible_scrollbars
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert not await is_scrollbar_visible(client)


@pytest.mark.disable_webkit_scrollbar
@pytest.mark.skip_platforms("android")
@pytest.mark.need_visible_scrollbars
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert await is_scrollbar_visible(client)
