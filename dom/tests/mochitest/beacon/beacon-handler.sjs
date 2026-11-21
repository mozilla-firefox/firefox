/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const CC = Components.Constructor;
const BinaryInputStream = CC(
  "@mozilla.org/binaryinputstream;1",
  "nsIBinaryInputStream",
  "setInputStream"
);

function DEBUG(str) {
  // dump("********** " + str + "\n");
}

function setOurState(data) {
  x = {
    data,
    QueryInterface(iid) {
      return this;
    },
  };
  x.wrappedJSObject = x;
  setObjectState("beacon-handler", x);
  DEBUG("our state is " + data);
}

function getOurState() {
  var data;
  getObjectState("beacon-handler", function (x) {
    // x can be null if no one has set any state yet
    if (x) {
      data = x.wrappedJSObject.data;
    }
  });
  return data;
}

function handleRequest(request, response) {
  DEBUG("Entered request handler");
  response.setHeader("Cache-Control", "no-cache", false);

  function finishControlResponse(id, response) {
    DEBUG("********* sending out the control GET response");
    var beacons = getOurState();
    let { data, mimetype } = beacons[id];
    DEBUG("GET was sending : " + data + "\n");
    DEBUG("GET was sending : " + mimetype + "\n");
    var result = {
      data,
      mimetype,
    };
    response.write(JSON.stringify(result));
    delete beacons[id];
    setOurState(beacons);
  }

  function getFinishOrWait(id, response) {
    var beacons = getOurState() || {};
    const item = beacons[id];
    if (item && typeof item === "object" && item.data !== "") {
      finishControlResponse(id, response);
    } else {
      DEBUG("GET has  arrived, but POST has not, blocking response!");
      beacons[id] = {};
      beacons[id].response = response;
      setOurState(beacons);
      response.processAsync();
    }
  }

  if (request.method == "GET") {
    DEBUG(" ------------ GET --------------- ");
    response.setHeader("Content-Type", "application/json", false);

    let searchParams = new URLSearchParams(request.queryString);
    if (searchParams.has("getLastBeaconCors")) {
      // Allow CORS responses of the last beacon
      var originHeader = request.getHeader("origin");
      response.setHeader("Access-Control-Allow-Headers", "content-type", false);
      response.setHeader("Access-Control-Allow-Methods", "POST, GET", false);
      response.setHeader("Access-Control-Allow-Origin", originHeader, false);
      response.setHeader("Access-Control-Allow-Credentials", "true", false);
      getFinishOrWait("0", response);
    } else if (searchParams.has("getBeacon")) {
      let id = searchParams.get("getBeacon");
      getFinishOrWait(id, response);
    } else {
      response.setStatusLine(request.httpVersion, 400, "Bad Request");
    }
    return;
  }

  if (request.method == "POST") {
    DEBUG(" ------------ POST --------------- ");
    var body = new BinaryInputStream(request.bodyInputStream);
    var avail;
    var bytes = [];

    while ((avail = body.available()) > 0) {
      Array.prototype.push.apply(bytes, body.readByteArray(avail));
    }

    var data = "";
    for (var i = 0; i < bytes.length; i++) {
      // We are only passing strings at this point.
      if (bytes[i] < 32) {
        continue;
      }
      var charcode = String.fromCharCode(bytes[i]);
      data += charcode;
    }

    var mimetype = "";
    if (request.hasHeader("Content-Type")) {
      mimetype = request.getHeader("Content-Type");
    }

    // check to see if this is form data.
    if (mimetype.indexOf("multipart/form-data") != -1) {
      // trim the mime type to make testing easier.
      mimetype = "multipart/form-data";
      // Extract only the form-data name.

      var pattern = /; name=\"(.+)\";/;
      data = data.split(pattern)[1];
    }

    DEBUG("**********   POST was sending : " + data + "\n");
    DEBUG("**********   POST was sending : " + mimetype + "\n");

    response.setHeader("Content-Type", "text/plain", false);
    response.write("ok");

    // associate the data from the beacon with the id
    let searchParams = new URLSearchParams(request.queryString);
    let id = searchParams.get("beaconid") || "0"; // "0" because test_beaconWithSafelistedContentType.html doesn't use id param
    let beacons = getOurState() || {};

    const item = beacons[id];
    if (item && typeof item.response === "object" && item.response) {
      DEBUG("GET is already pending, finishing!");
      beacons[id].data = data;
      beacons[id].mimetype = mimetype;
      finishControlResponse(id, item.response);
      item.response.finish();
    } else {
      DEBUG("GET has not arrived, marking it as unblocked");
      beacons[id] = {};
      beacons[id].data = data;
      beacons[id].mimetype = mimetype;
      setOurState(beacons);
    }

    return;
  }

  response.setStatusLine(request.httpVersion, 402, "Bad Request");
}
