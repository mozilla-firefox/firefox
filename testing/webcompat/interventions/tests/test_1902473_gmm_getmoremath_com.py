import pytest

URL = "https://gmm.getmoremath.com/student.html"
LOGIN_CSS = "input[type=password]"
UNSUPPORTED_TEXT = "unreliable with Microsoft or Firefox"


async def visit_site(client):
    await client.navigate(URL, wait="none")
    client.await_css(LOGIN_CSS, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.with_interventions
@pytest.mark.skip_platforms("android")
async def test_enabled(client):
    await visit_site(client)
    assert not client.find_text(UNSUPPORTED_TEXT, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
@pytest.mark.skip_platforms("android")
async def test_disabled(client):
    await visit_site(client)
    assert client.find_text(UNSUPPORTED_TEXT, is_displayed=True)
