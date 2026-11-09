# Networking

The Necko (aka Networking) component is Gecko's implementation of the web's networking protocols.
Most of the component's source lives in `netwerk` directory and this document's source can be found in `netwerk/docs`.
This page points to helpful resources for developers interested in contributing to Necko.

Necko's [wiki page](https://wiki.mozilla.org/Networking) is dedicated to help users of firefox and bug authors. Readers can find various information related to bug filing, configuration of various networking features in the wiki page.



The team can be reached:
* on Matrix: [#necko:mozilla.org](https://chat.mozilla.org/#/room/#necko:mozilla.org)
* by email: necko@mozilla.com
* or by submitting a `Core::Networking` bug on [bugzilla.mozilla.org](https://bugzilla.mozilla.org/enter_bug.cgi?product=Core&component=Networking)

## How-to's
```{toctree}
:maxdepth: 1
http/logging
submitting_networking_bugs.md
captive_portals.md
connectivity_checking.md
```

## Tutorials
```{toctree}
:maxdepth: 1
network_test_guidelines.md
http_server_for_testing
NodeServers.md
mochitest_with_http3.md
```

## Deep Dives
### Necko Design
```{toctree}
:maxdepth: 1
sec-necko-components.md
cache2/doc
http/http3.md
http/connect_udp.md
Necko Bird’s-eye View  <https://docs.google.com/presentation/d/1BRCK4WMYg-dUy07PB5H4jFVTpc4YnkQX8f5Y3KXqCs8>
Gecko HTTP Walkthrough <https://docs.google.com/presentation/d/1iuYNLJfz24MN9SS5ljjhG07452-kZKtXmOeGjcc1-lU/>
http/prioritization.md
http/resource_tailing.md
```

### Necko Features
```{toctree}
:maxdepth: 1
http/lifecycle
dns/dns-over-https-trr
url_parsers.md
webtransport/webtransport
captive_portals.md
early_hints.md
network_interfaces.md
```

## References
```{toctree}
:maxdepth: 1
new_to_necko_resources
neqo_triage_guideline.md
necko_lingo.md
dns/trr-skip-reasons
webtransport/webtransportsessionproxy
```
