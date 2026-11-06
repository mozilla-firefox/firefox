// Serves the media for the test, depending on the query:
// - key=K: the first request for K gets the first 64 KiB of a WebM file and is
//   then held open. The file's video frames are marked as not shown, so the
//   element never gets a first frame and keeps delaying its document's load
//   event. Every later request for K gets a 302 to the same file on another
//   origin.
// - redirected: the target of that redirect, held open the same way.
// - release: finishes every held-open response.
// - ping: answers right away, for the synchronous requests that spin the
//   event loop.
const FIRST_CHUNK = 65536;
const PENDING_KEY = "bug2079058-pending";

function loadHiddenFramesWebM() {
  const file = Services.dirsvc.get("CurWorkD", Ci.nsIFile);
  for (const part of "tests/dom/media/test/seek.webm".split("/")) {
    file.append(part);
  }
  const input = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
    Ci.nsIFileInputStream
  );
  input.init(file, -1, -1, false);
  const binary = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
    Ci.nsIBinaryInputStream
  );
  binary.setInputStream(input);
  const bytes = binary.readByteArray(binary.available());
  binary.close();

  function readVint(pos, keepMarker) {
    let length = 1;
    let mask = 0x80;
    while (mask && !(bytes[pos] & mask)) {
      length++;
      mask >>= 1;
    }
    if (!mask) {
      throw new Error(`Invalid EBML vint at ${pos}`);
    }
    let value = keepMarker ? bytes[pos] : bytes[pos] & (mask - 1);
    for (let i = 1; i < length; i++) {
      value = value * 256 + bytes[pos + i];
    }
    return [value, length];
  }

  const SEGMENT = 0x18538067;
  const CLUSTER = 0x1f43b675;
  const SIMPLE_BLOCK = 0xa3;
  function walk(pos, end) {
    while (pos < end) {
      const [id, idLength] = readVint(pos, true);
      const [size, sizeLength] = readVint(pos + idLength, false);
      const body = pos + idLength + sizeLength;
      if (id == SIMPLE_BLOCK) {
        // Clear the VP8 show_frame bit of the frame following the block header,
        // which is the track number, a 16-bit timecode and a flags byte.
        const [, trackLength] = readVint(body, false);
        const flags = bytes[body + trackLength + 2];
        if (flags & 0x06) {
          throw new Error("Laced SimpleBlocks are not supported");
        }
        bytes[body + trackLength + 3] &= ~0x10;
      } else if (id == SEGMENT || id == CLUSTER) {
        walk(body, body + size);
      }
      pos = body + size;
    }
  }
  walk(0, bytes.length);
  // String.fromCharCode() gets one argument for each byte. Convert the bytes
  // in small parts to stay below the limits for arguments and stack size.
  const BYTES_PER_CALL = 8192;
  let data = "";
  for (let i = 0; i < bytes.length; i += BYTES_PER_CALL) {
    data += String.fromCharCode(...bytes.slice(i, i + BYTES_PER_CALL));
  }
  return data;
}

function holdOpen(response) {
  getObjectState(PENDING_KEY, stored => {
    const pending = stored ? stored.wrappedJSObject : { responses: [] };
    pending.responses.push(response);
    pending.wrappedJSObject = pending;
    pending.QueryInterface = () => pending;
    setObjectState(PENDING_KEY, pending);
  });
}

function sendStartAndHold(request, response) {
  const data = loadHiddenFramesWebM();
  response.processAsync();
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "video/webm", false);
  response.setHeader("Content-Length", String(data.length), false);
  response.setHeader("Cache-Control", "no-store", false);
  response.write(data.slice(0, FIRST_CHUNK));
  holdOpen(response);
}

function handleRequest(request, response) {
  const params = new URLSearchParams(request.queryString);

  if (params.has("release")) {
    getObjectState(PENDING_KEY, stored => {
      if (stored) {
        for (const r of stored.wrappedJSObject.responses) {
          try {
            r.finish();
          } catch (e) {}
        }
      }
      setObjectState(PENDING_KEY, null);
    });
    response.setHeader("Cache-Control", "no-store", false);
    response.write("ok");
    return;
  }

  if (params.has("ping")) {
    response.setHeader("Cache-Control", "no-store", false);
    response.write("ok");
    return;
  }

  if (params.has("redirected")) {
    sendStartAndHold(request, response);
    return;
  }

  const key = params.get("key");
  const count = parseInt(getState(key) || "0");
  setState(key, String(count + 1));
  if (count == 0) {
    sendStartAndHold(request, response);
    return;
  }

  response.setStatusLine(request.httpVersion, 302, "Found");
  response.setHeader(
    "Location",
    // eslint-disable-next-line sdl/no-insecure-url
    `http://example.org/tests/dom/media/test/bug2079058.sjs?redirected&key=${encodeURIComponent(key)}`,
    false
  );
  response.setHeader("Cache-Control", "no-store", false);
}
