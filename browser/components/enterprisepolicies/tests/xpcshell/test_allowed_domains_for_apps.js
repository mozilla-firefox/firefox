/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);

function getHeaderForURL(url) {
  let channel = NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  Services.obs.notifyObservers(channel, "http-on-modify-request");
  try {
    return channel.getRequestHeader("X-GoogApps-Allowed-Domains");
  } catch (e) {
    return null;
  }
}

add_task(async function test_allowed_domains_for_apps() {
  await setupPolicyEngineWithJson({
    policies: {
      AllowedDomainsForApps: "example.org",
    },
  });

  equal(getHeaderForURL("https://www.google.com/"), "example.org");
  equal(getHeaderForURL("https://www.google.com./"), "example.org");
  equal(getHeaderForURL("https://www.example.com/"), null);
});
