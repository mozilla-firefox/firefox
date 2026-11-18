import pytest

from webdriver import WebElement
from tests.support.classic.asserts import assert_success


def perform_actions(session, actions):
    return session.transport.send(
        "POST",
        "/session/{session_id}/actions".format(session_id=session.session_id),
        {"actions": actions})


def release_actions(session):
    return session.transport.send(
        "DELETE",
        "/session/{session_id}/actions".format(session_id=session.session_id))


def click_element_actions(element, x=0, y=0):
    # session.transport.send posts the action dicts as raw JSON, so the element has to be
    # referenced in wire format rather than passed as a WebElement.
    origin = {WebElement.identifier: element.id}
    return [
        {
            "type": "pointer",
            "id": "pointer_id",
            "parameters": {"pointerType": "mouse"},
            "actions": [
                {"type": "pointerMove", "x": x, "y": y, "origin": origin},
                {"type": "pointerDown", "button": 0},
                {"type": "pointerUp", "button": 0},
            ],
        }
    ]


@pytest.mark.parametrize("domain", ["", "alt"], ids=["same_origin", "cross_origin"])
def test_perform_actions_on_element_in_frame(session, inline, iframe, domain):
    clicker = "<button id='b' onclick=\"document.title='clicked'\">go</button>"
    session.url = inline(iframe(clicker, domain=domain))

    frame = session.find.css("iframe", all=False)
    session.switch_to_frame(frame)

    button = session.find.css("#b", all=False)

    try:
        response = perform_actions(session, click_element_actions(button))
        assert assert_success(response) is None

        # The click synthesized by the actions must fire inside the frame.
        assert session.execute_script("return document.title") == "clicked"
    finally:
        # Reset the input state so a lingering pointer press cannot leak into later tests.
        release_actions(session)
