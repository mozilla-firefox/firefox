//! Optimistic DNS: the state machine uses an answer the resolver served from a
//! stale (expired) cache entry, and revalidates it with a background query.
//!
//! <https://datatracker.ietf.org/doc/draft-gakiwate-dnsop-optimistic-dns/>
//! <https://github.com/mozilla/happy-eyeballs/issues/125>
#![allow(clippy::disallowed_methods, reason = "tests may use Instant::now")]

mod common;
use common::*;

use std::net::SocketAddr;
use std::time::Instant;

use happy_eyeballs::{
    CONNECTION_ATTEMPT_DELAY, ConnectionAttemptHttpVersions, DnsRecordType, DnsResult, Endpoint,
    EndpointTarget, FailureReason, HappyEyeballs, Id, Input, IpPreference, NetworkConfig, Output,
};

/// Dual-stack (the default), with the HTTPS query answered negative so the AAAA
/// and A queries (ids 1 and 2) drive the flow. Leaves the state machine having
/// emitted the initial HTTPS/AAAA/A queries (ids 0, 1, 2).
fn setup_dual() -> (Instant, HappyEyeballs) {
    let (now, mut he) = setup();
    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    (now, he)
}

/// A record's first query permits a stale answer; the revalidation query
/// forbids one. This is the wire contract the rest of the tests rely on.
#[test]
fn query_allow_stale_flags() {
    let (now, mut he) = setup();

    // Every initial query (HTTPS, AAAA, A) permits a stale answer.
    for _ in 0..3 {
        match he.process_output(now) {
            Some(Output::SendDnsQuery { allow_stale, .. }) => assert!(allow_stale),
            other => panic!("expected SendDnsQuery, got {other:?}"),
        }
    }

    he.input(in_dns_https_negative(Id::from(0)), now);
    he.input(in_dns_a_negative(Id::from(2)), now);
    he.input(in_dns_aaaa_stale(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);

    // The revalidation query forbids a stale answer.
    match he.process_output(now) {
        Some(Output::SendDnsQuery {
            allow_stale,
            record_type,
            ..
        }) => {
            assert!(!allow_stale);
            assert_eq!(record_type, DnsRecordType::Aaaa);
        }
        other => panic!("expected refresh SendDnsQuery, got {other:?}"),
    }
}

/// A stale AAAA answer is used at once to race a connection, and the state
/// machine emits a background query that forbids a stale answer
/// (`allow_stale: false`) to revalidate it. The A query is still in flight,
/// as it would be when AAAA came straight from an expired cache entry.
#[test]
fn stale_answer_races_and_refreshes() {
    let (now, mut he) = setup_dual();

    he.input(in_dns_aaaa_stale(Id::from(1)), now);

    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.expect(
        out_send_dns_refresh(Id::from(4), HOSTNAME, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
}

/// A stale A answer is revalidated the same way as a stale AAAA answer.
#[test]
fn stale_a_answer_races_and_refreshes() {
    let (now, mut he) = setup_dual();

    he.input(in_dns_aaaa_negative(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_a_stale(Id::from(2)), now);

    he.expect(out_attempt_v4_h1_h2(Id::from(3)), now);
    he.expect(
        out_send_dns_refresh(Id::from(4), HOSTNAME, DnsRecordType::A),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
}

/// Both address families answered stale: each is revalidated exactly once, and
/// the preferred family is raced first.
#[test]
fn both_families_stale_each_refreshed() {
    let (now, mut he) = setup_dual();

    he.input(in_dns_aaaa_stale(Id::from(1)), now);
    he.input(in_dns_a_stale(Id::from(2)), now);

    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.expect(
        out_send_dns_refresh(Id::from(4), HOSTNAME, DnsRecordType::Aaaa),
        now,
    );
    he.expect(
        out_send_dns_refresh(Id::from(5), HOSTNAME, DnsRecordType::A),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
}

/// A fresh (non-stale) answer is not revalidated: after the connection attempt
/// the next output is the delay timer, not a refresh query.
#[test]
fn fresh_answer_is_not_refreshed() {
    let (now, mut he) = setup_dual();

    he.input(in_dns_aaaa_positive(Id::from(1)), now);

    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.expect(out_connection_attempt_delay(), now);
}

/// A stale negative answer is revalidated too, matching the draft's Optimistic
/// Negative Answers.
#[test]
fn stale_negative_answer_is_refreshed() {
    let (now, mut he) = setup_dual();

    he.input(
        Input::DnsResult {
            id: Id::from(1),
            result: DnsResult::Aaaa(Err(())),
            stale: true,
        },
        now,
    );

    // No address to race yet, but the stale negative answer is revalidated.
    he.expect(
        out_send_dns_refresh(Id::from(3), HOSTNAME, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_resolution_delay(), now);
}

/// A stale SVCB/HTTPS answer is revalidated too.
#[test]
fn stale_https_record_is_refreshed() {
    let (now, mut he) = setup();
    expect_initial_dns_queries(&mut he, now);

    he.input(in_dns_https_stale_v6_hints(Id::from(0)), now);

    // The stale record's v6 hint is raced right away, and the record is
    // revalidated.
    he.expect(out_attempt_v6_h3(Id::from(3)), now);
    he.expect(
        out_send_dns_refresh(Id::from(4), HOSTNAME, DnsRecordType::Https),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
}

/// A stale answer for a follow-up `TargetName` query (named by an HTTPS record) is
/// revalidated as well.
#[test]
fn stale_target_name_record_is_refreshed() {
    let (mut now, mut he) = setup();
    expect_initial_dns_queries(&mut he, now);

    // HTTPS names SVC1; the machine races its v6 hint and queries SVC1's AAAA
    // and A.
    he.input(in_dns_https_positive_svc1(Id::from(0)), now);
    he.expect(
        out_attempt(
            Id::from(3),
            V6_ADDR_2.into(),
            PORT,
            ConnectionAttemptHttpVersions::H3,
        ),
        now,
    );
    he.expect(out_send_dns(Id::from(4), SVC1, DnsRecordType::Aaaa), now);
    he.expect(out_send_dns(Id::from(5), SVC1, DnsRecordType::A), now);
    he.expect(out_connection_attempt_delay(), now);

    // SVC1's AAAA comes back stale: it is revalidated for SVC1 and raced (H3
    // from the record's ALPN) once the connection attempt delay has passed.
    he.input(in_dns_aaaa_stale(Id::from(4)), now);
    he.expect(
        out_send_dns_refresh(Id::from(6), SVC1, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    now += CONNECTION_ATTEMPT_DELAY;
    he.expect(
        out_attempt(
            Id::from(7),
            V6_ADDR.into(),
            PORT,
            ConnectionAttemptHttpVersions::H3,
        ),
        now,
    );
}

/// Optimistic refresh also works on a single-stack (IPv4-only) network.
#[test]
fn stale_answer_refreshed_single_stack() {
    let (now, mut he) = setup_with_config(NetworkConfig {
        ip: IpPreference::Ipv4Only,
        ..NetworkConfig::default()
    });
    he.expect(out_send_dns_https(Id::from(0)), now);
    he.expect(out_send_dns_a(Id::from(1)), now);
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);

    he.input(in_dns_a_stale(Id::from(1)), now);
    he.expect(out_attempt_v4_h1_h2(Id::from(2)), now);
    he.expect(
        out_send_dns_refresh(Id::from(3), HOSTNAME, DnsRecordType::A),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
}

/// When the revalidation answer confirms the same address, nothing new is
/// attempted and no second refresh is emitted.
#[test]
fn refresh_confirming_same_address_is_quiet() {
    let (now, mut he) = setup_dual();

    he.input(in_dns_aaaa_stale(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.expect(
        out_send_dns_refresh(Id::from(4), HOSTNAME, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    he.input(in_dns_aaaa_positive(Id::from(4)), now);
    he.expect(out_connection_attempt_delay(), now);
}

/// A refresh query is sent with `allow_stale: false`, so a stale response to one
/// is a resolver contract violation. In debug builds it trips a debug assertion.
/// In release builds the assertion compiles out and the answer is accepted as-is
/// (the `Done` state still prevents a second revalidation).
#[cfg(debug_assertions)]
#[test]
#[should_panic(expected = "stale response for refresh query")]
fn stale_response_to_refresh_query_asserts() {
    let (now, mut he) = setup_dual();

    he.input(in_dns_aaaa_stale(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.expect(
        out_send_dns_refresh(Id::from(4), HOSTNAME, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    // The revalidation query forbade a stale answer; returning one violates the
    // contract.
    he.input(in_dns_aaaa_stale(Id::from(4)), now);
}

/// When the revalidation answer carries a changed address, the state machine
/// adds it to the race once the connection-attempt delay elapses.
#[test]
fn refresh_with_changed_address_updates_candidates() {
    let (mut now, mut he) = setup_dual();

    he.input(in_dns_aaaa_stale(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.expect(
        out_send_dns_refresh(Id::from(4), HOSTNAME, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(out_connection_attempt_delay(), now);
    now += CONNECTION_ATTEMPT_DELAY;
    he.expect(
        out_attempt(
            Id::from(5),
            V6_ADDR_2.into(),
            PORT,
            ConnectionAttemptHttpVersions::H2OrH1,
        ),
        now,
    );
}

/// It is fine to finish on the stale address: a success is reported even while
/// the revalidation is still in flight.
#[test]
fn success_finishes_despite_inflight_refresh() {
    let (now, mut he) = setup_dual();

    he.input(in_dns_aaaa_stale(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.expect(
        out_send_dns_refresh(Id::from(4), HOSTNAME, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    he.input(in_connection_result_positive(Id::from(3)), now);
    he.expect(Output::Succeeded, now);
}

/// A revalidation answer that arrives after the connection already succeeded is
/// handled gracefully; the machine stays succeeded.
#[test]
fn refresh_answer_after_success_is_ignored() {
    let (now, mut he) = setup_dual();

    he.input(in_dns_aaaa_stale(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.expect(
        out_send_dns_refresh(Id::from(4), HOSTNAME, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    he.input(in_connection_result_positive(Id::from(3)), now);
    he.expect(Output::Succeeded, now);

    // Late revalidation answer with a different address changes nothing.
    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(Output::Succeeded, now);
}

/// If the stale address fails to connect while its revalidation is still in
/// flight, the state machine waits for the fresh answer rather than declaring
/// failure, then races the address the fresh answer provides.
///
/// The A query is answered negative up front, so the only thing keeping the
/// state machine from failing after the stale connection dies is the in-flight
/// revalidation.
#[test]
fn stale_failure_waits_for_refresh() {
    let (now, mut he) = setup_dual();

    he.input(in_dns_a_negative(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_stale(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.expect(
        out_send_dns_refresh(Id::from(4), HOSTNAME, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    // The stale address fails, but the refresh is still outstanding.
    he.input(in_connection_result_negative(Id::from(3)), now);
    he.expect_idle(now);

    // The fresh answer arrives with a usable address, which is raced.
    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR_2])),
            stale: false,
        },
        now,
    );
    he.expect(
        out_attempt(
            Id::from(5),
            V6_ADDR_2.into(),
            PORT,
            ConnectionAttemptHttpVersions::H2OrH1,
        ),
        now,
    );
}

/// The refresh must not paper over a genuine failure: once the revalidation
/// returns and it, too, cannot yield a working connection, the state machine
/// finishes with a failure.
#[test]
fn failure_reported_after_refresh_also_fails() {
    let (now, mut he) = setup_dual();

    he.input(in_dns_a_negative(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_stale(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.expect(
        out_send_dns_refresh(Id::from(4), HOSTNAME, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    he.input(in_connection_result_negative(Id::from(3)), now);
    he.expect_idle(now);

    // The refresh confirms the same address, which has already failed. No new
    // endpoint to try, so the state machine now reports the failure.
    he.input(in_dns_aaaa_positive(Id::from(4)), now);
    he.expect(Output::Failed(FailureReason::Connection), now);
}

/// A stale HTTPS answer that advertises ECH restricts the race to ECH endpoints.
/// The revalidation answer takes preference over the stale one: when the
/// operator has since removed ECH, the fresh answer lifts that restriction and
/// the non-ECH endpoint joins the race.
#[test]
fn refresh_dropping_ech_takes_preference() {
    let (mut now, mut he) = setup();
    expect_initial_dns_queries(&mut he, now);

    he.input(in_dns_https_stale_ech(Id::from(0)), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.input(in_dns_a_negative(Id::from(2)), now);

    // The stale answer is used at once, so the first attempt carries ECH, and
    // the stale record is revalidated.
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
    he.expect(
        out_send_dns_refresh(Id::from(4), HOSTNAME, DnsRecordType::Https),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    // The fresh answer no longer advertises ECH.
    he.input(in_dns_https_positive(Id::from(4)), now);
    he.expect(out_connection_attempt_delay(), now);

    // Past the connection-attempt delay, the endpoint is raced without ECH.
    now += CONNECTION_ATTEMPT_DELAY;
    he.expect(out_attempt_v6_h3(Id::from(5)), now);
}

/// A revalidation that returns a negative answer removes the candidate; with the
/// stale connection already failed, the machine reports failure.
#[test]
fn refresh_negative_then_failure() {
    let (now, mut he) = setup_dual();

    he.input(in_dns_a_negative(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_stale(Id::from(1)), now);
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);
    he.expect(
        out_send_dns_refresh(Id::from(4), HOSTNAME, DnsRecordType::Aaaa),
        now,
    );
    he.expect(out_connection_attempt_delay(), now);

    he.input(in_connection_result_negative(Id::from(3)), now);
    he.expect_idle(now);

    // Revalidation says the address is gone.
    he.input(
        Input::DnsResult {
            id: Id::from(4),
            result: DnsResult::Aaaa(Err(())),
            stale: false,
        },
        now,
    );
    he.expect(Output::Failed(FailureReason::Connection), now);
}
