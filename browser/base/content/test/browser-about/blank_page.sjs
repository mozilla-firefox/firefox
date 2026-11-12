/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

function decode(str) {
  return decodeURIComponent(str.replace(/\+/g, encodeURIComponent(" ")));
}

function handleRequest(request, response) {
  const queryString = request.queryString;
  let params = queryString.split("&").reduce((memo, pair) => {
    let [key, val] = pair.split("=");
    if (!val) {
      val = key;
      key = "query";
    }

    try {
      memo[decode(key)] = decode(val);
    } catch (e) {
      memo[key] = val;
    }

    return memo;
  }, {});

  const status = parseInt(params.status);
  const message = params.message;
  const header = params.header;

  // Set default if missing parameters
  if (!status || !message) {
    response.setStatusLine(request.httpVersion, 400, "Bad Request");
    response.setHeader("Content-Length", "0", false);
    return;
  }

  if (header === "hide") {
    response.setStatusLine(request.httpVersion, status, message);
    return;
  }
  if (header === "lie") {
    response.setStatusLine(request.httpVersion, status, message);
    response.setHeader("Content-Length", "100", false);
    return;
  }
  if (header === "gzip-content") {
    // gzip-encoded "Not Found", which must not be replaced by an error page.
    const bytes = [
      0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xf3, 0xcb,
      0x2f, 0x51, 0x70, 0xcb, 0x2f, 0xcd, 0x4b, 0x01, 0x00, 0x4d, 0x8c, 0x74,
      0xb1, 0x09, 0x00, 0x00, 0x00,
    ];
    response.setStatusLine(request.httpVersion, status, message);
    response.setHeader("Content-Encoding", "gzip", false);
    response.setHeader("Content-Type", "text/plain; charset=utf-8", false);
    response.setHeader("Content-Length", String(bytes.length), false);
    response.write(String.fromCharCode(...bytes));
    return;
  }
  if (header === "brotli" || header === "gzip") {
    // A content-encoded body that decodes to nothing: a 1-byte empty brotli
    // stream, or an empty gzip member.
    const bytes =
      header === "brotli"
        ? [0x3b]
        : [
            0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          ];
    response.setStatusLine(request.httpVersion, status, message);
    response.setHeader(
      "Content-Encoding",
      header === "brotli" ? "br" : header,
      false
    );
    response.setHeader("Content-Type", "text/plain; charset=utf-8", false);
    response.setHeader("Content-Length", String(bytes.length), false);
    response.write(String.fromCharCode(...bytes));
    return;
  }
  response.setStatusLine(request.httpVersion, status, message);
  response.setHeader("Content-Length", "0", false);
}
