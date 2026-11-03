import pytest

URL = "https://www.weather.gov/srh/"
TILE_HOST = "tile.openstreetmap.org/"


async def is_tile_blocked(client):
    def check(method, data):
        if TILE_HOST in data["request"]["url"]:
            return any(
                h["name"].lower() == "x-blocked" for h in data["response"]["headers"]
            )

    tile = await client.promise_event_listener(
        "network.responseCompleted", check, timeout=60
    )
    await client.navigate(URL, wait="none")
    return await tile


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert not await is_tile_blocked(client)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert await is_tile_blocked(client)
