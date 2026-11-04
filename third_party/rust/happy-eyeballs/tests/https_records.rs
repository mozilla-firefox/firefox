#![allow(clippy::disallowed_methods, reason = "tests may use Instant::now")]

/// Tests for HTTPS/SVCB DNS record handling including ECH, port `SvcParams`,
/// multiple `ServiceInfo` records, and SVC1 target name resolution.
mod common;
use common::*;

use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr};

use happy_eyeballs::{
    AltSvc, CONNECTION_ATTEMPT_DELAY, ConnectionAttemptHttpVersions, ConnectionResult,
    DnsRecordType, DnsResult, EchConfig, Endpoint, EndpointTarget, FailureReason, HttpVersion, Id,
    Input, IpPreference, NetworkConfig, Output,
};

#[test]
fn ech_config_propagated_to_endpoint() {
    let (now, mut he) = setup();

    // HTTPS arrives with an ECH config and a v6 hint while AAAA and A are
    // still in-flight. The hint is used right away, and the ECH config must be
    // carried onto the endpoint.
    expect_initial_dns_queries(&mut he, now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H3, HttpVersion::H2])
                    .ipv6_hints(vec![V6_ADDR])
                    .ech(),
            ])),
            stale: false,
        },
        now,
    );
    he.expect(
        Output::AttemptConnection {
            id: Id::from(3),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H3,
                ech_config: Some(ech_config()),
            },
            is_ech_retry: false,
        },
        now,
    );
}

/// A negative A/AAAA answer does NOT discard the HTTPS IP hint: the hint is
/// kept and tried as a fallback alongside the other family's real address. The
/// preferred family is ordered first, so the hint of the (negative) preferred
/// family is tried ahead of the other family's real address, then the two are
/// interleaved by protocol, and finally the origin fallback for the resolved
/// family.
///
/// Tested for both preferences (prefer-V6 with AAAA negative, prefer-V4 with
/// A negative) to verify symmetry.
#[test]
fn hints_kept_as_fallback_on_negative_answer() {
    struct Case {
        config: NetworkConfig,
        /// Non-preferred family, returns positive — arrives first.
        first_arrives: Input,
        /// Preferred family, returns negative — arrives second.
        second_arrives: Input,
        ipv6_hints: Vec<Ipv6Addr>,
        ipv4_hints: Vec<Ipv4Addr>,
        attempt_1: Output,
        rest: Vec<Output>,
    }

    let cases = vec![
        // Prefer V6: AAAA negative, A positive. The V6 hint is kept and, being
        // the preferred family, is tried first; the real V4 address follows.
        Case {
            config: NetworkConfig::default(),
            first_arrives: in_dns_a_positive(Id::from(2)),
            second_arrives: in_dns_aaaa_negative(Id::from(1)),
            ipv6_hints: vec![V6_ADDR],
            ipv4_hints: vec![],
            attempt_1: out_attempt_v6_h3(Id::from(3)),
            rest: vec![
                out_attempt_v4_h3(Id::from(4)),
                out_attempt_v6_h2(Id::from(5)),
                out_attempt_v4_h2(Id::from(6)),
                out_attempt_v4_h1_h2(Id::from(7)),
            ],
        },
        // Prefer V4: A negative, AAAA positive. The V4 hint is kept and tried
        // first; the real V6 address follows.
        Case {
            config: NetworkConfig {
                ip: IpPreference::DualStackPreferV4,
                ..NetworkConfig::default()
            },
            first_arrives: in_dns_aaaa_positive(Id::from(1)),
            second_arrives: in_dns_a_negative(Id::from(2)),
            ipv6_hints: vec![],
            ipv4_hints: vec![V4_ADDR],
            attempt_1: out_attempt_v4_h3(Id::from(3)),
            rest: vec![
                out_attempt_v6_h3(Id::from(4)),
                out_attempt_v4_h2(Id::from(5)),
                out_attempt_v6_h2(Id::from(6)),
                out_attempt_v6_h1_h2(Id::from(7)),
            ],
        },
    ];

    for case in cases {
        let (mut now, mut he) = setup_with_config(case.config);

        expect_initial_dns_queries(&mut he, now);
        he.input(case.first_arrives, now);
        he.expect(out_resolution_delay(), now);
        he.input(case.second_arrives, now);
        he.expect(out_resolution_delay(), now);
        he.input(
            Input::DnsResult {
                id: Id::from(0),
                result: DnsResult::Https(Ok(vec![
                    service_info(1, HOSTNAME, &[HttpVersion::H3, HttpVersion::H2])
                        .ipv6_hints(case.ipv6_hints)
                        .ipv4_hints(case.ipv4_hints),
                ])),
                stale: false,
            },
            now,
        );
        he.expect(case.attempt_1, now);

        he.expect_connection_attempts(case.rest, &mut now);
    }
}

/// When the real A/AAAA records arrive with addresses and the HTTPS record also
/// carries an IP hint, both are used: the real addresses are tried first and
/// the hint is tried after them as a fallback, rather than the hint being
/// dropped.
///
/// AAAA resolves to `V6_ADDR`, then the HTTPS record arrives advertising a
/// different v6 hint (`V6_ADDR_2`). Expect the real address first, then the
/// hint.
#[test]
fn hints_kept_as_fallback_after_address_records() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H2]).ipv6_hints(vec![V6_ADDR_2]),
            ])),
            stale: false,
        },
        now,
    );

    // The real address is attempted first.
    he.expect(
        out_attempt(
            Id::from(3),
            V6_ADDR.into(),
            PORT,
            ConnectionAttemptHttpVersions::H2,
        ),
        now,
    );

    // The hint is then attempted as a fallback, after the connection attempt
    // delay.
    now += CONNECTION_ATTEMPT_DELAY;
    he.expect(
        out_attempt(
            Id::from(4),
            V6_ADDR_2.into(),
            PORT,
            ConnectionAttemptHttpVersions::H2,
        ),
        now,
    );
}

