#![allow(clippy::disallowed_methods, reason = "tests may use Instant::now")]

mod common;
use common::*;

use std::net::{IpAddr, Ipv4Addr, SocketAddr};

use happy_eyeballs::{
    ConnectionAttemptHttpVersions, Endpoint, EndpointTarget, FailureReason, HappyEyeballs, Id,
    Output,
};

/// All DNS queries fail. No connections are attempted.
#[test]
fn all_dns_failed() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_negative(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_a_negative(Id::from(2)), now);
    he.expect(Output::Failed(FailureReason::DnsResolution), now);
}

/// DNS partially fails (HTTPS and A fail) but AAAA succeeds, then connection fails.
#[test]
fn dns_partial_failure_then_connection_failed() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.input(in_dns_a_negative(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);
    he.input(in_connection_result_negative(Id::from(3)), now);
    he.expect(Output::Failed(FailureReason::Connection), now);
}

/// All DNS succeeds but all connection attempts fail.
#[test]
fn all_connections_failed() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_positive_no_alpn(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);
    he.input(in_connection_result_negative(Id::from(3)), now);
    he.expect(out_attempt_v4_h1_h2(Id::from(4)), now);
    he.input(in_connection_result_negative(Id::from(4)), now);
    he.expect(Output::Failed(FailureReason::Connection), now);
}

/// When the target is an IP address and the connection fails, the state
/// machine must report `Failed(Connection)` instead of retrying the same
/// endpoint indefinitely.
#[test]
fn ip_host_connection_failure() {
    let now = std::time::Instant::now();
    let mut he = HappyEyeballs::new("127.0.0.1", PORT).unwrap();

    he.expect(
        Output::AttemptConnection {
            id: Id::from(0),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(
                    IpAddr::V4(Ipv4Addr::LOCALHOST),
                    PORT,
                )),
                http_version: ConnectionAttemptHttpVersions::H2OrH1,
                ech_config: None,
            },
            is_ech_retry: false,
        },
        now,
    );
    he.input(in_connection_result_negative(Id::from(0)), now);
    he.expect(Output::Failed(FailureReason::Connection), now);
}

/// First connection fails, second succeeds. Should not emit `Failed`.
#[test]
fn first_connection_fails_second_succeeds() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_positive_no_alpn(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);
    he.input(in_connection_result_negative(Id::from(3)), now);
    he.expect(out_attempt_v4_h1_h2(Id::from(4)), now);
    he.input(in_connection_result_positive(Id::from(4)), now);
    he.expect(Output::Succeeded, now);
}
