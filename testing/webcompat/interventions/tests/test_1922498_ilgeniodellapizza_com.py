import pytest

URL = "http://www.ilgeniodellapizza.com/le-nostre-pizze.html"


async def can_scroll_filters(client):
    await client.navigate(URL)
    body = client.await_css("body")

    def get_top():
        return client.execute_script("return window.scrollY")

    top = get_top()
    for i in range(20):
        await client.stall(0.1)
        client.apz_scroll(body, dy=100)
        new_top = get_top()
        if new_top < top:
            return False
        top = new_top
    return True


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await can_scroll_filters(client)


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await can_scroll_filters(client)