/// When the HTTPS record's IP hint is the same address the A/AAAA query
/// resolves to, the duplicate endpoint is dropped at the attempt layer (the
/// `already_attempted` check in `connection_attempt`), so the address is not
/// connected to twice.
///
/// AAAA resolves to `V6_ADDR`, then the HTTPS record arrives with that same
/// `V6_ADDR` as its v6 hint.
/// Only one `V6_ADDR`/H2 attempt is made (the hint copy is skipped), followed
/// by the origin fallback at `H2OrH1`.
#[test]
fn hint_equal_to_resolved_address_is_not_attempted_twice() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H2]).ipv6_hints(vec![V6_ADDR]),
            ])),
            stale: false,
        },
        now,
    );

    // A single V6/H2 attempt: the hint is deduplicated against the resolved
    // address rather than being attempted a second time.
    he.expect(
        out_attempt(
            Id::from(3),
            V6_ADDR.into(),
            PORT,
            ConnectionAttemptHttpVersions::H2,
        ),
        now,
    );

    // The next attempt is the origin fallback (H2OrH1), not a repeat of V6/H2.
    now += CONNECTION_ATTEMPT_DELAY;
    he.expect(
        out_attempt(
            Id::from(4),
            V6_ADDR.into(),
            PORT,
            ConnectionAttemptHttpVersions::H2OrH1,
        ),
        now,
    );
}

/// When ECH is disabled in the network config, ECH configs from HTTPS records
/// are ignored: endpoints get `ech_config: None` and the origin fallback is
/// not skipped.
///
/// HTTPS record has ECH + H3 ALPN with v6 hints. AAAA positive for origin.
/// With ECH disabled:
///   - HTTPS bucket uses hints: V6:H3 (no ECH)
///   - Origin fallback is NOT skipped: V6:H2OrH1
///
/// <https://github.com/mozilla/happy-eyeballs/issues/20>
#[test]
fn ech_disabled() {
    let (mut now, mut he) = setup_with_config(NetworkConfig {
        ech: false,
        ..NetworkConfig::default()
    });

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_a_negative(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            // Only H3 in ALPN — fallback bucket uses H2OrH1 by default.
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H3])
                    .ipv6_hints(vec![V6_ADDR])
                    .ech(),
            ])),
            stale: false,
        },
        now,
    );
    // HTTPS bucket: V6:H3, but ECH stripped.
    he.expect(
        Output::AttemptConnection {
            id: Id::from(3),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H3,
                ech_config: None,
            },
            is_ech_retry: false,
        },
        now,
    );

    // Origin fallback is NOT skipped despite HTTPS record having ECH.
    he.expect_connection_attempts(
        [Output::AttemptConnection {
            id: Id::from(4),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H2OrH1,
                ech_config: None,
            },
            is_ech_retry: false,
        }],
        &mut now,
    );
}

#[test]
fn ech_config_from_https_applies_to_aaaa() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H3, HttpVersion::H2]).ech(),
            ])),
            stale: false,
        },
        now,
    );
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(
        Output::AttemptConnection {
            id: Id::from(3),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H3,
                ech_config: Some(ech_config()),
            },
            is_ech_retry: false,
        },
        now,
    );
}

#[test]
fn multiple_target_names() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    // HTTPS response with a different target name and a v6 hint. The hint is
    // attempted right away, then the target name is queried too.
    he.input(in_dns_https_positive_svc1(Id::from(0)), now);
    he.expect(
        Output::AttemptConnection {
            id: Id::from(3),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR_2.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H3,
                ech_config: None,
            },
            is_ech_retry: false,
        },
        now,
    );
    he.expect(out_send_dns_svc1(Id::from(4)), now);
}

/// Two HTTPS `ServiceInfo` records where only the first has ECH config ("partial ECH").
/// When any `ServiceInfo` has ECH, those without ECH are skipped.
/// The origin fallback is also skipped.
///
/// ```dns
/// test.partial_ech.org  HTTPS  1 svc1.example.com. alpn="h3" port=9443 ech="..."
/// test.partial_ech.org  HTTPS  2 svc2.example.com. alpn="h2" port=10443
/// ```
///
/// HOSTNAME resolves AAAA to `V6_ADDR` and A to `V4_ADDR`.
/// SVC1 resolves A to `V4_ADDR_2`. SVC2 DNS is never queried (no ECH).
///
/// Only the ECH-enabled `ServiceInfo` produces connection attempts:
///
///   priority-1 bucket (SVC1, port 9443, ech): `V4_2:H3` (alpn=h3 only)
///   priority-2 bucket (SVC2, port 10443):     skipped (no ECH, not even resolved)
///   fallback   bucket (HOSTNAME):             skipped (no ECH)
#[test]
fn partial_ech_two_service_infos() {
    const SVC1_PORT: u16 = 9443;
    const SVC2_PORT: u16 = 10443;

    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, SVC1, &[HttpVersion::H3])
                    .ech()
                    .port(SVC1_PORT),
                service_info(2, SVC2, &[HttpVersion::H2]).port(SVC2_PORT),
            ])),
            stale: false,
        },
        now,
    );
    // Only SVC1 gets DNS queries — SVC2 is skipped (no ECH)
    expect_svc1_dns_queries(&mut he, now);
    // HOSTNAME AAAA positive -> move-on criteria met, but SVC1 has no
    // addresses yet and ECH filtering skips fallback -> no attempt yet.
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
    // SVC1 AAAA negative
    he.input(in_dns_aaaa_negative(Id::from(3)), now);
    he.expect(out_resolution_delay(), now);
    // SVC1 A positive -> SVC1 bucket now has addresses, first attempt
    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::A(Ok(vec![V4_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(
        Output::AttemptConnection {
            id: Id::from(5),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V4_ADDR_2.into(), SVC1_PORT)),
                http_version: ConnectionAttemptHttpVersions::H3,
                ech_config: Some(ech_config()),
            },
            is_ech_retry: false,
        },
        now,
    );

    // SVC1 advertises only alpn=h3, so it produces a single H3 attempt; there
    // is no H2 attempt because H2 belongs to SVC2's record.
    now += CONNECTION_ATTEMPT_DELAY;
    he.expect_idle(now);
}

