#![allow(clippy::disallowed_methods, reason = "tests may use Instant::now")]

/// > 4. Hostname Resolution
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4>
mod common;
use common::*;

use std::{
    net::SocketAddr,
    time::{Duration, Instant},
};

use happy_eyeballs::{
    CONNECTION_ATTEMPT_DELAY, ConnectionAttemptHttpVersions, DnsRecordType, DnsResult, Endpoint,
    EndpointTarget, HappyEyeballs, HttpVersions, Id, Input, IpPreference, NetworkConfig, Output,
    RESOLUTION_DELAY,
};

/// The origin's HTTPS record name when the origin is on [`CUSTOM_PORT`].
const HTTPS_NAME_CUSTOM_PORT: &str = "_8443._https.example.com";

fn expect_hints_move_on_with_timeout(
    he: &mut HappyEyeballs,
    now: &mut Instant,
    https_input: Input,
    expected_attempt: Output,
) {
    expect_initial_dns_queries(he, *now);
    he.input(https_input, *now);
    he.expect(out_resolution_delay(), *now);
    *now += RESOLUTION_DELAY;
    he.expect(expected_attempt, *now);
}

#[test]
fn initial_state() {
    let (now, mut he) = setup();

    he.expect(out_send_dns_https(Id::from(0)), now);
}

/// > All of the DNS queries SHOULD be made as soon after one another as
/// > possible. The order in which the queries are sent SHOULD be as follows
/// > (omitting any query that doesn't apply based on the logic described
/// > above):
/// >
/// > 1. SVCB or HTTPS query
/// > 2. AAAA query
/// > 3. A query
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.1>
#[test]
fn sendig_dns_queries() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
}

/// An HTTPS record is published per port, so the record for `example.com:8443`
/// lives at `_8443._https.example.com`. Querying the bare origin name would
/// read the record of whatever the host serves on port 443. Address records
/// carry no prefix labels.
///
/// > The HTTPS RR uses Port Prefix Naming (Section 2.3), with one modification:
/// > if the scheme is "https" and the port is 443, then the client's original
/// > QNAME is equal to the service name (i.e., the origin's hostname), without
/// > any prefix labels.
///
/// <https://www.rfc-editor.org/rfc/rfc9460#section-9.1>
#[test]
fn https_query_for_a_non_default_port_is_port_prefixed() {
    let now = Instant::now();
    let mut he = HappyEyeballs::new(HOSTNAME, CUSTOM_PORT).unwrap();

    he.expect_all(
        [
            out_send_dns(Id::from(0), HTTPS_NAME_CUSTOM_PORT, DnsRecordType::Https),
            out_send_dns(Id::from(1), HOSTNAME, DnsRecordType::Aaaa),
            out_send_dns(Id::from(2), HOSTNAME, DnsRecordType::A),
        ],
        now,
    );
}

/// The port-prefixed query is still the origin's HTTPS query, so its answer
/// satisfies the SVCB/HTTPS half of the move-on condition and the first attempt
/// goes out without waiting out the resolution delay.
#[test]
fn non_default_port_https_answer_satisfies_move_on() {
    let now = Instant::now();
    let mut he = HappyEyeballs::new(HOSTNAME, CUSTOM_PORT).unwrap();

    he.expect_all(
        [
            out_send_dns(Id::from(0), HTTPS_NAME_CUSTOM_PORT, DnsRecordType::Https),
            out_send_dns(Id::from(1), HOSTNAME, DnsRecordType::Aaaa),
            out_send_dns(Id::from(2), HOSTNAME, DnsRecordType::A),
        ],
        now,
    );

    he.input(in_dns_https_negative(Id::from(0)), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);

    he.expect(
        out_attempt(
            Id::from(3),
            V6_ADDR.into(),
            CUSTOM_PORT,
            ConnectionAttemptHttpVersions::H2OrH1,
        ),
        now,
    );
}

