function handleRequest(request, response) {
  response.setHeader("Content-Type", "text/javascript", false);

  var state = getState("alreadySent");
  if (!state) {
    setState("alreadySent", "1");
  } else {
    response.setStatusLine("1.1", 404, "Not Found");
  }

  response.write("42");
}