/// Both `ServiceInfo` records have ECH. The origin fallback is still skipped
/// because it has no ECH config.
///
/// ```dns
/// example.com  HTTPS  1 svc1.example.com. alpn="h3" port=9443 ech="..."
/// example.com  HTTPS  2 svc2.example.com. alpn="h2" port=10443 ech="..."
/// ```
///
/// HOSTNAME resolves AAAA to `V6_ADDR` and A to `V4_ADDR`.
/// SVC1 resolves A to `V4_ADDR_2`. SVC2 resolves A to `V4_ADDR`.
///
///   priority-1 bucket (SVC1, port 9443, ech):  `V4_2:H3` (alpn=h3 only)
///   priority-2 bucket (SVC2, port 10443, ech): V4:H2 (alpn=h2 only)
///   fallback   bucket (HOSTNAME):              skipped (no ECH)
#[test]
fn both_service_infos_have_ech_no_origin_fallback() {
    const SVC1_PORT: u16 = 9443;
    const SVC2_PORT: u16 = 10443;

    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, SVC1, &[HttpVersion::H3])
                    .ech()
                    .port(SVC1_PORT),
                service_info(2, SVC2, &[HttpVersion::H2])
                    .ech()
                    .port(SVC2_PORT),
            ])),
            stale: false,
        },
        now,
    );
    // Both SVC1 and SVC2 get DNS queries (both have ECH)
    expect_svc1_svc2_dns_queries(&mut he, now);
    // HOSTNAME AAAA/A positive — but fallback will be skipped (no ECH)
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
    // SVC1 AAAA negative
    he.input(in_dns_aaaa_negative(Id::from(3)), now);
    he.expect(out_resolution_delay(), now);
    // SVC1 A positive -> first attempt from SVC1 bucket
    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::A(Ok(vec![V4_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(
        Output::AttemptConnection {
            id: Id::from(7),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V4_ADDR_2.into(), SVC1_PORT)),
                http_version: ConnectionAttemptHttpVersions::H3,
                ech_config: Some(ech_config()),
            },
            is_ech_retry: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    // SVC2 AAAA negative
    he.input(in_dns_aaaa_negative(Id::from(5)), now);
    he.expect(out_connection_attempt_delay(), now);
    // SVC2 A positive
    he.input(
        Input::DnsResult {
            id: Id::from(6),
            result: DnsResult::A(Ok(vec![V4_ADDR])),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    // Both SVC1 and SVC2 produce attempts (both have ECH), each using only its
    // own record's ALPN: SVC1 is H3-only, SVC2 is H2-only. Origin fallback is
    // skipped — no ECH on the origin.
    he.expect_connection_attempts(
        [
            // priority=2 (SVC2, port 10443, ech, alpn=h2)
            Output::AttemptConnection {
                id: Id::from(8),
                endpoint: Endpoint {
                    target: EndpointTarget::Address(SocketAddr::new(V4_ADDR.into(), SVC2_PORT)),
                    http_version: ConnectionAttemptHttpVersions::H2,
                    ech_config: Some(ech_config()),
                },
                is_ech_retry: false,
            },
        ],
        &mut now,
    );
}

/// Two HTTPS records steering to different targets advertise different ALPNs:
/// the priority-1 target is h3-only, the priority-2 target is h2-only. Each
/// target's resolved addresses must be attempted with that record's own ALPN,
/// never the union of ALPNs across records. The origin fallback uses the
/// default `H2OrH1`.
///
/// ```dns
/// example.com       HTTPS 1 svc1.example.com. alpn="h3"
/// example.com       HTTPS 2 svc2.example.com. alpn="h2"
/// svc1.example.com. AAAA  2001:db8::2
/// svc2.example.com. AAAA  2001:db8::3
/// example.com       AAAA  2001:db8::1
/// ```
///
/// Expected attempts:
///   priority-1 bucket (svc1): `V6_2:H3`   (alpn=h3 only, no H2)
///   priority-2 bucket (svc2): `V6_3:H2`   (alpn=h2 only, no H3)
///   fallback   bucket:        V6:H2OrH1 (origin default)
#[test]
fn per_record_alpn_not_unioned_across_records() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, SVC1, &[HttpVersion::H3]),
                service_info(2, SVC2, &[HttpVersion::H2]),
            ])),
            stale: false,
        },
        now,
    );
    expect_svc1_svc2_dns_queries(&mut he, now);
    // svc1 (alpn=h3) AAAA arrives -> first attempt is H3-only.
    he.input(
        Input::DnsResult {
            id: Id::from(3),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(
        out_attempt(
            Id::from(7),
            V6_ADDR_2.into(),
            PORT,
            ConnectionAttemptHttpVersions::H3,
        ),
        now,
    );
    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::A(Err(())),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(5),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR_3])),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(6),
            result: DnsResult::A(Err(())),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_connection_attempt_delay(), now);
    he.input(in_dns_a_negative(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);

    he.expect_connection_attempts(
        [
            // svc2 (alpn=h2): H2-only, no spurious H3 from svc1's record.
            out_attempt(
                Id::from(8),
                V6_ADDR_3.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2,
            ),
            // origin fallback: default H2OrH1.
            out_attempt_v6_h1_h2(Id::from(9)),
        ],
        &mut now,
    );
}

