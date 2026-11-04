//! # Happy Eyeballs v3 Implementation
//!
//! This crate provides an implementation of Happy Eyeballs v3 as specified in
//! [draft-ietf-happy-happyeyeballs-v3-02](https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html).
//!
//! It is implemented as a deterministic, pure state machine. The caller drives
//! all I/O and timers. Current time is explicitly provided by the caller. The
//! state machine itself performs no side effects (e.g. network calls or
//! blocking operations).
//!
//! Happy Eyeballs v3 is an algorithm for improving the performance of dual-stack
//! applications by racing IPv4 and IPv6 connections while optimizing for modern
//! network conditions including HTTPS service discovery and QUIC.
//!
//! ## Usage
//!
//! ```rust
//! # use happy_eyeballs::{
//! #     DnsRecordType, DnsResult, HappyEyeballs, Id, Input, Output, TargetName,
//! # };
//! # use std::{net::{Ipv4Addr, Ipv6Addr}, time::Instant};
//!
//! let mut he = HappyEyeballs::new("example.com", 443).unwrap();
//! let now = Instant::now();
//!
//! // First process outputs from the state machine, e.g. a DNS query to send:
//! # let mut dns_id: Option<Id> = None;
//! while let Some(output) = he.process_output(now) {
//!     match output {
//!         Output::SendDnsQuery { id, hostname, record_type, allow_stale } => {
//!             // Send DNS query. `allow_stale` says whether the resolver may
//!             // answer from an expired cache entry (Optimistic DNS).
//! #           dns_id = Some(id);
//!         }
//!         Output::AttemptConnection { id, endpoint, is_ech_retry } => {
//!             // Attempt connection.
//!         }
//!         _ => {}
//!     }
//! }
//!
//! // Later pass results as input back to the state machine, e.g. a DNS
//! // response arrives:
//! # let dns_result = DnsResult::Aaaa(Ok(vec![Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1)]));
//! he.process_input(Input::DnsResult { id: dns_id.unwrap(), result: dns_result, stale: false }, Instant::now());
//! ```
//!
//! For complete example usage, see the [`tests/`](tests/).

use std::collections::{BTreeMap, HashSet, VecDeque};
use std::fmt::Debug;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr};
use std::num::NonZeroU32;
use std::time::{Duration, Instant};

use thiserror::Error;
use tracing::{debug, trace};
use url::Host as UrlHost;

mod id;
pub use id::Id;
use id::IdGenerator;

/// > The RECOMMENDED value for the Resolution Delay is 50 milliseconds.
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2>
pub const RESOLUTION_DELAY: Duration = Duration::from_millis(50);

/// > Connection Attempt Delay (Section 6): The time to wait between connection
/// > attempts in the absence of RTT data. Recommended to be 250 milliseconds.
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-9>
pub const CONNECTION_ATTEMPT_DELAY: Duration = Duration::from_millis(250);

/// The default multiplier applied to the connection attempt delay after each
/// successive attempt. A value of `1` keeps the delay constant, matching the
/// RFC behavior.
pub const CONNECTION_ATTEMPT_DELAY_MULTIPLIER: NonZeroU32 = NonZeroU32::MIN;

/// Input events to the Happy Eyeballs state machine
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Input {
    /// DNS query result received.
    ///
    /// `stale` is `true` when the resolver answered from an expired (stale)
    /// cache entry, which it may do only for a query that allowed it
    /// (`allow_stale` on [`Output::SendDnsQuery`]). The state machine uses a
    /// stale answer at once and emits a background query to revalidate it, per
    /// [Optimistic DNS].
    ///
    /// [Optimistic DNS]: https://datatracker.ietf.org/doc/draft-gakiwate-dnsop-optimistic-dns/
    DnsResult {
        id: Id,
        result: DnsResult,
        stale: bool,
    },

    /// Connection attempt result
    ConnectionResult { id: Id, result: ConnectionResult },
}

/// An ECH (Encrypted Client Hello) configuration.
///
/// Wraps the raw bytes of one or more serialised `ECHConfig` structures
/// as defined in [RFC 9849 Section 4].
///
/// [RFC 9849 Section 4]: https://datatracker.ietf.org/doc/html/rfc9849#section-4
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EchConfig(Vec<u8>);

impl EchConfig {
    #[must_use]
    pub const fn new(bytes: Vec<u8>) -> Self {
        Self(bytes)
    }
}

impl AsRef<[u8]> for EchConfig {
    fn as_ref(&self) -> &[u8] {
        &self.0
    }
}

/// Result of a connection attempt.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ConnectionResult {
    /// Connection succeeded.
    Success,
    /// Connection failed.
    Failure(String),
    /// The server rejected ECH but provided `retry_configs` (per [RFC 9849
    /// Section 6.1.6]). The state machine will schedule a new connection
    /// attempt to the **same endpoint** (address + HTTP version) using the
    /// updated ECH config.
    ///
    /// A retry to a retry will be ignored. See RFC:
    ///
    /// > Clients SHOULD NOT accept "retry_config" in response to a connection
    /// > initiated in response to a "retry_config".
    ///
    /// [RFC 9849 Section 6.1.6]: https://datatracker.ietf.org/doc/html/rfc9849#section-6.1.6
    EchRetry(EchConfig),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DnsResult {
    Https(Result<Vec<ServiceInfo>, ()>),
    Aaaa(Result<Vec<Ipv6Addr>, ()>),
    A(Result<Vec<Ipv4Addr>, ()>),
}

impl DnsResult {
    /// Returns true if this result provides address information, i.e.
    /// non-empty AAAA/A records or HTTPS records with IP hints.
    fn has_addrs(&self) -> bool {
        match self {
            Self::Aaaa(Ok(v)) => !v.is_empty(),
            Self::A(Ok(v)) => !v.is_empty(),
            Self::Https(Ok(infos)) => infos
                .iter()
                .any(|i| !i.ipv4_hints.is_empty() || !i.ipv6_hints.is_empty()),
            _ => false,
        }
    }

    fn ip_addrs(&self) -> impl Iterator<Item = IpAddr> {
        let v6 = match self {
            Self::Aaaa(Ok(addrs)) => addrs.as_slice(),
            _ => &[],
        };
        let v4 = match self {
            Self::A(Ok(addrs)) => addrs.as_slice(),
            _ => &[],
        };
        v6.iter()
            .copied()
            .map(IpAddr::V6)
            .chain(v4.iter().copied().map(IpAddr::V4))
    }
}

#[derive(Clone, PartialEq, Eq, Hash)]
pub struct TargetName(String);

impl From<&str> for TargetName {
    fn from(s: &str) -> Self {
        Self(s.to_string())
    }
}

impl From<TargetName> for String {
    fn from(t: TargetName) -> Self {
        t.0
    }
}

impl TargetName {
    fn as_str(&self) -> &str {
        &self.0
    }
}

impl Debug for TargetName {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

/// The port of the "https" scheme, whose HTTPS records carry no prefix labels.
const HTTPS_DEFAULT_PORT: u16 = 443;

/// The name under which the HTTPS record for `host` at `port` is published.
///
/// An HTTPS record is scoped to one port: the record for
/// `https://example.com:8443` lives at `_8443._https.example.com`, so it cannot
/// be confused with the record of whatever else the host serves on another
/// port. Only port 443 uses the bare hostname.
///
/// > The HTTPS RR uses Port Prefix Naming (Section 2.3), with one modification:
/// > if the scheme is "https" and the port is 443, then the client's original
/// > QNAME is equal to the service name (i.e., the origin's hostname), without
/// > any prefix labels.
///
/// <https://www.rfc-editor.org/rfc/rfc9460#section-9.1>
fn https_query_name(host: &str, port: u16) -> TargetName {
    if port == HTTPS_DEFAULT_PORT {
        host.into()
    } else {
        TargetName(format!("_{port}._https.{host}"))
    }
}

/// Output events from the Happy Eyeballs state machine
#[derive(Debug, Clone, PartialEq, Eq)]
#[must_use]
pub enum Output {
    /// Send a DNS query.
    ///
    /// `allow_stale` tells the resolver whether it may answer from an expired
    /// (stale) cache entry. It is `true` for a record's first query, so the
    /// resolver can return an optimistic answer without waiting for the
    /// network, and `false` for the follow-up query that revalidates a stale
    /// answer, which must come from a fresh network lookup. See [Optimistic
    /// DNS].
    ///
    /// [Optimistic DNS]: https://datatracker.ietf.org/doc/draft-gakiwate-dnsop-optimistic-dns/
    SendDnsQuery {
        id: Id,
        hostname: TargetName,
        record_type: DnsRecordType,
        allow_stale: bool,
    },

    /// Start a timer
    Timer { duration: Duration },

    /// Attempt to connect to an address.
    ///
    /// `is_ech_retry` is `true` iff this attempt was scheduled in response to
    /// a [`ConnectionResult::EchRetry`] on a prior attempt (i.e. an in-band
    /// ECH configuration update).
    AttemptConnection {
        id: Id,
        endpoint: Endpoint,
        is_ech_retry: bool,
    },

