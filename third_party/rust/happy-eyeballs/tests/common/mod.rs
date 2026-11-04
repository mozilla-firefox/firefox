#![allow(dead_code, reason = "each test binary uses a subset of these helpers")]

use std::{
    net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr},
    time::Instant,
};

use happy_eyeballs::{
    CONNECTION_ATTEMPT_DELAY, ConnectionAttemptHttpVersions, ConnectionResult, DnsRecordType,
    DnsResult, EchConfig, Endpoint, EndpointTarget, HappyEyeballs, HttpVersion, Id, Input,
    NetworkConfig, Output, RESOLUTION_DELAY, ServiceInfo,
};

pub const HOSTNAME: &str = "example.com";
pub const SVC1: &str = "svc1.example.com.";
pub const SVC2: &str = "svc2.example.com.";
pub const PORT: u16 = 443;
pub const CUSTOM_PORT: u16 = 8443;
pub const V6_ADDR: Ipv6Addr = Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1);
pub const V6_ADDR_2: Ipv6Addr = Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 2);
pub const V6_ADDR_3: Ipv6Addr = Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 3);
pub const V4_ADDR: Ipv4Addr = Ipv4Addr::new(192, 0, 2, 1);
pub const V4_ADDR_2: Ipv4Addr = Ipv4Addr::new(192, 0, 2, 2);
pub const ECH_CONFIG_BYTES: &[u8] = &[1, 2, 3, 4, 5];

pub fn ech_config() -> EchConfig {
    EchConfig::new(ECH_CONFIG_BYTES.to_vec())
}

/// Build a [`ServiceInfo`] from the fields tests usually vary: priority, target
/// name, and ALPN versions. The remaining fields (hints, ECH, port) default to
/// empty/none; set them with the chainable [`ServiceInfoExt`] methods, e.g.
/// `service_info(1, SVC1, &[HttpVersion::H3]).ech().port(8443)`.
pub fn service_info(priority: u16, target_name: &str, alpns: &[HttpVersion]) -> ServiceInfo {
    ServiceInfo {
        priority,
        target_name: target_name.into(),
        alpn_http_versions: alpns.iter().copied().collect(),
        ipv6_hints: vec![],
        ipv4_hints: vec![],
        ech_config: None,
        port: None,
    }
}

/// Chainable setters for the non-default [`ServiceInfo`] fields, so tests can
/// build records without spelling out the defaults, e.g.
/// `service_info(1, SVC1, &[HttpVersion::H3]).ech().port(9443)`.
pub trait ServiceInfoExt {
    /// Attach the default test ECH config ([`ech_config`]).
    fn ech(self) -> Self;
    /// Attach a specific ECH config.
    fn ech_with(self, ech: EchConfig) -> Self;
    fn port(self, port: u16) -> Self;
    fn ipv6_hints(self, hints: Vec<Ipv6Addr>) -> Self;
    fn ipv4_hints(self, hints: Vec<Ipv4Addr>) -> Self;
}

impl ServiceInfoExt for ServiceInfo {
    fn ech(mut self) -> Self {
        self.ech_config = Some(ech_config());
        self
    }
    fn ech_with(mut self, ech: EchConfig) -> Self {
        self.ech_config = Some(ech);
        self
    }
    fn port(mut self, port: u16) -> Self {
        self.port = Some(port);
        self
    }
    fn ipv6_hints(mut self, hints: Vec<Ipv6Addr>) -> Self {
        self.ipv6_hints = hints;
        self
    }
    fn ipv4_hints(mut self, hints: Vec<Ipv4Addr>) -> Self {
        self.ipv4_hints = hints;
        self
    }
}

/// Test-only driver methods on [`HappyEyeballs`]. All methods take `now` last.
pub trait HappyEyeballsExt {
    /// Feed a single input into the state machine.
    fn input(&mut self, input: Input, now: Instant);
    /// Assert that the next output equals `expected`.
    fn expect(&mut self, expected: Output, now: Instant);
    /// Assert that the next outputs equal `expected`, in order. Convenience for
    /// a run of consecutive outputs emitted at the same `now`.
    fn expect_all<I: IntoIterator<Item = Output>>(&mut self, expected: I, now: Instant);
    /// Assert that the state machine has no further output to produce.
    fn expect_idle(&mut self, now: Instant);
    fn expect_connection_attempts<I: IntoIterator<Item = Output>>(
        &mut self,
        connections: I,
        now: &mut Instant,
    );
}

impl HappyEyeballsExt for HappyEyeballs {
    fn input(&mut self, input: Input, now: Instant) {
        self.process_input(input, now);
    }

    fn expect(&mut self, expected: Output, now: Instant) {
        assert_eq!(Some(expected), self.process_output(now));
    }