/// A `ServiceMode` record with no ALPN carries no usable protocol. Assembling the
/// SVCB ALPN set -- including adding the scheme default ("http/1.1") when no
/// "alpn" is present -- is the caller's responsibility per RFC 9460 Section
/// 7.1.1, so such a record contributes no endpoints here, and in particular it
/// never inherits a sibling record's ALPN. A priority-1 record advertises
/// `alpn=h3` and a priority-2 record carries no ALPN.
///
/// ```dns
/// example.com       HTTPS 1 svc1.example.com. alpn="h3"
/// example.com       HTTPS 2 svc2.example.com.            (no alpn)
/// svc1.example.com. AAAA  2001:db8::2
/// svc2.example.com. AAAA  2001:db8::3
/// example.com       AAAA  2001:db8::1
/// ```
///
/// Expected attempts:
///   priority-1 bucket (svc1): `V6_2:H3`   (alpn=h3)
///   priority-2 bucket (svc2): none      (no alpn -> no usable protocol)
///   fallback   bucket:        V6:H2OrH1 (origin default)
#[test]
fn record_without_alpn_contributes_no_endpoints() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, SVC1, &[HttpVersion::H3]),
                service_info(2, SVC2, &[]),
            ])),
            stale: false,
        },
        now,
    );
    expect_svc1_svc2_dns_queries(&mut he, now);
    // svc1 (alpn=h3) AAAA -> first attempt is H3.
    he.input(
        Input::DnsResult {
            id: Id::from(3),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(
        out_attempt(
            Id::from(7),
            V6_ADDR_2.into(),
            PORT,
            ConnectionAttemptHttpVersions::H3,
        ),
        now,
    );
    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::A(Err(())),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(5),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR_3])),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(6),
            result: DnsResult::A(Err(())),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_connection_attempt_delay(), now);
    he.input(in_dns_a_negative(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);

    // svc2 has no ALPN, so it produces no endpoints (its resolved V6_ADDR_3 is
    // never attempted). Only svc1's H3 attempt and the origin fallback remain.
    he.expect_connection_attempts([out_attempt_v6_h1_h2(Id::from(8))], &mut now);
}

/// Partial ECH with an alt-svc record on the origin. Both alt-svc and origin
/// fallback are skipped because they carry no ECH config.
///
/// ```dns
/// example.com  HTTPS  1 svc1.example.com. alpn="h3" port=9443 ech="..."
/// example.com  HTTPS  2 svc2.example.com. alpn="h2" port=10443
/// ```
/// Alt-svc: h3 on port 8443
///
/// HOSTNAME resolves AAAA to `V6_ADDR` and A to `V4_ADDR`.
/// SVC1 resolves A to `V4_ADDR_2`.
///
///   priority-1 bucket (SVC1, port 9443, ech): `V4_2:H3` (alpn=h3 only)
///   priority-2 bucket (SVC2, port 10443):     skipped (no ECH, not resolved)
///   alt-svc    bucket (port 8443):            skipped (no ECH)
///   fallback   bucket (HOSTNAME, port 443):   skipped (no ECH)
#[test]
fn partial_ech_with_alt_svc() {
    const SVC1_PORT: u16 = 9443;
    const SVC2_PORT: u16 = 10443;
    const ALT_SVC_PORT: u16 = 8443;

    let config = NetworkConfig {
        alt_svc: vec![AltSvc {
            host: None,
            port: Some(ALT_SVC_PORT),
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let (mut now, mut he) = setup_with_config(config);

    expect_initial_dns_queries(&mut he, now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, SVC1, &[HttpVersion::H3])
                    .ech()
                    .port(SVC1_PORT),
                service_info(2, SVC2, &[HttpVersion::H2]).port(SVC2_PORT),
            ])),
            stale: false,
        },
        now,
    );
    // Only SVC1 gets DNS queries — SVC2 skipped (no ECH)
    expect_svc1_dns_queries(&mut he, now);
    // HOSTNAME AAAA/A positive
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
    // SVC1 AAAA negative
    he.input(in_dns_aaaa_negative(Id::from(3)), now);
    he.expect(out_resolution_delay(), now);
    // SVC1 A positive -> first attempt from SVC1 bucket
    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::A(Ok(vec![V4_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(
        Output::AttemptConnection {
            id: Id::from(5),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V4_ADDR_2.into(), SVC1_PORT)),
                http_version: ConnectionAttemptHttpVersions::H3,
                ech_config: Some(ech_config()),
            },
            is_ech_retry: false,
        },
        now,
    );

    // Only SVC1 (with ECH), and it advertises only alpn=h3, so a single H3
    // attempt. Alt-svc, SVC2, and fallback all skipped.
    now += CONNECTION_ATTEMPT_DELAY;
    he.expect_idle(now);
}

mod https_port_svcparam_overrides_port_for {
    use super::*;

    fn check(ipv4_hints: Vec<Ipv4Addr>) {
        let (now, mut he) = setup(); // constructed with PORT (443)

        // HTTPS arrives with port=8443 while AAAA and A are still in-flight.
        // The v6 hint is used right away; the connection attempt must use
        // 8443, not the authority port 443. IPv6 is preferred.
        expect_initial_dns_queries(&mut he, now);
        he.input(
            Input::DnsResult {
                id: Id::from(0),
                result: DnsResult::Https(Ok(vec![
                    service_info(1, HOSTNAME, &[HttpVersion::H3, HttpVersion::H2])
                        .ipv6_hints(vec![V6_ADDR])
                        .ipv4_hints(ipv4_hints)
                        .port(CUSTOM_PORT),
                ])),
                stale: false,
            },
            now,
        );
        he.expect(out_attempt_v6_h3_custom_port(Id::from(3)), now);
    }

    #[test]
    fn v6_hints() {
        check(vec![]);
    }

    /// HTTPS record with both IPv4 and IPv6 hints and a `port` `SvcParam`: both
    /// families use the overridden port.
    #[test]
    fn v4_and_v6_hints() {
        check(vec![V4_ADDR]);
    }
}

