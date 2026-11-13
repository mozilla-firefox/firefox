from uuid import uuid4

import pytest

URL = "https://www.jobs.abbott/us/en/search-results"

FIRST_JOB_CSS = "a[ph-tevent=job_click]"
APPLY_CSS = "a[ph-tevent=apply_click]"
EMAIL_INPUT = "#email"
GET_OTP_BUTTON = "button.otp-send-btn"
OTP_DROP_DOWN = "input.otp-field-input"


async def otp_renders_as_dropdown(client):
    await client.navigate(URL, wait="none")
    client.await_css(FIRST_JOB_CSS, is_displayed=True, timeout=30).click()
    client.await_css(APPLY_CSS, is_displayed=True).click()
    # Unique email per run so the site treats each attempt as a fresh applicant:
    # a reused address can trip "already registered"/rate-limit paths and skip the
    # OTP step, which would make this test flaky or silently stop exercising the fix.
    client.await_css(EMAIL_INPUT, is_displayed=True).send_keys(
        f"{uuid4().hex}@example.com"
    )
    # Click via JS: the consent overlay can still intercept a normal click here.
    get_otp = client.await_css(GET_OTP_BUTTON, is_displayed=True)
    client.execute_script("arguments[0].click()", get_otp)
    otp = client.await_css(OTP_DROP_DOWN, is_displayed=True)
    # Broken: Firefox shows the number-input spinner (the up/down "drop-down"
    # arrows). The fix applies appearance:textfield, which removes them.
    appearance = client.execute_script(
        "return getComputedStyle(arguments[0]).appearance", otp
    )
    return appearance != "textfield"


@pytest.mark.only_platforms("desktop")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert not await otp_renders_as_dropdown(client)


@pytest.mark.only_platforms("desktop")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert await otp_renders_as_dropdown(client)
