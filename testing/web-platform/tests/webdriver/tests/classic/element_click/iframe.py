import pytest

from tests.support.classic.asserts import assert_success


def element_click(session, element):
    return session.transport.send(
        "POST", "session/{session_id}/element/{element_id}/click".format(
            session_id=session.session_id,
            element_id=element.id))


@pytest.mark.parametrize("domain", ["", "alt"], ids=["same_origin", "cross_origin"])
def test_click_element_in_frame(session, inline, iframe, domain):
    clicker = "<button id='b' onclick=\"document.title='clicked'\">go</button>"
    session.url = inline(iframe(clicker, domain=domain))

    frame = session.find.css("iframe", all=False)
    session.switch_to_frame(frame)

    button = session.find.css("#b", all=False)
    response = element_click(session, button)
    assert assert_success(response) is None

    # The click's effect must be observable in the same frame.
    assert session.execute_script("return document.title") == "clicked"