/// > Implementations SHOULD NOT wait for all answers to return before
/// > starting the next steps of connection establishment.
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2>
#[test]
fn dont_wait_for_all_dns_answers() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_positive(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_attempt_v6_h3(Id::from(3)), now);
}

/// > The client moves onto sorting addresses and establishing
/// > connections once one of the following condition sets is met:
/// >
/// > Either:
/// >
/// > - Some positive (non-empty) address answers have been received AND
/// > - A postive (non-empty) or negative (empty) answer has been
/// >   received for the preferred address family that was queried AND
/// > - SVCB/HTTPS service information has been received (or has received a negative response)
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2>
#[test]
fn move_on_non_timeout() {
    #[derive(Debug)]
    struct Case {
        address_family: NetworkConfig,
        positive: Input,
        preferred: Option<Input>,
        expected: Option<Output>,
    }

    let test_cases = vec![
        // V6 preferred, V6 positive, HTTPS positive, expect V6 connection attempt
        Case {
            address_family: NetworkConfig {
                http_versions: HttpVersions::default(),
                ip: IpPreference::DualStackPreferV6,
                ..NetworkConfig::default()
            },
            positive: in_dns_aaaa_positive(Id::from(1)),
            preferred: None,
            expected: Some(out_attempt_v6_h1_h2(Id::from(3))),
        },
        // V6 preferred, V4 positive, V6 positive, HTTPS positive, expect V6 connection attempt
        Case {
            address_family: NetworkConfig {
                http_versions: HttpVersions::default(),
                ip: IpPreference::DualStackPreferV6,
                ..NetworkConfig::default()
            },
            positive: in_dns_a_positive(Id::from(2)),
            preferred: Some(in_dns_aaaa_positive(Id::from(1))),
            expected: Some(out_attempt_v6_h1_h2(Id::from(3))),
        },
        // V6 preferred, V6 negative, V4 positive, HTTPS positive, expect V4 connection attempt
        Case {
            address_family: NetworkConfig {
                http_versions: HttpVersions::default(),
                ip: IpPreference::DualStackPreferV6,
                ..NetworkConfig::default()
            },
            positive: in_dns_a_positive(Id::from(2)),
            preferred: Some(in_dns_aaaa_negative(Id::from(1))),
            expected: Some(out_attempt_v4_h1_h2(Id::from(3))),
        },
        // V4 preferred, V4 positive, HTTPS positive, expect V4 connection attempt
        Case {
            address_family: NetworkConfig {
                http_versions: HttpVersions::default(),
                ip: IpPreference::DualStackPreferV4,
                ..NetworkConfig::default()
            },
            positive: in_dns_a_positive(Id::from(2)),
            preferred: None,
            expected: Some(out_attempt_v4_h1_h2(Id::from(3))),
        },
        // V4 preferred, V6 positive, V4 positive, HTTPS positive, expect V4 connection attempt
        Case {
            address_family: NetworkConfig {
                http_versions: HttpVersions::default(),
                ip: IpPreference::DualStackPreferV4,
                ..NetworkConfig::default()
            },
            positive: in_dns_aaaa_positive(Id::from(1)),
            preferred: Some(in_dns_a_positive(Id::from(2))),
            expected: Some(out_attempt_v4_h1_h2(Id::from(3))),
        },
        // V4 preferred, V4 negative, V6 positive, HTTPS positive, expect V6 connection attempt
        Case {
            address_family: NetworkConfig {
                http_versions: HttpVersions::default(),
                ip: IpPreference::DualStackPreferV4,
                ..NetworkConfig::default()
            },
            positive: in_dns_aaaa_positive(Id::from(1)),
            preferred: Some(in_dns_a_negative(Id::from(2))),
            expected: Some(out_attempt_v6_h1_h2(Id::from(3))),
        },
    ];

    for test_case in test_cases {
        for https in [
            in_dns_https_positive_no_alpn(Id::from(0)),
            in_dns_https_negative(Id::from(0)),
        ] {
            let (now, mut he) = setup_with_config(test_case.address_family.clone());

            expect_initial_dns_queries(&mut he, now);
            he.input(test_case.positive.clone(), now);
            he.expect(out_resolution_delay(), now);
            if let Some(preferred) = test_case.preferred.clone() {
                he.input(preferred, now);
            }
            he.expect(out_resolution_delay(), now);
            he.input(https, now);
            match test_case.expected.clone() {
                Some(expected) => he.expect(expected, now),
                None => he.expect_idle(now),
            }
        }
    }
}

