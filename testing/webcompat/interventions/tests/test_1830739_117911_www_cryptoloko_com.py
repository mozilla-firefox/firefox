import pytest

URL = "https://www.cryptoloko.com/webplay/?play=sweet-16-blast"
IFRAME_CSS = ".gameframe iframe"
GOOD_MSG = "GameViewModel"
BAD_MSG = "UnsupportedDevice"


async def check_for_message(client, message):
    await client.navigate(URL)
    client.switch_to_frame(client.await_css(IFRAME_CSS))
    await (await client.promise_console_message_listener(message))


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await check_for_message(client, GOOD_MSG)


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await check_for_message(client, BAD_MSG)
