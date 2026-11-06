import pytest

URL = "https://search.yahoo.com/preferences/preferences"
SELECT_CSS = "#pref select"


async def is_select_text_centered(client):
    await client.navigate(URL)
    select = client.await_css(SELECT_CSS, is_displayed=True)
    return client.execute_script(
        """
        const cs = getComputedStyle(arguments[0]);
        return Math.abs(parseFloat(cs.paddingTop) - parseFloat(cs.paddingBottom)) <= 1;
        """,
        select,
    )


@pytest.mark.asyncio
@pytest.mark.skip_platforms("android")
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await is_select_text_centered(client)


@pytest.mark.asyncio
@pytest.mark.skip_platforms("android")
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await is_select_text_centered(client)
