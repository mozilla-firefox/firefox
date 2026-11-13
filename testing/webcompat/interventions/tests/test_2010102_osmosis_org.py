import pytest

URL = "https://www.osmosis.org/notes/Urticaria_and_erythema_nodosum#page-1"
LEFT_PANEL_CSS = "[data-testid='notesPage.leftsection']"
PAGE_CSS = "[data-testid=page-1] img"


async def is_layout_correct(client):
    await client.navigate(URL)
    await client.stall(1)
    return client.execute_script(
        """
          const [left, page] = arguments;
          const leftBox = left.getBoundingClientRect();
          const pageBox = page.getBoundingClientRect();
          return leftBox.right < pageBox.left;
        """,
        client.await_css(LEFT_PANEL_CSS, is_displayed=True),
        client.await_css(PAGE_CSS, is_displayed=True),
    )


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client):
    assert await is_layout_correct(client)
