import pytest

URL = "https://sirhus.dsi.cnrs.fr/"
GONE_TEXT = "L’accès à SIRHUS est indisponible."


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client):
    await client.navigate(URL, wait="none")
    assert client.await_text(GONE_TEXT, is_displayed=True)
