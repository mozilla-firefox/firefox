import pytest

URL = "https://www.bateaux.com/contact/redaction"
SUPPORTED_CSS = "#centrale"
UNSUPPORTED_TEXT = "Access Denied."


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client):
    await client.navigate(URL, wait="none")
    assert client.await_css(SUPPORTED_CSS, is_displayed=True)
    assert not client.find_text(UNSUPPORTED_TEXT, is_displayed=True)
