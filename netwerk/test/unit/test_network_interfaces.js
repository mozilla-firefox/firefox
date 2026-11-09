/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const IPV4_RE =
  /^(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}$/;
const MAC_RE = /^([0-9a-f]{2}:){5}[0-9a-f]{2}$/;

function getLinkService() {
  return Cc["@mozilla.org/network/network-link-service;1"].getService(
    Ci.nsINetworkLinkService
  );
}

function isValidIPv6(aIp) {
  if (!aIp.includes(":")) {
    return false;
  }
  try {
    Services.io.newURI(`http://[${aIp}]/`);
    return true;
  } catch (e) {
    return false;
  }
}

function isLoopbackIPv4(aIp) {
  return aIp.startsWith("127.");
}

function isLoopbackIPv6(aIp) {
  return aIp == "::1";
}

// Global unicast addresses are in 2000::/3.
function isGlobalIPv6(aIp) {
  return /^[23][0-9a-f]{0,3}:/i.test(aIp);
}

function isWin10_2009() {
  return mozinfo.os == "win" && mozinfo.os_version == "10.2009";
}

// The interface list is filled asynchronously.
async function waitForInterfaces(aCondition, aMsg) {
  const linkService = getLinkService();
  let interfaces = [];
  await TestUtils.waitForCondition(() => {
    interfaces = linkService.networkInterfaces;
    return interfaces.some(aCondition);
  }, aMsg);
  return interfaces;
}

add_task(async function test_network_interfaces_ipv4() {
  const interfaces = await waitForInterfaces(
    i => i.ipv4.some(ip => !isLoopbackIPv4(ip)),
    "Waiting for an interface with a non-loopback IPv4 address"
  );

  for (const intf of interfaces) {
    info(`${intf.name} ipv4=${intf.ipv4} gwv4=${intf.gwv4}`);
    Assert.ok(intf instanceof Ci.nsINetworkInterface, "Is an interface");
    Assert.greater(intf.name.length, 0, "Interface has a name");
    for (const ip of [...intf.ipv4, ...intf.gwv4]) {
      Assert.stringMatches(ip, IPV4_RE, `${intf.name}: ${ip} is IPv4`);
    }
  }
});

add_task(
  {
    // CI Windows 10 2009 workers have no non-loopback IPv6 address.
    skip_if: isWin10_2009,
  },
  async function test_network_interfaces_ipv6() {
    const interfaces = await waitForInterfaces(
      i => i.ipv6.some(ip => !isLoopbackIPv6(ip)),
      "Waiting for an interface with a non-loopback IPv6 address"
    );

    for (const intf of interfaces) {
      info(`${intf.name} ipv6=${intf.ipv6} gwv6=${intf.gwv6}`);
      for (const ip of [...intf.ipv6, ...intf.gwv6]) {
        Assert.ok(isValidIPv6(ip), `${intf.name}: ${ip} is IPv6`);
      }
    }
  }
);

add_task(async function test_network_interfaces_gwv4() {
  const interfaces = await waitForInterfaces(
    i => i.gwv4.length,
    "Waiting for an interface with an IPv4 default gateway"
  );

  for (const intf of interfaces.filter(i => i.gwv4.length)) {
    info(`${intf.name} ipv4=${intf.ipv4} gwv4=${intf.gwv4}`);
    Assert.greater(intf.ipv4.length, 0, `${intf.name} has an IPv4 address`);
    for (const gw of intf.gwv4) {
      Assert.stringMatches(gw, IPV4_RE, `${intf.name}: ${gw} is IPv4`);
      Assert.ok(!isLoopbackIPv4(gw), `${intf.name}: ${gw} is not loopback`);
    }
  }
});

add_task(
  {
    // CI Windows 10 2009 workers have no non-loopback IPv6 address.
    skip_if: isWin10_2009,
  },
  async function test_network_interfaces_gwv6() {
    const all = await waitForInterfaces(
      i => i.ipv4.length || i.ipv6.length,
      "Waiting for an interface with an address"
    );
    if (!all.some(i => i.ipv6.some(isGlobalIPv6))) {
      info("No global IPv6 address, so no IPv6 default gateway to check");
      return;
    }

    const interfaces = await waitForInterfaces(
      i => i.gwv6.length,
      "Waiting for an interface with an IPv6 default gateway"
    );

    for (const intf of interfaces.filter(i => i.gwv6.length)) {
      info(`${intf.name} ipv6=${intf.ipv6} gwv6=${intf.gwv6}`);
      Assert.greater(intf.ipv6.length, 0, `${intf.name} has an IPv6 address`);
      for (const gw of intf.gwv6) {
        Assert.ok(isValidIPv6(gw), `${intf.name}: ${gw} is IPv6`);
        Assert.ok(!isLoopbackIPv6(gw), `${intf.name}: ${gw} is not loopback`);
      }
    }
  }
);

add_task(async function test_network_interfaces_mac() {
  const interfaces = await waitForInterfaces(
    i => i.mac != "00:00:00:00:00:00",
    "Waiting for an interface with a non-null MAC"
  );

  for (const intf of interfaces) {
    info(`${intf.name} mac=${intf.mac}`);
    Assert.stringMatches(intf.mac, MAC_RE, `${intf.name} has a valid MAC`);
  }
});

add_task(async function test_network_interfaces_parent_only() {
  const result = await new Promise(resolve =>
    sendCommand(
      `try {
        Components.classes["@mozilla.org/network/network-link-service;1"]
          .getService(Components.interfaces.nsINetworkLinkService)
          .networkInterfaces;
        "no exception";
      } catch (e) {
        String(e.result);
      }`,
      resolve
    )
  );
  Assert.equal(
    result,
    String(Cr.NS_ERROR_NOT_AVAILABLE),
    "Network interfaces are not available in a child"
  );
});
