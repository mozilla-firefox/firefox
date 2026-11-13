import pytest

URL = "https://account.gov.il/sspr/public/newuser?forwardURL=https%3A%2F%2Flogin.gov.il%2Fnidp%2Fsaml2%2Fsso%3Fid%3DusernamePasswordSMSOtp%26sid%3D0%26option%3Dcredential%26sid%3D0&locale=iw"

SUPPORTED_CSS = "#givenName"
UNSUPPORTED_CSS = "#bgBrowserCheck"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL)
    await client.stall(1)
    assert not client.is_displayed(client.find_css(UNSUPPORTED_CSS))


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL)
    await client.stall(1)
    client.await_css(UNSUPPORTED_CSS, is_displayed=True)
