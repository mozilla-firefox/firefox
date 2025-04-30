const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

let geolocation = null;
let locations = [
  [1, 2],
  [3, 4],
  [5, 6],
];

function geoHandler(metadata, response) {
  let [lat, lng] = locations.shift();
  response.setStatusLine("1.0", 200, "OK");
  response.setHeader("Cache-Control", "no-cache", false);
  response.setHeader("Content-Type", "application/x-javascript", false);
  response.write(
    JSON.stringify({
      status: "OK",
      location: { lat, lng },
      accuracy: 42,
    })
  );
}

function toJSON(pos) {
  return { lat: pos.coords.latitude, lng: pos.coords.longitude };
}

function getPosition() {
  return new Promise(function (resolve, reject) {
    geolocation.getCurrentPosition(resolve, reject);
  });
}

function watchPosition() {
  let seen = 0;
  return new Promise(function (resolve, reject) {
    let id = geolocation.watchPosition(position => {
      seen++;
      if (seen === 1) {
        Assert.deepEqual(toJSON(position), { lat: 3, lng: 4 });
        Assert.deepEqual(observer._lastData, { lat: 3, lng: 4 });
        Assert.equal(observer._countEvents, 2);
      } else if (seen === 2) {
        Assert.deepEqual(toJSON(position), { lat: 5, lng: 6 });
        Assert.deepEqual(observer._lastData, { lat: 5, lng: 6 });
        Assert.equal(observer._countEvents, 3);
        geolocation.clearWatch(id);
        resolve();
      }
    }, reject);
  });
}

let observer = {
  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),

  observe(subject, topic) {
    Assert.equal(topic, "geolocation-position-events");
    observer._countEvents++;
    observer._lastData = toJSON(subject);
  },

  _lastData: null,
  _countEvents: 0,
};

add_task(async function test_resetClient() {
  do_get_profile();
  geolocation = Cc["@mozilla.org/geolocation;1"].getService(Ci.nsISupports);

  let server = new HttpServer();
  server.registerPathHandler("/geo", geoHandler);
  server.start(-1);

  Services.prefs.setCharPref(
    "geo.provider.network.url",
    "http://localhost:" + server.identity.primaryPort + "/geo"
  );
  Services.prefs.setBoolPref(
    "geo.provider.network.debug.requestCache.enabled",
    false
  );
  Services.prefs.setBoolPref("geo.provider.network.scan", false);

  var obs = Cc["@mozilla.org/observer-service;1"].getService();
  obs = obs.QueryInterface(Ci.nsIObserverService);
  obs.addObserver(observer, "geolocation-position-events");

  let position = await getPosition();
  Assert.deepEqual(toJSON(position), { lat: 1, lng: 2 });
  Assert.equal(observer._countEvents, 1);
  Assert.deepEqual(observer._lastData, { lat: 1, lng: 2 });

  await watchPosition();

  await new Promise(resolve => server.stop(resolve));
});
