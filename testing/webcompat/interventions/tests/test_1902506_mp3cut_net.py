import pytest
from webdriver.error import WebDriverException

URL = "https://mp3cut.net/it/"
PICKER_DROPDOWN_BUTTON_CSS = (
    ".file-picker.el-dropdown button[aria-haspopup=list]:not(:disabled)"
)
PICKER_FROM_URL_CSS = ".el-dropdown-menu__item.url"
AUDIO_FILE_URL = (
    "https://searchfox.org/firefox-main/source/toolkit/content/tests/widgets/audio.wav"
)
TEMPO_MENU_CSS = ".toolbar:has(.el-radio-button.item.atempo)"
TEMPO_BUTTON_CSS = ".el-radio-button.item.atempo, .el-dropdown-menu__item.atempo"
UNSUPPORTED_CSS = ".s-atempo.unsupported"
SUPPORTED_CSS = ".s-atempo:not(.unsupported) .el-slider.horizontal"


async def open_editor(client):
    await client.set_prompt_responses(AUDIO_FILE_URL)
    await client.navigate(URL)
    client.await_css(PICKER_DROPDOWN_BUTTON_CSS, is_displayed=True).click()
    for _ in range(10):
        try:
            client.await_css(PICKER_FROM_URL_CSS, is_displayed=True).click()
            break
        except WebDriverException:  # element not interactable
            await client.stall(0.5)
    # The tempo/velocità option may be hidden in a drop-down menu on narrow displays/mobile
    client.await_css(TEMPO_MENU_CSS, is_displayed=True).click()
    client.await_css(TEMPO_BUTTON_CSS, is_displayed=True).click()


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await open_editor(client)
    assert client.await_css(SUPPORTED_CSS, is_displayed=True)
    assert not client.find_css(UNSUPPORTED_CSS, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await open_editor(client)
    assert client.await_css(UNSUPPORTED_CSS, is_displayed=True)
    assert not client.find_css(SUPPORTED_CSS, is_displayed=True)