    /// Cancel a connection attempt
    CancelConnection { id: Id },

    /// Connection attempt succeeded
    Succeeded,

    /// Failed to establish a connection, either due to DNS resolution failure
    /// or because all connection attempts have failed.
    Failed(FailureReason),
}

/// Reason for a connection failure.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum FailureReason {
    /// All DNS resolutions failed.
    DnsResolution,
    /// All connection attempts failed.
    Connection,
}

impl Output {
    #[must_use]
    pub fn attempt(self) -> Option<Endpoint> {
        match self {
            Self::AttemptConnection { endpoint, .. } => Some(endpoint),
            _ => None,
        }
    }
}

/// DNS record types
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum DnsRecordType {
    Https,
    Aaaa,
    A,
}

/// Service information from HTTPS records
#[derive(Clone, PartialEq, Eq)]
pub struct ServiceInfo {
    pub priority: u16,
    pub target_name: TargetName,
    pub alpn_http_versions: HashSet<HttpVersion>,
    pub ech_config: Option<EchConfig>,
    pub ipv4_hints: Vec<Ipv4Addr>,
    pub ipv6_hints: Vec<Ipv6Addr>,
    pub port: Option<u16>,
}

#[expect(clippy::missing_fields_in_debug, reason = "empty fields are omitted")]
impl Debug for ServiceInfo {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut debug_struct = f.debug_struct("ServiceInfo");

        debug_struct.field("priority", &self.priority);
        debug_struct.field("target", &self.target_name);

        if !self.alpn_http_versions.is_empty() {
            debug_struct.field("alpn", &self.alpn_http_versions);
        }

        if self.ech_config.is_some() {
            debug_struct.field("ech", &self.ech_config);
        }

        if !self.ipv4_hints.is_empty() {
            debug_struct.field("ipv4", &self.ipv4_hints);
        }

        if !self.ipv6_hints.is_empty() {
            debug_struct.field("ipv6", &self.ipv6_hints);
        }

        debug_struct.finish()
    }
}

impl ServiceInfo {
    fn flatten_into_endpoints(
        &self,
        port: u16,
        // `None` if no A response has arrived yet. `Some(Ok(addrs))` for a
        // positive answer (`addrs` empty for a NODATA answer). `Some(Err(()))`
        // for a negative answer.
        ipv4_addrs: Option<Result<&[Ipv4Addr], ()>>,
        // As `ipv4_addrs`, but for the AAAA query.
        ipv6_addrs: Option<Result<&[Ipv6Addr], ()>>,
        // Filters this record's ALPNs, ECH and IP hints.
        network_config: &NetworkConfig,
        // When `Some(origin_host)`, build by-name endpoints
        // ([`EndpointTarget::Name`]) to this record's target name instead of
        // address endpoints, ignoring the IP hints. Used by
        // [`ResolutionMode::NameWithHttpsRr`]. The origin host is the fallback
        // for a record whose target name is the root (".").
        by_name: Option<&str>,
    ) -> Vec<Endpoint> {
        let port = self.port.unwrap_or(port);
        let ech_enabled = network_config.ech;

        // Each ServiceMode record's ALPN SvcParam lists the protocols available
        // at its own TargetName, so use only this record's ALPNs, never another
        // record's. Assembling the "SVCB ALPN set" -- including adding the
        // scheme default ("http/1.1" for "https") when no "alpn" is present --
        // is the caller's responsibility when interpreting the record (RFC 9460
        // Section 7.1.1). A record that still carries no ALPN here is not usable
        // (a "no-default-alpn" record without "alpn" is not even self-consistent,
        // Section 2.4.3) and yields no endpoints.
        //
        // <https://www.rfc-editor.org/rfc/rfc9460#section-7.1.1>
        let mut versions = self.alpn_http_versions.clone();
        network_config.http_versions.filter_disabled(&mut versions);
        let http_versions = ConnectionAttemptHttpVersions::from_http_versions(&versions);

        // By-name mode: connect to the record's target name over each advertised
        // ALPN, carrying its ECH. Everything below is address racing, which does
        // not apply: the IP hints are ignored.
        if let Some(origin_host) = by_name {
            let ech_config = ech_enabled.then(|| self.ech_config.clone()).flatten();
            // The target name is a DNS name, but this is a connect host (SNI, or
            // a proxy's CONNECT target), so drop the root label's trailing dot.
            // A ServiceMode target of "." denotes the owner name, which is the
            // origin.
            let target = self.target_name.as_str().trim_end_matches('.');
            let host = if target.is_empty() {
                origin_host
            } else {
                target
            };
            return http_versions
                .iter()
                .map(|&http_version| Endpoint {
                    target: EndpointTarget::Name {
                        host: host.to_string(),
                        port,
                    },
                    http_version,
                    ech_config: ech_config.clone(),
                })
                .collect();
        }

        // > ServiceMode records can contain address hints via ipv6hint and
        // > ipv4hint parameters. When these are received, they SHOULD be
        // > considered as positive non-empty answers for the purpose of the
        // > algorithm when A and AAAA records corresponding to the TargetName
        // > are not available yet.
        //
        // <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2.1>
        //
        // The hint is a last-resort fallback the operator put in the SVCB/HTTPS
        // record. The resolved A/AAAA addresses, when present, are tried first
        // (see the ordering below), but the hint is always kept and tried after
        // them; an empty (NODATA) or a negative A/AAAA answer removes no address,
        // so it does not remove the hint either.
        //
        // This is a deliberate deviation from RFC 9460 Section 7.3:
        //
        // > If A and AAAA records for TargetName are locally available, the
        // > client SHOULD ignore these hints.
        //
        // That is a SHOULD, not a MUST, and its stated reason is that relying on
        // the hints can interfere with load balancing and geo-aware selection.
        // We keep that concern satisfied by trying the resolved addresses first
        // and only falling back to the hint when they fail: the hint is an extra
        // chance to connect, never a substitute for the authoritative records.
        //
        // <https://www.rfc-editor.org/rfc/rfc9460#section-7.3>
        let hint_v6: &[Ipv6Addr] = self.ipv6_hints.as_slice();
        let hint_v4: &[Ipv4Addr] = self.ipv4_hints.as_slice();

        let hints = hint_v6
            .iter()
            .copied()
            .map(IpAddr::V6)
            .chain(hint_v4.iter().copied().map(IpAddr::V4))
            // Hints come from the DNS answer whatever the configured address
            // family. On a single-stack network drop those of the disabled
            // family, which is never queried either.
            .filter(|&ip| network_config.ip.allows(ip))
            .flat_map(|ip| {
                // TODO: way around allocation?
                let ech_config = ech_enabled.then(|| self.ech_config.clone()).flatten();
                http_versions.iter().map(move |&http_version| Endpoint {
                    target: EndpointTarget::Address(SocketAddr::new(ip, port)),
                    http_version,
                    ech_config: ech_config.clone(),
                })
            });

        let v6 = ipv6_addrs.and_then(Result::ok).unwrap_or(&[]);
        let v4 = ipv4_addrs.and_then(Result::ok).unwrap_or(&[]);
        let addrs = v6
            .iter()
            .copied()
            .map(IpAddr::V6)
            .chain(v4.iter().copied().map(IpAddr::V4))
            .flat_map(|ip| {
                // TODO: way around allocation?
                let ech_config = ech_enabled.then(|| self.ech_config.clone()).flatten();
                http_versions.iter().map(move |v| Endpoint {
                    target: EndpointTarget::Address(SocketAddr::new(ip, port)),
                    http_version: *v,
                    ech_config: ech_config.clone(),
                })
            });

        // Real addresses first, hints after them as a fallback.
        addrs.chain(hints).collect()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum HttpVersion {
    H3,
    H2,
    H1,
}

/// Possible connection attempt HTTP version combinations.
///
/// While on a QUIC connection attempts one can only use HTTP/3, on a TCP
/// connection attempt one might either negotiate HTTP/2 or HTTP/1.1 via TLS
/// ALPN.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum ConnectionAttemptHttpVersions {
    H3,
    H2OrH1,
    H2,
    H1,
}

impl From<HttpVersion> for ConnectionAttemptHttpVersions {
    fn from(v: HttpVersion) -> Self {
        match v {
            HttpVersion::H3 => Self::H3,
            HttpVersion::H2 => Self::H2,
            HttpVersion::H1 => Self::H1,
        }
    }
}

impl ConnectionAttemptHttpVersions {
    /// [`HttpVersion::H2`] and [`HttpVersion::H1`] into [`ConnectionAttemptHttpVersions::H2OrH1`].
    fn from_http_versions(http_versions: &HashSet<HttpVersion>) -> HashSet<Self> {
        let mut combinations = HashSet::new();
        if http_versions.contains(&HttpVersion::H3) {
            combinations.insert(Self::H3);
        }
        if http_versions.contains(&HttpVersion::H2) && http_versions.contains(&HttpVersion::H1) {
            combinations.insert(Self::H2OrH1);
        } else if http_versions.contains(&HttpVersion::H2) {
            combinations.insert(Self::H2);
        } else if http_versions.contains(&HttpVersion::H1) {
            combinations.insert(Self::H1);
        }
        combinations
    }
}

#[derive(Debug, Clone, PartialEq)]
struct DnsQuery {
    id: Id,
    target_name: TargetName,
    record_type: DnsRecordType,
    state: DnsQueryState,
    /// Optimistic-DNS revalidation of a stale answer for this record.
    refresh: Refresh,
}

#[derive(Debug, Clone, PartialEq)]
enum DnsQueryState {
    InProgress,
    Completed {
        completed: Instant,
        response: DnsResult,
        /// Whether the resolver served this answer from a stale cache entry.
        stale: bool,
    },
}

/// Tracks the background query that revalidates a stale answer, per Optimistic
/// DNS. A stale answer is revalidated at most once.
#[derive(Debug, Clone, PartialEq)]
enum Refresh {
    /// No revalidation is in flight: the answer is fresh, or a stale answer has
    /// not been revalidated yet.
    Idle,
    /// A revalidation query is in flight, carrying this id.
    InFlight(Id),
    /// A stale answer has been revalidated.
    Done,
}

impl DnsQuery {
    const fn response(&self) -> Option<&DnsResult> {
        match &self.state {
            DnsQueryState::InProgress => None,
            DnsQueryState::Completed { response, .. } => Some(response),
        }
    }

