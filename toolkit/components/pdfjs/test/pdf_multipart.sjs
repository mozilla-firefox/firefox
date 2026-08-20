const nsTimer = Components.Constructor(
  "@mozilla.org/timer;1",
  "nsITimer",
  "initWithCallback"
);

const PDF = `
Content-Type: application/pdf

%PDF-2.0
1 0 obj <</Type /Catalog /Pages 2 0 R>>
endobj
2 0 obj <</Type /Pages /Kids [3 0 R] /Count 1>>
endobj
3 0 obj<</Type /Page /Parent 2 0 R /Resources 4 0 R /MediaBox [0 0 500 800] /Contents 6 0 R>>
endobj
4 0 obj<</Font <</F1 5 0 R>>>>
endobj
5 0 obj<</Type /Font /Subtype /Type1 /BaseFont /Helvetica>>
endobj
6 0 obj
<</Length 44>>
stream
BT /F1 24 Tf 175 720 Td (Hello World!)Tj ET
endstream
endobj
xref
0 7
0000000000 65535 f
0000000009 00000 n
0000000056 00000 n
0000000111 00000 n
0000000212 00000 n
0000000250 00000 n
0000000317 00000 n
trailer <</Size 7/Root 1 0 R>>
startxref
406
%%EOF
`;
const HTML = `
Content-Type: text/html

<html>
  <body>
    <p>Test</p>
  </body>
</html>
`;

const BOUNDARY = "--boundary";

function handleRequest(request, response) {
  response.processAsync();
  response.setHeader(
    "Content-Type",
    "multipart/x-mixed-replace;boundary=boundary",
    false
  );
  response.setHeader("Cache-Control", "no-cache", false);
  response.setStatusLine(request.httpVersion, "200", "Found");
  response.write(BOUNDARY);
  response.write(PDF);
  response.write(BOUNDARY);

  new nsTimer(
    () => {
      response.write(HTML);
      response.write(`${BOUNDARY}--`);
      response.finish();
    },
    1000,
    Ci.nsITimer.TYPE_ONE_SHOT
  );
}