#[test]
fn https_port_svcparam_applies_to_resolved_a_and_aaaa() {
    let (now, mut he) = setup(); // constructed with PORT (443)

    expect_initial_dns_queries(&mut he, now);
    // HTTPS record with port=8443, no hints
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H3, HttpVersion::H2]).port(CUSTOM_PORT),
            ])),
            stale: false,
        },
        now,
    );
    he.expect(out_resolution_delay(), now);
    // Positive AAAA: connection attempt must use port 8443, not 443
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h3_custom_port(Id::from(3)), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);
    // Positive A: connection attempt must use port 8443, not 443
    he.input(in_connection_result_negative(Id::from(3)), now);
    he.expect(out_attempt_v4_h3_custom_port(Id::from(4)), now);
}

#[test]
fn https_port_svcparam_applies_but_fallbacks_follow() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    // HTTPS record with port=8443, no hints
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H3, HttpVersion::H2]).port(CUSTOM_PORT),
            ])),
            stale: false,
        },
        now,
    );
    he.expect(out_resolution_delay(), now);
    // Positive AAAA: connection attempt must use port 8443, not 443
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(
        Output::AttemptConnection {
            id: Id::from(3),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), CUSTOM_PORT)),
                http_version: ConnectionAttemptHttpVersions::H3,
                ech_config: None,
            },
            is_ech_retry: false,
        },
        now,
    );
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);

    // Connection attempts using custom port: V4:H3, V6:H2, V4:H2, then
    // fallback on port 443 with default HTTP versions (H2OrH1).
    he.expect_connection_attempts(
        [
            out_attempt_v4_h3_custom_port(Id::from(4)),
            out_attempt_v6_h2_custom_port(Id::from(5)),
            out_attempt_v4_h2_custom_port(Id::from(6)),
            out_attempt_v6_h1_h2(Id::from(7)),
            out_attempt_v4_h1_h2(Id::from(8)),
        ],
        &mut now,
    );
}

/// Two HTTPS `ServiceInfo` records with different priorities and `port` `SvcParams`.
///
/// ```dns
/// example.com  HTTPS  1 . alpn="h2,h3" port=20007
/// example.com  HTTPS  2 . alpn="h2,h3" port=20008
/// ```
///
/// Connection attempts are grouped by port in priority order, then the
/// authority port as a final fallback:
///
///   priority-1 bucket (port 20007): V6:H3, V4:H3, V6:H2, V4:H2
///   priority-2 bucket (port 20008): V6:H3, V4:H3, V6:H2, V4:H2
///   fallback   bucket (port   443): V6:H3, V4:H3, V6:H2, V4:H2
#[test]
fn https_two_service_infos_with_different_ports() {
    const PORT_1: u16 = 20007;
    const PORT_2: u16 = 20008;
    let (mut now, mut he) = setup(); // PORT = 443

    let attempt =
        |id: u64, addr: IpAddr, port: u16, http_version: ConnectionAttemptHttpVersions| {
            Output::AttemptConnection {
                id: Id::from(id),
                endpoint: Endpoint {
                    target: EndpointTarget::Address(SocketAddr::new(addr, port)),
                    http_version,
                    ech_config: None,
                },
                is_ech_retry: false,
            }
        };

    expect_initial_dns_queries(&mut he, now);
    // Two ServiceInfo records; the lower priority number wins first.
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H3, HttpVersion::H2]).port(PORT_1),
                service_info(2, HOSTNAME, &[HttpVersion::H3, HttpVersion::H2]).port(PORT_2),
            ])),
            stale: false,
        },
        now,
    );
    he.expect(out_resolution_delay(), now);
    // AAAA arrives; move-on criteria met. First bucket is PORT_1.
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(
        attempt(3, V6_ADDR.into(), PORT_1, ConnectionAttemptHttpVersions::H3),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);

    he.expect_connection_attempts(
        [
            // Priority-1 bucket (port 20007): V4:H3, V6:H2, V4:H2.
            attempt(4, V4_ADDR.into(), PORT_1, ConnectionAttemptHttpVersions::H3),
            attempt(5, V6_ADDR.into(), PORT_1, ConnectionAttemptHttpVersions::H2),
            attempt(6, V4_ADDR.into(), PORT_1, ConnectionAttemptHttpVersions::H2),
            // Priority-2 bucket (port 20008).
            attempt(7, V6_ADDR.into(), PORT_2, ConnectionAttemptHttpVersions::H3),
            attempt(8, V4_ADDR.into(), PORT_2, ConnectionAttemptHttpVersions::H3),
            attempt(9, V6_ADDR.into(), PORT_2, ConnectionAttemptHttpVersions::H2),
            attempt(
                10,
                V4_ADDR.into(),
                PORT_2,
                ConnectionAttemptHttpVersions::H2,
            ),
            // Fallback bucket (port 443) uses default HTTP versions.
            out_attempt_v6_h1_h2(Id::from(11)),
            out_attempt_v4_h1_h2(Id::from(12)),
        ],
        &mut now,
    );
}

/// Website with HTTPS record with `noDefaultAlpn` set.
///
/// See e.g. <adamwoodland.com>.
#[test]
fn no_default_alpn() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_positive(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h3(Id::from(3)), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);
    he.input(in_connection_result_negative(Id::from(3)), now);
    he.expect(out_attempt_v4_h3(Id::from(4)), now);
    he.input(in_connection_result_negative(Id::from(4)), now);
    he.expect(out_attempt_v6_h2(Id::from(5)), now);
    he.input(in_connection_result_negative(Id::from(5)), now);
    he.expect(out_attempt_v4_h2(Id::from(6)), now);
    // Fallback bucket with default HTTP versions (H2OrH1).
    he.input(in_connection_result_negative(Id::from(6)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(7)), now);
    he.input(in_connection_result_negative(Id::from(7)), now);
    he.expect(out_attempt_v4_h1_h2(Id::from(8)), now);
    he.input(in_connection_result_negative(Id::from(8)), now);
    he.expect(Output::Failed(FailureReason::Connection), now);
}