/// > Or:
/// >
/// > - Some positive (non-empty) address answers have been received AND
/// > - A resolution time delay has passed after which other answers have not been received
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2>
// TODO: Other combinations
#[test]
fn move_on_timeout() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);

    now += RESOLUTION_DELAY;

    he.expect(out_attempt_v4_h1_h2(Id::from(3)), now);
}

/// > Resolution Delay (Section 4): The time to wait for a AAAA record after
/// > receiving an A record. Recommended to be 50 milliseconds.
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-9>
#[test]
fn resolution_delay_starts_after_other_response() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    // No other response received yet.
    he.expect_idle(now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);

    now += RESOLUTION_DELAY;

    he.expect(out_attempt_v4_h1_h2(Id::from(3)), now);
}

/// Start of the Resolution Delay is not the first DNS query is sent, but
/// the first response received.
///
/// > A resolution time delay has passed after which other answers have not been received
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2>
#[test]
fn resolution_delay_starts_on_first_response() {
    const RESPONSE_DELAY: Duration = Duration::from_millis(10);
    let (start, mut he) = setup();

    expect_initial_dns_queries(&mut he, start);
    // No other response received yet.
    he.expect_idle(start);

    // Receive first response, thus activating the resolution delay.
    he.input(in_dns_a_positive(Id::from(2)), start + RESPONSE_DELAY);
    he.expect(out_resolution_delay(), start + RESPONSE_DELAY);

    // Resolution delay is off of the response, not the query start (i.e. `start`).
    he.expect(
        Output::Timer {
            duration: RESPONSE_DELAY,
        },
        start + RESOLUTION_DELAY,
    );

    he.expect(
        out_attempt_v4_h1_h2(Id::from(3)),
        start + RESPONSE_DELAY + RESOLUTION_DELAY,
    );
}

/// > ServiceMode records can contain address hints via ipv6hint and
/// > ipv4hint parameters. When these are received, they SHOULD be
/// > considered as positive non-empty answers for the purpose of the
/// > algorithm when A and AAAA records corresponding to the TargetName
/// > are not available yet.
///
/// HTTPS arrives first with both v6 and v4 hints while AAAA and A are still
/// in-flight. The v6 hint counts as the preferred family answer, so it is used
/// right away, without waiting out the resolution delay. When AAAA and A
/// subsequently arrive with negative answers, the hints are kept (a negative
/// answer removes no address, so it does not remove the hint). After the
/// in-flight v6 hint attempt fails, the v4 hint is tried next.
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2.1>
#[test]
fn https_hints() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_positive_v4_and_v6_hints(Id::from(0)), now);
    he.expect(out_attempt_v6_h3(Id::from(3)), now);
    he.expect(out_connection_attempt_delay(), now);
    // AAAA and A arrive negative, but the hints are kept. The connection
    // attempt delay is re-emitted while the in-flight v6 attempt is pending.
    he.input(in_dns_aaaa_negative(Id::from(1)), now);
    he.expect(out_connection_attempt_delay(), now);
    he.input(in_dns_a_negative(Id::from(2)), now);
    he.expect(out_connection_attempt_delay(), now);
    // The v6 hint attempt fails; the v4 hint is tried next.
    he.input(in_connection_result_negative(Id::from(3)), now);
    he.expect(out_attempt_v4_h3(Id::from(4)), now);
}

