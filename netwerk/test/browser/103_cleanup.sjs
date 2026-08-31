"use strict";

// A 1x1 PNG.
const IMAGE = atob(
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=="
);

// Sends a 103 Early Hint preloading square2.png, then serves an *image*
// document. An image document has no nsContentSink, so the content process
// never runs nsContentSink::ProcessHTTPHeaders and therefore never connects
// back for the early hint. That makes EarlyHintPreloader's parent-connect
// timeout the only thing that can cancel the preload. See bug 1829935.
function handleRequest(_request, response) {
  response.seizePower();

  response.write(
    `HTTP/1.1 103 Early Hint\r\n` +
      `Link: <https://example.com/browser/netwerk/test/browser/square2.png>;` +
      ` rel=preload; as=image\r\n` +
      `\r\n`
  );

  response.write(
    `HTTP/1.1 200 OK\r\n` +
      `Content-Type: image/png\r\n` +
      `Cache-Control: no-cache\r\n` +
      `Content-Length: ${IMAGE.length}\r\n` +
      `\r\n` +
      IMAGE
  );
  response.finish();
}
