import pytest

URL = "https://milkbarstore.com/products/assorted-cookie-tin"
ADD_TO_CART_CSS = "button[name=add]"
VIEW_CART_CSS = ".show-cart[data-item-count='1']"
INCREASE_QUANTITY_CSS = "quantity-input [name=plus]"
QUANTITY_WRAPPER_CSS = "quantity-input"


async def do_cart_quantities_appear(client):
    # A screenshot of the quantity lines will show the spinner
    # arrows of the number-inputs when the site bug manifests.
    await client.navigate(URL, wait="none")
    client.hide_elements("[id^=alia-root], #PopupModal-reviews")
    client.await_css(ADD_TO_CART_CSS, is_displayed=True).click()
    client.soft_click(client.await_css(VIEW_CART_CSS, is_displayed=True, timeout=3))
    client.await_css(INCREASE_QUANTITY_CSS, is_displayed=True)
    await client.stall(1)
    pre = client.await_css(QUANTITY_WRAPPER_CSS).screenshot()
    client.await_css(INCREASE_QUANTITY_CSS, is_displayed=True).click()
    await client.stall(1)
    post = client.await_css(QUANTITY_WRAPPER_CSS).screenshot()
    return pre != post


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client):
    assert await do_cart_quantities_appear(client)