/// An HTTPS IP hint of the preferred family counts as the preferred family
/// answer: with only HTTPS (v6 hint) in, and AAAA and A still in-progress, move
/// on right away instead of waiting out the resolution delay.
///
/// > When these are received, they SHOULD be considered as positive non-empty
/// > answers for the purpose of the algorithm when A and AAAA records
/// > corresponding to the TargetName are not available yet.
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2.1>
#[test]
fn https_preferred_family_hints_move_on_without_timeout() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_positive_v6_hints(Id::from(0)), now);
    he.expect(out_attempt_v6_h3(Id::from(3)), now);
}

/// HTTPS IP hints of the non-preferred family count as positive address
/// answers for the resolution delay timeout path (`move_on_with_timeout`).
///
/// Scenario: only HTTPS with v4 hints has arrived, AAAA and A are still
/// in-progress. After the resolution delay we should move on.
///
/// <https://github.com/mozilla/happy-eyeballs/issues/39>
#[test]
fn https_v4_hints_move_on_with_timeout() {
    let (mut now, mut he) = setup();
    expect_hints_move_on_with_timeout(
        &mut he,
        &mut now,
        in_dns_https_positive_v4_hints(Id::from(0)),
        out_attempt_v4_h3(Id::from(3)),
    );
}

/// With IPv4 preferred, an HTTPS record with an IPv6 hint (advertising only
/// HTTP/1.1) arrives first, so the hint should be connected to once the
/// resolution delay expires (see `https_v4_hints_move_on_with_timeout`). Then
/// empty (NODATA) A and AAAA answers arrive at that same instant.
///
/// An empty answer means the name simply has no A/AAAA records, so it carries
/// no address to replace the hint with. The operator-supplied hint is still the
/// only address available and is used: an HTTP/1.1 connection attempt to the v6
/// hint. (A negative answer, by contrast, still discards the hint, see
/// `hints_discarded_on_negative_answer`.)
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2.1>
#[test]
fn empty_a_aaaa_at_resolution_delay_keeps_v6_hint() {
    const RESOLUTION_DELAY_25MS: Duration = Duration::from_millis(25);
    let (mut now, mut he) = setup_with_config(NetworkConfig {
        resolution_delay: RESOLUTION_DELAY_25MS,
        ip: IpPreference::DualStackPreferV4,
        ..NetworkConfig::default()
    });

    he.expect_all(
        [
            out_send_dns_https(Id::from(0)),
            out_send_dns(Id::from(1), HOSTNAME, DnsRecordType::Aaaa),
            out_send_dns_a(Id::from(2)),
        ],
        now,
    );
    he.input(in_dns_https_v6_hint_h1(Id::from(0)), now);
    he.expect(
        Output::Timer {
            duration: RESOLUTION_DELAY_25MS,
        },
        now,
    );

    // The resolution delay expires and the empty A and AAAA answers arrive at
    // that same instant.
    now += RESOLUTION_DELAY_25MS;
    he.input(in_dns_aaaa_empty(Id::from(1)), now);
    he.input(in_dns_a_empty(Id::from(2)), now);

    // The empty answers carry no addresses, so the v6 hint remains the only
    // address available: expect an H1 connection attempt to it.
    he.expect(
        out_attempt(
            Id::from(3),
            V6_ADDR.into(),
            PORT,
            ConnectionAttemptHttpVersions::H1,
        ),
        now,
    );
}

/// Mirror of `empty_a_aaaa_at_resolution_delay_keeps_v6_hint` with the opposite
/// arrival order: the empty (NODATA) A and AAAA answers arrive first, then the
/// HTTPS record with the IPv6 hint arrives right after. The empty answers still
/// carry no addresses, so the hint is used: an H1 connection attempt to it.
#[test]
fn empty_a_aaaa_before_https_v6_hint_keeps_hint() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_aaaa_empty(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_a_empty(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);

    he.input(in_dns_https_v6_hint_h1(Id::from(0)), now);
    he.expect(
        out_attempt(
            Id::from(3),
            V6_ADDR.into(),
            PORT,
            ConnectionAttemptHttpVersions::H1,
        ),
        now,
    );
}

