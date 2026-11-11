import pytest

URL = "https://NDMFiles3344.quickconnect.to/sharing/etR5hlb8S"
DROP_ZONE_CSS = "[class*=webfm-sharing-upload-addpanel]"
DROPPED_NAME = "webcompat-drop-test.txt"


async def is_drop_handled(client):
    await client.navigate(URL, wait="none")
    zone = client.await_css(DROP_ZONE_CSS, is_displayed=True, timeout=60)
    return client.execute_script(
        """
        const [zone, name] = arguments;
        const rect = zone.getBoundingClientRect();
        const target =
          document.elementFromPoint(
            rect.left + rect.width / 2,
            rect.top + rect.height / 2
          ) || zone;
        const dataTransfer = new DataTransfer();
        dataTransfer.items.add(new File(["test"], name, { type: "text/plain" }));
        let prevented = true;
        for (const type of ["dragenter", "dragover", "drop"]) {
          const ev = new DragEvent(type, {
            bubbles: true,
            cancelable: true,
            dataTransfer,
          });
          target.dispatchEvent(ev);
          if (type !== "dragenter") {
            prevented = prevented && ev.defaultPrevented;
          }
        }
        return prevented;
      """,
        zone,
        DROPPED_NAME,
    )


@pytest.mark.asyncio
@pytest.mark.skip_platforms("android")
@pytest.mark.with_interventions
async def test_enabled(client):
    assert await is_drop_handled(client)
    assert client.await_text(DROPPED_NAME, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.skip_platforms("android")
@pytest.mark.without_interventions
async def test_disabled(client):
    assert not await is_drop_handled(client)
    await client.stall(2)
    assert not client.find_text(DROPPED_NAME, is_displayed=True)