    const fn is_completed(&self) -> bool {
        matches!(self.state, DnsQueryState::Completed { .. })
    }
}

/// Configuration for supported HTTP versions.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HttpVersions {
    /// Whether HTTP/1.1 is enabled.
    pub h1: bool,
    /// Whether HTTP/2 is enabled.
    pub h2: bool,
    /// Whether HTTP/3 is enabled.
    pub h3: bool,
}

impl HttpVersions {
    /// Remove the [`HttpVersion`]s disabled by this configuration from `versions`.
    fn filter_disabled(&self, versions: &mut HashSet<HttpVersion>) {
        if !self.h3 {
            versions.remove(&HttpVersion::H3);
        }
        if !self.h2 {
            versions.remove(&HttpVersion::H2);
        }
        if !self.h1 {
            versions.remove(&HttpVersion::H1);
        }
    }
}

impl Default for HttpVersions {
    fn default() -> Self {
        // Enable all by default.
        Self {
            h1: true,
            h2: true,
            h3: true,
        }
    }
}

/// IP connectivity and preference mode.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum IpPreference {
    /// Dual-stack available, prefer IPv6 over IPv4.
    DualStackPreferV6,
    /// Dual-stack available, prefer IPv4 over IPv6.
    DualStackPreferV4,
    /// IPv6-only network.
    Ipv6Only,
    /// IPv4-only network.
    Ipv4Only,
}

impl IpPreference {
    fn address_record_types(&self) -> impl Iterator<Item = DnsRecordType> {
        let aaaa = matches!(
            self,
            Self::DualStackPreferV6 | Self::DualStackPreferV4 | Self::Ipv6Only
        )
        .then_some(DnsRecordType::Aaaa);
        let a = matches!(
            self,
            Self::DualStackPreferV6 | Self::DualStackPreferV4 | Self::Ipv4Only
        )
        .then_some(DnsRecordType::A);
        aaaa.into_iter().chain(a)
    }

    /// Whether an address of `ip`'s family may be attempted. On a single-stack
    /// network that is only the enabled family, matching the record types
    /// queried per [`Self::address_record_types`].
    const fn allows(&self, ip: IpAddr) -> bool {
        match self {
            Self::DualStackPreferV6 | Self::DualStackPreferV4 => true,
            Self::Ipv6Only => ip.is_ipv6(),
            Self::Ipv4Only => ip.is_ipv4(),
        }
    }
}

/// Alternative service information from previous connections.
///
/// See [RFC 7838](https://datatracker.ietf.org/doc/html/rfc7838).
#[derive(Debug, Clone)]
pub struct AltSvc {
    pub host: Option<String>,
    pub port: Option<u16>,
    pub http_version: HttpVersion,
}

// TODO: Should we make HappyEyeballs proxy aware? E.g. should it know that the
// proxy is resolving the domain? Should it still trigger an HTTP RR lookup to
// see whether the remote supports HTTP/3? Should it first do MASQUE connect-udp
// and HTTP/3 and then HTTP CONNECT with HTTP/2?
//
// TODO: Should we make HappyEyeballs aware of whether this is a WebSocket
// connection? That way we could e.g. track EXTENDED CONNECT support, or
// fallback to a different connection in case WebSocket doesn't work? Likely for
// v2 of the project.
//
// TODO: Should we make HappyEyeballs aware of whether this is a WebTransport
// connection? That way we could e.g. track EXTENDED CONNECT support, or
// fallback to a different connection in case WebTransport doesn't work? Likely
// for v2 of the project.
//
/// Network configuration for Happy Eyeballs behavior
#[derive(Debug, Clone)]
pub struct NetworkConfig {
    /// Supported HTTP versions
    pub http_versions: HttpVersions,
    /// IP connectivity and preference
    pub ip: IpPreference,
    /// Alternative services from previous connections
    pub alt_svc: Vec<AltSvc>,
    /// The time to wait after receiving the first DNS response before moving on
    /// to the connection phase, giving the remaining queries a chance to arrive.
    ///
    /// Defaults to [`RESOLUTION_DELAY`] (50 ms) per
    /// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2>.
    pub resolution_delay: Duration,
    /// The time to wait between successive connection attempts.
    ///
    /// Defaults to [`CONNECTION_ATTEMPT_DELAY`] (250 ms) per
    /// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-9>.
    pub connection_attempt_delay: Duration,
    /// Multiplier applied to [`connection_attempt_delay`](Self::connection_attempt_delay)
    /// as concurrent connection attempts pile up, growing the delay
    /// exponentially.
    ///
    /// The delay before starting another attempt while `n` attempts are already
    /// in progress is `connection_attempt_delay * multiplier^(n - 1)`. With a
    /// base delay of 250 ms and a multiplier of `2`, racing attempts are
    /// scheduled at `t=0`, `t=250`, `t=750`, `t=1750`, ... (intervals of 250,
    /// 500, 1000 ms). This lets callers lower the base delay below the
    /// RFC-recommended 250 ms while still backing off between attempts.
    ///
    /// Only in-progress attempts count, so attempts triggered by a previous
    /// attempt failing do not grow the delay.
    ///
    /// Defaults to [`CONNECTION_ATTEMPT_DELAY_MULTIPLIER`] (`1`), which keeps the
    /// delay constant per the RFC.
    pub connection_attempt_delay_multiplier: NonZeroU32,
    /// Whether Encrypted Client Hello (ECH) is enabled.
    ///
    /// When `false`, ECH configs from HTTPS records are ignored: endpoints
    /// always get `ech_config: None` and the ECH-based filtering (skip
    /// non-ECH `ServiceInfos`, skip origin fallback) does not apply.
    ///
    /// Defaults to `true`.
    pub ech: bool,
    /// Whether to wait for an answer for the preferred address family before
    /// moving on to the connection phase.
    ///
    /// Per the spec, moving on without waiting out the resolution delay
    /// requires a positive or negative answer for the preferred address family
    /// (e.g. AAAA when IPv6 is preferred). When that answer is slow to arrive,
    /// a client that already has the non-preferred family (e.g. A) still waits
    /// out the [`resolution_delay`](Self::resolution_delay).
    ///
    /// When `false`, that requirement is dropped: once positive address answers
    /// have been received and the SVCB/HTTPS query has completed (whether with a
    /// positive or a negative response), the state machine moves on without
    /// waiting for the preferred address family answer (and thus without the
    /// resolution delay when the non-preferred family arrives first). The delay
    /// still applies while the SVCB/HTTPS query is outstanding.
    ///
    /// Defaults to `true`, matching
    /// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2>.
    pub wait_for_preferred_address: bool,
    /// How the origin host is resolved before connecting.
    ///
    /// Defaults to [`ResolutionMode::Ip`], the normal Happy Eyeballs path.
    pub resolution: ResolutionMode,
}

