import pytest

URL = "https://myaccountrwd.allstate.com/anon/account/login/logout"

LOGGED_OUT_TEXT = "You have logged out of your account"
UNSUPPORTED_CSS = "#warningMessagePanel"


async def is_warning_shown(client):
    await client.navigate(URL)
    client.await_text(LOGGED_OUT_TEXT, is_displayed=True)
    return client.find_css(UNSUPPORTED_CSS, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert not await is_warning_shown(client)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert await is_warning_shown(client)
