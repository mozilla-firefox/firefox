import pytest

URL = "https://m.youtube.com/results?search_query=trailer"
FIRST_VIDEO_CSS = "a[href*=watch]"
FULLSCREEN_ICON_CSS = "button.fullscreen-icon"
PLAY_BUTTON_CSS = ".ytp-large-play-button.ytp-button"
VIDEO_UI_CSS = ".player-controls-background"


async def pip_activates_properly(client, also_test_fullscreen_button=False):
    await client.make_preload_script("delete navigator.__proto__.webdriver")

    await client.navigate(URL)
    client.await_css(FIRST_VIDEO_CSS, is_displayed=True).click()

    # wait for the video to start playing
    client.execute_async_script(
        """
            const done = arguments[0];
            const i = setInterval(() => {
                const vid = document.querySelector("video");
                if (vid && !vid.paused) {
                    done();
                }
            }, 100);
        """
    )

    def get_video_dims(video):
        return client.execute_script(
            """
            const { width, height } = arguments[0].getBoundingClientRect();
            return `${width}:${height}`;
        """,
            video,
        )

    # give ads up to 30 seconds to play
    fs = client.await_css(FULLSCREEN_ICON_CSS, timeout=30)

    video = client.await_css("video", is_displayed=True)
    last_known_dims = get_video_dims(video)

    async def toggle_fullscreen():
        nonlocal last_known_dims, video

        for _ in range(5):
            await client.stall(0.5)

            if client.find_css(FULLSCREEN_ICON_CSS, is_displayed=True):
                await client.apz_click(element=fs, offset=[5, 5])
            else:
                await client.apz_click(element=video, offset=[50, 50])
                await client.stall(0.25)
                await client.apz_click(element=fs, offset=[5, 5])

            await client.stall(0.5)

            # confirm that the video went into fullscreen mode
            dims = get_video_dims(video)
            if dims != last_known_dims:
                last_known_dims = dims
                return True

        return False

    assert await toggle_fullscreen()

    if also_test_fullscreen_button:
        # test that we can toggle back out of fullscreen
        assert await toggle_fullscreen()

        await client.stall(0.5)

        # go back into fullscreen mode for the rest of the test
        assert await toggle_fullscreen()

    await client.stall(1)

    with client.using_context("chrome"):
        client.execute_script(
            """
            ChromeUtils.androidMoveTaskToBack();
        """
        )

    await client.stall(1)
    return client.execute_script(
        """
            return !(arguments[0]?.paused);
        """,
        video,
    )


@pytest.mark.only_platforms("android")
@pytest.mark.actual_platform_required
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await pip_activates_properly(client, True)


@pytest.mark.only_platforms("android")
@pytest.mark.actual_platform_required
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await pip_activates_properly(client)