/// How the origin host is turned into connection attempts.
///
/// Happy Eyeballs normally resolves the target host and races the resulting
/// IPs. That is wrong when the host should not (or cannot) be resolved
/// client-side, e.g. when a proxy resolves the hostname for us, or when
/// establishing an inner proxy connection. In those cases the client has no IPs
/// to race and should connect by hostname instead. The two by-name variants
/// cover that, differing only in whether the HTTPS (SVCB) record is fetched for
/// its ALPN.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub enum ResolutionMode {
    /// Resolve the origin (A, AAAA, and HTTPS records) and race the resulting
    /// IPs. The default Happy Eyeballs behavior.
    #[default]
    Ip,
    /// Connect to the origin by name with no client-side DNS whatsoever: neither
    /// the HTTPS record nor A/AAAA are queried.
    ///
    /// The state machine emits no [`Output::SendDnsQuery`] and produces by-name
    /// connection attempts ([`EndpointTarget::Name`]) for the origin host and
    /// port over the enabled H2/H1 versions, attempting immediately. Alt-svc
    /// entries are attempted by name too, over their advertised protocol.
    ///
    /// Use this when even a single leaked DNS query is unacceptable, e.g. when
    /// the resolver is the operating system's and every query must stay inside
    /// the proxy connection.
    Name,
    /// Connect to the origin by name, but first fetch the origin's HTTPS (SVCB)
    /// record to learn its ALPN.
    ///
    /// Only the HTTPS query is sent; no A or AAAA query is emitted and no
    /// address-family racing happens. The state machine waits for the HTTPS
    /// answer, then produces one by-name connection attempt
    /// ([`EndpointTarget::Name`]) per advertised ALPN version (so a record
    /// advertising h3 yields a by-name h3 attempt), using the record's target
    /// name when it aliases (otherwise the origin host) and its port, and
    /// carrying its ECH config. The record's `ipv4hint`/`ipv6hint` are ignored:
    /// they are never used to race IPs. If the HTTPS query fails, is empty, or
    /// is negative, the machine falls back to the by-name origin over the
    /// enabled H2/H1 versions.
    ///
    /// Alt-svc entries are attempted by name too, over their advertised
    /// protocol, so an alt-svc that advertises h3 is raced over h3.
    ///
    /// Use this when the resolver is trusted not to leak the query (e.g. `DoH`),
    /// so the ALPN (and thus HTTP/3) can be honored while still connecting by
    /// name.
    NameWithHttpsRr,
}

impl Default for NetworkConfig {
    fn default() -> Self {
        Self {
            http_versions: HttpVersions::default(),
            ip: IpPreference::DualStackPreferV6,
            alt_svc: Vec::new(),
            resolution_delay: RESOLUTION_DELAY,
            connection_attempt_delay: CONNECTION_ATTEMPT_DELAY,
            connection_attempt_delay_multiplier: CONNECTION_ATTEMPT_DELAY_MULTIPLIER,
            ech: true,
            wait_for_preferred_address: true,
            resolution: ResolutionMode::Ip,
        }
    }
}

impl NetworkConfig {
    const fn prefer_v6(&self) -> bool {
        match self.ip {
            IpPreference::DualStackPreferV6 | IpPreference::Ipv6Only => true,
            IpPreference::DualStackPreferV4 | IpPreference::Ipv4Only => false,
        }
    }

    const fn preferred_dns_record_type(&self) -> DnsRecordType {
        match self.ip {
            IpPreference::DualStackPreferV6 | IpPreference::Ipv6Only => DnsRecordType::Aaaa,
            IpPreference::DualStackPreferV4 | IpPreference::Ipv4Only => DnsRecordType::A,
        }
    }

    const fn is_http_version_disabled(&self, http_version: HttpVersion) -> bool {
        match http_version {
            HttpVersion::H3 => !self.http_versions.h3,
            HttpVersion::H2 => !self.http_versions.h2,
            HttpVersion::H1 => !self.http_versions.h1,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ConnectionState {
    InProgress,
    Succeeded,
    Failed,
    Cancelled,
}

#[derive(Debug, Clone)]
pub struct ConnectionAttempt {
    pub id: Id,
    pub endpoint: Endpoint,
    pub started: Instant,
    pub state: ConnectionState,
    /// Whether this attempt was initiated by an ECH `retry_config`.
    /// Per RFC 9849 Section 6.1.6, a second `EchRetry` on such an attempt
    /// must be treated as a failure.
    pub is_ech_retry: bool,
}

impl ConnectionAttempt {
    fn within_delay(&self, now: Instant, connection_attempt_delay: Duration) -> bool {
        now.duration_since(self.started) < connection_attempt_delay
    }
}

/// What an [`Endpoint`] connects to: either a resolved socket address (the
/// normal Happy Eyeballs path) or a bare hostname and port to connect to
/// without client-side resolution.
///
/// The by-name variant exists for cases where the host's address should not (or
/// cannot) be resolved client-side, e.g. when a proxy resolves the hostname for
/// us, or when establishing an inner proxy connection. See
/// [`NetworkConfig::resolution`] and [`ResolutionMode`].
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum EndpointTarget {
    /// A resolved socket address to connect to directly.
    Address(SocketAddr),
    /// A hostname and port to connect to by name, leaving *address* resolution
    /// to a downstream party (e.g. a proxy), so there is no address family to
    /// race. This does not imply that no DNS happens at all:
    /// [`ResolutionMode::NameWithHttpsRr`] still queries the origin's HTTPS
    /// record for its ALPN.
    Name { host: String, port: u16 },
}

/// All information (target, HTTP version, ...) needed to attempt a connection to a specific endpoint.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Endpoint {
    pub target: EndpointTarget,
    pub http_version: ConnectionAttemptHttpVersions,
    pub ech_config: Option<EchConfig>,
}

impl Endpoint {
    /// The resolved socket address for this endpoint, or [`None`] for a by-name
    /// target (see [`EndpointTarget::Name`]).
    #[must_use]
    pub const fn address(&self) -> Option<SocketAddr> {
        match self.target {
            EndpointTarget::Address(address) => Some(address),
            EndpointTarget::Name { .. } => None,
        }
    }
}

/// Interleave a group's endpoints across protocol variants and address
/// families so the diversity of options is tried early, instead of draining
/// every attempt of one variant before moving on to the next.
///
/// Endpoints are grouped by `(protocol variant, address family)` and dealt one
/// from each group per round, groups ordered by protocol preference and then
/// preferred family. For three IPv6 and one IPv4 address that each offer HTTP/3
/// and HTTP/2 that yields:
///
/// 1. v6a / H3 (most preferred)
/// 2. v4 / H3 (next address family)
/// 3. v6a / `H2OrH1` (next protocol)
/// 4. v4 / `H2OrH1`
/// 5. v6b / H3 (second round)
/// 6. v6b / `H2OrH1`
/// 7. v6c / H3
/// 8. v6c / `H2OrH1`
///
/// so IPv4 (the other family) and HTTP/2 (the other protocol) are both reached
/// within the first few attempts, rather than after every IPv6 HTTP/3 attempt.
///
/// All endpoints belong to the same group (same application protocols and
/// security properties, same service priority). The round-robin honors the
/// draft's two interleavings.
///
/// Address families, per Section 5.3:
///
/// > Whichever address family is first in the list should be followed by an
/// > endpoint of the other address family.
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-03.html#section-5.3>
///
/// Protocol variants, per Section 5.1.1, since the HTTP version (HTTP/3 over
/// QUIC vs. HTTP/2 over TCP) is non-critical here:
///
/// > Clients SHOULD avoid grouping and sorting separately in cases where their
/// > use of an application protocol or feature is non-critical.
///
/// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-03.html#section-5.1.1>
fn interleave_endpoints(endpoints: Vec<Endpoint>, prefer_v6: bool) -> Vec<Endpoint> {
    // Where an address family sits relative to the preference; `Preferred` sorts
    // before `Other`, which orders the preferred family first.
    #[derive(PartialEq, Eq, PartialOrd, Ord)]
    enum FamilyPreference {
        Preferred,
        Other,
    }

    let total = endpoints.len();

    // Group endpoints into a queue per (protocol, address family), keeping DNS
    // order. The `BTreeMap` orders the queues most preferred first: by protocol
    // (the enum is ordered `H3 < H2OrH1 < H2 < H1`), then by family preference.
    let mut groups: BTreeMap<
        (ConnectionAttemptHttpVersions, FamilyPreference),
        VecDeque<Endpoint>,
    > = BTreeMap::new();
    for endpoint in endpoints {
        let family = match &endpoint.target {
            EndpointTarget::Address(address) => {
                if address.is_ipv6() == prefer_v6 {
                    FamilyPreference::Preferred
                } else {
                    FamilyPreference::Other
                }
            }
            // A by-name target has no address family to alternate with, so it
            // groups on its own as the preferred family and is dealt promptly.
            EndpointTarget::Name { .. } => FamilyPreference::Preferred,
        };
        groups
            .entry((endpoint.http_version, family))
            .or_default()
            .push_back(endpoint);
    }

    // Deal the front of every queue, round after round, dropping queues as they
    // empty, until none are left.
    let mut ordered = Vec::with_capacity(total);
    while !groups.is_empty() {
        groups.retain(|_, queue| {
            if let Some(endpoint) = queue.pop_front() {
                ordered.push(endpoint);
            }
            !queue.is_empty()
        });
    }
    ordered
}

#[derive(Debug, Clone)]
enum Host {
    Ip(IpAddr),
    Domain(String),
}

impl From<UrlHost> for Host {
    fn from(host: UrlHost) -> Self {
        match host {
            UrlHost::Ipv4(v4) => Self::Ip(IpAddr::V4(v4)),
            UrlHost::Ipv6(v6) => Self::Ip(IpAddr::V6(v6)),
            UrlHost::Domain(d) => Self::Domain(d),
        }
    }
}

impl std::fmt::Display for Host {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Ip(ip) => write!(f, "{ip}"),
            Self::Domain(d) => write!(f, "{d}"),
        }
    }
}

