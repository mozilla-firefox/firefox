import pytest

URL = "https://infyspringboard.us.onwingspan.com/web/en/viewer/video/lex_auth_01384259527732428818276_shared?collectionId=lex_auth_01384259143485030418294_shared&collectionType=Course&pathId=lex_auth_01384258794270720018232_shared"

UNSUPPORTED_TEXT = "Chromium"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL)
    await client.stall(3)
    assert not client.find_text(UNSUPPORTED_TEXT, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL)
    assert client.await_text(UNSUPPORTED_TEXT, is_displayed=True)
