function parseQueryString(str) {
  if (str == "") {
    return {};
  }

  var paramArray = str.split("&");
  var regex = /^([^=]+)=(.*)$/;
  var params = {};
  for (var i = 0, sz = paramArray.length; i < sz; i++) {
    var match = regex.exec(paramArray[i]);
    if (!match) {
      throw new Error(`Bad parameter in queryString!  '${paramArray[i]}'`);
    }
    params[decodeURIComponent(match[1])] = decodeURIComponent(match[2]);
  }

  return params;
}

function getPosition(action) {
  var response = {
    status: "OK",
    location: {
      lat: 37.41857,
      lng: -122.08769,
    },
    accuracy: action == "worse-accuracy" ? 100 : 42,
  };

  return JSON.stringify(response);
}

var timer;
function handleRequest(request, response) {
  var params = parseQueryString(request.queryString);

  if (params.action == "stop-responding") {
    response.processAsync();
    return;
  }

  var position = getPosition(params.action);

  if (params.action == "respond-garbage") {
    // better way?
    var chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXTZabcdefghiklmnopqrstuvwxyz";
    position = "";
    var len = Math.floor(Math.random() * 5000);

    for (var i = 0; i < len; i++) {
      var c = Math.floor(Math.random() * chars.length);
      position += chars.substring(c, c + 1);
    }
  }

  response.processAsync();
  response.setStatusLine("1.0", 200, "OK");
  response.setHeader("Cache-Control", "no-cache", false);
  response.setHeader("Content-Type", "aplication/x-javascript", false);

  var delay = 0;
  if ("delay" in params) {
    delay = params.delay;
  }
  if (params.action === "send404") {
    response.setStatusLine("1.0", 404, "Not Found");
    position = "";
  }
  timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  timer.initWithCallback(
    function () {
      response.write(position);
      response.finish();
    },
    delay,
    timer.TYPE_ONE_SHOT
  );
}
