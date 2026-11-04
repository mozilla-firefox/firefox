#![allow(clippy::disallowed_methods, reason = "tests may use Instant::now")]

mod common;
use common::*;

use std::collections::HashSet;

use happy_eyeballs::{
    ConnectionResult, EchConfig, HttpVersion, Id, Input, ServiceInfo, TargetName,
};

#[test]
fn id_roundtrip() {
    for n in [0u64, 42] {
        assert_eq!(u64::from(Id::from(n)), n);
    }
}

#[test]
fn ech_config_as_ref() {
    assert_eq!(ech_config().as_ref(), ECH_CONFIG_BYTES);
}

#[test]
fn target_name_conversions() {
    let name = TargetName::from(HOSTNAME);
    assert_eq!(format!("{name:?}"), HOSTNAME);
    assert_eq!(String::from(name), HOSTNAME);
}

#[test]
fn service_info_debug() {
    // With optional fields populated: all conditional fields must appear.
    let full = ServiceInfo {
        priority: 1,
        target_name: HOSTNAME.into(),
        alpn_http_versions: [HttpVersion::H3].into(),
        ech_config: Some(ech_config()),
        ipv4_hints: vec![V4_ADDR],
        ipv6_hints: vec![V6_ADDR],
        port: None,
    };
    let s = format!("{full:?}");
    assert!(s.contains("alpn"), "missing 'alpn': {s}");
    assert!(s.contains("ipv4"), "missing 'ipv4': {s}");
    assert!(s.contains("ipv6"), "missing 'ipv6': {s}");

    // With optional fields empty: conditional fields must not appear.
    let bare = ServiceInfo {
        alpn_http_versions: HashSet::default(),
        ech_config: None,
        ipv4_hints: vec![],
        ipv6_hints: vec![],
        ..full
    };
    let s = format!("{bare:?}");
    assert!(!s.contains("alpn"), "unexpected 'alpn': {s}");
    assert!(!s.contains("ipv4"), "unexpected 'ipv4': {s}");
    assert!(!s.contains("ipv6"), "unexpected 'ipv6': {s}");
}

#[test]
fn happy_eyeballs_debug() {
    let (now, mut he) = setup();

    // Fresh domain host: always has "target" and "port", never "dns_queries" yet.
    let s = format!("{he:?}");
    assert!(s.contains("target"), "missing 'target': {s}");
    assert!(s.contains("port"), "missing 'port': {s}");
    assert!(!s.contains("dns_queries"), "unexpected 'dns_queries': {s}");

    // After first process_output, dns_queries is populated.
    _ = he.process_output(now);
    let s = format!("{he:?}");
    assert!(s.contains("dns_queries"), "missing 'dns_queries': {s}");

    // Set up a hostname-based HE with an HTTPS record that provides ECH
    // config, so the connection attempt carries ECH and EchRetry is valid.
    let (now2, mut he2) = setup();

    // Drive through DNS queries, feed HTTPS+ECH and AAAA to get a connection
    // attempt that carries ECH config.
    expect_initial_dns_queries(&mut he2, now2);
    he2.input(in_dns_https_positive_ech(Id::from(0)), now2);
    he2.expect(out_resolution_delay(), now2);

    // AAAA arrives; connection attempt with ECH is emitted.
    he2.process_input(in_dns_aaaa_positive(Id::from(1)), now2);
    _ = he2.process_output(now2);
    let s = format!("{he2:?}");
    assert!(
        s.contains("connection_attempts"),
        "missing 'connection_attempts': {s}"
    );

    // Feed EchRetry for the in-progress connection to populate ech_retries.
    he2.process_input(
        Input::ConnectionResult {
            id: Id::from(3),
            result: ConnectionResult::EchRetry(EchConfig::new(vec![10, 20, 30])),
        },
        now2,
    );
    let s = format!("{he2:?}");
    assert!(s.contains("ech_retries"), "missing 'ech_retries': {s}");
}
