# mozai

Drive Firefox's AI features from Python.

`mozai` hands Python code a live Firefox and a set of commands for driving the AI features
inside it. This makes it easy for the Python ecosystem, which has rich ML tooling, to
integrate with the Gecko AI Runtime.

Requires Nightly or a local build. Other channels do not ship the driver.

## Installing

It is not published to PyPI. Install it from a Firefox checkout:

```sh
pip install -e browser/components/ai/driver/python
```

Or, without a checkout, from the repository:

```sh
pip install "mozai @ git+https://github.com/mozilla-firefox/firefox#subdirectory=browser/components/ai/driver/python"
```

Or a specific `<commit-hash>`:

```sh
pip install "mozai @ git+https://github.com/mozilla-firefox/firefox@<commit-hash>#subdirectory=browser/components/ai/driver/python"
```

Inside a Firefox checkout you can run `./mach ai` against your current build.

```sh
./mach ai describe --headless
./mach ai get-page-text --headless https://example.com
```

## Using it

```python
from pathlib import Path
from mozai import AIDriver

driver = AIDriver(
    headless=True,
    firefox_bin=Path(
        # Adjust the path for your file system and OS.
        "/path/to/firefox/objdir/dist/Nightly.app/Contents/MacOS/firefox"
    ),
)
try:
    print(driver.get_page_text("https://example.com")["text"])
finally:
    driver.quit()
```

## Commands

The command surface lives in the browser, so what a build supports depends on the build.
`describe()` reports it, and `supports()` checks a single command:

```python
if driver.supports("get_page_metadata"):
    ...
```
