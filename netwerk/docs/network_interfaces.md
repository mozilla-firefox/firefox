# Network Interfaces

`nsINetworkLinkService.networkInterfaces` lists the network interfaces of the
device as `nsINetworkInterface` objects: name, MAC address, IP addresses and
default gateways.

```js
const linkService = Cc["@mozilla.org/network/network-link-service;1"]
  .getService(Ci.nsINetworkLinkService);
for (const intf of linkService.networkInterfaces) {
  console.log(intf.name, intf.mac, intf.ipv4, intf.ipv6, intf.gwv4, intf.gwv6);
}
```

Collection is only built with `--enable-enterprise`. In other builds, reading
`networkInterfaces` throws `NS_ERROR_NOT_IMPLEMENTED`, assuming the service
exists. When that is not the case, then existing `NS_ERROR_NOT_AVAILABLE` may
be returned.

The list is only available in the parent process: reading it elsewhere throws
`NS_ERROR_NOT_AVAILABLE`. Consumers must not forward it, or data derived from
it, to child processes.

## How the list is built

Each platform collects the interfaces on a background thread and keeps the
result in a cache. Reading `networkInterfaces` copies that cache, so it never
blocks on the system, but it can be empty right after startup and lag behind a
network change for a few seconds.

```{mermaid}
flowchart LR
  linux["Linux, Android<br/>NetlinkService"]
  mac["macOS<br/>nsNetworkLinkService"]
  win["Windows<br/>nsNotifyAddrListener"]
  cache[("Cached list")]
  api["networkInterfaces"]
  linux -- "address or route change" --> cache
  mac -- "IP configuration change" --> cache
  win -- "address change" --> cache
  cache -- "copy on read" --> api
```

## Values

| Attribute | Format | Example |
|---|---|---|
| `name` | System name; adapter friendly name on Windows | `eth0`, `en0`, `Wi-Fi` |
| `mac` | Six lowercase hex bytes | `02:00:5e:10:00:01` |
| `ipv4`, `gwv4` | Dotted quad | `192.168.1.1` |
| `ipv6`, `gwv6` | Compressed form of RFC 5952 | `fe80::5a1d:d8ff:fe3c:ba90` |

- Addresses never carry a prefix length or an IPv6 zone (no `%en0`).
- `ipv4` and `ipv6` include link-local addresses (`169.254.0.0/16`, `fe80::/10`).
- `mac` keeps the first six bytes of longer hardware addresses, pads shorter
  ones with zeros, and is `00:00:00:00:00:00` when there is none, e.g. on a
  VPN tunnel.
- Lists are unordered and may be empty.

## Which interfaces are reported

Only interfaces that are up and not loopback:

| Platform | Reported when |
|---|---|
| Linux, Android | running, not loopback, with at least one global address |
| macOS | up, running, not loopback, with an active link |
| Windows | operationally up, with a unicast address, not loopback and not a VMware `VMnet` adapter |

## Gateways

`gwv4` and `gwv6` hold the next hops of the default routes (`0.0.0.0/0`,
`::/0`) that go through the interface:

- A gateway is listed under its own address family.
- IPv6 gateways are usually link-local router addresses, learned from router
  advertisements.
- A default route without a next hop, as on a point-to-point VPN, adds no
  gateway.
- An interface without a default route has empty lists.

| Platform | Source |
|---|---|
| Linux, Android | Netlink default routes with a gateway |
| macOS | Routing table entries with a `0.0.0.0` or `::` destination |
| Windows | Gateway addresses of the adapter |
