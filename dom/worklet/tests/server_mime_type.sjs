const BODIES = {
  js: "",
  json: "{}",
  text: "hello",
  css: "",
  "import-js": `import "./server_mime_type.sjs?type=text/plain&body=js";`,
  "import-json": `import "./server_mime_type.sjs?type=application/json&body=json" with { type: "json" };`,
  "import-bad-json": `import "./server_mime_type.sjs?type=text/plain&body=json" with { type: "json" };`,
  "import-text": `import "./server_mime_type.sjs?type=text/plain&body=text" with { type: "text" };`,
  "import-text-octet-stream": `import "./server_mime_type.sjs?type=application/octet-stream&body=text" with { type: "text" };`,
  "import-css": `import "./server_mime_type.sjs?type=text/css&body=css" with { type: "css" };`,
};

function handleRequest(request, response) {
  const params = new URLSearchParams(request.queryString);
  response.setHeader("Content-Type", params.get("type"), false);
  response.write(BODIES[params.get("body")]);
}