#[test]
fn https_svc1_addresses_trigger_additional_attempts() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H2, HttpVersion::H3]),
                service_info(2, SVC1, &[HttpVersion::H2, HttpVersion::H3]),
            ])),
            stale: false,
        },
        now,
    );
    expect_svc1_dns_queries(&mut he, now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h3(Id::from(5)), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(3),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::A(Ok(vec![V4_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    let attempt = |id: u64, addr: IpAddr, http_version: ConnectionAttemptHttpVersions| {
        Output::AttemptConnection {
            id: Id::from(id),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(addr, PORT)),
                http_version,
                ech_config: None,
            },
            is_ech_retry: false,
        }
    };

    // Addresses respect HTTPS record priority: P1 (HOSTNAME, priority=1) endpoints
    // come before P2 (SVC1, priority=2) endpoints.  V6_ADDR:H3 was already
    // attempted (id=5); the remaining follow in priority order, then fallback.
    he.expect_connection_attempts(
        [
            attempt(6, V4_ADDR.into(), ConnectionAttemptHttpVersions::H3), // priority=1
            attempt(7, V6_ADDR.into(), ConnectionAttemptHttpVersions::H2), // priority=1
            attempt(8, V4_ADDR.into(), ConnectionAttemptHttpVersions::H2), // priority=1
            attempt(9, V6_ADDR_2.into(), ConnectionAttemptHttpVersions::H3), // priority=2
            attempt(10, V4_ADDR_2.into(), ConnectionAttemptHttpVersions::H3), // priority=2
            attempt(11, V6_ADDR_2.into(), ConnectionAttemptHttpVersions::H2), // priority=2
            attempt(12, V4_ADDR_2.into(), ConnectionAttemptHttpVersions::H2), // priority=2
            // Fallback bucket with default HTTP versions (H2OrH1).
            attempt(13, V6_ADDR.into(), ConnectionAttemptHttpVersions::H2OrH1),
            attempt(14, V4_ADDR.into(), ConnectionAttemptHttpVersions::H2OrH1),
        ],
        &mut now,
    );
}

/// HTTPS record port takes precedence over alt-svc port.
///
/// HTTPS record with port=8443 and H3+H2; alt-svc with port=9443 and H3.
/// Expected order:
///   HTTPS bucket    (port 8443): V6:H3, V4:H3, V6:H2, V4:H2
///   alt-svc bucket  (port 9443): V6:H3, V4:H3
///   fallback bucket (port  443): V6:H2OrH1, V4:H2OrH1
#[test]
fn https_port_takes_precedence_over_alt_svc_port() {
    const HTTPS_PORT: u16 = 8443;
    const ALT_SVC_PORT: u16 = 9443;

    let config = NetworkConfig {
        alt_svc: vec![AltSvc {
            host: None,
            port: Some(ALT_SVC_PORT),
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let (mut now, mut he) = setup_with_config(config);

    expect_initial_dns_queries(&mut he, now);
    // HTTPS record with port=8443
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H3, HttpVersion::H2]).port(HTTPS_PORT),
            ])),
            stale: false,
        },
        now,
    );
    he.expect(out_resolution_delay(), now);
    // AAAA arrives; HTTPS bucket first (port 8443)
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(
        out_attempt(
            Id::from(3),
            V6_ADDR.into(),
            HTTPS_PORT,
            ConnectionAttemptHttpVersions::H3,
        ),
        now,
    );
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);

    he.expect_connection_attempts(
        [
            // HTTPS bucket (port 8443)
            out_attempt(
                Id::from(4),
                V4_ADDR.into(),
                HTTPS_PORT,
                ConnectionAttemptHttpVersions::H3,
            ),
            out_attempt(
                Id::from(5),
                V6_ADDR.into(),
                HTTPS_PORT,
                ConnectionAttemptHttpVersions::H2,
            ),
            out_attempt(
                Id::from(6),
                V4_ADDR.into(),
                HTTPS_PORT,
                ConnectionAttemptHttpVersions::H2,
            ),
            // Alt-svc bucket (port 9443)
            out_attempt(
                Id::from(7),
                V6_ADDR.into(),
                ALT_SVC_PORT,
                ConnectionAttemptHttpVersions::H3,
            ),
            out_attempt(
                Id::from(8),
                V4_ADDR.into(),
                ALT_SVC_PORT,
                ConnectionAttemptHttpVersions::H3,
            ),
            // Fallback bucket (port 443) uses default versions only.
            out_attempt(
                Id::from(9),
                V6_ADDR.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2OrH1,
            ),
            out_attempt(
                Id::from(10),
                V4_ADDR.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2OrH1,
            ),
        ],
        &mut now,
    );
}

