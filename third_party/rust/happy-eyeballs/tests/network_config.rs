#![allow(clippy::disallowed_methods, reason = "tests may use Instant::now")]

mod common;
use common::*;

use std::time::{Duration, Instant};

use happy_eyeballs::{
    AltSvc, ConnectionAttemptHttpVersions, DnsRecordType, DnsResult, FailureReason, HappyEyeballs,
    HttpVersion, HttpVersions, Id, Input, IpPreference, NetworkConfig, Output,
};

#[test]
fn ip_host() {
    let now = Instant::now();
    let mut he = HappyEyeballs::new("[2001:0DB8::1]", PORT).unwrap();

    he.expect(out_attempt_v6_h1_h2(Id::from(0)), now);
}

#[test]
fn not_url_but_ip() {
    // Neither of these are a valid URL, but they are valid IP addresses.
    HappyEyeballs::new("::1", PORT).unwrap();
    HappyEyeballs::new("127.0.0.1", PORT).unwrap();
}

#[test]
fn alt_svc_construction() {
    let now = Instant::now();
    let config = NetworkConfig {
        alt_svc: vec![AltSvc {
            host: None,
            port: None,
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let mut he = HappyEyeballs::new_with_network_config(HOSTNAME, PORT, config).unwrap();

    // Should still send DNS queries as normal
    he.expect(out_send_dns_https(Id::from(0)), now);
}

#[test]
fn alt_svc_used_immediately() {
    let now = Instant::now();
    let config = NetworkConfig {
        alt_svc: vec![AltSvc {
            host: None,
            port: None,
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let mut he = HappyEyeballs::new_with_network_config(HOSTNAME, PORT, config).unwrap();

    // Alt-svc with H3 should make H3 available even without HTTPS DNS response
    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    // Alt-svc provided H3, so we should attempt H3 connection
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h3(Id::from(3)), now);
}

/// Alt-svc with a custom port: connections are attempted at both the alt-svc
/// port and the origin port.
///
/// No HTTPS records in this scenario. Alt-svc says H3 on port 8443.
/// Expected endpoint order:
///   alt-svc bucket  (port 8443): V6:H3, V4:H3
///   fallback bucket (port  443): V6:H2OrH1, V4:H2OrH1
#[test]
fn alt_svc_with_port() {
    let alt_port: u16 = CUSTOM_PORT;
    let config = NetworkConfig {
        alt_svc: vec![AltSvc {
            host: None,
            port: Some(alt_port),
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let (mut now, mut he) = setup_with_config(config);

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    // AAAA arrives, move-on met. First endpoint: alt-svc port V6:H3
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(
        out_attempt(
            Id::from(3),
            V6_ADDR.into(),
            alt_port,
            ConnectionAttemptHttpVersions::H3,
        ),
        now,
    );
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);

    he.expect_connection_attempts(
        [
            // Alt-svc bucket (port 8443): V4:H3
            out_attempt(
                Id::from(4),
                V4_ADDR.into(),
                alt_port,
                ConnectionAttemptHttpVersions::H3,
            ),
            // Fallback bucket (port 443): V6:H2OrH1, V4:H2OrH1
            out_attempt(
                Id::from(5),
                V6_ADDR.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2OrH1,
            ),
            out_attempt(
                Id::from(6),
                V4_ADDR.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2OrH1,
            ),
        ],
        &mut now,
    );

    // All connection attempts fail -> should report Failed(Connection)
    for id in 3..=5 {
        he.input(in_connection_result_negative(Id::from(id)), now);
        he.expect_idle(now);
    }
    he.input(in_connection_result_negative(Id::from(6)), now);
    he.expect(Output::Failed(FailureReason::Connection), now);
}

/// When the host is an IP address and alt-svc specifies a custom port,
/// endpoints should be attempted at both the alt-svc port and the origin port.
///
/// Expected endpoint order:
///   alt-svc bucket  (port 8443): `V4_ADDR:H3`
///   fallback bucket (port  443): `V4_ADDR:H2OrH1`
#[test]
fn ip_host_alt_svc_with_port() {
    let mut now = Instant::now();
    let config = NetworkConfig {
        alt_svc: vec![AltSvc {
            host: None,
            port: Some(CUSTOM_PORT),
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let mut he =
        HappyEyeballs::new_with_network_config(&V4_ADDR.to_string(), PORT, config).unwrap();

    // Alt-svc bucket (port 8443): H3
    he.expect(
        out_attempt(
            Id::from(0),
            V4_ADDR.into(),
            CUSTOM_PORT,
            ConnectionAttemptHttpVersions::H3,
        ),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    he.expect_connection_attempts(
        [
            // Fallback bucket (port 443): H2OrH1
            out_attempt(
                Id::from(1),
                V4_ADDR.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2OrH1,
            ),
        ],
        &mut now,
    );
}

/// Alt-svc entry that names a custom host: the host gets its own A/AAAA
/// queries and is attempted (at the alt-svc port, over the alt-svc protocol)
/// ahead of the plain origin fallback.
///
/// Expected endpoint order:
///   alt-svc host bucket (`ALT_HOST`, port 8443): `V6_ADDR_2:H3`, `V4_ADDR_2:H3`
///   fallback bucket     (HOSTNAME, port  443): `V6_ADDR:H2OrH1`, `V4_ADDR:H2OrH1`
#[test]
fn alt_svc_with_host() {
    const ALT_HOST: &str = "alt.example.com";
    let config = NetworkConfig {
        alt_svc: vec![AltSvc {
            host: Some(ALT_HOST.to_string()),
            port: Some(CUSTOM_PORT),
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let (mut now, mut he) = setup_with_config(config);

    // Origin queries (ids 0-2), then the alt-svc host's AAAA/A queries (ids 3-4).
    expect_initial_dns_queries(&mut he, now);
    he.expect(
        out_send_dns(Id::from(3), ALT_HOST, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_send_dns(Id::from(4), ALT_HOST, DnsRecordType::A), now);
    he.expect_idle(now);

    // Resolve all address records first. Without the HTTPS answer the machine
    // keeps waiting out the resolution delay.
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(3),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(out_resolution_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::A(Ok(vec![V4_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(out_resolution_delay(), now);

    // The negative HTTPS answer completes the move-on conditions.
    he.input(in_dns_https_negative(Id::from(0)), now);

    // Alt-svc host endpoints (port 8443, H3) come first, then origin fallback.
    he.expect(
        out_attempt(
            Id::from(5),
            V6_ADDR_2.into(),
            CUSTOM_PORT,
            ConnectionAttemptHttpVersions::H3,
        ),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    he.expect_connection_attempts(
        [
            out_attempt(
                Id::from(6),
                V4_ADDR_2.into(),
                CUSTOM_PORT,
                ConnectionAttemptHttpVersions::H3,
            ),
            out_attempt(
                Id::from(7),
                V6_ADDR.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2OrH1,
            ),
            out_attempt(
                Id::from(8),
                V4_ADDR.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2OrH1,
            ),
        ],
        &mut now,
    );
}

/// An alt-svc host that is already an IP literal needs no DNS resolution: it is
/// attempted directly, ahead of the plain origin fallback.
#[test]
fn alt_svc_host_ip_literal() {
    let config = NetworkConfig {
        alt_svc: vec![AltSvc {
            host: Some(V4_ADDR_2.to_string()),
            port: Some(CUSTOM_PORT),
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let (mut now, mut he) = setup_with_config(config);

    // No extra DNS query is issued for the IP-literal alt-svc host.
    expect_initial_dns_queries(&mut he, now);
    he.expect_idle(now);

    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_https_negative(Id::from(0)), now);

    // Alt-svc host (the IP literal) first, then origin fallback.
    he.expect(
        out_attempt(
            Id::from(3),
            V4_ADDR_2.into(),
            CUSTOM_PORT,
            ConnectionAttemptHttpVersions::H3,
        ),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    he.expect_connection_attempts(
        [
            out_attempt(
                Id::from(4),
                V6_ADDR.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2OrH1,
            ),
            out_attempt(
                Id::from(5),
                V4_ADDR.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2OrH1,
            ),
        ],
        &mut now,
    );
}

/// On a single-stack network, an alt-svc host that is an IP literal of the
/// disabled address family is not attempted: only the origin fallback is.
#[test]
fn alt_svc_host_ip_literal_of_disabled_family() {
    let config = NetworkConfig {
        ip: IpPreference::Ipv4Only,
        alt_svc: vec![AltSvc {
            host: Some(V6_ADDR_2.to_string()),
            port: Some(CUSTOM_PORT),
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let (mut now, mut he) = setup_with_config(config);

    he.expect(out_send_dns_https(Id::from(0)), now);
    he.expect(out_send_dns_a(Id::from(1)), now);
    he.expect_idle(now);

    he.input(in_dns_a_positive(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_https_negative(Id::from(0)), now);

    he.expect(out_attempt_v4_h1_h2(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);
    he.expect_connection_attempts([], &mut now);
}

/// An IP-literal origin combined with an alt-svc host that is a domain: the
/// origin IP is attempted immediately while the alt-svc host is resolved in the
/// background and attempted once its addresses arrive.
#[test]
fn ip_host_alt_svc_with_host() {
    const ALT_HOST: &str = "alt.example.com";
    let mut now = Instant::now();
    let config = NetworkConfig {
        alt_svc: vec![AltSvc {
            host: Some(ALT_HOST.to_string()),
            port: Some(CUSTOM_PORT),
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let mut he =
        HappyEyeballs::new_with_network_config(&V4_ADDR.to_string(), PORT, config).unwrap();

    // The origin IP is attempted right away (no DNS needed for it)...
    he.expect(
        out_attempt(
            Id::from(0),
            V4_ADDR.into(),
            PORT,
            ConnectionAttemptHttpVersions::H2OrH1,
        ),
        now,
    );
    // ...while the alt-svc host still needs to be resolved.
    he.expect(
        out_send_dns(Id::from(1), ALT_HOST, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_send_dns(Id::from(2), ALT_HOST, DnsRecordType::A), now);
    he.expect(out_connection_attempt_delay(), now);

    he.input(
        Input::DnsResult {
            id: Id::from(1),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.input(
        Input::DnsResult {
            id: Id::from(2),
            result: DnsResult::A(Ok(vec![V4_ADDR_2])),
            stale: false,
        },
        now,
    );

    he.expect_connection_attempts(
        [
            out_attempt(
                Id::from(3),
                V6_ADDR_2.into(),
                CUSTOM_PORT,
                ConnectionAttemptHttpVersions::H3,
            ),
            out_attempt(
                Id::from(4),
                V4_ADDR_2.into(),
                CUSTOM_PORT,
                ConnectionAttemptHttpVersions::H3,
            ),
        ],
        &mut now,
    );
}

/// When the alt-svc host fails to resolve (negative AAAA and A), the state
/// machine must fall back to the origin endpoints rather than stall, and
/// eventually report failure once those also fail.
#[test]
fn alt_svc_host_resolution_fails() {
    const ALT_HOST: &str = "alt.example.com";
    let config = NetworkConfig {
        alt_svc: vec![AltSvc {
            host: Some(ALT_HOST.to_string()),
            port: Some(CUSTOM_PORT),
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let (mut now, mut he) = setup_with_config(config);

    // Origin queries (ids 0-2), then the alt-svc host's AAAA/A queries (ids 3-4).
    expect_initial_dns_queries(&mut he, now);
    he.expect(
        out_send_dns(Id::from(3), ALT_HOST, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_send_dns(Id::from(4), ALT_HOST, DnsRecordType::A), now);
    he.expect_idle(now);

    // Origin resolves, but the alt-svc host's AAAA and A both come back empty.
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(3),
            result: DnsResult::Aaaa(Err(())),
            stale: false,
        },
        now,
    );
    he.expect(out_resolution_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::A(Err(())),
            stale: false,
        },
        now,
    );
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_https_negative(Id::from(0)), now);

    // The alt-svc host yielded no addresses, so only the origin fallback
    // endpoints are attempted (no stall, no alt-svc endpoints).
    he.expect(
        out_attempt(
            Id::from(5),
            V6_ADDR.into(),
            PORT,
            ConnectionAttemptHttpVersions::H2OrH1,
        ),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    he.expect_connection_attempts(
        [out_attempt(
            Id::from(6),
            V4_ADDR.into(),
            PORT,
            ConnectionAttemptHttpVersions::H2OrH1,
        )],
        &mut now,
    );

    // Both origin attempts fail -> the machine reports failure rather than
    // waiting on the dead alt-svc host.
    he.input(in_connection_result_negative(Id::from(5)), now);
    he.expect_idle(now);
    he.input(in_connection_result_negative(Id::from(6)), now);
    he.expect(Output::Failed(FailureReason::Connection), now);
}

/// Custom resolution and connection attempt delays should be respected by
/// the state machine instead of the default constants.
#[test]
fn custom_delays() {
    let custom_resolution_delay = Duration::from_millis(10);
    let custom_connection_attempt_delay = Duration::from_millis(50);

    let (mut now, mut he) = setup_with_config(NetworkConfig {
        resolution_delay: custom_resolution_delay,
        connection_attempt_delay: custom_connection_attempt_delay,
        ..NetworkConfig::default()
    });

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    // Should use the custom resolution delay, not the default 50ms.
    he.expect(
        Output::Timer {
            duration: custom_resolution_delay,
        },
        now,
    );

    now += custom_resolution_delay;

    he.expect(out_attempt_v4_h1_h2(Id::from(3)), now);
    // Should use the custom connection attempt delay, not the default 250ms.
    he.expect(
        Output::Timer {
            duration: custom_connection_attempt_delay,
        },
        now,
    );
}

/// With `wait_for_preferred_address` disabled, the non-preferred family (A)
/// plus an HTTPS answer is enough to move on, without waiting out the
/// resolution delay for the preferred family (AAAA).
#[test]
fn skip_wait_for_preferred_address() {
    let (now, mut he) = setup_with_config(NetworkConfig {
        wait_for_preferred_address: false,
        ..NetworkConfig::default()
    });

    expect_initial_dns_queries(&mut he, now);
    // HTTPS answer arrives, then A. AAAA (the preferred family) is still
    // outstanding, but with the flag disabled we move on immediately instead of
    // arming the resolution-delay timer.
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_attempt_v4_h1_h2(Id::from(3)), now);
    he.expect(out_connection_attempt_delay(), now);
}

/// Symmetric to [`skip_wait_for_preferred_address`] but with
/// `DualStackPreferV4`, so the preferred family is A and the non-preferred AAAA
/// arrives first. With the flag disabled, the AAAA answer plus an HTTPS answer
/// is enough to move on, without waiting out the resolution delay for the
/// preferred family (A).
#[test]
fn skip_wait_for_preferred_address_v4_preferred() {
    let (now, mut he) = setup_with_config(NetworkConfig {
        ip: IpPreference::DualStackPreferV4,
        wait_for_preferred_address: false,
        ..NetworkConfig::default()
    });

    expect_initial_dns_queries(&mut he, now);
    // HTTPS answer arrives, then AAAA. A (the preferred family) is still
    // outstanding, but with the flag disabled we move on immediately instead of
    // arming the resolution-delay timer.
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.expect(out_connection_attempt_delay(), now);
}

/// `wait_for_preferred_address` only drops the wait for the preferred family.
/// The resolution delay still applies while the HTTPS answer is outstanding.
#[test]
fn skip_wait_for_preferred_address_still_waits_for_https() {
    let (now, mut he) = setup_with_config(NetworkConfig {
        wait_for_preferred_address: false,
        ..NetworkConfig::default()
    });

    expect_initial_dns_queries(&mut he, now);
    // Only A has arrived; HTTPS and AAAA are still outstanding. Without the
    // HTTPS answer we must not move on, so the resolution-delay timer is armed.
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
}

/// Config with `version` disabled in `http_versions` and present as the sole alt-svc entry.
fn alt_svc_disabled_config(version: HttpVersion) -> NetworkConfig {
    let http_versions = match version {
        HttpVersion::H3 => HttpVersions {
            h3: false,
            ..Default::default()
        },
        HttpVersion::H2 => HttpVersions {
            h2: false,
            ..Default::default()
        },
        HttpVersion::H1 => HttpVersions {
            h1: false,
            ..Default::default()
        },
    };
    NetworkConfig {
        http_versions,
        alt_svc: vec![AltSvc {
            host: None,
            port: None,
            http_version: version,
        }],
        ..NetworkConfig::default()
    }
}

fn assert_alt_svc_version_disabled(
    version: HttpVersion,
    expected_fallback: ConnectionAttemptHttpVersions,
) {
    let now = Instant::now();
    let mut he = HappyEyeballs::new_with_network_config(
        &V4_ADDR.to_string(),
        PORT,
        alt_svc_disabled_config(version),
    )
    .expect("valid host");
    he.expect(
        out_attempt(Id::from(0), V4_ADDR.into(), PORT, expected_fallback),
        now,
    );
}

/// Alt-svc H2 entry is filtered out when H2 is disabled in the network config.
#[test]
fn alt_svc_h2_disabled() {
    assert_alt_svc_version_disabled(HttpVersion::H2, ConnectionAttemptHttpVersions::H1);
}

/// Alt-svc H1 entry is filtered out when H1 is disabled in the network config.
#[test]
fn alt_svc_h1_disabled() {
    assert_alt_svc_version_disabled(HttpVersion::H1, ConnectionAttemptHttpVersions::H2);
}

/// With several IPv6 addresses, a single IPv4 address and an HTTP/3 alt-svc,
/// the QUIC/IPv6 attempts must not be exhausted before IPv4 (a different
/// address family) or TCP (a different protocol variant) alternatives are
/// tried. The round-robin deals one endpoint from each group per round:
///   V6:H3, V4:H3, V6:H2OrH1, V4:H2OrH1, `V6_2:H3`, `V6_2:H2OrH1`, `V6_3:H3`, `V6_3:H2OrH1`
#[test]
fn interleaves_protocol_variants_and_address_families() {
    let config = NetworkConfig {
        alt_svc: vec![AltSvc {
            host: None,
            port: None,
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let (mut now, mut he) = setup_with_config(config);

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(1),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR, V6_ADDR_2, V6_ADDR_3])),
            stale: false,
        },
        now,
    );
    he.input(
        Input::DnsResult {
            id: Id::from(2),
            result: DnsResult::A(Ok(vec![V4_ADDR])),
            stale: false,
        },
        now,
    );

    // First attempt: the most preferred option, QUIC over IPv6.
    he.expect(out_attempt_v6_h3(Id::from(3)), now);
    he.expect(out_connection_attempt_delay(), now);

    // The first round deals one endpoint from each group: IPv4 QUIC, then IPv6
    // TCP, then IPv4 TCP, before the second round returns to the remaining IPv6
    // addresses.
    he.expect_connection_attempts(
        [
            out_attempt_v4_h3(Id::from(4)),
            out_attempt_v6_h1_h2(Id::from(5)),
            out_attempt_v4_h1_h2(Id::from(6)),
            out_attempt(
                Id::from(7),
                V6_ADDR_2.into(),
                PORT,
                ConnectionAttemptHttpVersions::H3,
            ),
            out_attempt(
                Id::from(8),
                V6_ADDR_2.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2OrH1,
            ),
            out_attempt(
                Id::from(9),
                V6_ADDR_3.into(),
                PORT,
                ConnectionAttemptHttpVersions::H3,
            ),
            out_attempt(
                Id::from(10),
                V6_ADDR_3.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2OrH1,
            ),
        ],
        &mut now,
    );
}

/// Same interleaving with IPv4 preferred: the preferred family flips, so every
/// round leads with IPv4 instead of IPv6.
///   V4:H3, V6:H3, V4:H2OrH1, V6:H2OrH1, `V4_2:H3`, `V4_2:H2OrH1`
#[test]
fn interleaves_with_ipv4_preferred() {
    let config = NetworkConfig {
        ip: IpPreference::DualStackPreferV4,
        alt_svc: vec![AltSvc {
            host: None,
            port: None,
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let (mut now, mut he) = setup_with_config(config);

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(1),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR])),
            stale: false,
        },
        now,
    );
    he.input(
        Input::DnsResult {
            id: Id::from(2),
            result: DnsResult::A(Ok(vec![V4_ADDR, V4_ADDR_2])),
            stale: false,
        },
        now,
    );

    // First attempt: the most preferred option, now QUIC over IPv4.
    he.expect(out_attempt_v4_h3(Id::from(3)), now);
    he.expect(out_connection_attempt_delay(), now);

    he.expect_connection_attempts(
        [
            out_attempt_v6_h3(Id::from(4)),
            out_attempt_v4_h1_h2(Id::from(5)),
            out_attempt_v6_h1_h2(Id::from(6)),
            out_attempt(
                Id::from(7),
                V4_ADDR_2.into(),
                PORT,
                ConnectionAttemptHttpVersions::H3,
            ),
            out_attempt(
                Id::from(8),
                V4_ADDR_2.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2OrH1,
            ),
        ],
        &mut now,
    );
}
