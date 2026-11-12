import pytest

URL = "https://soffice.11st.co.kr/view/intro"

BLOCKED_TEXT = "해외 IP 접속 제한 안내"

UNSUPPORTED_CSS = ".c-browser"
UNSUPPORTED_TEXT = "크롬 브라우저"


async def await_text(client, do_expect_text):
    await client.navigate(URL)
    if client.find_text(BLOCKED_TEXT):
        pytest.skip("Region-locked, cannot test. Try using a VPN set to South Korea.")
    assert client.await_css(
        UNSUPPORTED_CSS,
        condition=f"elem.innerText.includes('{UNSUPPORTED_TEXT}')",
        is_displayed=do_expect_text,
        timeout=30,
    )


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await await_text(client, False)


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await await_text(client, True)


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_not_on_mobile(client):
    await client.navigate(URL)
    await client.stall(2)
    assert not client.find_text(UNSUPPORTED_TEXT)