    fn expect_all<I: IntoIterator<Item = Output>>(&mut self, expected: I, now: Instant) {
        for output in expected {
            self.expect(output, now);
        }
    }

    fn expect_idle(&mut self, now: Instant) {
        assert_eq!(None, self.process_output(now));
    }

    fn expect_connection_attempts<I: IntoIterator<Item = Output>>(
        &mut self,
        connections: I,
        now: &mut Instant,
    ) {
        for conn in connections {
            *now += CONNECTION_ATTEMPT_DELAY;
            self.expect(conn, *now);
            self.expect(out_connection_attempt_delay(), *now);
        }
        *now += CONNECTION_ATTEMPT_DELAY;
        self.expect_idle(*now);
    }
}

pub fn in_dns_https_positive(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::Https(Ok(vec![service_info(
            1,
            HOSTNAME,
            &[HttpVersion::H3, HttpVersion::H2],
        )])),
        stale: false,
    }
}

pub fn in_dns_https_positive_ech(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::Https(Ok(vec![
            service_info(1, HOSTNAME, &[HttpVersion::H3, HttpVersion::H2]).ech(),
        ])),
        stale: false,
    }
}

pub fn in_dns_https_positive_no_alpn(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::Https(Ok(vec![service_info(1, HOSTNAME, &[])])),
        stale: false,
    }
}

fn in_dns_https_with_hints(id: Id, ipv4_hints: Vec<Ipv4Addr>, ipv6_hints: Vec<Ipv6Addr>) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::Https(Ok(vec![
            service_info(1, HOSTNAME, &[HttpVersion::H3, HttpVersion::H2])
                .ipv4_hints(ipv4_hints)
                .ipv6_hints(ipv6_hints),
        ])),
        stale: false,
    }
}

pub fn in_dns_https_positive_v6_hints(id: Id) -> Input {
    in_dns_https_with_hints(id, vec![], vec![V6_ADDR])
}

/// HTTPS record for the origin advertising only HTTP/1.1 (the "https" scheme
/// default ALPN) with a single IPv6 address hint.
pub fn in_dns_https_v6_hint_h1(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::Https(Ok(vec![
            service_info(1, HOSTNAME, &[HttpVersion::H1]).ipv6_hints(vec![V6_ADDR]),
        ])),
        stale: false,
    }
}

pub fn in_dns_https_positive_v4_hints(id: Id) -> Input {
    in_dns_https_with_hints(id, vec![V4_ADDR], vec![])
}

pub fn in_dns_https_positive_v4_and_v6_hints(id: Id) -> Input {
    in_dns_https_with_hints(id, vec![V4_ADDR], vec![V6_ADDR])
}

pub fn in_dns_https_positive_svc1(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::Https(Ok(vec![
            service_info(1, SVC1, &[HttpVersion::H3, HttpVersion::H2]).ipv6_hints(vec![V6_ADDR_2]),
        ])),
        stale: false,
    }
}

pub const fn in_dns_https_negative(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::Https(Err(())),
        stale: false,
    }
}

/// An HTTPS answer with an IPv6 hint, served from a stale (expired) cache entry.
pub fn in_dns_https_stale_v6_hints(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::Https(Ok(vec![
            service_info(1, HOSTNAME, &[HttpVersion::H3, HttpVersion::H2])
                .ipv6_hints(vec![V6_ADDR]),
        ])),
        stale: true,
    }
}

/// An HTTPS answer advertising ECH, served from a stale (expired) cache entry.
pub fn in_dns_https_stale_ech(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::Https(Ok(vec![
            service_info(1, HOSTNAME, &[HttpVersion::H3, HttpVersion::H2]).ech(),
        ])),
        stale: true,
    }
}

pub fn in_dns_aaaa_positive(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::Aaaa(Ok(vec![V6_ADDR])),
        stale: false,
    }
}

/// A positive AAAA answer the resolver served from a stale (expired) cache
/// entry, as under Optimistic DNS.
pub fn in_dns_aaaa_stale(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::Aaaa(Ok(vec![V6_ADDR])),
        stale: true,
    }
}

pub fn in_dns_a_positive(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::A(Ok(vec![V4_ADDR])),
        stale: false,
    }
}

/// A positive A answer the resolver served from a stale (expired) cache entry,
/// as under Optimistic DNS.
pub fn in_dns_a_stale(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::A(Ok(vec![V4_ADDR])),
        stale: true,
    }
}

pub const fn in_dns_aaaa_negative(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::Aaaa(Err(())),
        stale: false,
    }
}

/// A positive but empty AAAA answer (`Ok(vec![])`): the resolver returned no
/// IPv6 addresses. Distinct from a negative answer (`Err`).
pub const fn in_dns_aaaa_empty(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::Aaaa(Ok(vec![])),
        stale: false,
    }
}

