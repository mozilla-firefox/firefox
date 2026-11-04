//! By-name connection modes: [`NetworkConfig::resolution`] set to
//! [`ResolutionMode::Name`] or [`ResolutionMode::NameWithHttpsRr`].
//!
//! When the host should not (or cannot) be resolved client-side (e.g. a proxy
//! resolves it for us, or an inner proxy connection is being established), the
//! state machine attempts by name instead of racing IPs. `Name` skips DNS
//! entirely; `NameWithHttpsRr` still fetches the origin HTTPS record for its
//! ALPN (so h3 is attempted when advertised) but never queries A/AAAA.

#![allow(clippy::disallowed_methods, reason = "tests may use Instant::now")]

mod common;
use common::*;

use std::time::Instant;

use happy_eyeballs::{
    ConnectionAttemptHttpVersions, DnsRecordType, Endpoint, EndpointTarget, FailureReason,
    HappyEyeballs, HttpVersions, Id, NetworkConfig, Output, ResolutionMode,
};

fn by_name_config() -> NetworkConfig {
    NetworkConfig {
        resolution: ResolutionMode::Name,
        ..NetworkConfig::default()
    }
}

fn by_name_https_rr_config() -> NetworkConfig {
    NetworkConfig {
        resolution: ResolutionMode::NameWithHttpsRr,
        ..NetworkConfig::default()
    }
}

fn out_attempt_by_name(id: Id, http_version: ConnectionAttemptHttpVersions) -> Output {
    Output::AttemptConnection {
        id,
        endpoint: Endpoint {
            target: EndpointTarget::Name {
                host: HOSTNAME.to_string(),
                port: PORT,
            },
            http_version,
            ech_config: None,
        },
        is_ech_retry: false,
    }
}

/// In `Name` mode the machine emits no DNS query at all and immediately
/// yields a by-name connection attempt over the default H2/H1 versions.
#[test]
fn by_name_no_dns_query_and_by_name_attempt() {
    let now = Instant::now();
    let mut he = HappyEyeballs::new_with_network_config(HOSTNAME, PORT, by_name_config()).unwrap();

    // First output is the by-name attempt, not a SendDnsQuery.
    he.expect(
        out_attempt_by_name(Id::from(0), ConnectionAttemptHttpVersions::H2OrH1),
        now,
    );
    // The attempt carries no resolved address.
    let attempt = out_attempt_by_name(Id::from(0), ConnectionAttemptHttpVersions::H2OrH1)
        .attempt()
        .unwrap();
    assert_eq!(attempt.address(), None);

    // The only follow-up is the connection-attempt delay timer while the
    // attempt is in flight. No DNS query is ever emitted.
    he.expect(out_connection_attempt_delay(), now);
}

/// A successful by-name attempt is reported as `Succeeded`.
#[test]
fn by_name_success() {
    let now = Instant::now();
    let mut he = HappyEyeballs::new_with_network_config(HOSTNAME, PORT, by_name_config()).unwrap();

    he.expect(
        out_attempt_by_name(Id::from(0), ConnectionAttemptHttpVersions::H2OrH1),
        now,
    );
    he.input(in_connection_result_positive(Id::from(0)), now);
    he.expect(Output::Succeeded, now);
}

/// When the single by-name attempt fails and there is nothing left to try, the
/// machine reports a connection failure (never a DNS-resolution failure, since
/// no DNS was attempted).
#[test]
fn by_name_failure() {
    let now = Instant::now();
    let mut he = HappyEyeballs::new_with_network_config(HOSTNAME, PORT, by_name_config()).unwrap();

    he.expect(
        out_attempt_by_name(Id::from(0), ConnectionAttemptHttpVersions::H2OrH1),
        now,
    );
    he.input(in_connection_result_negative(Id::from(0)), now);
    he.expect(Output::Failed(FailureReason::Connection), now);
}

/// The enabled HTTP versions are respected: with only HTTP/1.1 enabled the
/// by-name attempt uses H1.
#[test]
fn by_name_respects_http_versions() {
    let now = Instant::now();
    let config = NetworkConfig {
        resolution: ResolutionMode::Name,
        http_versions: HttpVersions {
            h1: true,
            h2: false,
            h3: false,
        },
        ..NetworkConfig::default()
    };
    let mut he = HappyEyeballs::new_with_network_config(HOSTNAME, PORT, config).unwrap();

    he.expect(
        out_attempt_by_name(Id::from(0), ConnectionAttemptHttpVersions::H1),
        now,
    );
}

/// Even with HTTP/3 enabled, a plain `Name` attempt never offers H3: without
/// an HTTPS record proving H3 support the machine falls back to H2/H1,
/// mirroring how it treats an IP-literal host.
#[test]
fn by_name_does_not_offer_h3() {
    let now = Instant::now();
    let mut he = HappyEyeballs::new_with_network_config(HOSTNAME, PORT, by_name_config()).unwrap();

    let attempt = he.process_output(now).unwrap().attempt().unwrap();
    assert_eq!(attempt.http_version, ConnectionAttemptHttpVersions::H2OrH1);
}

