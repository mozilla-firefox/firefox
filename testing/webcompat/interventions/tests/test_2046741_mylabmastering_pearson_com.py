import pytest

URL = "https://mylabmastering.pearson.com"
MODAL_CSS = ".osb-modal-content"
COOKIE_DISMISS = "Reject All"
POPUP_TEXT = "Allow pop-ups to continue"


async def dismiss_cookie_banners(client):
    # The cookie consent banner appears randomly and may reappear
    # multiple times after dismissal before the page settles.
    for _ in range(5):
        try:
            btn = client.await_text(COOKIE_DISMISS, is_displayed=True, timeout=5)
            await client.stall(2)
            btn.click()
            await client.stall(2)
        except Exception:
            break


async def maybe_dismiss_popup_notice(client):
    # With the intervention active the unsupported-browser modal is hidden,
    # which causes a secondary "Allow pop-ups to continue" notice to appear.
    # Dismiss it so the login page is fully visible for the assertion.
    try:
        client.await_text(POPUP_TEXT, is_displayed=True, timeout=10)
        await client.stall(2)
        btn = client.find_css("button.close", is_displayed=True)
        if btn:
            btn.click()
        await client.stall(2)
    except Exception:
        pass


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL, wait="none")
    await dismiss_cookie_banners(client)
    await maybe_dismiss_popup_notice(client)
    await client.stall(3)
    assert not client.find_css(MODAL_CSS, is_displayed=True)


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL, wait="none")
    await dismiss_cookie_banners(client)
    # The unsupported-browser modal is triggered with a delay after the
    # cookie banner is dismissed.
    await client.stall(3)
    assert client.await_css(MODAL_CSS, is_displayed=True)
