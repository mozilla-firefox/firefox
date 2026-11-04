#![allow(clippy::disallowed_methods, reason = "tests may use Instant::now")]

/// > 6. Establishing Connections
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-6>
mod common;
use common::*;

use std::{net::SocketAddr, num::NonZeroU32, time::Duration};

use happy_eyeballs::{
    CONNECTION_ATTEMPT_DELAY, ConnectionAttemptHttpVersions, DnsResult, Endpoint, EndpointTarget,
    Id, Input, NetworkConfig, Output,
};

#[test]
fn ipv6_blackhole() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_positive(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h3(Id::from(3)), now);

    for _ in 0..42 {
        now += CONNECTION_ATTEMPT_DELAY;
        let connection_attempt = he.process_output(now).unwrap().attempt().unwrap();
        if connection_attempt.address().unwrap().is_ipv4() {
            return;
        }
    }

    panic!("Did not fall back to IPv4.");
}

#[test]
fn connection_attempt_delay() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_positive_no_alpn(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);

    now += CONNECTION_ATTEMPT_DELAY;

    he.expect(out_attempt_v4_h1_h2(Id::from(4)), now);
}

/// With a multiplier of 2, the delay before each successive connection attempt
/// doubles: attempts land at t=0, t=250, t=750, t=1750.
#[test]
fn connection_attempt_delay_multiplier() {
    let base = Duration::from_millis(250);
    let (mut now, mut he) = setup_with_config(NetworkConfig {
        connection_attempt_delay: base,
        connection_attempt_delay_multiplier: NonZeroU32::new(2).unwrap(),
        ..NetworkConfig::default()
    });

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

    // First attempt at t=0; one attempt in flight, so the next is one base
    // delay away.
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.expect(Output::Timer { duration: base }, now);

    // Second attempt at t=250 (after base * 2^0). The next delay then doubles.
    now += base;
    let second = he.process_output(now).unwrap().attempt().unwrap();
    assert_eq!(second.address().unwrap().ip(), V6_ADDR_2);
    he.expect(Output::Timer { duration: base * 2 }, now);

    // Third attempt at t=750 (after base * 2^1). The next delay doubles again.
    now += base * 2;
    let third = he.process_output(now).unwrap().attempt().unwrap();
    assert_eq!(third.address().unwrap().ip(), V6_ADDR_3);
    he.expect(Output::Timer { duration: base * 4 }, now);
}

/// Attempts triggered by a previous attempt failing must not grow the delay:
/// only concurrently in-progress attempts increase it. With one attempt in
/// progress the delay stays at the base value even after an earlier failure.
#[test]
fn failed_attempts_do_not_increase_delay() {
    let base = Duration::from_millis(250);
    let (now, mut he) = setup_with_config(NetworkConfig {
        connection_attempt_delay: base,
        connection_attempt_delay_multiplier: NonZeroU32::new(2).unwrap(),
        ..NetworkConfig::default()
    });

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(1),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR, V6_ADDR_2])),
            stale: false,
        },
        now,
    );

    // First attempt, then fail it: the next attempt starts immediately.
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.input(in_connection_result_negative(Id::from(3)), now);
    let second = he.process_output(now).unwrap().attempt().unwrap();
    assert_eq!(second.address().unwrap().ip(), V6_ADDR_2);

    // Only one attempt is in progress, so the delay stays at the base value
    // rather than growing because a previous attempt failed.
    he.expect(Output::Timer { duration: base }, now);
}

#[test]
fn never_try_same_attempt_twice() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_a_negative(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);

    now += CONNECTION_ATTEMPT_DELAY;

    he.expect_idle(now);
}

#[test]
fn successful_connection_cancels_others() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_positive_no_alpn(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(1),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR, V6_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);

    // Address families are interleaved, so the single IPv4 address is attempted
    // before the second IPv6 address.
    now += CONNECTION_ATTEMPT_DELAY;
    he.expect(out_attempt_v4_h1_h2(Id::from(4)), now);

    now += CONNECTION_ATTEMPT_DELAY;
    he.expect(
        Output::AttemptConnection {
            id: Id::from(5),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR_2.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H2OrH1,
                ech_config: None,
            },
            is_ech_retry: false,
        },
        now,
    );
    he.input(in_connection_result_positive(Id::from(3)), now);
    he.expect_all(
        [
            Output::CancelConnection { id: Id::from(4) },
            Output::CancelConnection { id: Id::from(5) },
            Output::Succeeded,
        ],
        now,
    );
}

#[test]
fn failed_connection_tries_next_immediately() {
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
}

#[test]
fn successful_connection_emits_succeeded() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_positive_no_alpn(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.input(in_connection_result_positive(Id::from(3)), now);
    he.expect(Output::Succeeded, now);
}

#[test]
fn succeeded_keeps_emitting_succeeded() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_positive_no_alpn(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.input(in_connection_result_positive(Id::from(3)), now);
    he.expect(Output::Succeeded, now);
    // After succeeded, continue to emit Succeeded
    he.expect(Output::Succeeded, now);
    he.expect(Output::Succeeded, now);
}

/// The connection-attempt-delay timer reflects the time *remaining*, not the full delay.
/// Calling `process_output` partway through the delay should return a timer for the remainder.
#[test]
fn connection_attempt_delay_partial_elapsed() {
    let custom_delay = Duration::from_millis(100);
    let (now, mut he) = setup_with_config(NetworkConfig {
        connection_attempt_delay: custom_delay,
        ..NetworkConfig::default()
    });

    // Drive to first connection attempt at time T=now.
    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);

    let elapsed = Duration::from_millis(40);
    he.expect(
        Output::Timer {
            duration: custom_delay.checked_sub(elapsed).unwrap(),
        },
        now + elapsed,
    );
}

#[test]
fn cancelled_connection_result_ignored() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_positive_no_alpn(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);

    now += CONNECTION_ATTEMPT_DELAY;

    // Start second connection attempt.
    he.expect(out_attempt_v4_h1_h2(Id::from(4)), now);

    // First connection succeeds, triggering cancellation of the second.
    he.input(in_connection_result_positive(Id::from(3)), now);
    he.expect(Output::CancelConnection { id: Id::from(4) }, now);
    he.expect(Output::Succeeded, now);

    // User reports an error for the already-cancelled connection.
    // This must not panic.
    he.input(in_connection_result_negative(Id::from(4)), now);
    he.expect(Output::Succeeded, now);
}