/// Happy Eyeballs v3 state machine
pub struct HappyEyeballs {
    id_generator: IdGenerator,
    dns_queries: Vec<DnsQuery>,
    connection_attempts: Vec<ConnectionAttempt>,
    /// ECH retries received over the lifetime of this state machine.
    /// Each entry is `(previous_attempt_id, new_ech_config)`.
    ech_retries: Vec<(Id, EchConfig)>,
    /// Network configuration
    network_config: NetworkConfig,
    host: Host,
    port: u16,
}

#[derive(Error, Debug)]
#[error(transparent)]
pub struct ConstructorError {
    inner: ConstructorErrorInner,
}

impl From<ConstructorErrorInner> for ConstructorError {
    fn from(inner: ConstructorErrorInner) -> Self {
        Self { inner }
    }
}

#[derive(Error, Debug)]
enum ConstructorErrorInner {
    #[error("invalid host: {0}")]
    InvalidHost(#[from] url::ParseError),
}

#[expect(clippy::missing_fields_in_debug, reason = "empty fields are omitted")]
impl Debug for HappyEyeballs {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut ds = f.debug_struct("HappyEyeballs");

        // Always include target and network configuration.
        ds.field("target", &self.host);
        ds.field("port", &self.port);
        ds.field("network_config", &self.network_config);

        // Only include vectors when non-empty to reduce noise.
        if !self.dns_queries.is_empty() {
            ds.field("dns_queries", &self.dns_queries);
        }
        if !self.connection_attempts.is_empty() {
            ds.field("connection_attempts", &self.connection_attempts);
        }
        if !self.ech_retries.is_empty() {
            ds.field("ech_retries", &self.ech_retries);
        }

        ds.finish()
    }
}

impl HappyEyeballs {
    /// Create a new Happy Eyeballs state machine with default network config
    ///
    /// # Errors
    ///
    /// When `host` is not a valid host.
    pub fn new(host: &str, port: u16) -> Result<Self, ConstructorError> {
        Self::new_with_network_config(host, port, NetworkConfig::default())
    }

    /// Create a new Happy Eyeballs state machine with custom network configuration
    ///
    /// # Errors
    ///
    /// When `host` is not a valid host.
    pub fn new_with_network_config(
        host: &str,
        port: u16,
        network_config: NetworkConfig,
    ) -> Result<Self, ConstructorError> {
        // Prefer URL-style host parsing (domains and bracketed IPv6).
        // If that fails, accept raw IP literals (IPv4/IPv6) without brackets.
        let host = match UrlHost::parse(host) {
            Ok(h) => Host::from(h),
            Err(e) => match host.parse::<IpAddr>() {
                Ok(ip) => Host::Ip(ip),
                Err(_) => return Err(ConstructorErrorInner::InvalidHost(e).into()),
            },
        };
        let s = Self {
            id_generator: IdGenerator::new(),
            network_config,
            dns_queries: Vec::new(),
            connection_attempts: Vec::new(),
            ech_retries: Vec::new(),
            host,
            port,
        };
        trace!("new_with_network_config: {s:?}");
        Ok(s)
    }

    /// Process an input event
    ///
    /// Updates internal state based on the input.
    ///
    /// After calling this, call [`HappyEyeballs::process_output`] to get any pending outputs.
    pub fn process_input(&mut self, input: Input, now: Instant) {
        trace!("target={} input={input:?}", self.host);

        match input {
            Input::DnsResult { id, result, stale } => {
                self.on_dns_response(id, result, stale, now);
            }
            Input::ConnectionResult { id, result } => {
                self.on_connection_result(id, result);
            }
        }
    }

    // TODO: Does this ever return None given the timeouts?
    /// Generate output based on current state
    ///
    /// Call this to advance the state machine and get any pending outputs.
    ///
    /// The caller must call [`HappyEyeballs::process_output`] repeatedly
    /// until it returns [`None`] or [`Output::Timer`].
    #[must_use]
    pub fn process_output(&mut self, now: Instant) -> Option<Output> {
        let output = self.process_output_inner(now);
        trace!("target={} process_output: {output:?}", self.host);
        output
    }

    fn process_output_inner(&mut self, now: Instant) -> Option<Output> {
        // Check if we have any successful connection that requires canceling other attempts.
        if let Some(o) = self.cancel_remaining_attempts() {
            return Some(o);
        }

        // Attempt connections.
        if let Some(o) = self.connection_attempt(now) {
            return Some(o);
        }

        // Send DNS queries. Which ones depends on the resolution mode.
        match self.network_config.resolution {
            // Resolve everything and race the addresses: the origin's HTTPS and
            // A/AAAA records, plus any HTTPS target name and alt-svc host.
            ResolutionMode::Ip => {
                if let Some(o) = self.send_dns_request() {
                    return Some(o);
                }

                if let Some(o) = self.send_dns_request_for_target_name() {
                    return Some(o);
                }

                if let Some(o) = self.send_dns_request_for_alt_svc() {
                    return Some(o);
                }

                if let Some(o) = self.send_dns_refresh() {
                    return Some(o);
                }
            }
            // Only the origin HTTPS record, for its ALPN (A/AAAA are gated off
            // inside `send_dns_request`). The target name and alt-svc hosts are
            // attempted by name, so neither is resolved.
            ResolutionMode::NameWithHttpsRr => {
                if let Some(o) = self.send_dns_request() {
                    return Some(o);
                }

                if let Some(o) = self.send_dns_refresh() {
                    return Some(o);
                }
            }
            // No client-side DNS whatsoever.
            ResolutionMode::Name => {}
        }

        if let Some(o) = self.delay(now) {
            return Some(o);
        }

        if let Some(reason) = self.failed() {
            return Some(Output::Failed(reason));
        }

        // TODO: Instead of returning None, how about happy-eyeballs also owns
        // the dns and connection attempt timeout, thus returning either that
        // timeout, or Output::Failed here.
        None
    }

    /// The delay to wait before starting the next connection attempt, growing
    /// exponentially with the number of attempts currently in progress per the
    /// configured [`connection_attempt_delay_multiplier`](NetworkConfig::connection_attempt_delay_multiplier).
    ///
    /// Only in-progress (racing) attempts count: an attempt that has already
    /// failed does not inflate the delay, so a sequence of attempts each
    /// triggered by the previous one failing keeps the base delay.
    fn connection_attempt_delay(&self) -> Duration {
        let base = self.network_config.connection_attempt_delay;
        let in_progress = self
            .connection_attempts
            .iter()
            .filter(|a| a.state == ConnectionState::InProgress)
            .count();
        let exponent = u32::try_from(in_progress)
            .unwrap_or(u32::MAX)
            .saturating_sub(1);
        let factor = self
            .network_config
            .connection_attempt_delay_multiplier
            .get()
            .checked_pow(exponent)
            .unwrap_or(u32::MAX);
        base.checked_mul(factor).unwrap_or(Duration::MAX)
    }

    fn delay(&self, now: Instant) -> Option<Output> {
        // If we have a successful connection, no connection attempt delay
        // needed.
        if self.has_successful_connection() {
            return None;
        }

        let connection_attempt_delay = self.connection_attempt_delay();
        if let Some(remaining) = self
            .connection_attempts
            .iter()
            .filter(|a| a.state == ConnectionState::InProgress)
            .map(|a| &a.started)
            .max()
            .and_then(|started| {
                let elapsed = now.duration_since(*started);
                connection_attempt_delay
                    .checked_sub(elapsed)
                    .filter(|d| !d.is_zero())
            })
        {
            return Some(Output::Timer {
                duration: remaining,
            });
        }

        // If we have no in-progress DNS queries, no resolution delay needed.
        if !self.dns_queries.iter().any(|q| !q.is_completed()) {
            return None;
        }

        // Considers every query type, SVCB/HTTPS included, not just A and AAAA:
        // the delay exists to receive the preferred addresses and the service
        // information together. See draft-ietf-happy-happyeyeballs-v3-04:
        // <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-04.html#section-4.2>
        self.dns_queries
            .iter()
            .filter_map(|q| match &q.state {
                DnsQueryState::Completed { completed, .. } => Some(completed),
                DnsQueryState::InProgress => None,
            })
            .min()
            .and_then(|completed| {
                let elapsed = now.duration_since(*completed);
                self.network_config
                    .resolution_delay
                    .checked_sub(elapsed)
                    .filter(|d| !d.is_zero())
            })
            .map(|duration| Output::Timer { duration })
    }