/// HTTPS record redirects to a different target name (no IP hints). Addresses
/// resolved for that target name are used in connection attempts, with higher
/// priority than the origin fallback.
///
/// ```dns
/// example.com          HTTPS  1  svc1.example.com.  alpn="h3"
/// svc1.example.com.    AAAA   2001:db8::2
/// svc1.example.com.    A      192.0.2.2
/// example.com          AAAA   2001:db8::1
/// example.com          A      192.0.2.1
/// ```
///
/// Expected connection attempts:
///   SVC1 bucket (priority 1): `V6_ADDR_2:H3`, `V4_ADDR_2:H3`
///   fallback bucket (origin): V6:H2OrH1,    V4:H2OrH1
///
/// <https://github.com/mozilla/happy-eyeballs/issues/10>
#[test]
fn target_name_redirect_addresses_used_in_connection_attempts() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    // HTTPS response redirects to SVC1 (different target name, no hints)
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![service_info(1, SVC1, &[HttpVersion::H3])])),
            stale: false,
        },
        now,
    );
    // Follow-up DNS for the redirected target name
    expect_svc1_dns_queries(&mut he, now);
    // SVC1 AAAA positive → move-on criteria met, first attempt uses
    // the redirected target name's resolved address.
    he.input(
        Input::DnsResult {
            id: Id::from(3),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(
        Output::AttemptConnection {
            id: Id::from(5),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR_2.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H3,
                ech_config: None,
            },
            is_ech_retry: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    // Remaining DNS arrives while first attempt is in progress
    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::A(Ok(vec![V4_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_connection_attempt_delay(), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);

    // Remaining attempts: SVC1's V4 address, then origin fallback.
    // SVC1 (priority 1) addresses come before the origin fallback.
    he.expect_connection_attempts(
        [
            // SVC1 bucket (priority 1)
            Output::AttemptConnection {
                id: Id::from(6),
                endpoint: Endpoint {
                    target: EndpointTarget::Address(SocketAddr::new(V4_ADDR_2.into(), PORT)),
                    http_version: ConnectionAttemptHttpVersions::H3,
                    ech_config: None,
                },
                is_ech_retry: false,
            },
            // fallback bucket (origin)
            out_attempt_v6_h1_h2(Id::from(7)),
            out_attempt_v4_h1_h2(Id::from(8)),
        ],
        &mut now,
    );
}

/// HTTPS record with `alpn="h3"` and `port=8443`. The HTTPS bucket should use
/// H3 at port 8443, but the fallback bucket (origin domain, authority port)
/// must use the default HTTP versions (`H2OrH1`), not H3 which came from the
/// HTTPS record.
///
/// ```dns
/// example.com  HTTPS  1 . alpn="h3" port=8443
/// example.com  A      192.0.2.1
/// ```
///
/// Expected connection attempts:
///   HTTPS bucket (port 8443): V4:H3
///   fallback bucket (port 443): V4:H2OrH1
#[test]
fn https_fallback_uses_default_http_versions() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    // HTTPS record with port=8443, alpn=h3 only
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H3]).port(CUSTOM_PORT),
            ])),
            stale: false,
        },
        now,
    );
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_negative(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    // Positive A: connection attempt uses port 8443 with H3 from HTTPS record
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_attempt_v4_h3_custom_port(Id::from(3)), now);
    he.expect(out_connection_attempt_delay(), now);

    // Fallback on port 443 must use default H2OrH1, NOT H3.
    he.expect_connection_attempts([out_attempt_v4_h1_h2(Id::from(4))], &mut now);
}

/// When a connection attempt fails with `EchRetry`, the state machine should
/// emit a new connection attempt to the same endpoint with the new ECH config.
///
/// Setup:
///   HTTPS record with ECH config, AAAA positive.
///   First connection attempt uses original ECH config.
///   Server rejects ECH and provides `retry_configs`.
///   State machine emits a new attempt with updated ECH config.
#[test]
fn ech_retry_same_endpoint() {
    let (now, mut he) = setup();

    let new_ech_config = EchConfig::new(vec![10, 20, 30, 40, 50]);

    expect_initial_dns_queries(&mut he, now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H2]).ech(),
            ])),
            stale: false,
        },
        now,
    );
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    // First connection attempt with original ECH config.
    he.expect(
        Output::AttemptConnection {
            id: Id::from(3),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H2,
                ech_config: Some(ech_config()),
            },
            is_ech_retry: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    // Server rejects ECH and provides retry_configs.
    he.input(
        Input::ConnectionResult {
            id: Id::from(3),
            result: ConnectionResult::EchRetry(new_ech_config.clone()),
        },
        now,
    );
    // State machine emits a new attempt with the new ECH config
    // immediately (no delay — this is a server-initiated retry,
    // not a new candidate).
    he.expect(
        Output::AttemptConnection {
            id: Id::from(4),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H2,
                ech_config: Some(new_ech_config),
            },
            is_ech_retry: true,
        },
        now,
    );
}

/// `EchRetry` with an empty `EchConfig` models the `SSL_ERROR_ECH_RETRY_WITHOUT_ECH`
/// path on the consumer side (server told us to retry *without* ECH). The state
/// machine forwards the bytes verbatim, but the retry attempt must still be
/// flagged `is_ech_retry: true` so consumers can label it.
#[test]
fn ech_retry_without_ech_sets_flag() {
    let (now, mut he) = setup();

    let empty_ech_config = EchConfig::new(vec![]);

    expect_initial_dns_queries(&mut he, now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H2]).ech(),
            ])),
            stale: false,
        },
        now,
    );
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(
        Output::AttemptConnection {
            id: Id::from(3),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H2,
                ech_config: Some(ech_config()),
            },
            is_ech_retry: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    he.input(
        Input::ConnectionResult {
            id: Id::from(3),
            result: ConnectionResult::EchRetry(empty_ech_config.clone()),
        },
        now,
    );
    he.expect(
        Output::AttemptConnection {
            id: Id::from(4),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H2,
                ech_config: Some(empty_ech_config),
            },
            is_ech_retry: true,
        },
        now,
    );
}