/// A positive but empty A answer (`Ok(vec![])`): the resolver returned no IPv4
/// addresses. Distinct from a negative answer (`Err`).
pub const fn in_dns_a_empty(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::A(Ok(vec![])),
        stale: false,
    }
}

pub const fn in_dns_a_negative(id: Id) -> Input {
    Input::DnsResult {
        id,
        result: DnsResult::A(Err(())),
        stale: false,
    }
}

pub const fn in_connection_result_positive(id: Id) -> Input {
    Input::ConnectionResult {
        id,
        result: ConnectionResult::Success,
    }
}

pub fn in_connection_result_negative(id: Id) -> Input {
    Input::ConnectionResult {
        id,
        result: ConnectionResult::Failure("connection refused".to_string()),
    }
}

pub fn in_connection_result_ech_retry(id: Id) -> Input {
    Input::ConnectionResult {
        id,
        result: ConnectionResult::EchRetry(ech_config()),
    }
}

pub fn out_send_dns(id: Id, hostname: &str, record_type: DnsRecordType) -> Output {
    Output::SendDnsQuery {
        id,
        hostname: hostname.into(),
        record_type,
        allow_stale: true,
    }
}

/// Expected refresh query: the follow-up that revalidates a stale answer, so it
/// forbids a stale answer (`allow_stale: false`).
pub fn out_send_dns_refresh(id: Id, hostname: &str, record_type: DnsRecordType) -> Output {
    Output::SendDnsQuery {
        id,
        hostname: hostname.into(),
        record_type,
        allow_stale: false,
    }
}

pub fn out_send_dns_https(id: Id) -> Output {
    out_send_dns(id, HOSTNAME, DnsRecordType::Https)
}

pub fn out_send_dns_aaaa(id: Id) -> Output {
    out_send_dns(id, HOSTNAME, DnsRecordType::Aaaa)
}

pub fn out_send_dns_svc1(id: Id) -> Output {
    out_send_dns(id, SVC1, DnsRecordType::Aaaa)
}

pub fn out_send_dns_a(id: Id) -> Output {
    out_send_dns(id, HOSTNAME, DnsRecordType::A)
}

pub fn out_attempt_v6_h1_h2(id: Id) -> Output {
    Output::AttemptConnection {
        id,
        endpoint: Endpoint {
            target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), PORT)),
            http_version: ConnectionAttemptHttpVersions::H2OrH1,
            ech_config: None,
        },
        is_ech_retry: false,
    }
}

pub fn out_attempt_v6_h2(id: Id) -> Output {
    Output::AttemptConnection {
        id,
        endpoint: Endpoint {
            target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), PORT)),
            http_version: ConnectionAttemptHttpVersions::H2,
            ech_config: None,
        },
        is_ech_retry: false,
    }
}

pub fn out_attempt_v6_h3(id: Id) -> Output {
    Output::AttemptConnection {
        id,
        endpoint: Endpoint {
            target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), PORT)),
            http_version: ConnectionAttemptHttpVersions::H3,
            ech_config: None,
        },
        is_ech_retry: false,
    }
}

pub fn out_attempt_v6_h3_custom_port(id: Id) -> Output {
    Output::AttemptConnection {
        id,
        endpoint: Endpoint {
            target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), CUSTOM_PORT)),
            http_version: ConnectionAttemptHttpVersions::H3,
            ech_config: None,
        },
        is_ech_retry: false,
    }
}

pub fn out_attempt_v4_h1_h2(id: Id) -> Output {
    Output::AttemptConnection {
        id,
        endpoint: Endpoint {
            target: EndpointTarget::Address(SocketAddr::new(V4_ADDR.into(), PORT)),
            http_version: ConnectionAttemptHttpVersions::H2OrH1,
            ech_config: None,
        },
        is_ech_retry: false,
    }
}

pub fn out_attempt_v4_h2(id: Id) -> Output {
    Output::AttemptConnection {
        id,
        endpoint: Endpoint {
            target: EndpointTarget::Address(SocketAddr::new(V4_ADDR.into(), PORT)),
            http_version: ConnectionAttemptHttpVersions::H2,
            ech_config: None,
        },
        is_ech_retry: false,
    }
}

pub fn out_attempt_v4_h3(id: Id) -> Output {
    Output::AttemptConnection {
        id,
        endpoint: Endpoint {
            target: EndpointTarget::Address(SocketAddr::new(V4_ADDR.into(), PORT)),
            http_version: ConnectionAttemptHttpVersions::H3,
            ech_config: None,
        },
        is_ech_retry: false,
    }
}

