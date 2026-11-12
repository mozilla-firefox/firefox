import pytest

URL = "https://ncvtmis.gov.in/pages/trainee/myprofile.aspx"
NOT_FOUND_TEXT = "Page not found"


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client):
    await client.navigate(URL, wait="none")
    assert client.await_text(NOT_FOUND_TEXT, is_displayed=True)
