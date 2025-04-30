import asyncio

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

    orig_video_dims = client.execute_script(
        """
      const { width, height } = document.querySelector("video").getBoundingClientRect();
      return `${width}:${height}`;
    """
    )

    client.soft_click(client.await_css(FULLSCREEN_ICON_CSS, is_displayed=True))
    await asyncio.sleep(1)

    # confirm that the video went into fullscreen mode
    client.execute_async_script(
        """
        const [orig_dims, done] = arguments;
        const timer = setInterval(() => {
          const { width, height } = document.querySelector("video").getBoundingClientRect();
          if (`${width}:${height}` !== orig_dims) {
            clearInterval(timer);
            done();
          }
        }, 100);
    """,
        orig_video_dims,
    )

    if also_test_fullscreen_button:
        # bring up the video UI (hides when entering fullscreen)
        client.click(client.await_css(VIDEO_UI_CSS, is_displayed=True))
        # confirm that exiting fullscreen works
        client.soft_click(client.await_css(FULLSCREEN_ICON_CSS, is_displayed=True))
        await asyncio.sleep(1)
        client.execute_async_script(
            """
            const [orig_dims, done] = arguments;
            const timer = setInterval(() => {
              const { width, height } = document.querySelector("video").getBoundingClientRect();
              if (`${width}:${height}` === orig_dims) {
                clearInterval(timer);
                done();
              }
            }, 100);
        """,
            orig_video_dims,
        )

        # go back into fullscreen mode for the rest of the test
        client.soft_click(client.await_css(FULLSCREEN_ICON_CSS, is_displayed=True))
        await asyncio.sleep(1)

    with client.using_context("chrome"):
        client.execute_script(
            """
            ChromeUtils.androidMoveTaskToBack();
        """
        )

    await asyncio.sleep(1)
    return client.execute_script(
        """
            return !(document.querySelector("video")?.paused);
        """
    )


@pytest.mark.only_platforms("fenix")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await pip_activates_properly(client, True)


@pytest.mark.only_platforms("fenix")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await pip_activates_properly(client)
