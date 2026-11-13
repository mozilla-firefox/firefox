from asyncio.exceptions import TimeoutError

import pytest

URL = "https://www.capital.gr/"


async def is_reload_cycle_detected(client):
    await client.navigate(URL)
    client.await_css("#alwaysFetch")
    client.execute_script("location.reload()")
    await client.stall(1)
    try:
        await (await client.promise_navigation_begins(URL, timeout=3))
        return True
    except TimeoutError:
        return False


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert not await is_reload_cycle_detected(client)


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert await is_reload_cycle_detected(client)