/// When the resolution delay has exactly elapsed, `process_output` returns `None`,
/// not a zero-duration timer.
#[test]
fn resolution_delay_boundary() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    // HTTPS negative and A positive arrive at T; AAAA still pending.
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_a_positive(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);

    now += RESOLUTION_DELAY;

    // Resolution delay has elapsed; move_on_with_timeout fires.
    he.expect(out_attempt_v4_h1_h2(Id::from(3)), now);

    // Connection fails immediately. AAAA still pending, resolution delay exactly
    // expired — no timer, just None.
    he.input(in_connection_result_negative(Id::from(3)), now);
    he.expect_idle(now);
}

/// > Note that clients are still required to issue A and AAAA queries
/// > for those TargetNames if they haven't yet received those records.
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2.1>
///
/// The v6 hint is attempted right away, the target name queries follow.
#[test]
fn https_hints_still_query_a_aaaa() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
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
    he.expect(out_send_dns_svc1(Id::from(4)), now);
}

#[test]
fn https_h3_upgrade_without_hints() {
    let (now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_https_positive(Id::from(0)), now);
    he.expect(out_attempt_v6_h3(Id::from(3)), now);
}

/// A `ServiceInfo` advertising H3 must not produce an H3 connection attempt
/// when H3 is disabled in the network config.
#[test]
fn https_h3_disabled() {
    let (now, mut he) = setup_with_config(NetworkConfig {
        http_versions: HttpVersions {
            h1: true,
            h2: true,
            h3: false,
        },
        ..NetworkConfig::default()
    });

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_aaaa_positive(Id::from(1)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_https_positive(Id::from(0)), now);
    he.expect(out_attempt_v6_h2(Id::from(3)), now);
}

#[test]
fn multiple_ips_per_record() {
    let (mut now, mut he) = setup();

    expect_initial_dns_queries(&mut he, now);
    he.input(in_dns_https_negative(Id::from(0)), now);
    he.expect(out_resolution_delay(), now);
    he.input(in_dns_a_negative(Id::from(2)), now);
    he.expect(out_resolution_delay(), now);
    he.input(
        Input::DnsResult {
            id: Id::from(1),
            result: DnsResult::Aaaa(Ok(vec![V6_ADDR, V6_ADDR_2, V6_ADDR_3])),
            stale: false,
        },
        now,
    );
    he.expect(out_attempt_v6_h1_h2(Id::from(3)), now);

    now += CONNECTION_ATTEMPT_DELAY;

    he.expect(
        Output::AttemptConnection {
            id: Id::from(4),
            endpoint: Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(V6_ADDR_2.into(), PORT)),
                http_version: ConnectionAttemptHttpVersions::H2OrH1,
                ech_config: None,
            },
            is_ech_retry: false,
        },
        now,
    );
}

/// On a single-stack network, the state machine should skip querying the
/// disabled address family. IPv4-only skips AAAA, IPv6-only skips A.
#[test]
fn single_stack_skips_disabled_address_family() {
    struct Case {
        ip: IpPreference,
        expected_dns_query: Output,
        dns_response: Input,
        expected_connection: Output,
    }

    let cases = vec![
        Case {
            ip: IpPreference::Ipv4Only,
            expected_dns_query: out_send_dns_a(Id::from(1)),
            dns_response: in_dns_a_positive(Id::from(1)),
            expected_connection: out_attempt_v4_h1_h2(Id::from(2)),
        },
        Case {
            ip: IpPreference::Ipv6Only,
            expected_dns_query: out_send_dns_aaaa(Id::from(1)),
            dns_response: in_dns_aaaa_positive(Id::from(1)),
            expected_connection: out_attempt_v6_h1_h2(Id::from(2)),
        },
    ];

    for case in cases {
        let (now, mut he) = setup_with_config(NetworkConfig {
            ip: case.ip,
            ..NetworkConfig::default()
        });

        he.expect(out_send_dns_https(Id::from(0)), now);
        // Should skip the disabled address family query.
        he.expect(case.expected_dns_query, now);
        he.input(in_dns_https_negative(Id::from(0)), now);
        he.expect(out_resolution_delay(), now);
        he.input(case.dns_response, now);
        he.expect(case.expected_connection, now);
    }
}

