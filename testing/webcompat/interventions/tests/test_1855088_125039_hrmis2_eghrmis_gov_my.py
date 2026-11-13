import pytest

URL = "https://hrmis2.eghrmis.gov.my/HRMISNET/Common/Main/Login.aspx"
DIALOG_CSS = "#dialog4"


async def is_dialog_shown(client):
    await client.navigate(URL)
    await client.stall(2)
    dialog = client.await_css(DIALOG_CSS)
    return client.is_displayed(dialog)


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert not await is_dialog_shown(client)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert await is_dialog_shown(client)
