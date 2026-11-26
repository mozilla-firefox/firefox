import pytest
from webdriver.bidi.modules.script import ContextTarget

from .realm_created import REALM_CREATED_EVENT

pytestmark = pytest.mark.asyncio


@pytest.mark.parametrize("type_hint", ["tab", "window"])
async def test_existing_realm_top_context(
    bidi_session,
    wait_for_event,
    wait_for_future_safe,
    subscribe_events,
    test_origin,
    type_hint,
):
    # See https://w3c.github.io/webdriver-bidi/#event-script-realmCreated
    # "The remote end subscribe steps with subscribe priority 2"
    top_level_context = await bidi_session.browsing_context.create(type_hint=type_hint)

    await bidi_session.browsing_context.navigate(
        context=top_level_context["context"], url=test_origin, wait="complete"
    )

    existing_realms = await bidi_session.script.get_realms(
        context=top_level_context["context"]
    )
    assert len(existing_realms) == 1

    on_entry = wait_for_event(REALM_CREATED_EVENT)
    await subscribe_events(
        [REALM_CREATED_EVENT], contexts=[top_level_context["context"]]
    )
    realm = await wait_for_future_safe(on_entry)

    assert realm == existing_realms[0]


async def test_existing_realm_child_context(
    bidi_session,
    subscribe_events,
    wait_for_bidi_events,
    new_tab,
    inline,
):
    await bidi_session.browsing_context.navigate(
        context=new_tab["context"],
        url=inline("<div>foo</div><script>0</script>"),
        wait="complete",
    )

    # Add two frames, each with a realm, once the page has loaded.
    frame_url = inline("<script>1</script>")
    await bidi_session.script.evaluate(
        expression=f"""Promise.all([0, 1].map(() => new Promise(resolve => {{
            const frame = document.createElement("iframe");
            frame.onload = resolve;
            frame.src = "{frame_url}";
            document.body.append(frame);
        }})))""",
        target=ContextTarget(new_tab["context"]),
        await_promise=True,
    )

    contexts = await bidi_session.browsing_context.get_tree(root=new_tab["context"])
    assert len(contexts[0]["children"]) == 2
    frame_1 = contexts[0]["children"][0]
    frame_2 = contexts[0]["children"][1]

    existing_realms = []
    for context in [new_tab, frame_1, frame_2]:
        result = await bidi_session.script.get_realms(context=context["context"])
        assert len(result) == 1
        existing_realms.append(result[0])

    events = []

    async def on_event(method, data):
        events.append(data)

    remove_listener = bidi_session.add_event_listener(REALM_CREATED_EVENT, on_event)

    # A subscription made with a child context applies to its top-level context,
    # so the existing realms of every frame in the tab are reported.
    await subscribe_events(events=[REALM_CREATED_EVENT], contexts=[frame_1["context"]])
    await wait_for_bidi_events(events, 3)
    assert len(events) == 3

    for realm in existing_realms:
        assert realm in events

    remove_listener()