/// On a single-stack network, target-name follow-up queries must also skip
/// the disabled address family.
///
/// <https://github.com/mozilla/happy-eyeballs/issues/38>
#[test]
fn single_stack_target_name_skips_disabled_address_family() {
    struct Case {
        ip: IpPreference,
        /// The only address-family query sent for the origin domain.
        origin_dns_query: Output,
        /// Outputs after the HTTPS answer: the target name's only
        /// address-family query and then a timer, with an attempt to the v6
        /// hint first when IPv6 is preferred.
        after_https: Vec<Output>,
    }

    let cases = vec![
        Case {
            ip: IpPreference::Ipv6Only,
            origin_dns_query: out_send_dns_aaaa(Id::from(1)),
            after_https: vec![
                out_attempt(
                    Id::from(2),
                    V6_ADDR_2.into(),
                    PORT,
                    ConnectionAttemptHttpVersions::H3,
                ),
                out_send_dns_svc1(Id::from(3)),
                out_connection_attempt_delay(),
            ],
        },
        Case {
            ip: IpPreference::Ipv4Only,
            origin_dns_query: out_send_dns_a(Id::from(1)),
            after_https: vec![
                out_send_dns(Id::from(2), SVC1, DnsRecordType::A),
                out_resolution_delay(),
            ],
        },
    ];

    for case in cases {
        let (now, mut he) = setup_with_config(NetworkConfig {
            ip: case.ip,
            ..NetworkConfig::default()
        });

        he.expect(out_send_dns_https(Id::from(0)), now);
        he.expect(case.origin_dns_query, now);
        he.input(in_dns_https_positive_svc1(Id::from(0)), now);
        // No query for the disabled address family should appear.
        he.expect_all(case.after_https, now);
    }
}

/// On a single-stack network, an HTTPS record's IP hints for the disabled
/// address family must not be attempted. The hints come straight from the DNS
/// answer, so without this the record decides which family the client connects
/// over, e.g. IPv6 for a client that has IPv6 disabled.
#[test]
fn single_stack_skips_disabled_address_family_hints() {
    struct Case {
        ip: IpPreference,
        /// The only address-family query sent for the origin domain.
        dns_query: Output,
        dns_response: Input,
        /// The enabled family's hint over each protocol; nothing else.
        expected_connections: [Output; 2],
    }

    let cases = vec![
        Case {
            ip: IpPreference::Ipv4Only,
            dns_query: out_send_dns_a(Id::from(1)),
            dns_response: in_dns_a_negative(Id::from(1)),
            expected_connections: [
                out_attempt_v4_h3(Id::from(2)),
                out_attempt_v4_h2(Id::from(3)),
            ],
        },
        Case {
            ip: IpPreference::Ipv6Only,
            dns_query: out_send_dns_aaaa(Id::from(1)),
            dns_response: in_dns_aaaa_negative(Id::from(1)),
            expected_connections: [
                out_attempt_v6_h3(Id::from(2)),
                out_attempt_v6_h2(Id::from(3)),
            ],
        },
    ];

    for case in cases {
        let (mut now, mut he) = setup_with_config(NetworkConfig {
            ip: case.ip,
            ..NetworkConfig::default()
        });

        he.expect(out_send_dns_https(Id::from(0)), now);
        he.expect(case.dns_query, now);
        // The HTTPS record carries a hint for both address families. The
        // enabled family's hint counts as its answer, so no resolution delay.
        he.input(in_dns_https_positive_v4_and_v6_hints(Id::from(0)), now);
        let [first, second] = case.expected_connections;
        he.expect(first, now);
        he.expect(out_connection_attempt_delay(), now);
        he.input(case.dns_response, now);
        he.expect(out_connection_attempt_delay(), now);
        // No attempt to the disabled family's hint follows.
        he.expect_connection_attempts([second], &mut now);
    }
}
