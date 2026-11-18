import re

import pytest
from webdriver.bidi.error import SessionNotCreatedException

pytestmark = pytest.mark.asyncio


@pytest.mark.parametrize("name", ["browserName", "browserVersion", "platformName"])
async def test_no_candidates_match(new_session, name):
    with pytest.raises(SessionNotCreatedException):
        await new_session(
            capabilities={
                "firstMatch": [{name: "not-a-match"}, {name: "another-mismatch"}]
            }
        )


@pytest.mark.parametrize("name", ["browserName", "browserVersion", "platformName"])
async def test_later_match(new_session, name):
    session = await new_session(capabilities={})
    value = session.capabilities[name]
    requested_value = (
        re.match(r"\d+(?:\.\d+)*", value).group() if name == "browserVersion" else value
    )
    await session.session.end()

    session = await new_session(
        capabilities={
            "alwaysMatch": {"unhandledPromptBehavior": {"alert": "accept"}},
            "firstMatch": [{name: "not-a-match"}, {name: requested_value}],
        }
    )
    assert session.capabilities[name] == value
    assert session.capabilities["unhandledPromptBehavior"] == {"alert": "accept"}


async def test_version_constraint(new_session):
    session = await new_session(capabilities={})
    version = session.capabilities["browserVersion"]
    numeric_version = re.match(r"\d+(?:\.\d+)*", version).group()
    await session.session.end()

    session = await new_session(
        capabilities={"alwaysMatch": {"browserVersion": f">={numeric_version}"}}
    )
    assert session.capabilities["browserVersion"] == version
