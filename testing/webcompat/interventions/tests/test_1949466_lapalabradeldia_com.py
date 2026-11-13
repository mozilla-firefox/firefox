from random import randint

import pytest

URL = "https://lapalabradeldia.com/"

JUGAR_BUTTON_TEXT = "¡Jugar!"
CARD1_CSS = "#board .react-card-flip:nth-of-type(1)"
CARD2_CSS = "#board .react-card-flip:nth-of-type(2)"
CARD3_CSS = "#board .react-card-flip:nth-of-type(3)"
CARD4_CSS = "#board .react-card-flip:nth-of-type(4)"
CARD5_CSS = "#board .react-card-flip:nth-of-type(5)"
LETTERS_CSS = "#keyboard button:not(:has(svg))"  # avoid check and delete buttons


async def letters_sometimes_vanish(client):
    await client.navigate(URL, wait="load")
    client.await_text(JUGAR_BUTTON_TEXT, is_displayed=True).click()
    letters = client.await_css(LETTERS_CSS, is_displayed=True, all=True)
    body = client.find_css("body")

    # This is a very subtle test, because the problem isn't well-understood. I found that I could
    # only reproduce the issue if I move the mouse immediately after clicking a letter, while the
    # css animation runs. Then the card will sometimes vanish immediately after the animation ends,
    # and if I move the mouse again, it seems to reappear. So this test will simulate that kind of
    # movement while clicking on 5 random letters, then wait a half-second, see if any of the cards
    # are invisible (by taking a "screenshot" of the element, with their CSS borders hidden to make
    # is_one_solid_color not have to fuzz), and try another 5 random letters a few times just to make
    # sure the cards always appear. The need for delaying the various simulated events during the
    # animation makes this test slower than it probably needs to be, but that's fine.
    client.add_stylesheet(
        "#board .react-card-flip * { border:0 !important; animation-duration:0.5s !important; }"
    )

    # click on 5 random letters, check if any are invisible, then delete them and retry
    # repeat this 10 times, and presume everything is fine if none disappear.
    for attempt in range(10):
        for click_five_keys in range(5):
            letter = letters[randint(0, len(letters) - 1)]

            coords = client.get_element_screen_position(letter)
            coords = [coords[0] + 20, coords[1] + 20]
            await client.apz_down(coords=coords)
            await client.stall(0.025)
            coords = [coords[0] + 20, coords[1] + 20]
            await client.send_apz_mouse_event("move", coords=coords)
            await client.apz_up(coords=coords)

            await client.stall(0.025)
            await client.send_apz_mouse_event(
                "move", coords=[coords[0] + 50, coords[1] + 50]
            )
            await client.stall(0.025)
            await client.send_apz_mouse_event(
                "move", coords=[coords[0] + 100, coords[1] + 100]
            )
            await client.stall(0.025)
            await client.send_apz_mouse_event(
                "move", coords=[coords[0] + 150, coords[1] + 150]
            )
            await client.stall(0.025)
            await client.send_apz_mouse_event(
                "move", coords=[coords[0] + 200, coords[1] + 200]
            )
            await client.stall(0.025)

        # wait a moment to let the animations settle down to increase the
        # likelihood that we'll see the problem.
        await client.stall(0.5)

        if (
            client.is_one_solid_color(client.find_css(CARD1_CSS))
            or client.is_one_solid_color(client.find_css(CARD2_CSS))
            or client.is_one_solid_color(client.find_css(CARD3_CSS))
            or client.is_one_solid_color(client.find_css(CARD4_CSS))
            or client.is_one_solid_color(client.find_css(CARD5_CSS))
        ):
            return True

        # press backspace key 5 times to clear the inputs so we can try again
        for backspace in range(5):
            body.send_keys("\ue003")

    return False


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert not await letters_sometimes_vanish(client)


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert await letters_sometimes_vanish(client)
