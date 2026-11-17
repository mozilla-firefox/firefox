import pytest

URL = "https://www.uol.com.br/"
WEBCOMPONENT_CSS = "news-player"


async def is_volume_slider_track_visible(client):
    await client.navigate(URL, wait="none")
    return client.execute_async_script(
        """
        const [comp, done] = arguments;
        const i = setInterval(() => {
            const slider = comp.shadowRoot?.querySelector("#customRange");
            if (slider) {
              done(getComputedStyle(slider).backgroundColor != "rgba(0, 0, 0, 0)");
              clearInterval(i);
            }
        }, 200);
    """,
        client.await_css(WEBCOMPONENT_CSS, is_displayed=True),
    )


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await is_volume_slider_track_visible(client)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await is_volume_slider_track_visible(client)
