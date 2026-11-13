import pytest

URL = "https://littlealchemy2.com/"
PLAY_BUTTON_CSS = "#loading-screen .btn.js-ready"


async def did_music_play(client):
    await client.navigate(URL, wait="none")
    play = client.await_css(PLAY_BUTTON_CSS, is_displayed=True)
    client.execute_script(
        """
      window.__musicPlayed = false;
      AudioBufferSourceNode.prototype.start = function() {
        window.__musicPlayed = true;
      }
    """
    )
    play.click()

    # click somewhere so audio can start playing on Android
    client.apz_click(element=client.await_css("#library", is_displayed=True))

    await client.stall(2)
    return client.execute_script("return window.__musicPlayed")


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await did_music_play(client)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    # the problem is intermittent, so we try multiple times just in case.
    for _ in range(5):
        if not await did_music_play(client):
            assert True
            return
    assert False