pub fn out_attempt_v4_h3_custom_port(id: Id) -> Output {
    Output::AttemptConnection {
        id,
        endpoint: Endpoint {
            target: EndpointTarget::Address(SocketAddr::new(V4_ADDR.into(), CUSTOM_PORT)),
            http_version: ConnectionAttemptHttpVersions::H3,
            ech_config: None,
        },
        is_ech_retry: false,
    }
}

pub fn out_attempt_v6_h2_custom_port(id: Id) -> Output {
    Output::AttemptConnection {
        id,
        endpoint: Endpoint {
            target: EndpointTarget::Address(SocketAddr::new(V6_ADDR.into(), CUSTOM_PORT)),
            http_version: ConnectionAttemptHttpVersions::H2,
            ech_config: None,
        },
        is_ech_retry: false,
    }
}

pub fn out_attempt_v4_h2_custom_port(id: Id) -> Output {
    Output::AttemptConnection {
        id,
        endpoint: Endpoint {
            target: EndpointTarget::Address(SocketAddr::new(V4_ADDR.into(), CUSTOM_PORT)),
            http_version: ConnectionAttemptHttpVersions::H2,
            ech_config: None,
        },
        is_ech_retry: false,
    }
}

pub const fn out_attempt(
    id: Id,
    addr: IpAddr,
    port: u16,
    http_version: ConnectionAttemptHttpVersions,
) -> Output {
    Output::AttemptConnection {
        id,
        endpoint: Endpoint {
            target: EndpointTarget::Address(SocketAddr::new(addr, port)),
            http_version,
            ech_config: None,
        },
        is_ech_retry: false,
    }
}

pub const fn out_resolution_delay() -> Output {
    Output::Timer {
        duration: RESOLUTION_DELAY,
    }
}

pub const fn out_connection_attempt_delay() -> Output {
    Output::Timer {
        duration: CONNECTION_ATTEMPT_DELAY,
    }
}

pub fn setup() -> (Instant, HappyEyeballs) {
    setup_with_config(NetworkConfig::default())
}

pub fn setup_with_config(config: NetworkConfig) -> (Instant, HappyEyeballs) {
    use tracing_subscriber::{
        filter::{LevelFilter, Targets},
        prelude::*,
    };

    let targets = std::env::var("RUST_LOG")
        .ok()
        .and_then(|directives| directives.parse::<Targets>().ok())
        .unwrap_or_else(|| Targets::new().with_default(LevelFilter::ERROR));
    _ = tracing_subscriber::registry()
        .with(tracing_subscriber::fmt::layer().with_test_writer())
        .with(targets)
        .try_init();
    let now = Instant::now();
    let he = HappyEyeballs::new_with_network_config(HOSTNAME, PORT, config).expect("valid host");
    (now, he)
}

/// Assert that the next output is a DNS query for `hostname`/`record_type` with `id`.
pub fn expect_query(
    he: &mut HappyEyeballs,
    now: Instant,
    id: u64,
    hostname: &str,
    rt: DnsRecordType,
) {
    he.expect(out_send_dns(Id::from(id), hostname, rt), now);
}

/// Assert the standard opening burst of DNS queries: HTTPS, AAAA, then A for the
/// default `HOSTNAME` with ids 0, 1, 2.
pub fn expect_initial_dns_queries(he: &mut HappyEyeballs, now: Instant) {
    expect_query(he, now, 0, HOSTNAME, DnsRecordType::Https);
    expect_query(he, now, 1, HOSTNAME, DnsRecordType::Aaaa);
    expect_query(he, now, 2, HOSTNAME, DnsRecordType::A);
}

/// After an HTTPS record names SVC1 as its target, the resolver emits SVC1's
/// AAAA+A queries (ids 3-4) and then the resolution-delay timer.
pub fn expect_svc1_dns_queries(he: &mut HappyEyeballs, now: Instant) {
    he.expect_all(
        [
            out_send_dns(Id::from(3), SVC1, DnsRecordType::Aaaa),
            out_send_dns(Id::from(4), SVC1, DnsRecordType::A),
            out_resolution_delay(),
        ],
        now,
    );
}

/// Like [`expect_svc1_dns_queries`] but for two targets: SVC1 (ids 3-4) and
/// SVC2 (ids 5-6), then the resolution-delay timer.
pub fn expect_svc1_svc2_dns_queries(he: &mut HappyEyeballs, now: Instant) {
    he.expect_all(
        [
            out_send_dns(Id::from(3), SVC1, DnsRecordType::Aaaa),
            out_send_dns(Id::from(4), SVC1, DnsRecordType::A),
            out_send_dns(Id::from(5), SVC2, DnsRecordType::Aaaa),
            out_send_dns(Id::from(6), SVC2, DnsRecordType::A),
            out_resolution_delay(),
        ],
        now,
    );
}
