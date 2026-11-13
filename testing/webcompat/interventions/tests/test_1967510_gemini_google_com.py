import pytest
from webdriver.error import NoSuchElementException

URL = "https://gemini.google.com/"

ADD_PROMPT_CSS = ".ql-editor.textarea.new-input-ui"
SEND_CSS = "[data-mat-icon-name=send]"
EDIT_OLD_PROMPT_CSS = ".mdc-icon-button:has([fonticon=edit])"
OLD_PROMPT_CSS = "textarea[id^=mat-input-]"
CANCEL_CSS = ".cancel-button"


async def check_paste_works(client):
    client.set_clipboard("")
    await client.navigate(URL)
    prompt = client.await_css(ADD_PROMPT_CSS, is_displayed=True)
    await client.stall(1)
    client.await_css(ADD_PROMPT_CSS, is_displayed=True).send_keys("hello")
    client.send_key("Enter")
    for _ in range(20):
        try:
            client.soft_click(client.await_css(EDIT_OLD_PROMPT_CSS))
        except NoSuchElementException:
            break
        if client.find_css(OLD_PROMPT_CSS, is_displayed=True):
            break
        await client.stall(0.5)
    client.await_css(OLD_PROMPT_CSS, is_displayed=True).click()
    client.execute_script("document.execCommand('selectAll')")
    client.execute_script("document.execCommand('copy')")
    client.await_css(CANCEL_CSS, is_displayed=True).click()
    prompt = client.await_css(ADD_PROMPT_CSS, is_displayed=True)
    await client.apz_click(element=prompt, offset=[40, 20])
    old_value = client.execute_script("return arguments[0].innerText", prompt)
    client.do_paste()
    await client.stall(1)
    return client.execute_script(
        """
        return arguments[0].innerText.trim() === (arguments[1] + "hello").trim()
      """,
        prompt,
        old_value,
    )


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client):
    assert await check_paste_works(client)
