import pytest
from webdriver.error import JavascriptErrorException

URL = "https://global.gotomeeting.com/join/750803053"


async def is_join_allowed(client):
    try:
        await client.navigate(URL, no_skip=True)
    except Exception:
        pass
    # On Windows and macOS the page offers "Download the app" (and "Join in
    # this browser" when supported). On Linux it either says the browser is
    # unsupported, or redirects to the web app, which may unload the document
    # while the script below is waiting, so we retry in that case.
    for _ in range(5):
        try:
            return client.execute_async_script("""
                const done = arguments[0];
                setInterval(() => {
                  const text = document.body?.innerText ?? "";
                  if (
                    text.includes("Join in this browser") ||
                    text.includes("Join when session starts")
                  ) {
                    done(true);
                  } else if (
                    text.includes("Download the app") ||
                    text.includes("Sorry, this browser")
                  ) {
                    done(false);
                  }
                }, 100);
              """)
        except JavascriptErrorException:
            pass
    return False


@pytest.mark.asyncio
@pytest.mark.with_interventions
@pytest.mark.skip_platforms("android")
async def test_enabled(client):
    assert await is_join_allowed(client)


@pytest.mark.asyncio
@pytest.mark.without_interventions
@pytest.mark.skip_platforms("android")
async def test_disabled(client):
    assert not await is_join_allowed(client)
