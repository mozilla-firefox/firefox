import pytest
from webdriver.bidi.error import UnknownErrorException

URL = "https://www.aliexpress.com/ssr/300000512/nn2024update?spm=a2g0n.home.3fornn.2.50c276dbeE1R1C&disableNav=YES&pha_manifest=ssr&_immersiveMode=true&productIds=1005006422500160&browser_id=b0b0e7b79568430794a11f7d8cab49c8&aff_trace_key=null&aff_platform=msite&m_page_id=drilnynmcabwbkwi194dba601458b8fe2231f53dc9&gclid="

HEADER_CSS = ".header-wrapper"


async def is_layout_correct(client):
    try:
        await client.navigate(URL, no_skip=True)
    except UnknownErrorException:
        pass
    # among other layout problems, the mobile search bar appears at the top of the page,
    # rather than in its correct place, when using a Firefox user-agent string.
    return client.execute_script(
        """
        return arguments[0].getBoundingClientRect().top > 0
    """,
        client.await_css(HEADER_CSS, is_displayed=True),
    )


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await is_layout_correct(client)


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await is_layout_correct(client)