    fn send_dns_request(&mut self) -> Option<Output> {
        let target_name: TargetName = match &self.host {
            Host::Ip(_) => {
                // No DNS queries needed for IP hosts.
                return None;
            }
            Host::Domain(domain) => domain.as_str(),
        }
        .into();

        // `ResolutionMode::NameWithHttpsRr` fetches only the HTTPS record for
        // its ALPN; it must never emit an A or AAAA query.
        let address_record_types = (self.network_config.resolution
            != ResolutionMode::NameWithHttpsRr)
            .then(|| self.network_config.ip.address_record_types())
            .into_iter()
            .flatten();
        let record_types = std::iter::once(DnsRecordType::Https).chain(address_record_types);
        for record_type in record_types {
            if !self
                .dns_queries
                .iter()
                .any(|q| q.record_type == record_type)
            {
                let id = self.id_generator.next_id();
                // The HTTPS record is published per port, so a non-443 origin
                // has to ask for the port-prefixed name. Address records are
                // never prefixed.
                let query_name = match record_type {
                    DnsRecordType::Https => https_query_name(target_name.as_str(), self.port),
                    DnsRecordType::Aaaa | DnsRecordType::A => target_name,
                };
                self.dns_queries.push(DnsQuery {
                    id,
                    target_name: query_name.clone(),
                    record_type,
                    state: DnsQueryState::InProgress,
                    refresh: Refresh::Idle,
                });
                return Some(Output::SendDnsQuery {
                    id,
                    hostname: query_name,
                    record_type,
                    allow_stale: true,
                });
            }
        }

        None
    }

    // TODO: Limit number of target names.
    /// > Note that clients are still required to issue A and AAAA queries
    /// > for those TargetNames if they haven't yet received those records.
    ///
    /// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2.1>
    fn send_dns_request_for_target_name(&mut self) -> Option<Output> {
        let any_ech = self.any_ech();

        let target_names = self
            .completed_service_infos()
            // When any ServiceInfo has ECH, skip resolving targets without ECH.
            .filter(move |i| !any_ech || i.ech_config.is_some())
            .map(|i| &i.target_name);

        // Next AAAA or A query, respecting single-stack preferences.
        let (target_name, record_type) = target_names
            .flat_map(|tn| {
                self.network_config
                    .ip
                    .address_record_types()
                    .map(move |rt| (tn, rt))
            })
            .find(|(tn, rt)| {
                !self
                    .dns_queries
                    .iter()
                    .any(|q| q.target_name == **tn && q.record_type == *rt)
            })?;

        let target_name = target_name.clone();
        let id = self.id_generator.next_id();
        self.dns_queries.push(DnsQuery {
            id,
            target_name: target_name.clone(),
            record_type,
            state: DnsQueryState::InProgress,
            refresh: Refresh::Idle,
        });
        Some(Output::SendDnsQuery {
            id,
            hostname: target_name,
            record_type,
            allow_stale: true,
        })
    }

    /// A/AAAA queries for alt-svc entries that name a custom host.
    ///
    /// Alt-svc hosts that are IP literals need no resolution and are skipped.
    fn send_dns_request_for_alt_svc(&mut self) -> Option<Output> {
        let hosts = self
            .network_config
            .alt_svc
            .iter()
            .filter_map(|a| a.host.as_deref())
            .filter(|h| h.parse::<IpAddr>().is_err());

        let (target_name, record_type) = hosts
            .flat_map(|h| {
                self.network_config
                    .ip
                    .address_record_types()
                    .map(move |rt| (h, rt))
            })
            .find(|(h, rt)| {
                !self
                    .dns_queries
                    .iter()
                    .any(|q| q.target_name.as_str() == *h && q.record_type == *rt)
            })?;

        let target_name: TargetName = target_name.into();
        let id = self.id_generator.next_id();
        self.dns_queries.push(DnsQuery {
            id,
            target_name: target_name.clone(),
            record_type,
            state: DnsQueryState::InProgress,
            refresh: Refresh::Idle,
        });
        Some(Output::SendDnsQuery {
            id,
            hostname: target_name,
            record_type,
            allow_stale: true,
        })
    }

    /// Emit a background query to revalidate an answer the resolver served from
    /// a stale cache entry, per [Optimistic DNS].
    ///
    /// The state machine has already used the stale answer to race connections;
    /// this query forbids a stale answer (`allow_stale: false`) so the resolver
    /// performs a fresh network lookup. When the fresh answer arrives it
    /// replaces the stale one. Each stale answer is revalidated at most once.
    ///
    /// [Optimistic DNS]: https://datatracker.ietf.org/doc/draft-gakiwate-dnsop-optimistic-dns/
    fn send_dns_refresh(&mut self) -> Option<Output> {
        let idx = self.dns_queries.iter().position(|q| {
            q.refresh == Refresh::Idle
                && matches!(q.state, DnsQueryState::Completed { stale: true, .. })
        })?;
        let id = self.id_generator.next_id();
        let query = &mut self.dns_queries[idx];
        query.refresh = Refresh::InFlight(id);
        Some(Output::SendDnsQuery {
            id,
            hostname: query.target_name.clone(),
            record_type: query.record_type,
            allow_stale: false,
        })
    }

    fn on_dns_response(&mut self, id: Id, response: DnsResult, stale: bool, now: Instant) {
        // A revalidation response replaces the stale answer of the query it
        // belongs to, rather than opening a new record.
        if let Some(query) = self
            .dns_queries
            .iter_mut()
            .find(|q| q.refresh == Refresh::InFlight(id))
        {
            // A refresh query is sent with `allow_stale: false`, so the resolver
            // must not answer it from a stale cache entry.
            debug_assert!(
                !stale,
                "got a stale response for refresh query {id:?}, which forbade stale answers"
            );
            query.refresh = Refresh::Done;
            query.state = DnsQueryState::Completed {
                completed: now,
                response,
                stale,
            };
            return;
        }

        let Some(query) = self.dns_queries.iter_mut().find(|q| q.id == id) else {
            debug_assert!(false, "got {response:?} for unknown id {id:?}");
            return;
        };

        if query.is_completed() {
            debug_assert!(false, "got {response:?} for already completed {query:?}");
            return;
        }

        query.state = DnsQueryState::Completed {
            completed: now,
            response,
            stale,
        };
    }

    fn on_connection_result(&mut self, id: Id, result: ConnectionResult) {
        let Some(attempt) = self.connection_attempts.iter_mut().find(|a| a.id == id) else {
            debug_assert!(false, "got connection result for unknown id {id:?}");
            return;
        };

        match attempt.state {
            ConnectionState::InProgress => {}
            ConnectionState::Cancelled => {
                debug!("ignoring connection result for cancelled attempt {id:?}: {result:?}");
                return;
            }
            ConnectionState::Succeeded | ConnectionState::Failed => {
                debug_assert!(
                    false,
                    "got connection result but attempt is in unexpected state: {attempt:?}"
                );
                return;
            }
        }

        match result {
            ConnectionResult::Success => {
                attempt.state = ConnectionState::Succeeded;
                // Cancellations will be issued by cancel_remaining_attempts()
            }
            ConnectionResult::Failure(_error) => {
                attempt.state = ConnectionState::Failed;
                // The state machine will naturally attempt the next connection
                // when process() is called again with None input
            }
            ConnectionResult::EchRetry(ech_config) => {
                attempt.state = ConnectionState::Failed;

                if !self.network_config.ech {
                    debug_assert!(false, "got EchRetry on attempt {id:?} but ECH is disabled");
                    return;
                }

                if attempt.endpoint.ech_config.is_none() {
                    debug_assert!(false, "got EchRetry on attempt {id:?} but ECH was not sent");
                    return;
                }

                // > Clients SHOULD NOT accept "retry_config" in response
                // > to a connection initiated in response to a
                // > "retry_config".
                //
                // https://datatracker.ietf.org/doc/html/rfc9849#section-6.1.6
                if attempt.is_ech_retry {
                    debug!("ignoring EchRetry on attempt {id:?} that is itself an ECH retry");
                    return;
                }

                self.ech_retries.push((id, ech_config));
            }
        }
    }

    /// If a connection has succeeded, cancel all remaining in-progress attempts.
    fn cancel_remaining_attempts(&mut self) -> Option<Output> {
        // Check if we have a successful connection
        if !self.has_successful_connection() {
            return None;
        }

        // Find the first in-progress attempt to cancel
        if let Some(attempt) = self
            .connection_attempts
            .iter_mut()
            .find(|a| a.state == ConnectionState::InProgress)
        {
            let id = attempt.id;
            attempt.state = ConnectionState::Cancelled;
            return Some(Output::CancelConnection { id });
        }

        // All connections have been canceled, return Succeeded
        Some(Output::Succeeded)
    }

