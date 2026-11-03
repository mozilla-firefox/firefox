"use strict";

/* import-globals-from trr_common.js */
/* import-globals-from head_trr.js */

let trrServer;
add_setup(async function setup() {
  trr_test_setup();
  Services.dns.clearCache(true);
  Services.prefs.setBoolPref("network.trr.preserve_on_background", true);

  trrServer = new TRRServer();
  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref(
      "network.http.close_connections_on_background"
    );
    await trrServer.stop();
  });
  await trrServer.start();
  dump(`port = ${trrServer.port()}\n`);

  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query?conncycle=true`
  );
  Services.prefs.setIntPref("network.trr.mode", Ci.nsIDNSService.MODE_TRRONLY);

  for (const [domain, ip] of [
    ["before.example.com", "1.2.3.4"],
    ["after.example.com", "1.2.3.5"],
  ]) {
    await trrServer.registerDoHAnswers(domain, "A", {
      answers: [{ name: domain, ttl: 55, type: "A", flush: false, data: ip }],
    });
  }
});

// Verify that TRR connections survive the application-background cleanup that
// Android uses to conserve power when Firefox goes to the background.
async function checkTrrConnectionSurvivesBackground(closeOnBackground) {
  Services.prefs.setBoolPref(
    "network.http.close_connections_on_background",
    closeOnBackground
  );
  Services.dns.clearCache(true);
  await trrServer.execute("global.gDoHPortsLog = []");

  // Warm up the TRR connection.
  await new TRRDNSListener("before.example.com", "1.2.3.4");

  // Simulate the Android application-background event. This posts a cleanup
  // message to the socket thread that closes persistent connections (only idle
  // ones if closeOnBackground is false) for all non-TRR entries.
  Services.obs.notifyObservers(null, "application-background");

  // This lookup must reuse the same TRR connection (same TCP port).
  await new TRRDNSListener("after.example.com", "1.2.3.5");

  let portLog = await trrServer.execute("global.gDoHPortsLog");
  info(`Port log: ${JSON.stringify(portLog)}`);

  // Collect the client ports seen for each domain. A and AAAA queries may
  // open separate TCP connections, so we get a set of ports per domain.
  const portsFor = domain =>
    new Set(portLog.filter(([d]) => d === domain).map(([, p]) => p));
  const beforePorts = portsFor("before.example.com");
  const afterPorts = portsFor("after.example.com");

  Assert.greater(beforePorts.size, 0, "before.example.com lookup was logged");
  Assert.greater(afterPorts.size, 0, "after.example.com lookup was logged");

  // At least one port must be shared, confirming the TRR connection was not
  // torn down by the background cleanup.
  const reused = [...afterPorts].some(p => beforePorts.has(p));
  Assert.ok(
    reused,
    `TRR connection reused after application-background (before=${[...beforePorts]}, after=${[...afterPorts]})`
  );
}

add_task(async function test_trr_connection_survives_background() {
  await checkTrrConnectionSurvivesBackground(true);
});

add_task(async function test_trr_connection_survives_idle_cleanup() {
  await checkTrrConnectionSurvivesBackground(false);
});
