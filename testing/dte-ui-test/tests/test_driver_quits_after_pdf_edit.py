# Any copyright is dedicated to the Public Domain.
# https://creativecommons.org/publicdomain/zero/1.0/

import threading
from pathlib import Path

from selenium.webdriver import Firefox
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait

FORM_PDF = (
    Path(__file__).resolve().parents[3]
    / "toolkit/components/pdfjs/test/file_pdfjs_form.pdf"
)
QUIT_TIMEOUT = 30


def test_driver_quits_after_pdf_edit(driver: Firefox):
    """driver.quit() returns after a PDF edit that would show a beforeunload prompt."""
    driver.get(FORM_PDF.as_uri())
    field = driver.find_element(By.ID, "pdfjs_internal_id_7R")
    field.click()
    field.send_keys("hello world")
    # Leaving the field records the edit, as in PDF.js's own unload-dialog test.
    driver.find_element(By.CSS_SELECTOR, ".textLayer span").click()
    # Same check PDF.js's beforeunload handler uses to decide whether to prompt.
    WebDriverWait(driver, 10).until(
        lambda d: d.execute_script("return PDFViewerApplication._hasChanges();"),
        message="PDF.js did not record the edit as an unsaved change",
    )

    quitter = threading.Thread(target=driver.quit, daemon=True)
    quitter.start()
    quitter.join(QUIT_TIMEOUT)
    if quitter.is_alive():
        # Stop geckodriver so the fixture's own quit() doesn't hang as well.
        driver.service.process.kill()
    assert not quitter.is_alive(), f"driver.quit() took over {QUIT_TIMEOUT}s"
