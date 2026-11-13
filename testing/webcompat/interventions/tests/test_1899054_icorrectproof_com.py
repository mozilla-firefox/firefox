import pytest

URL = "https://www.icorrectproof.com/"

SUPPORTED_CSS = "#Username"
UNSUPPORTED_CSS = ".Versionerrormessage"


async def visit_site(client):
    await client.navigate(URL)
    if client.find_text("Error 403 - This web app is stopped"):
        pytest.skip("App is down right now. Try again later.")


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await visit_site(client)
    client.await_css(SUPPORTED_CSS, is_displayed=True)
    assert not client.find_css(UNSUPPORTED_CSS, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await visit_site(client)
    client.await_css(UNSUPPORTED_CSS, is_displayed=True)
    assert not client.find_css(SUPPORTED_CSS, is_displayed=True)
