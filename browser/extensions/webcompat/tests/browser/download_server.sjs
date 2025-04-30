function decode(str) {
  return decodeURIComponent(str.replace(/\+/g, encodeURIComponent(" ")));
}

function handleRequest(request, response) {
  response.setHeader("Access-Control-Allow-Origin", "*", false);
  response.setHeader("Cache-Control", "no-cache", false);
  response.setHeader("Content-Type", "text/plain");
  const corsAllow = [];
  if (request.queryString) {
    for (const [name, value] of JSON.parse(decode(request.queryString))) {
      response.setHeader(name, value);
      corsAllow.push(name);
    }
  }
  if (corsAllow.length) {
    response.setHeader("Access-Control-Expose-Headers", corsAllow.join(", "));
  }
  response.write("Success!");
}
