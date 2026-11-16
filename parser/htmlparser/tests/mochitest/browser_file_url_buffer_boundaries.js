/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const LOCAL_FILE_UTF_8_BUFFER_SIZE = 4 * 1024 * 1024;

const WORD = "päivää";
const WORD_BYTES = {
  "UTF-8": new Uint8Array([
    0x70, 0xc3, 0xa4, 0x69, 0x76, 0xc3, 0xa4, 0xc3, 0xa4,
  ]),
  "windows-1252": new Uint8Array([0x70, 0xe4, 0x69, 0x76, 0xe4, 0xe4]),
};

const HEAD_FILLER_LENGTHS = {
  "short-head": 0,
  "medium-head": 2 * 1024 * 1024,
  "long-head": LOCAL_FILE_UTF_8_BUFFER_SIZE + 64 * 1024,
};

const PIPE_PREFS = [
  [
    ["network.buffer.cache.size", 524288],
    ["network.buffer.cache.count", 48],
  ],
  [
    ["network.buffer.cache.size", 327680],
    ["network.buffer.cache.count", 240],
  ],
];

const DEFAULT_TOTAL_LENGTH = LOCAL_FILE_UTF_8_BUFFER_SIZE + 256 * 1024;

const STALL_FRAME_SCRIPT =
  "data:,(" +
  encodeURIComponent(
    function () {
      const STALL_MS = 2000;
      let stalled = false;
      const webProgress = docShell
        .QueryInterface(Ci.nsIInterfaceRequestor)
        .getInterface(Ci.nsIWebProgress);
      this.stallListener = {
        QueryInterface: ChromeUtils.generateQI([
          "nsIWebProgressListener",
          "nsISupportsWeakReference",
        ]),
        onStateChange(aWebProgress, aRequest, aStateFlags) {
          if (
            stalled ||
            !(aStateFlags & Ci.nsIWebProgressListener.STATE_START) ||
            !(aRequest instanceof Ci.nsIFileChannel)
          ) {
            return;
          }
          stalled = true;
          const end = Date.now() + STALL_MS;
          while (Date.now() < end) {
            // Busy-wait while the stream transport fills the pipe.
          }
        },
      };
      webProgress.addProgressListener(
        this.stallListener,
        Ci.nsIWebProgress.NOTIFY_STATE_REQUEST
      );
    }.toString()
  ) +
  ").call(this)";

requestLongerTimeout(4);

let gDir;
let gStartPath;

add_setup(async function () {
  gDir = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "html-parser-file-url"
  );
  registerCleanupFunction(() => IOUtils.remove(gDir, { recursive: true }));
  gStartPath = PathUtils.join(gDir, "start.html");
  await IOUtils.writeUTF8(gStartPath, "<!DOCTYPE html><title>start</title>");
});

function ascii(str) {
  return new TextEncoder().encode(str);
}

function concat(parts) {
  const total = parts.reduce((sum, part) => sum + part.length, 0);
  const bytes = new Uint8Array(total);
  let offset = 0;
  for (const part of parts) {
    bytes.set(part, offset);
    offset += part.length;
  }
  return bytes;
}

function buildDocument({ word, headFillerLength, totalLength }) {
  const parts = [ascii("<!DOCTYPE html><html><head><title>"), word];
  parts.push(ascii("</title>"));
  if (headFillerLength) {
    parts.push(
      ascii("<style>/*"),
      ascii("x".repeat(headFillerLength)),
      ascii("*/</style>")
    );
  }
  parts.push(ascii('</head><body><p id="word">'), word, ascii("</p><pre>"));
  const suffix = ascii('</pre><p id="end">end</p></body></html>\n');

  const lengthSoFar =
    parts.reduce((sum, part) => sum + part.length, 0) + suffix.length;
  const fillerLength = totalLength - lengthSoFar;
  Assert.greater(fillerLength, 0, "document must have room for body filler");
  const line = "b".repeat(63) + "\n";
  const filler =
    line.repeat(Math.floor(fillerLength / line.length)) +
    "b".repeat(fillerLength % line.length);
  parts.push(ascii(filler), suffix);

  const bytes = concat(parts);
  Assert.equal(bytes.length, totalLength, "document has the requested size");
  return bytes;
}

async function checkDocument(name, bytes, expectedCharset) {
  const path = PathUtils.join(gDir, `${name}.html`);
  await IOUtils.write(path, bytes);
  for (const prefs of PIPE_PREFS) {
    await SpecialPowers.pushPrefEnv({ set: prefs });
    await checkDocumentWithPrefs(name, path, expectedCharset);
    await SpecialPowers.popPrefEnv();
  }
}

async function checkDocumentWithPrefs(name, path, expectedCharset) {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: PathUtils.toFileURI(gStartPath) },
    async browser => {
      browser.messageManager.loadFrameScript(STALL_FRAME_SCRIPT, false);
      const url = PathUtils.toFileURI(path);
      const loaded = BrowserTestUtils.browserLoaded(browser, false, url);
      BrowserTestUtils.startLoadingURIString(browser, url);
      await loaded;
      const result = await SpecialPowers.spawn(browser, [], () => {
        const doc = content.document;
        return {
          charset: doc.characterSet,
          title: doc.title,
          word: doc.getElementById("word")?.textContent,
          hasEnd: !!doc.getElementById("end"),
        };
      });
      Assert.equal(result.charset, expectedCharset, `${name}: encoding`);
      Assert.equal(result.title, WORD, `${name}: title decoded`);
      Assert.equal(result.word, WORD, `${name}: body text decoded`);
      Assert.ok(result.hasEnd, `${name}: document parsed to the end`);
    }
  );
}

for (const encoding of Object.keys(WORD_BYTES)) {
  for (const [headKind, headFillerLength] of Object.entries(
    HEAD_FILLER_LENGTHS
  )) {
    const name = `${encoding}-${headKind}`;
    add_task(async function () {
      const bytes = buildDocument({
        word: WORD_BYTES[encoding],
        headFillerLength,
        totalLength: DEFAULT_TOTAL_LENGTH,
      });
      await checkDocument(name, bytes, encoding);
    });
  }

  // A file whose length is exactly the UTF-8 speculation limit, with
  // the head ending between the two special offsets, so that the end
  // of the <meta charset> scan and the speculation limit coincide
  // with the end of the stream.
  add_task(async function () {
    const name = `${encoding}-exact-limit`;
    const bytes = buildDocument({
      word: WORD_BYTES[encoding],
      headFillerLength: HEAD_FILLER_LENGTHS["medium-head"],
      totalLength: LOCAL_FILE_UTF_8_BUFFER_SIZE,
    });
    await checkDocument(name, bytes, encoding);
  });
}
