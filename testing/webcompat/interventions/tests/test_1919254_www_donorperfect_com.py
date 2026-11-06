import pytest

URL = "https://www.donorperfect.com/"
NOTICE_TEXT = "tracking protection is"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL, wait="load")
    assert not client.find_text(NOTICE_TEXT, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL, wait="load")
    assert client.await_text(NOTICE_TEXT, is_displayed=True)