    /// > The client moves onto sorting addresses and establishing connections
    /// > once one of the following condition sets is met:
    /// >
    /// > Either:
    /// >  
    /// > - Some positive (non-empty) address answers have been received AND
    /// > - A postive (non-empty) or negative (empty) answer has been received for the preferred address family that was queried AND
    /// > - SVCB/HTTPS service information has been received (or has received a negative response)
    /// >
    /// > Or:
    /// > - Some positive (non-empty) address answers have been received AND
    /// > - A resolution time delay has passed after which other answers have not been received
    ///
    /// <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2>
    fn connection_attempt(&mut self, now: Instant) -> Option<Output> {
        // ECH retries are emitted immediately, bypassing move-on and delay checks.
        if let Some(o) = self.ech_retry_attempt(now) {
            return Some(o);
        }

        let mut move_on = false;
        move_on |= self.move_on_without_timeout();
        move_on |= self.move_on_with_timeout(now);
        move_on |= matches!(self.host, Host::Ip(_));
        // `ResolutionMode::Name` has no DNS to wait for at all: move on
        // immediately. `ResolutionMode::NameWithHttpsRr` instead waits for the
        // origin HTTPS answer, which `move_on_without_timeout` keys on.
        move_on |= self.network_config.resolution == ResolutionMode::Name;
        if !move_on {
            return None;
        }

        let connection_attempt_delay = self.connection_attempt_delay();
        if self
            .connection_attempts
            .iter()
            .filter(|a| a.state == ConnectionState::InProgress)
            .any(|a| a.within_delay(now, connection_attempt_delay))
        {
            return None;
        }
        let endpoint = self.endpoints_to_attempt().into_iter().find(|endpoint| {
            !self
                .connection_attempts
                .iter()
                .any(|attempt| attempt.endpoint == *endpoint)
        })?;
        let id = self.id_generator.next_id();

        self.connection_attempts.push(ConnectionAttempt {
            id,
            endpoint: endpoint.clone(),
            started: now,
            state: ConnectionState::InProgress,
            is_ech_retry: false,
        });

        Some(Output::AttemptConnection {
            id,
            endpoint,
            is_ech_retry: false,
        })
    }

    /// Emit a connection attempt for a pending ECH retry, if any.
    fn ech_retry_attempt(&mut self, now: Instant) -> Option<Output> {
        let endpoint = self.ech_retries.iter().find_map(|(prev_id, ech_config)| {
            let prev = self.connection_attempts.iter().find(|a| a.id == *prev_id)?;
            let endpoint = Endpoint {
                ech_config: Some(ech_config.clone()),
                ..prev.endpoint.clone()
            };
            let already_attempted = self
                .connection_attempts
                .iter()
                .any(|a| a.endpoint == endpoint);
            (!already_attempted).then_some(endpoint)
        })?;

        let id = self.id_generator.next_id();
        self.connection_attempts.push(ConnectionAttempt {
            id,
            endpoint: endpoint.clone(),
            started: now,
            state: ConnectionState::InProgress,
            is_ech_retry: true,
        });

        Some(Output::AttemptConnection {
            id,
            endpoint,
            is_ech_retry: true,
        })
    }

    fn endpoints_to_attempt(&self) -> Vec<Endpoint> {
        let any_ech = self.any_ech();

        // HTTPS-record endpoints come first, ordered by priority.
        let mut endpoints = self.service_info_endpoints();

        // Alt-svc and the plain origin fallback never carry ECH (an alt-svc
        // target may differ from the origin), so when at least one ServiceInfo
        // advertises ECH we use only the HTTPS-record endpoints above.
        // Otherwise both are tried, after the HTTPS-record endpoints and
        // interleaved together as a single tier by protocol and address family.
        if !any_ech {
            let mut tier = self.alt_svc_endpoints();
            tier.extend(self.origin_fallback_endpoints());
            endpoints.extend(interleave_endpoints(tier, self.network_config.prefer_v6()));
        }

        endpoints
    }

    /// Endpoints from completed HTTPS records, ordered by priority and
    /// interleaved per record by protocol and address family.
    fn service_info_endpoints(&self) -> Vec<Endpoint> {
        let any_ech = self.any_ech();
        let prefer_v6 = self.network_config.prefer_v6();

        // Collect all ServiceInfos sorted by priority.
        let mut service_infos: Vec<&ServiceInfo> = self
            .completed_service_infos()
            // When at least one ServiceInfo has ECH config, skip those without it.
            .filter(|i| !any_ech || i.ech_config.is_some())
            .collect();
        service_infos.sort_by_key(|i| i.priority);

        let mut endpoints: Vec<Endpoint> = Vec::new();
        for info in &service_infos {
            let ipv4_addrs: Option<Result<&[Ipv4Addr], ()>> =
                self.dns_queries.iter().find_map(|q| match &q.state {
                    DnsQueryState::Completed {
                        response: DnsResult::A(result),
                        ..
                    } if q.target_name == info.target_name => {
                        Some(result.as_deref().map_err(|&()| ()))
                    }
                    _ => None,
                });
            let ipv6_addrs: Option<Result<&[Ipv6Addr], ()>> =
                self.dns_queries.iter().find_map(|q| match &q.state {
                    DnsQueryState::Completed {
                        response: DnsResult::Aaaa(result),
                        ..
                    } if q.target_name == info.target_name => {
                        Some(result.as_deref().map_err(|&()| ()))
                    }
                    _ => None,
                });
            let bucket = info.flatten_into_endpoints(
                self.port,
                ipv4_addrs,
                ipv6_addrs,
                &self.network_config,
                (self.network_config.resolution == ResolutionMode::NameWithHttpsRr)
                    .then(|| self.origin_host_str())
                    .flatten(),
            );
            endpoints.extend(interleave_endpoints(bucket, prefer_v6));
        }

        endpoints
    }

    fn has_successful_connection(&self) -> bool {
        self.connection_attempts
            .iter()
            .any(|a| a.state == ConnectionState::Succeeded)
    }

    fn failed(&self) -> Option<FailureReason> {
        if self.has_successful_connection()
            || self.dns_queries.iter().any(|q| !q.is_completed())
            // A revalidation of a stale answer is still outstanding: its fresh
            // answer may yet yield a usable address, so do not fail yet.
            || self
                .dns_queries
                .iter()
                .any(|q| matches!(q.refresh, Refresh::InFlight(_)))
            || self
                .connection_attempts
                .iter()
                .any(|a| a.state == ConnectionState::InProgress)
        {
            return None;
        }

        Some(
            if self
                .connection_attempts
                .iter()
                .any(|a| a.state == ConnectionState::Failed)
                // Without a single DNS query (e.g. `ResolutionMode::Name`)
                // there is no resolution that could have failed.
                || self.dns_queries.is_empty()
            {
                FailureReason::Connection
            } else {
                FailureReason::DnsResolution
            },
        )
    }

    /// `ServiceInfos` from all completed HTTPS responses.
    fn completed_service_infos(&self) -> impl Iterator<Item = &ServiceInfo> {
        self.dns_queries
            .iter()
            .filter_map(|q| match &q.state {
                DnsQueryState::Completed {
                    response: DnsResult::Https(Ok(infos)),
                    ..
                } => Some(infos.as_slice()),
                _ => None,
            })
            .flatten()
    }

    fn any_ech(&self) -> bool {
        if !self.network_config.ech {
            return false;
        }
        self.completed_service_infos()
            .any(|i| i.ech_config.is_some())
    }

    /// HTTP versions when the host is an IP address (no DNS involved).
    ///
    /// Default H2/H1, filtered by network config.
    fn ip_host_http_versions(&self) -> HashSet<ConnectionAttemptHttpVersions> {
        let mut http_versions = HashSet::from([HttpVersion::H2, HttpVersion::H1]);
        self.network_config
            .http_versions
            .filter_disabled(&mut http_versions);
        ConnectionAttemptHttpVersions::from_http_versions(&http_versions)
    }

    /// HTTP versions for the origin fallback bucket.
    ///
    /// Default H2/H1, filtered by network config.
    /// HTTPS-record ALPNs are excluded: those apply only to the HTTPS bucket.
    fn fallback_http_versions(&self) -> HashSet<ConnectionAttemptHttpVersions> {
        self.ip_host_http_versions()
    }

    /// Endpoints for every alt-svc entry, flat (interleaved by the caller).
    ///
    /// Per [RFC 7838](https://datatracker.ietf.org/doc/html/rfc7838), an alt-svc
    /// entry advertises the origin's service at a host (and optionally port) over
    /// a given protocol. An entry without a host of its own simply defaults to
    /// the origin host, so both kinds are handled the same way: the effective
    /// host is resolved (or taken as an IP literal) and attempted at the alt-svc
    /// port (defaulting to the origin port) over the alt-svc protocol.
    ///
    /// ECH is never applied: an alt-svc target may differ from the origin, so
    /// the origin's HTTPS-record ECH config does not apply to it.
    ///
    /// In a by-name mode (see [`ResolutionMode`]) a domain alt-svc target is
    /// attempted by name instead of being resolved; an IP-literal target still
    /// takes the address path.
    fn alt_svc_endpoints(&self) -> Vec<Endpoint> {
        let mut endpoints = Vec::new();
        for alt_svc in &self.network_config.alt_svc {
            if self
                .network_config
                .is_http_version_disabled(alt_svc.http_version)
            {
                continue;
            }
            let port = alt_svc.port.unwrap_or(self.port);
            let http_version: ConnectionAttemptHttpVersions = alt_svc.http_version.into();

            // By-name mode: attempt the alt-svc target by name over its
            // advertised protocol (so an h3 alt-svc is raced over h3), unless the
            // target is an IP literal, which needs no resolution and takes the
            // address path below.
            if matches!(
                self.network_config.resolution,
                ResolutionMode::Name | ResolutionMode::NameWithHttpsRr
            ) && let Some(host) = self.alt_svc_by_name_host(alt_svc)
            {
                endpoints.push(Endpoint {
                    target: EndpointTarget::Name { host, port },
                    http_version,
                    ech_config: None,
                });
                continue;
            }

            endpoints.extend(self.alt_svc_addrs(alt_svc).into_iter().map(|ip| Endpoint {
                target: EndpointTarget::Address(SocketAddr::new(ip, port)),
                http_version,
                ech_config: None,
            }));
        }
        endpoints
    }

