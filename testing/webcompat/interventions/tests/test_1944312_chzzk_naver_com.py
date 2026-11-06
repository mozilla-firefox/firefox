import pytest

HOME_URL = "https://chzzk.naver.com/"
LIVE_URL = "https://chzzk.naver.com/live/{}"
LIVES_API = "https://api.chzzk.naver.com/service/v1/lives?size=20&sortType=POPULAR"

SETTINGS_ITEM_CSS = ".pzp-ui-setting-home-item"
VIVID_SCREEN_CSS = ".pzp-setting-intro-filter"


async def is_vivid_screen_offered(client):
    await client.navigate(HOME_URL, wait="none")
    channel_id = client.execute_async_script(
        """
      const [api, done] = arguments;
      fetch(api).then(r => r.json()).then(j => {
        const live = (j?.content?.data || []).find(
          x => x?.channel?.channelId && !x.adult && !x.watchPartyNo
        );
        done(live?.channel?.channelId ?? null);
      }).catch(() => done(null));
    """,
        LIVES_API,
    )
    assert channel_id, "no live channel found"
    await client.navigate(LIVE_URL.format(channel_id), wait="none")
    client.await_css(SETTINGS_ITEM_CSS, timeout=60)
    await client.stall(1)
    return client.find_css(VIVID_SCREEN_CSS)


@pytest.mark.asyncio
@pytest.mark.skip_platforms("android")
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await is_vivid_screen_offered(client)


@pytest.mark.asyncio
@pytest.mark.skip_platforms("android")
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await is_vivid_screen_offered(client)
