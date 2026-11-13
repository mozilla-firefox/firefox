function handleRequest(request, response) {
  if (getState("reloaded") == "reloaded") {
    setState("loaded", "");
    setState("reloaded", "");
    response.setHeader("Content-Type", "text/html", false);
    response.write("<meta charset=iso-2022-kr>\u00E4");
  } else {
    response.setHeader("Content-Type", "text/html", false);
    if (getState("loaded") == "loaded") {
      setState("reloaded", "reloaded");
    } else {
      setState("loaded", "loaded");
    }
    response.write("<meta charset=Shift_JIS>");
    // kilobyte to force late-detection reload
    response.write("a".repeat(1024));
    response.write("<body>");
    response.write("\u00E4");
  }
}