/// Per RFC 9849 Section 6.1.6:
///
/// > Clients SHOULD NOT accept "retry_config" in response to a connection
/// > initiated in response to a "retry_config".
///
/// The state machine must ignore `EchRetry` on an ECH-retried attempt and
/// treat it as a plain failure, then fall through to remaining endpoints.
#[test]
fn ech_retry_no_infinite_loop() {
    let (now, mut he) = setup();

    let retry_ech_config = EchConfig::new(vec![10, 20, 30, 40, 50]);
    let retry_ech_config_2 = EchConfig::new(vec![60, 70, 80]);

    expect_initial_dns_queries(&mut he, now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, HOSTNAME, &[HttpVersion::H2]).ech(),
            ])),
            stale: false,
        },
        now,
    );
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(
        Output::AttemptConnection {
            id: Id::from(3),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H2,
                ech_config: Some(ech_config()),
            },
            is_ech_retry: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    // First EchRetry: accepted, new attempt emitted.
    he.input(
        Input::ConnectionResult {
            id: Id::from(3),
            result: ConnectionResult::EchRetry(retry_ech_config.clone()),
        },
        now,
    );
    he.expect(
        Output::AttemptConnection {
            id: Id::from(4),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H2,
                ech_config: Some(retry_ech_config),
            },
            is_ech_retry: true,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    // Second EchRetry on the retried attempt: ignored, treated as
    // failure. A record still pending, so resolution delay.
    he.input(
        Input::ConnectionResult {
            id: Id::from(4),
            result: ConnectionResult::EchRetry(retry_ech_config_2),
        },
        now,
    );
    he.expect(out_resolution_delay(), now);
    // A record arrives, next endpoint attempted (V4, original ECH
    // from DNS).
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(
        Output::AttemptConnection {
            id: Id::from(5),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V4_ADDR.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H2,
                ech_config: Some(ech_config()),
            },
            is_ech_retry: false,
        },
        now,
    );
}

/// RFC 9460 multi-CDN configuration (Section 10.4.4 / Appendix B). A customer
/// domain is steered across several independent CDNs via CNAME, and each CDN
/// serves its own HTTPS/SVCB records pointing at its own pools. Because Happy
/// Eyeballs v3 resolves each record's `TargetName` and connects to the
/// addresses owned by that target, the multi-CDN setup just works: there is no
/// need to compare the HTTPS record's target against the A/AAAA canonical name.
///
/// Modelled on the example where `www.customer.example` is a CNAME to one CDN
/// (`cdn1.svc1.example`), which returns:
///
/// ```dns
/// cdn1.svc1.example.    HTTPS 1 h3pool.svc1.example. alpn="h3"
/// cdn1.svc1.example.    HTTPS 2 cdn1.svc1.example.   alpn="h2"
/// h3pool.svc1.example.  AAAA  2001:db8:192:7::3
/// h3pool.svc1.example.  A     192.0.2.3
/// cdn1.svc1.example.    AAAA  2001:db8:192::4
/// cdn1.svc1.example.    A     192.0.2.2
/// ```
///
/// with the origin's own A/AAAA acting as the non-CDN fallback. Expected
/// attempt order: the priority-1 pool (`h3pool`), then the priority-2 pool
/// (`cdn1`), then the origin fallback last.
#[test]
#[expect(clippy::too_many_lines, reason = "one end-to-end scenario")]
fn rfc_multi_cdn_target_names_resolved_and_attempted() {
    const H3POOL: &str = "h3pool.svc1.example.";
    const CDN1: &str = "cdn1.svc1.example.";
    const H3POOL_V6: Ipv6Addr = Ipv6Addr::new(0x2001, 0xdb8, 0x192, 7, 0, 0, 0, 3);
    const H3POOL_V4: Ipv4Addr = Ipv4Addr::new(192, 0, 2, 3);
    const CDN1_V6: Ipv6Addr = Ipv6Addr::new(0x2001, 0xdb8, 0x192, 0, 0, 0, 0, 4);
    const CDN1_V4: Ipv4Addr = Ipv4Addr::new(192, 0, 2, 2);

    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    // The CDN the customer is CNAME'd to returns two ServiceMode records,
    // each steering to a different pool with its own target name.
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![
                service_info(1, H3POOL, &[HttpVersion::H3]),
                service_info(2, CDN1, &[HttpVersion::H2]),
            ])),
            stale: false,
        },
        now,
    );
    // Both target names are resolved on their own.
    he.expect_all(
        [
            out_send_dns(Id::from(3), H3POOL, DnsRecordType::Aaaa),
            out_send_dns(Id::from(4), H3POOL, DnsRecordType::A),
            out_send_dns(Id::from(5), CDN1, DnsRecordType::Aaaa),
            out_send_dns(Id::from(6), CDN1, DnsRecordType::A),
            out_resolution_delay(),
        ],
        now,
    );
    // h3pool (priority 1) AAAA arrives -> first attempt uses the pool's
    // own resolved address, never the origin's canonical name.
    he.input(
        Input::DnsResult {
            id: Id::from(3),
            result: DnsResult::Aaaa(Ok(vec![H3POOL_V6])),
            stale: false,
        },
        now,
    );
    he.expect(
        out_attempt(
            Id::from(7),
            H3POOL_V6.into(),
            PORT,
            ConnectionAttemptHttpVersions::H3,
        ),
        now,
    );
    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::A(Ok(vec![H3POOL_V4])),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(5),
            result: DnsResult::Aaaa(Ok(vec![CDN1_V6])),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(6),
            result: DnsResult::A(Ok(vec![CDN1_V4])),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    // Origin A/AAAA: the non-CDN fallback addresses.
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_connection_attempt_delay(), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);

    // Remaining attempts: the rest of the priority-1 pool, then the priority-2
    // pool, then the origin fallback last.
    he.expect_connection_attempts(
        [
            // h3pool pool (priority 1): alpn="h3" -> H3 only.
            out_attempt(
                Id::from(8),
                H3POOL_V4.into(),
                PORT,
                ConnectionAttemptHttpVersions::H3,
            ),
            // cdn1 pool (priority 2): alpn="h2" -> H2 only.
            out_attempt(
                Id::from(9),
                CDN1_V6.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2,
            ),
            out_attempt(
                Id::from(10),
                CDN1_V4.into(),
                PORT,
                ConnectionAttemptHttpVersions::H2,
            ),
            // origin fallback (non-CDN) last
            out_attempt_v6_h1_h2(Id::from(11)),
            out_attempt_v4_h1_h2(Id::from(12)),
        ],
        &mut now,
    );
}