    /// The default origin endpoints: the baseline H2/H1 connection at the origin
    /// host and port, used when neither HTTPS records nor alt-svc apply. Flat
    /// (interleaved by the caller).
    fn origin_fallback_endpoints(&self) -> Vec<Endpoint> {
        let http_versions = self.fallback_http_versions();

        // By-name mode: connect to a domain origin by name, with no resolved
        // address (and thus no address-family racing). An IP-literal origin
        // needs no resolution regardless, so it keeps the normal address path
        // below.
        if matches!(
            self.network_config.resolution,
            ResolutionMode::Name | ResolutionMode::NameWithHttpsRr
        ) && let Some(host) = self.origin_host_str()
        {
            return http_versions
                .iter()
                .map(|&http_version| Endpoint {
                    target: EndpointTarget::Name {
                        host: host.to_string(),
                        port: self.port,
                    },
                    http_version,
                    ech_config: None,
                })
                .collect();
        }

        self.origin_addrs()
            .into_iter()
            .flat_map(|ip| {
                http_versions.iter().map(move |&http_version| Endpoint {
                    target: EndpointTarget::Address(SocketAddr::new(ip, self.port)),
                    http_version,
                    ech_config: None,
                })
            })
            .collect()
    }

    /// The origin host as a connect name, or [`None`] when the origin is an IP
    /// literal, which is connected to by address rather than by name.
    ///
    /// The root label's trailing dot is stripped: an origin may legitimately be
    /// given fully qualified (`example.com.`), but a connect name (SNI, or a
    /// proxy's CONNECT target) must not carry it.
    fn origin_host_str(&self) -> Option<&str> {
        match &self.host {
            Host::Domain(domain) => Some(domain.trim_end_matches('.')),
            Host::Ip(_) => None,
        }
    }

    /// The hostname to connect to by name for an alt-svc entry in a by-name mode:
    /// the alt-svc's own host when it is a domain, or the origin host when the
    /// alt-svc omits a host. Returns [`None`] when the effective host is an IP
    /// literal, which needs no resolution and uses the address path instead.
    fn alt_svc_by_name_host(&self, alt_svc: &AltSvc) -> Option<String> {
        alt_svc.host.as_ref().map_or_else(
            || self.origin_host_str().map(ToString::to_string),
            |host| {
                host.parse::<IpAddr>()
                    .is_err()
                    .then(|| host.trim_end_matches('.').to_string())
            },
        )
    }

    /// Addresses for an alt-svc entry's effective host: its own host when set,
    /// or the origin host otherwise.
    fn alt_svc_addrs(&self, alt_svc: &AltSvc) -> Vec<IpAddr> {
        alt_svc.host.as_ref().map_or_else(
            || self.origin_addrs(),
            // An alt-svc host is a raw string that may be an IP literal.
            |host| match host.parse::<IpAddr>() {
                Ok(ip) if self.network_config.ip.allows(ip) => vec![ip],
                // A literal of the address family disabled on a single-stack
                // network, for which a domain would not be resolved either.
                Ok(_) => Vec::new(),
                Err(_) => self.dns_resolved_addrs(host),
            },
        )
    }

    /// Addresses for the origin host: the literal when it is an IP, otherwise
    /// the addresses received for the origin's A/AAAA queries.
    fn origin_addrs(&self) -> Vec<IpAddr> {
        match &self.host {
            Host::Ip(ip) => vec![*ip],
            // A `Host::Domain` is never an IP literal (the constructor already
            // classified it), so resolve it directly.
            Host::Domain(domain) => self.dns_resolved_addrs(domain),
        }
    }

    /// Addresses received for `host`'s completed A/AAAA queries.
    fn dns_resolved_addrs(&self, host: &str) -> Vec<IpAddr> {
        self.dns_queries
            .iter()
            .filter(|q| q.target_name.as_str() == host)
            .filter_map(DnsQuery::response)
            .flat_map(DnsResult::ip_addrs)
            .collect()
    }

    /// Whether to move on to the connection attempt phase based on the received
    /// DNS responses, not based on a timeout.
    fn move_on_without_timeout(&self) -> bool {
        let hostname = match &self.host {
            Host::Domain(d) => d.as_str(),
            Host::Ip(_) => {
                return false;
            }
        };

        // The origin's HTTPS query carries the port-prefixed name on a non-443
        // origin, so match on that rather than the bare hostname.
        let https_name = https_query_name(hostname, self.port);

        // `ResolutionMode::NameWithHttpsRr` queries only the origin HTTPS
        // record and never any address, so there are no addresses to wait for:
        // move on once that HTTPS query has completed, whether its answer is
        // positive, empty, or negative. A non-positive answer simply yields no
        // HTTPS endpoints and falls back to the by-name origin.
        if self.network_config.resolution == ResolutionMode::NameWithHttpsRr {
            return self
                .dns_queries
                .iter()
                .filter(|q| q.target_name == https_name)
                .filter(|q| q.is_completed())
                .any(|q| q.record_type == DnsRecordType::Https);
        }

        // > Some positive (non-empty) address answers have been received AND
        //
        // <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2>
        if !self.dns_queries.iter().any(|q| match &q.state {
            DnsQueryState::Completed { response, .. } => response.has_addrs(),
            DnsQueryState::InProgress => false,
        }) {
            return false;
        }

        // > A postive (non-empty) or negative (empty) answer has been received
        // > for the preferred address family that was queried AND
        //
        // <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2>
        //
        // Skipped when `wait_for_preferred_address` is disabled, letting the
        // state machine move on with the non-preferred family rather than
        // waiting out the resolution delay for the preferred one.
        //
        // An HTTPS record hint of the preferred family also counts:
        //
        // > When these are received, they SHOULD be considered as positive
        // > non-empty answers for the purpose of the algorithm when A and AAAA
        // > records corresponding to the TargetName are not available yet.
        //
        // <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2.1>
        if self.network_config.wait_for_preferred_address
            && !self
                .dns_queries
                .iter()
                .filter(|q| q.is_completed())
                .any(|q| q.record_type == self.network_config.preferred_dns_record_type())
            && !self.has_preferred_family_hint()
        {
            return false;
        }

        // > SVCB/HTTPS service information has been received (or has received a negative response)
        //
        // <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2>
        if !self
            .dns_queries
            .iter()
            .filter(|q| q.target_name == https_name)
            .filter(|q| q.is_completed())
            .any(|q| q.record_type == DnsRecordType::Https)
        {
            return false;
        }

        true
    }

    /// Whether a completed HTTPS record carries an IP hint of the preferred
    /// address family.
    fn has_preferred_family_hint(&self) -> bool {
        let prefer_v6 = self.network_config.prefer_v6();
        self.completed_service_infos().any(|i| {
            if prefer_v6 {
                !i.ipv6_hints.is_empty()
            } else {
                !i.ipv4_hints.is_empty()
            }
        })
    }

    /// Whether to move on to the connection attempt phase based on a timeout.
    fn move_on_with_timeout(&self, now: Instant) -> bool {
        // > Or:
        // >
        // > - Some positive (non-empty) address answers have been received AND
        // > - A resolution time delay has passed after which other answers have not been received
        //
        // <https://www.ietf.org/archive/id/draft-ietf-happy-happyeyeballs-v3-02.html#section-4.2>

        if !self
            .dns_queries
            .iter()
            .filter_map(|q| q.response())
            .any(DnsResult::has_addrs)
        {
            return false;
        }

        self.dns_queries
            .iter()
            .filter_map(|q| match &q.state {
                DnsQueryState::InProgress => None,
                DnsQueryState::Completed { completed, .. } => Some(completed),
            })
            .any(|completed| now.duration_since(*completed) >= self.network_config.resolution_delay)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dns_result_has_addrs() {
        for result in [
            DnsResult::Aaaa(Ok(vec![])),
            DnsResult::Aaaa(Err(())),
            DnsResult::A(Ok(vec![])),
            DnsResult::A(Err(())),
            DnsResult::Https(Err(())),
            DnsResult::Https(Ok(vec![])),
        ] {
            assert!(!result.has_addrs());
        }
        assert!(DnsResult::Aaaa(Ok(vec![Ipv6Addr::LOCALHOST])).has_addrs());
        assert!(DnsResult::A(Ok(vec![Ipv4Addr::LOCALHOST])).has_addrs());
    }

    #[test]
    fn host_display() {
        let v4 = Ipv4Addr::LOCALHOST;
        assert_eq!(Host::Ip(v4.into()).to_string(), v4.to_string());
        let v6 = Ipv6Addr::LOCALHOST;
        assert_eq!(Host::Ip(v6.into()).to_string(), v6.to_string());
        let domain = "example.com";
        assert_eq!(Host::Domain(domain.into()).to_string(), domain);
    }
}