/// `Name` mode attempts a domain alt-svc target by name, over its advertised
/// protocol: an h3 alt-svc is raced over h3 (with no `SendDnsQuery` for it),
/// ahead of the by-name origin H2/H1 fallback.
#[test]
fn by_name_attempts_alt_svc_by_name_over_h3() {
    use happy_eyeballs::{AltSvc, HttpVersion};

    let mut now = Instant::now();
    let config = NetworkConfig {
        resolution: ResolutionMode::Name,
        alt_svc: vec![AltSvc {
            host: Some("alt.example.com".to_string()),
            port: None,
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let mut he = HappyEyeballs::new_with_network_config(HOSTNAME, PORT, config).unwrap();

    // First attempt: the alt-svc target, by name, over h3. It is preferred over
    // the by-name origin H2/H1 fallback, and no DNS query is emitted for it.
    he.expect(
        Output::AttemptConnection {
            id: Id::from(0),
            endpoint: Endpoint {
                target: EndpointTarget::Name {
                    host: "alt.example.com".to_string(),
                    port: PORT,
                },
                http_version: ConnectionAttemptHttpVersions::H3,
                ech_config: None,
            },
            is_ech_retry: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    // After the connection attempt delay fires, fall back to the non-alt-svc
    // origin, still by name, over H2/H1. Nothing remains to try after that.
    he.expect_connection_attempts(
        [out_attempt_by_name(
            Id::from(1),
            ConnectionAttemptHttpVersions::H2OrH1,
        )],
        &mut now,
    );
}

/// `NameWithHttpsRr` sends exactly one DNS query, the origin HTTPS record, and
/// never an A or AAAA query. Before the answer arrives it emits nothing else:
/// it waits for the HTTPS answer rather than racing addresses.
#[test]
fn https_rr_only_sends_https_query() {
    let now = Instant::now();
    let mut he =
        HappyEyeballs::new_with_network_config(HOSTNAME, PORT, by_name_https_rr_config()).unwrap();

    // The one and only DNS query: HTTPS for the origin. No AAAA/A burst follows.
    he.expect(
        out_send_dns(Id::from(0), HOSTNAME, DnsRecordType::Https),
        now,
    );
    he.expect_idle(now);

    // Feeding the HTTPS answer unblocks the by-name attempts; still no A/AAAA.
    he.input(in_dns_https_positive(Id::from(0)), now);
    let attempt = he.process_output(now).unwrap().attempt().unwrap();
    assert!(matches!(attempt.target, EndpointTarget::Name { .. }));
    // No further DNS query is ever produced.
    he.expect(out_connection_attempt_delay(), now);
}

/// An HTTPS record advertising h3 yields a by-name h3 attempt to the origin at
/// the origin port. The ALPN is honored (h3 first), and the endpoint carries no
/// resolved address.
#[test]
fn https_rr_h3_record_yields_by_name_h3() {
    let now = Instant::now();
    let mut he =
        HappyEyeballs::new_with_network_config(HOSTNAME, PORT, by_name_https_rr_config()).unwrap();

    he.expect(
        out_send_dns(Id::from(0), HOSTNAME, DnsRecordType::Https),
        now,
    );
    // The record advertises h3 (and h2); the first by-name attempt is over h3.
    he.input(in_dns_https_positive(Id::from(0)), now);
    he.expect(
        out_attempt_by_name(Id::from(1), ConnectionAttemptHttpVersions::H3),
        now,
    );

    let attempt = out_attempt_by_name(Id::from(1), ConnectionAttemptHttpVersions::H3)
        .attempt()
        .unwrap();
    assert_eq!(attempt.address(), None);
}

/// A negative HTTPS answer (as on failure or timeout) falls back to the by-name
/// origin over the enabled H2/H1 versions.
#[test]
fn https_rr_negative_falls_back_to_by_name_h2_h1() {
    let now = Instant::now();
    let mut he =
        HappyEyeballs::new_with_network_config(HOSTNAME, PORT, by_name_https_rr_config()).unwrap();

    he.expect(
        out_send_dns(Id::from(0), HOSTNAME, DnsRecordType::Https),
        now,
    );
    he.input(in_dns_https_negative(Id::from(0)), now);

    // No HTTPS endpoints, so the by-name origin H2/H1 fallback is used.
    he.expect(
        out_attempt_by_name(Id::from(1), ConnectionAttemptHttpVersions::H2OrH1),
        now,
    );
}

/// An empty (NODATA) HTTPS answer, like a negative one, falls back to the
/// by-name origin over H2/H1.
#[test]
fn https_rr_empty_falls_back_to_by_name_h2_h1() {
    let now = Instant::now();
    let mut he =
        HappyEyeballs::new_with_network_config(HOSTNAME, PORT, by_name_https_rr_config()).unwrap();

    he.expect(
        out_send_dns(Id::from(0), HOSTNAME, DnsRecordType::Https),
        now,
    );
    he.input(in_dns_https_positive_no_alpn(Id::from(0)), now);

    // A record with no usable ALPN yields no HTTPS endpoints, so the by-name
    // origin H2/H1 fallback is used.
    he.expect(
        out_attempt_by_name(Id::from(1), ConnectionAttemptHttpVersions::H2OrH1),
        now,
    );
}

/// Regression: with the default [`ResolutionMode::Ip`], a domain host still
/// opens the normal DNS phase, emitting HTTPS/AAAA/A queries before any
/// connection attempt.
#[test]
fn default_still_resolves() {
    let now = Instant::now();
    let mut he = HappyEyeballs::new(HOSTNAME, PORT).unwrap();

    expect_initial_dns_queries(&mut he, now);

    // Feeding a normal DNS flow yields an address-keyed attempt, exactly as
    // before this feature existed.
    he.input(in_dns_https_positive(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    let attempt = he.process_output(now).unwrap().attempt().unwrap();
    assert!(matches!(attempt.target, EndpointTarget::Address(_)));
    assert!(attempt.address().is_some());
}

/// A record's `TargetName` is a DNS name, but a by-name attempt needs a connect
/// host: the root label's trailing dot is dropped (it is invalid as SNI and is
/// rejected by proxies), and a `ServiceMode` target of "." denotes the owner name,
/// i.e. the origin.
#[test]
fn https_rr_by_name_host_is_a_connect_name() {
    use happy_eyeballs::{DnsResult, HttpVersion, Input};

    for (target, expected) in [(SVC1, "svc1.example.com"), (".", HOSTNAME)] {
        let now = Instant::now();
        let mut he =
            HappyEyeballs::new_with_network_config(HOSTNAME, PORT, by_name_https_rr_config())
                .unwrap();

        he.expect(
            out_send_dns(Id::from(0), HOSTNAME, DnsRecordType::Https),
            now,
        );
        he.input(
            Input::DnsResult {
                id: Id::from(0),
                result: DnsResult::Https(Ok(vec![service_info(1, target, &[HttpVersion::H3])])),
                stale: false,
            },
            now,
        );

        let attempt = he.process_output(now).unwrap().attempt().unwrap();
        assert_eq!(
            attempt.target,
            EndpointTarget::Name {
                host: expected.to_string(),
                port: PORT,
            },
            "target name {target:?} should connect to {expected:?}"
        );
    }
}

/// `Name` performs no DNS at all, so a failure can never be a resolution
/// failure. With no usable HTTP version there is nothing to attempt, and the
/// machine reports a connection failure rather than a DNS one.
#[test]
fn by_name_failure_is_never_dns_resolution() {
    let now = Instant::now();
    let config = NetworkConfig {
        resolution: ResolutionMode::Name,
        http_versions: HttpVersions {
            h1: false,
            h2: false,
            h3: true,
        },
        ..NetworkConfig::default()
    };
    let mut he = HappyEyeballs::new_with_network_config(HOSTNAME, PORT, config).unwrap();

    he.expect(Output::Failed(FailureReason::Connection), now);
}

/// An origin may legitimately be given fully qualified ("example.com."), but a
/// connect name must not carry the root label's trailing dot. That holds for the
/// by-name origin, for an alt-svc entry that omits a host (which defaults to the
/// origin), and for an HTTPS record whose target is "." (the owner name). The
/// DNS query itself still uses the name as given.
#[test]
fn by_name_origin_drops_trailing_root_label() {
    use happy_eyeballs::{AltSvc, DnsResult, HttpVersion, Input};

    const FQDN: &str = "example.com.";
    let expected = EndpointTarget::Name {
        host: HOSTNAME.to_string(),
        port: PORT,
    };
    let now = Instant::now();

    // The plain by-name origin.
    let mut he = HappyEyeballs::new_with_network_config(FQDN, PORT, by_name_config()).unwrap();
    let attempt = he.process_output(now).unwrap().attempt().unwrap();
    assert_eq!(attempt.target, expected, "by-name origin");

    // An alt-svc entry without a host of its own defaults to the origin.
    let config = NetworkConfig {
        resolution: ResolutionMode::Name,
        alt_svc: vec![AltSvc {
            host: None,
            port: None,
            http_version: HttpVersion::H3,
        }],
        ..NetworkConfig::default()
    };
    let mut he = HappyEyeballs::new_with_network_config(FQDN, PORT, config).unwrap();
    let attempt = he.process_output(now).unwrap().attempt().unwrap();
    assert_eq!(attempt.target, expected, "alt-svc defaulting to the origin");

    // An HTTPS record whose target name is the owner name (".").
    let mut he =
        HappyEyeballs::new_with_network_config(FQDN, PORT, by_name_https_rr_config()).unwrap();
    he.expect(out_send_dns(Id::from(0), FQDN, DnsRecordType::Https), now);
    he.input(
        Input::DnsResult {
            id: Id::from(0),
            result: DnsResult::Https(Ok(vec![service_info(1, ".", &[HttpVersion::H3])])),
            stale: false,
        },
        now,
    );
    let attempt = he.process_output(now).unwrap().attempt().unwrap();
    assert_eq!(
        attempt.target, expected,
        "HTTPS record targeting the owner name"
    );
}
