/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#![expect(clippy::missing_panics_doc, reason = "OK here")]

#[cfg(feature = "fuzzing")]
use std::time::Duration;
use std::{
    borrow::Cow,
    cell::RefCell,
    cmp::min,
    collections::HashMap,
    ffi::c_void,
    io,
    net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr},
    path::PathBuf,
    ptr,
    rc::Rc,
    slice, str,
    time::{Duration, Instant},
};

use firefox_on_glean::{
    metrics::networking,
    private::{LocalCustomDistribution, LocalMemoryDistribution},
};
#[cfg(not(windows))]
use libc::{c_int, AF_INET, AF_INET6};
use libc::{c_uchar, size_t};
use neqo_common::{
    datagram, event::Provider as _, qlog::Qlog, Datagram, Decoder, Encoder, Header, Role, Tos,
};
use neqo_http3::{
    connect_udp::ClientSession as _, features::extended_connect::session,
    webtransport::ClientSession as _, ConnectUdpEvent, Error as Http3Error, Http3Client,
    Http3ClientEvent, Http3Parameters, Http3State, Priority, WebTransportEvent,
};
use neqo_transport::{
    stream_id::StreamType, streams::SendGroupId, CongestionControl, Connection,
    ConnectionParameters, DatagramQueueOutcome, Error as TransportError, HyStartCssBaseline,
    Output, OutputBatch, RandomConnectionIdGenerator, SlowStart, Stats as TransportStats,
    StreamDataLimit, StreamId, Version,
};
use nserror::{
    nsresult, NS_BASE_STREAM_WOULD_BLOCK, NS_ERROR_CONNECTION_REFUSED,
    NS_ERROR_DOM_INVALID_HEADER_NAME, NS_ERROR_FILE_ALREADY_EXISTS, NS_ERROR_ILLEGAL_VALUE,
    NS_ERROR_INVALID_ARG, NS_ERROR_NET_HTTP3_PROTOCOL_ERROR, NS_ERROR_NET_INTERRUPT,
    NS_ERROR_NET_RESET, NS_ERROR_NET_TIMEOUT, NS_ERROR_NOT_AVAILABLE, NS_ERROR_NOT_CONNECTED,
    NS_ERROR_OUT_OF_MEMORY, NS_ERROR_SOCKET_ADDRESS_IN_USE, NS_ERROR_UNEXPECTED, NS_OK,
};
use nss_rs::{cert::CertificateCompressor, init, PRErrorCode};
use nsstring::{nsACString, nsCString};
use thin_vec::ThinVec;
use tracing::{debug, error, warn};
use uuid::Uuid;
#[cfg(windows)]
use winapi::{
    ctypes::c_int,
    shared::ws2def::{AF_INET, AF_INET6},
};
use xpcom::{AtomicRefcnt, RefCounted, RefPtr};
use zlib_rs::{decompress_slice, InflateConfig, ReturnCode};

std::thread_local! {
    static RECV_BUF: RefCell<neqo_udp::RecvBuf> = RefCell::new(neqo_udp::RecvBuf::default());
}

/// Upper bound on the bytes read from the socket in a single `neqo_http3conn_process_input` pass.
/// No legitimate connection reads this much at once, so the cap only bounds a misbehaving or
/// malicious peer that would otherwise make us buffer datagrams unboundedly. Nothing is lost when
/// the cap is hit: buffered events are delivered to the upper layer right after, and the
/// level-triggered poll re-fires `RecvData` to read the rest.
const MAX_BYTES_READ_PER_PASS: usize = 50 * 1024 * 1024;

#[cfg(target_vendor = "apple")]
use std::sync::atomic::{AtomicBool, Ordering};

#[cfg(target_vendor = "apple")]
static APPLE_FAST_PATH: AtomicBool = AtomicBool::new(false);

#[allow(clippy::cast_possible_truncation, reason = "see check below")]
const AF_INET_U16: u16 = AF_INET as u16;
static_assertions::const_assert_eq!(AF_INET_U16 as c_int, AF_INET);

#[allow(clippy::cast_possible_truncation, reason = "see check below")]
const AF_INET6_U16: u16 = AF_INET6 as u16;
static_assertions::const_assert_eq!(AF_INET6_U16 as c_int, AF_INET6);

#[repr(C)]
pub struct WouldBlockCounter {
    rx: usize,
    tx: usize,
}

impl WouldBlockCounter {
    pub fn new() -> Self {
        Self { rx: 0, tx: 0 }
    }

    pub fn increment_rx(&mut self) {
        self.rx += 1;
    }

    pub fn increment_tx(&mut self) {
        self.tx += 1;
    }

    pub fn rx_count(&self) -> usize {
        self.rx
    }

    pub fn tx_count(&self) -> usize {
        self.tx
    }
}

#[repr(C)]
pub struct NeqoHttp3Conn {
    conn: Http3Client,
    local_addr: SocketAddr,
    refcnt: AtomicRefcnt,
    /// Socket to use for IO.
    ///
    /// When [`None`], NSPR is used for IO.
    //
    // Use a `BorrowedSocket` instead of e.g. `std::net::UdpSocket`. The latter
    // would close the file descriptor on `Drop`. The lifetime of the underlying
    // OS socket is managed not by `neqo_glue` but `NSPR`.
    socket: Option<neqo_udp::Socket<BorrowedSocket>>,
    /// Buffered outbound datagram from previous send that failed with
    /// WouldBlock. To be sent once UDP socket has write-availability again.
    buffered_outbound_datagram: Option<datagram::Batch>,

    datagram_segment_size_sent: LocalMemoryDistribution<'static>,
    datagram_segment_size_received: LocalMemoryDistribution<'static>,
    datagram_size_sent: LocalMemoryDistribution<'static>,
    datagram_size_received: LocalMemoryDistribution<'static>,
    datagram_segments_sent: LocalCustomDistribution<'static>,
    datagram_segments_received: LocalCustomDistribution<'static>,
    would_block_counter: WouldBlockCounter,
    /// Maps the child-minted WebTransport send-group id (assigned synchronously in the
    /// content process) to the neqo-minted [`SendGroupId`].
    webtransport_send_groups: HashMap<u64, SendGroupId>,
    /// Whether the connection-close reason has already been recorded to Glean.
    /// A connection may surface a `Closing` state change followed later by a
    /// `Closed` one; this guards against counting the same close twice while
    /// still catching closes that skip `Closing` entirely (idle timeout).
    #[cfg(not(target_os = "android"))]
    close_reason_recorded: bool,
}

impl Drop for NeqoHttp3Conn {
    fn drop(&mut self) {
        self.record_stats_in_glean();
    }
}

// Opaque interface to mozilla::net::NetAddr defined in DNS.h
#[repr(C)]
pub union NetAddr {
    private: [u8; 0],
}

extern "C" {
    pub fn moz_netaddr_get_family(arg: *const NetAddr) -> u16;
    pub fn moz_netaddr_get_network_order_ip(arg: *const NetAddr) -> u32;
    pub fn moz_netaddr_get_ipv6(arg: *const NetAddr) -> *const u8;
    pub fn moz_netaddr_get_network_order_port(arg: *const NetAddr) -> u16;
}

extern "C" {
    fn moz_mock_network_udp_send_would_block(family: u16, addr: *const u8, port: u16) -> bool;
}

fn mock_udp_send_would_block(dest: SocketAddr) -> bool {
    if !static_prefs::pref!("network.socket.attach_mock_network_layer") {
        return false;
    }
    unsafe {
        match dest.ip() {
            IpAddr::V4(v4) => {
                moz_mock_network_udp_send_would_block(AF_INET_U16, v4.octets().as_ptr(), dest.port())
            }
            IpAddr::V6(v6) => {
                moz_mock_network_udp_send_would_block(AF_INET6_U16, v6.octets().as_ptr(), dest.port())
            }
        }
    }
}

fn netaddr_to_socket_addr(arg: *const NetAddr) -> Result<SocketAddr, nsresult> {
    if arg.is_null() {
        return Err(NS_ERROR_INVALID_ARG);
    }

    unsafe {
        let family = i32::from(moz_netaddr_get_family(arg));
        if family == AF_INET {
            let port = u16::from_be(moz_netaddr_get_network_order_port(arg));
            let ipv4 = Ipv4Addr::from(u32::from_be(moz_netaddr_get_network_order_ip(arg)));
            return Ok(SocketAddr::new(IpAddr::V4(ipv4), port));
        }

        if family == AF_INET6 {
            let port = u16::from_be(moz_netaddr_get_network_order_port(arg));
            let ipv6_slice: [u8; 16] = slice::from_raw_parts(moz_netaddr_get_ipv6(arg), 16)
                .try_into()
                .expect("slice with incorrect length");
            let ipv6 = Ipv6Addr::from(ipv6_slice);
            return Ok(SocketAddr::new(IpAddr::V6(ipv6), port));
        }
    }

    Err(NS_ERROR_UNEXPECTED)
}

fn enable_zlib_decoder(c: &mut Connection) -> neqo_transport::Res<()> {
    struct ZlibCertDecoder {}

    impl CertificateCompressor for ZlibCertDecoder {
        // RFC 8879
        const ID: u16 = 0x1;
        const NAME: &std::ffi::CStr = c"zlib";

        fn decode(input: &[u8], output: &mut [u8]) -> nss_rs::Res<()> {
            let (output_slice, error) = decompress_slice(output, &input, InflateConfig::default());
            if error != ReturnCode::Ok {
                return Err(nss_rs::Error::CertificateDecoding);
            }
            if output_slice.len() != output.len() {
                return Err(nss_rs::Error::CertificateDecoding);
            }

            Ok(())
        }
    }

    c.set_certificate_compression::<ZlibCertDecoder>()
}

extern "C" {
    pub fn ZSTD_decompress(
        dst: *mut ::core::ffi::c_void,
        dstCapacity: usize,
        src: *const ::core::ffi::c_void,
        compressedSize: usize,
    ) -> usize;
}

extern "C" {
    pub fn ZSTD_isError(result: usize) -> ::core::ffi::c_uint;
}

fn enable_zstd_decoder(c: &mut Connection) -> neqo_transport::Res<()> {
    struct ZstdCertDecoder {}

    impl CertificateCompressor for ZstdCertDecoder {
        // RFC 8879
        const ID: u16 = 0x3;
        const NAME: &std::ffi::CStr = c"zstd";

        fn decode(input: &[u8], output: &mut [u8]) -> nss_rs::Res<()> {
            if input.is_empty() {
                return Err(nss_rs::Error::CertificateDecoding);
            }
            if output.is_empty() {
                return Err(nss_rs::Error::CertificateDecoding);
            }

            let output_len = unsafe {
                ZSTD_decompress(
                    output.as_mut_ptr() as *mut c_void,
                    output.len(),
                    input.as_ptr() as *const c_void,
                    input.len(),
                )
            };

            // ZSTD_isError return 1 if error, 0 otherwise
            if unsafe { ZSTD_isError(output_len) != 0 } {
                debug!("zstd compression failed with {output_len}");
                return Err(nss_rs::Error::CertificateDecoding);
            }

            if output.len() != output_len {
                debug!("zstd compression `output_len` {output_len} doesn't match expected `output.len()` {}", output.len());
                return Err(nss_rs::Error::CertificateDecoding);
            }

            Ok(())
        }
    }

    c.set_certificate_compression::<ZstdCertDecoder>()
}

#[repr(C)]
#[derive(Debug, PartialEq)]
pub enum BrotliDecoderResult {
    Error = 0,
    Success = 1,
    NeedsMoreInput = 2,
    NeedsMoreOutput = 3,
}

extern "C" {
    pub fn BrotliDecoderDecompress(
        encoded_size: size_t,
        encoded_buffer: *const c_uchar,
        decoded_size: *mut size_t,
        decoded_buffer: *mut c_uchar,
    ) -> BrotliDecoderResult;
}

fn enable_brotli_decoder(c: &mut Connection) -> neqo_transport::Res<()> {
    struct BrotliCertDecoder {}

    impl CertificateCompressor for BrotliCertDecoder {
        // RFC 8879
        const ID: u16 = 0x2;
        const NAME: &std::ffi::CStr = c"brotli";

        fn decode(input: &[u8], output: &mut [u8]) -> nss_rs::Res<()> {
            if input.is_empty() {
                return Err(nss_rs::Error::CertificateDecoding);
            }
            if output.is_empty() {
                return Err(nss_rs::Error::CertificateDecoding);
            }

            let mut uncompressed_size = output.len();
            let result = unsafe {
                BrotliDecoderDecompress(
                    input.len(),
                    input.as_ptr(),
                    &mut uncompressed_size as *mut usize,
                    output.as_mut_ptr(),
                )
            };

            if result != BrotliDecoderResult::Success {
                return Err(nss_rs::Error::CertificateDecoding);
            }

            if uncompressed_size != output.len() {
                return Err(nss_rs::Error::CertificateDecoding);
            }

            Ok(())
        }
    }

    c.set_certificate_compression::<BrotliCertDecoder>()
}

type SendFunc = extern "C" fn(
    context: *mut c_void,
    addr_family: u16,
    addr: *const u8,
    port: u16,
    data: *const u8,
    size: u32,
) -> nsresult;

type SetTimerFunc = extern "C" fn(context: *mut c_void, timeout: u64);

#[cfg(unix)]
type BorrowedSocket = std::os::fd::BorrowedFd<'static>;
#[cfg(windows)]
type BorrowedSocket = std::os::windows::io::BorrowedSocket<'static>;

impl NeqoHttp3Conn {
    /// Create a new [`NeqoHttp3Conn`].
    ///
    /// Note that [`NeqoHttp3Conn`] works under the assumption that the UDP
    /// socket of the connection, i.e. the one provided to
    /// [`NeqoHttp3Conn::new`], does not change throughout the lifetime of
    /// [`NeqoHttp3Conn`].
    #[expect(
        clippy::too_many_arguments,
        clippy::too_many_lines,
        reason = "Nothing to be done about it."
    )]
    fn new(
        origin: &nsACString,
        alpn: &nsACString,
        local_addr: *const NetAddr,
        remote_addr: *const NetAddr,
        max_table_size: u64,
        max_blocked_streams: u16,
        max_data: u64,
        max_stream_data: u64,
        version_negotiation: bool,
        webtransport: bool,
        qlog_dir: &nsACString,
        idle_timeout: u32,
        fast_pto: u32,
        pmtud_enabled: bool,
        socket: Option<i64>,
    ) -> Result<RefPtr<Self>, nsresult> {
        // Nss init.
        init().map_err(|_| NS_ERROR_UNEXPECTED)?;

        let socket = socket
            .map(|socket| {
                #[cfg(unix)]
                let borrowed = {
                    use std::os::fd::{BorrowedFd, RawFd};
                    if socket == -1 {
                        error!("got invalid socked {}", socket);
                        return Err(NS_ERROR_INVALID_ARG);
                    }
                    let raw: RawFd = socket.try_into().map_err(|e| {
                        error!("got invalid socked {}: {}", socket, e);
                        NS_ERROR_INVALID_ARG
                    })?;
                    unsafe { BorrowedFd::borrow_raw(raw) }
                };
                #[cfg(windows)]
                let borrowed = {
                    use std::os::windows::io::{BorrowedSocket, RawSocket};
                    if socket as usize == winapi::um::winsock2::INVALID_SOCKET {
                        error!("got invalid socked {}", socket);
                        return Err(NS_ERROR_INVALID_ARG);
                    }
                    let raw: RawSocket = socket.try_into().map_err(|e| {
                        error!("got invalid socked {}: {}", socket, e);
                        NS_ERROR_INVALID_ARG
                    })?;
                    unsafe { BorrowedSocket::borrow_raw(raw) }
                };
                let s = neqo_udp::Socket::new(borrowed).map_err(|e| {
                    error!("failed to initialize socket {}: {}", socket, e);
                    into_nsresult(&e)
                })?;
                // Called after Socket::new (which sets up IP_RECVTOS etc.) since
                // enable_apple_fast_path only sets the fast-path flag on the
                // already-constructed UdpSocketState.
                #[cfg(target_vendor = "apple")]
                if APPLE_FAST_PATH.load(Ordering::Relaxed)
                    && static_prefs::pref!("network.http.http3.apple_fast_datapath")
                {
                    // SAFETY: The probe has verified the APIs work on this OS version.
                    unsafe { s.enable_apple_fast_path() }
                }
                Ok(s)
            })
            .transpose()?;

        let origin_conv = str::from_utf8(origin).map_err(|_| NS_ERROR_INVALID_ARG)?;

        let alpn_conv = str::from_utf8(alpn).map_err(|_| NS_ERROR_INVALID_ARG)?;

        let local: SocketAddr = netaddr_to_socket_addr(local_addr)?;

        let remote: SocketAddr = netaddr_to_socket_addr(remote_addr)?;

        let quic_version = match alpn_conv {
            "h3" => Version::Version1,
            _ => return Err(NS_ERROR_INVALID_ARG),
        };

        let version_list = if version_negotiation {
            Version::all()
        } else {
            vec![quic_version]
        };

        let cc_algorithm = match static_prefs::pref!("network.http.http3.cc_algorithm") {
            0 => CongestionControl::NewReno,
            1 => CongestionControl::Cubic,
            _ => {
                // Unknown preferences; default to Cubic
                CongestionControl::Cubic
            }
        };

        let slow_start = match static_prefs::pref!("network.http.http3.slow_start_algorithm") {
            0 => SlowStart::Classic,
            1 => SlowStart::HyStart,
            2 => SlowStart::Search,
            _ => {
                // Unknown preferences; default to Classic
                debug!("Unknown http3.slow_start_algorithm pref, defaulting to SlowStart::Classic");
                SlowStart::Classic
            }
        };

        let pmtud_enabled =
            // Check if PMTUD is explicitly enabled,
            pmtud_enabled
            // or enabled via pref,
            || static_prefs::pref!("network.http.http3.pmtud")
            // but disable PMTUD if NSPR is used (socket == None) or
            // transmitted UDP datagrams might get fragmented by the IP layer.
            && socket.as_ref().map_or(false, |s| !s.may_fragment());

        let spurious_recovery = static_prefs::pref!("network.http.http3.spurious_recovery");

        let css_baseline =
            if static_prefs::pref!("network.http.http3.hystart_alternative_css_baseline") {
                HyStartCssBaseline::EntryThreshold
            } else {
                HyStartCssBaseline::CurrentRoundMinRtt
            };

        let mut params = ConnectionParameters::default()
            .versions(quic_version, version_list)
            .congestion_control(cc_algorithm)
            .slow_start(slow_start)
            .max_data(max_data)
            .max_stream_data(StreamDataLimit::BiDiLocal, max_stream_data)
            .grease(static_prefs::pref!("security.tls.grease_http3_enable"))
            .sni_slicing(static_prefs::pref!("network.http.http3.sni-slicing"))
            .idle_timeout(Duration::from_secs(idle_timeout.into()))
            // Disabled on OpenBSD. See <https://bugzilla.mozilla.org/show_bug.cgi?id=1952304>.
            .pmtud_iface_mtu(cfg!(not(target_os = "openbsd")))
            // MLKEM support is configured further below. By default, disable it.
            .mlkem(false)
            .pmtud(pmtud_enabled)
            .spurious_recovery(spurious_recovery)
            .abe(static_prefs::pref!("network.http.http3.abe"))
            .hystart_css_baseline(css_baseline);

        // 0 means "use neqo's spec-compliant default PTO scaling".
        if fast_pto > 0 {
            if let Ok(v) = u8::try_from(fast_pto) {
                params = params.fast_pto(v);
            } else {
                debug_assert!(false, "fast_pto value {fast_pto} exceeds u8::MAX");
            }
        }

        // Set a short timeout when fuzzing.
        #[cfg(feature = "fuzzing")]
        if static_prefs::pref!("fuzzing.necko.http3") {
            params = params.idle_timeout(Duration::from_millis(10));
        }

        let http3_settings = Http3Parameters::default()
            .max_table_size_encoder(max_table_size)
            .max_table_size_decoder(max_table_size)
            .max_blocked_streams(max_blocked_streams)
            .connection_parameters(params)
            .webtransport(webtransport)
            .connect(true)
            .http3_datagram(true);

        let Ok(mut conn) = Connection::new_client(
            origin_conv,
            &[alpn_conv],
            Rc::new(RefCell::new(RandomConnectionIdGenerator::new(3))),
            local,
            remote,
            http3_settings.get_connection_parameters().clone(),
            Instant::now(),
        ) else {
            return Err(NS_ERROR_INVALID_ARG);
        };

        let mut additional_shares = 1;
        let mut named_groups = Vec::with_capacity(6);
        if static_prefs::pref!("security.tls.enable_kyber")
            && static_prefs::pref!("network.http.http3.enable_kyber")
        {
            named_groups.push(nss_rs::TLS_GRP_KEM_MLKEM768X25519);
            additional_shares += 1;
        }
        named_groups.extend_from_slice(&[
            nss_rs::TLS_GRP_EC_X25519,
            nss_rs::TLS_GRP_EC_SECP256R1,
            nss_rs::TLS_GRP_EC_SECP384R1,
            nss_rs::TLS_GRP_EC_SECP521R1,
        ]);
        // ML-KEM-1024 goes last so that it is never offered as a key share, but can
        // still be negotiated through a HelloRetryRequest.
        if static_prefs::pref!("security.tls.enable_mlkem1024") {
            named_groups.push(nss_rs::TLS_GRP_KEM_MLKEM1024);
        }
        // These operations are infallible when conn.state == State::Init.
        conn.set_groups(&named_groups)
            .map_err(|_| NS_ERROR_UNEXPECTED)?;
        // If additional_shares == 2, send mlkem768x25519, x25519, and p256.
        // If additional_shares == 1, send x25519 and p256.
        conn.send_additional_key_shares(additional_shares)
            .map_err(|_| NS_ERROR_UNEXPECTED)?;

        if static_prefs::pref!("security.tls.enable_certificate_compression_zlib")
            && static_prefs::pref!("network.http.http3.enable_certificate_compression_zlib")
        {
            enable_zlib_decoder(&mut conn).map_err(|_| NS_ERROR_UNEXPECTED)?;
        }

        if static_prefs::pref!("security.tls.enable_certificate_compression_zstd")
            && static_prefs::pref!("network.http.http3.enable_certificate_compression_zstd")
        {
            enable_zstd_decoder(&mut conn).map_err(|_| NS_ERROR_UNEXPECTED)?;
        }

        if static_prefs::pref!("security.tls.enable_certificate_compression_brotli")
            && static_prefs::pref!("network.http.http3.enable_certificate_compression_brotli")
        {
            enable_brotli_decoder(&mut conn).map_err(|_| NS_ERROR_UNEXPECTED)?;
        }

        let mut conn = Http3Client::new_with_conn(conn, http3_settings);

        if !qlog_dir.is_empty() {
            let qlog_dir_conv = str::from_utf8(qlog_dir).map_err(|_| NS_ERROR_INVALID_ARG)?;
            let qlog_path = PathBuf::from(qlog_dir_conv);

            match Qlog::enabled_with_file(
                qlog_path.clone(),
                Role::Client,
                Some("Firefox Client qlog".to_string()),
                Some("Firefox Client qlog".to_string()),
                format!("{}_{}.qlog", origin, Uuid::new_v4()),
                Instant::now(),
            ) {
                Ok(qlog) => conn.set_qlog(qlog),
                Err(e) => {
                    // Emit warnings but to not return an error if qlog initialization
                    // fails.
                    warn!("failed to create Qlog at {}: {}", qlog_path.display(), e);
                }
            }
        }

        let conn = Box::into_raw(Box::new(Self {
            conn,
            local_addr: local,
            refcnt: unsafe { AtomicRefcnt::new() },
            socket,
            datagram_segment_size_sent: networking::http_3_udp_datagram_segment_size_sent
                .start_buffer(),
            datagram_segment_size_received: networking::http_3_udp_datagram_segment_size_received
                .start_buffer(),
            datagram_size_sent: networking::http_3_udp_datagram_size_sent.start_buffer(),
            datagram_size_received: networking::http_3_udp_datagram_size_received.start_buffer(),
            datagram_segments_sent: networking::http_3_udp_datagram_segments_sent.start_buffer(),
            datagram_segments_received: networking::http_3_udp_datagram_segments_received
                .start_buffer(),
            buffered_outbound_datagram: None,
            would_block_counter: WouldBlockCounter::new(),
            webtransport_send_groups: HashMap::new(),
            #[cfg(not(target_os = "android"))]
            close_reason_recorded: false,
        }));
        unsafe { RefPtr::from_raw(conn).ok_or(NS_ERROR_NOT_CONNECTED) }
    }

    /// Record the reason this HTTP/3 connection closed to Glean, at most once
    /// per connection. Called from both the `Closing` and `Closed` state
    /// changes: closes that go through a closing handshake surface `Closing`
    /// first, while closes that skip it (idle timeout) surface only `Closed`.
    #[cfg(not(target_os = "android"))]
    fn record_close_reason(&mut self, reason: &neqo_transport::CloseReason) {
        if self.close_reason_recorded {
            return;
        }
        self.close_reason_recorded = true;

        let glean_label = match reason {
            neqo_transport::CloseReason::Application(_) => "Application",
            neqo_transport::CloseReason::Transport(r) => transport_error_to_glean_label(r),
        };
        networking::http_3_connection_close_reason
            .get(glean_label)
            .add(1);
    }

    fn record_stats_in_glean(&self) {
        use firefox_on_glean::metrics::networking as glean;
        use neqo_common::Ecn;
        use neqo_transport::{ecn, SlowStartExitReason};
        use std::cmp::Ordering;

        /// The biggest initial congestion window that can be set in neqo. Needs to be kept in sync with neqo.
        const MAX_INITIAL_CWND: usize = 12520;
        // Metric values must be recorded as integers. Glean does not support
        // floating point distributions. In order to represent values <1, they
        // are multiplied by `PRECISION_FACTOR`. A `PRECISION_FACTOR` of
        // `10_000` allows one to represent fractions down to 0.0001.
        const PRECISION_FACTOR: u64 = 10_000;
        #[allow(clippy::cast_possible_truncation, reason = "see check below")]
        const PRECISION_FACTOR_USIZE: usize = PRECISION_FACTOR as usize;
        static_assertions::const_assert_eq!(PRECISION_FACTOR_USIZE as u64, PRECISION_FACTOR);

        let stats = self.conn.transport_stats();

        if stats.packets_tx == 0 {
            return;
        }

        for (s, postfix) in [(&stats.frame_tx, "_tx"), (&stats.frame_rx, "_rx")] {
            let add = |label: &str, value: usize| {
                glean::http_3_quic_frame_count
                    .get(&(label.to_string() + postfix))
                    .add(value.try_into().unwrap_or(i32::MAX));
            };

            add("ack", s.ack);
            add("crypto", s.crypto);
            add("stream", s.stream);
            add("reset_stream", s.reset_stream);
            add("stop_sending", s.stop_sending);
            add("ping", s.ping);
            add("padding", s.padding);
            add("max_streams", s.max_streams);
            add("streams_blocked", s.streams_blocked);
            add("max_data", s.max_data);
            add("data_blocked", s.data_blocked);
            add("max_stream_data", s.max_stream_data);
            add("stream_data_blocked", s.stream_data_blocked);
            add("new_connection_id", s.new_connection_id);
            add("retire_connection_id", s.retire_connection_id);
            add("path_challenge", s.path_challenge);
            add("path_response", s.path_response);
            add("connection_close", s.connection_close);
            add("handshake_done", s.handshake_done);
            add("new_token", s.new_token);
            add("ack_frequency", s.ack_frequency);
            add("datagram", s.datagram);
        }

        // Only record the metrics below for connections that established. Not
        // gating on `stats.packets_rx` as it counts garbage too, so it would
        // still admit a network that replies to every UDP packet with junk
        // without ever speaking QUIC.
        if stats.frame_rx.handshake_done == 0 {
            return;
        }

        let version_label = match stats.version {
            Version::Version1 => "v1",
            Version::Version2 => "v2",
        };
        glean::http_3_quic_version.get(version_label).add(1);

        // neqo's `pto_counts` is a sliding histogram: the highest set bucket is
        // the longest run of consecutive PTOs the connection saw, i.e. how deep
        // it fell into a black hole. Record that run length once per connection.
        let max_consecutive_ptos = i64::try_from(
            stats
                .pto_counts
                .iter()
                .rposition(|&count| count > 0)
                .map_or(0, |i| i + 1),
        )
        .unwrap_or(i64::MAX);
        glean::http_3_max_consecutive_ptos.accumulate_single_sample_signed(max_consecutive_ptos);

        if !static_prefs::pref!("network.http.http3.use_nspr_for_io")
            && static_prefs::pref!("network.http.http3.ecn_report")
        {
            let rx_ect0_sum: u64 = stats.ecn_rx.into_values().map(|v| v[Ecn::Ect0]).sum();
            let rx_ect1_sum: u64 = stats.ecn_rx.into_values().map(|v| v[Ecn::Ect1]).sum();
            let rx_ce_sum: u64 = stats.ecn_rx.into_values().map(|v| v[Ecn::Ce]).sum();

            // A CE mark can't be attributed to a specific ECT type on connections that saw
            // both, so the per-type ratio and count metrics below are gated on exclusivity.
            if rx_ect0_sum > 0 && rx_ect1_sum == 0 {
                if let Ok(ratio) = i64::try_from((rx_ce_sum * PRECISION_FACTOR) / rx_ect0_sum) {
                    glean::http_3_ecn_ce_ect0_ratio_received.accumulate_single_sample_signed(ratio);
                } else {
                    let msg = "Failed to convert ratio to i64 for use with glean";
                    warn!("{msg}");
                    debug_assert!(false, "{msg}");
                }
            }

            // Per-connection classification of the server's ECN marking (answers "% of servers").
            let label = match (rx_ect0_sum > 0, rx_ect1_sum > 0, rx_ce_sum > 0) {
                (true, true, _) => "ect0-and-ect1",
                (true, false, false) => "ect0",
                (false, true, false) => "ect1",
                (true, false, true) => "ect0-and-ce",
                (false, true, true) => "ect1-and-ce",
                (false, false, true) => "ce-only",
                (false, false, false) => "none",
            };
            glean::http_3_ecn_ect_received.get(label).add(1);
        }

        if !static_prefs::pref!("network.http.http3.use_nspr_for_io")
            && static_prefs::pref!("network.http.http3.ecn_mark")
        {
            let tx_ect0_sum: u64 = stats.ecn_tx_acked.into_values().map(|v| v[Ecn::Ect0]).sum();
            let tx_ce_sum: u64 = stats.ecn_tx_acked.into_values().map(|v| v[Ecn::Ce]).sum();
            if tx_ect0_sum > 0 {
                if let Ok(ratio) = i64::try_from((tx_ce_sum * PRECISION_FACTOR) / tx_ect0_sum) {
                    glean::http_3_ecn_ce_ect0_ratio_sent.accumulate_single_sample_signed(ratio);
                } else {
                    let msg = "Failed to convert ratio to i64 for use with glean";
                    warn!("{msg}");
                    debug_assert!(false, "{msg}");
                }
            }
            for (outcome, value) in stats.ecn_path_validation.into_iter() {
                let Ok(value) = i32::try_from(value) else {
                    let msg = format!("Failed to convert {value} to i32 for use with glean");
                    warn!("{msg}");
                    debug_assert!(false, "{msg}");
                    continue;
                };
                match outcome {
                    ecn::ValidationOutcome::Capable => {
                        glean::http_3_ecn_path_capability.get("capable").add(value);
                    }
                    ecn::ValidationOutcome::NotCapable(ecn::ValidationError::BlackHole) => {
                        glean::http_3_ecn_path_capability
                            .get("black-hole")
                            .add(value);
                    }
                    ecn::ValidationOutcome::NotCapable(ecn::ValidationError::Bleaching) => {
                        glean::http_3_ecn_path_capability
                            .get("bleaching")
                            .add(value);
                    }
                    ecn::ValidationOutcome::NotCapable(
                        ecn::ValidationError::ReceivedUnsentECT1,
                    ) => {
                        glean::http_3_ecn_path_capability
                            .get("received-unsent-ect-1")
                            .add(value);
                    }
                }
            }
        }

        // Calculate and collect packet loss ratio. The value is used later to also record the filtered loss ratio for connections that used the congestion controller.
        let loss_ratio =
            match i64::try_from((stats.lost * PRECISION_FACTOR_USIZE) / stats.packets_tx) {
                Ok(v) => {
                    glean::http_3_loss_ratio.accumulate_single_sample_signed(v);
                    Some(v)
                }
                Err(e) => {
                    warn!("Failed to convert ratio to i64 for use with glean: {e}");
                    debug_assert!(
                        false,
                        "Failed to convert ratio to i64 for use with glean: {e}"
                    );
                    None
                }
            };
        // Records the unfiltered (old) slow start exit ratio
        if stats.cc.slow_start_exit.is_some() {
            glean::http_3_slow_start_exited.get("exited").add(1);
        } else {
            glean::http_3_slow_start_exited.get("not_exited").add(1);
        }

        let cwnd_that_grew = (stats.cc.cwnd > MAX_INITIAL_CWND).then_some(stats.cc.cwnd);
        let growth_label = match (
            cwnd_that_grew,
            stats.cc.slow_start_exit.as_ref().map(|e| e.exit_cwnd),
        ) {
            (Some(_), Some(exit_cwnd)) if exit_cwnd < MAX_INITIAL_CWND => {
                "no_growth_then_exit_then_growth"
            }
            (Some(_), _) => "had_growth",
            (None, Some(_)) => "no_growth_but_exit",
            (None, None) => "no_growth",
        };
        glean::http_3_congestion_window_growth
            .get(growth_label)
            .add(1);
        // Filtered: only record CC metrics for connections that grew past the initial window.
        if let Some(final_cwnd) = cwnd_that_grew {
            glean::http_3_final_cwnd.accumulate(final_cwnd as u64);
            if let Some(loss) = loss_ratio {
                glean::http_3_loss_ratio_filtered.accumulate_single_sample_signed(loss);
            }
            // Record metrics concerning the slow start exit point below this filter.
            let mut hystart_label = "not_exited";
            let mut search_label = "not_exited";
            if let Some(slow_start_exit) = stats.cc.slow_start_exit.as_ref() {
                let exit_cwnd = slow_start_exit.exit_cwnd;
                let reason = &slow_start_exit.reason;
                glean::http_3_slow_start_exit_cwnd.accumulate(exit_cwnd as u64);
                glean::http_3_slow_start_exited_filtered
                    .get("exited")
                    .add(1);
                let accuracy_cwnd =
                    ((exit_cwnd.abs_diff(final_cwnd) as f64) / final_cwnd as f64) * 100.0;
                let accuracy_w_max = if let Some(final_w_max) = stats.cc.w_max {
                    assert!(final_w_max > 0.0, "w_max can never be non-positive");
                    glean::http_3_final_w_max.accumulate(final_w_max as u64);
                    Some(((exit_cwnd as f64 - final_w_max).abs() / final_w_max) * 100.0)
                } else {
                    None
                };
                let direction_label = match exit_cwnd.cmp(&final_cwnd) {
                    Ordering::Greater => "overshoot",
                    Ordering::Less => "undershoot",
                    Ordering::Equal => "exact",
                };
                let (reason_label, accuracy_label) = match reason {
                    SlowStartExitReason::CongestionEvent(_) => {
                        glean::http_3_slow_start_exit_direction_loss
                            .get(direction_label)
                            .add(1);
                        hystart_label = "exited_ce";
                        search_label = "exited_ce";
                        ("ce", "ce_exit")
                    }
                    SlowStartExitReason::Heuristic => {
                        glean::http_3_slow_start_exit_direction_heuristic
                            .get(direction_label)
                            .add(1);
                        hystart_label = "exited_hystart";
                        search_label = "exited_search";
                        ("heuristic", "heuristic_exit")
                    }
                };
                glean::http_3_slow_start_exit_reason
                    .get(reason_label)
                    .add(1);
                glean::http_3_slow_start_exit_accuracy
                    .get(accuracy_label)
                    .accumulate_single_sample_signed(accuracy_cwnd as i64);
                if let Some(accuracy_w_max) = accuracy_w_max {
                    glean::http_3_slow_start_exit_accuracy_w_max
                        .get(accuracy_label)
                        .accumulate_single_sample_signed(accuracy_w_max as i64);
                }
            } else {
                glean::http_3_slow_start_exited_filtered
                    .get("not_exited")
                    .add(1);
            }
            // Only record HyStart metrics when HyStart is enabled (1 == HyStart, see constructor).
            if static_prefs::pref!("network.http.http3.slow_start_algorithm") == 1 {
                glean::http_3_hystart_css_rounds_finished
                    .get(hystart_label)
                    .accumulate_single_sample_signed(stats.cc.hystart_css_rounds_finished as i64);
                glean::http_3_hystart_css_entries
                    .get(hystart_label)
                    .accumulate_single_sample_signed(stats.cc.hystart_css_entries as i64);
            }

            // Only record SEARCH metrics when SEARCH is enabled (2 == SEARCH, see constructor).
            if static_prefs::pref!("network.http.http3.slow_start_algorithm") == 2 {
                // Metrics for drain phase evaluation
                if let Some(empty_buffer_bdp) = stats.cc.search_empty_buffer_target {
                    glean::http_3_search_empty_buffer_bdp_estimate.accumulate(empty_buffer_bdp);
                }
                if let Some(full_buffer_bdp) = stats.cc.search_full_buffer_target {
                    glean::http_3_search_full_buffer_bdp_estimate.accumulate(full_buffer_bdp);
                }
                // Metrics to tune EXTRA_BINS
                if let Some(lookback_bins) = stats.cc.search_lookback_bins_needed {
                    glean::http_3_search_lookback_bins
                        .accumulate_single_sample_signed(lookback_bins as i64);
                    glean::http_3_search_rtt_inflated.get("inflated").add(1);
                } else {
                    glean::http_3_search_rtt_inflated
                        .get("never_inflated")
                        .add(1);
                }
                // Metrics to tune THRESH
                if let Some(max_norm_diff) = stats.cc.search_max_norm_diff {
                    glean::http_3_search_max_norm_diff
                        .get(search_label)
                        .accumulate_single_sample_signed(max_norm_diff as i64);
                }
                // Metrics to calibrate reset mechanism
                glean::http_3_search_reset_count
                    .get(search_label)
                    .accumulate_single_sample_signed(stats.cc.search_reset.count as i64);
                if let Some(max_passed_bins) = stats.cc.search_reset.max_passed_bins {
                    glean::http_3_search_max_passed_bins
                        .accumulate_single_sample_signed(max_passed_bins as i64);
                }
                // Metrics to gain insights into app-limited behavior during SEARCH slow start
                glean::http_3_search_zero_bytes_sent
                    .get(search_label)
                    .accumulate_single_sample_signed(stats.cc.search_zero_sent_bytes as i64);

                // Metrics to evaluate whether the first RTT used to initialize SEARCH is inflated
                if let Some(first_rtt) = stats.cc.search_first_rtt {
                    let first_us = u64::try_from(first_rtt.as_micros()).unwrap_or(u64::MAX);
                    let min_us = u64::try_from(stats.min_rtt.as_micros()).unwrap_or(u64::MAX);
                    if min_us > 0 {
                        glean::http_3_search_first_rtt_vs_min_rtt
                            .accumulate_single_sample_signed((first_us * 100 / min_us) as i64);
                    }
                    // And whether using `min(first, second)` would be a viable fix
                    if let Some(second_rtt) = stats.cc.search_second_rtt {
                        let second_us = u64::try_from(second_rtt.as_micros()).unwrap_or(u64::MAX);
                        if second_us > 0 {
                            glean::http_3_search_first_rtt_vs_second_rtt
                                .accumulate_single_sample_signed(
                                    (first_us * 100 / second_us) as i64,
                                );
                        }
                    }
                }
            }
        }

        glean::http_3_congestion_event_count.accumulate_single_sample_signed(
            (stats.cc.congestion_events.ecn + stats.cc.congestion_events.loss)
                .saturating_sub(stats.cc.congestion_events.spurious) as i64,
        );

        if let Some(peer_max) = stats.pmtud_peer_max_udp_payload {
            if let Ok(v) = i64::try_from(peer_max) {
                glean::http_3_peer_max_udp_payload.accumulate_single_sample_signed(v);
            }
        }

        let rtt_ms = |d: Duration| i64::try_from(d.as_millis()).unwrap_or(i64::MAX);
        glean::http_3_rtt.accumulate_single_sample_signed(rtt_ms(stats.rtt));
        glean::http_3_rtt_var.accumulate_single_sample_signed(rtt_ms(stats.rttvar));
        glean::http_3_min_rtt.accumulate_single_sample_signed(rtt_ms(stats.min_rtt));

        // Ignore connections that never had loss induced congestion events (and prevent dividing by zero).
        if stats.cc.congestion_events.loss != 0 {
            if let Ok(spurious) = i64::try_from(
                (stats.cc.congestion_events.spurious * PRECISION_FACTOR_USIZE)
                    / stats.cc.congestion_events.loss,
            ) {
                glean::http_3_spurious_congestion_event_ratio
                    .accumulate_single_sample_signed(spurious);
            } else {
                let msg = "Failed to convert ratio to i64 for use with glean";
                warn!("{msg}");
                debug_assert!(false, "{msg}");
            }
        }

        // Collect congestion event reason metric
        if let Ok(ce_loss) = i32::try_from(stats.cc.congestion_events.loss) {
            glean::http_3_congestion_event_reason
                .get("loss")
                .add(ce_loss);
        } else {
            let msg = "Failed to convert to i32 for use with glean";
            warn!("{msg}");
            debug_assert!(false, "{msg}");
        }
        if let Ok(ce_ecn) = i32::try_from(stats.cc.congestion_events.ecn) {
            glean::http_3_congestion_event_reason
                .get("ecn-ce")
                .add(ce_ecn);
        } else {
            let msg = "Failed to convert to i32 for use with glean";
            warn!("{msg}");
            debug_assert!(false, "{msg}");
        }
    }

    fn increment_would_block_rx(&mut self) {
        self.would_block_counter.increment_rx();
    }

    fn would_block_rx_count(&self) -> usize {
        self.would_block_counter.rx_count()
    }

    fn increment_would_block_tx(&mut self) {
        self.would_block_counter.increment_tx();
    }

    fn would_block_tx_count(&self) -> usize {
        self.would_block_counter.tx_count()
    }
}

/// # Safety
///
/// See [`AtomicRefcnt::inc`].
#[no_mangle]
pub unsafe extern "C" fn neqo_http3conn_addref(conn: &NeqoHttp3Conn) {
    conn.refcnt.inc();
}

/// # Safety
///
/// Manually drops a pointer without consuming pointee. The caller needs to
/// ensure no other referenecs remain. In addition safety conditions of
/// [`AtomicRefcnt::dec`] apply.
#[no_mangle]
pub unsafe extern "C" fn neqo_http3conn_release(conn: &NeqoHttp3Conn) {
    let rc = conn.refcnt.dec();
    if rc == 0 {
        drop(Box::from_raw(ptr::from_ref(conn).cast_mut()));
    }
}

// xpcom::RefPtr support
unsafe impl RefCounted for NeqoHttp3Conn {
    unsafe fn addref(&self) {
        neqo_http3conn_addref(self);
    }
    unsafe fn release(&self) {
        neqo_http3conn_release(self);
    }
}

// Allocate a new NeqoHttp3Conn object.
#[no_mangle]
pub extern "C" fn neqo_http3conn_new(
    origin: &nsACString,
    alpn: &nsACString,
    local_addr: *const NetAddr,
    remote_addr: *const NetAddr,
    max_table_size: u64,
    max_blocked_streams: u16,
    max_data: u64,
    max_stream_data: u64,
    version_negotiation: bool,
    webtransport: bool,
    qlog_dir: &nsACString,
    idle_timeout: u32,
    fast_pto: u32,
    socket: i64,
    pmtud_enabled: bool,
    result: &mut *const NeqoHttp3Conn,
) -> nsresult {
    *result = ptr::null_mut();

    match NeqoHttp3Conn::new(
        origin,
        alpn,
        local_addr,
        remote_addr,
        max_table_size,
        max_blocked_streams,
        max_data,
        max_stream_data,
        version_negotiation,
        webtransport,
        qlog_dir,
        idle_timeout,
        fast_pto,
        pmtud_enabled,
        Some(socket),
    ) {
        Ok(http3_conn) => {
            http3_conn.forget(result);
            NS_OK
        }
        Err(e) => e,
    }
}

// Allocate a new NeqoHttp3Conn object using NSPR for IO.
#[no_mangle]
pub extern "C" fn neqo_http3conn_new_use_nspr_for_io(
    origin: &nsACString,
    alpn: &nsACString,
    local_addr: *const NetAddr,
    remote_addr: *const NetAddr,
    max_table_size: u64,
    max_blocked_streams: u16,
    max_data: u64,
    max_stream_data: u64,
    version_negotiation: bool,
    webtransport: bool,
    qlog_dir: &nsACString,
    idle_timeout: u32,
    fast_pto: u32,
    result: &mut *const NeqoHttp3Conn,
) -> nsresult {
    *result = ptr::null_mut();

    match NeqoHttp3Conn::new(
        origin,
        alpn,
        local_addr,
        remote_addr,
        max_table_size,
        max_blocked_streams,
        max_data,
        max_stream_data,
        version_negotiation,
        webtransport,
        qlog_dir,
        idle_timeout,
        fast_pto,
        false,
        None,
    ) {
        Ok(http3_conn) => {
            http3_conn.forget(result);
            NS_OK
        }
        Err(e) => e,
    }
}

/// Process a packet.
/// packet holds packet data.
///
/// # Safety
///
/// Use of raw (i.e. unsafe) pointers as arguments.
#[no_mangle]
pub unsafe extern "C" fn neqo_http3conn_process_input_use_nspr_for_io(
    conn: &mut NeqoHttp3Conn,
    remote_addr: *const NetAddr,
    packet: *const ThinVec<u8>,
) -> nsresult {
    assert!(conn.socket.is_none(), "NSPR IO path");

    let remote = match netaddr_to_socket_addr(remote_addr) {
        Ok(addr) => addr,
        Err(result) => return result,
    };
    let d = Datagram::new(
        remote,
        conn.local_addr,
        Tos::default(),
        (*packet).as_slice(),
    );
    conn.conn.process_input(d, Instant::now());
    NS_OK
}

#[repr(C)]
pub struct ProcessInputResult {
    pub result: nsresult,
    pub bytes_read: u32,
}

/// Process input, reading incoming datagrams from the socket and passing them
/// to the Neqo state machine.
///
/// # Safety
///
/// Marked as unsafe given exposition via FFI i.e. `extern "C"`.
#[no_mangle]
pub unsafe extern "C" fn neqo_http3conn_process_input(
    conn: &mut NeqoHttp3Conn,
) -> ProcessInputResult {
    let span = tracing::debug_span!(
        "neqo_http3conn_process_input",
        bytes_received = tracing::field::Empty,
        segments_received = tracing::field::Empty
    )
    .entered();

    let mut bytes_read = 0;
    let mut segments_received: u64 = 0;

    let result = RECV_BUF.with_borrow_mut(|recv_buf| {
        loop {
            let dgrams = match conn
                .socket
                .as_mut()
                .expect("non NSPR IO")
                .recv(conn.local_addr, recv_buf)
            {
                Ok(dgrams) => dgrams,
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                    conn.increment_would_block_rx();
                    break;
                }
                Err(e) => {
                    warn!("failed to receive datagrams: {}", e);
                    return ProcessInputResult {
                        result: into_nsresult(&e),
                        bytes_read: 0,
                    };
                }
            };

            // Attach metric instrumentation to `dgrams` iterator.
            let mut sum = 0;
            let mut segment_count = 0;
            let datagram_segment_size_received = &mut conn.datagram_segment_size_received;
            let dgrams = dgrams.inspect(|d| {
                datagram_segment_size_received.accumulate(d.len() as u64);
                sum += d.len();
                segment_count += 1;
            });

            // Override `dgrams` ECN marks according to prefs.
            let ecn_enabled = static_prefs::pref!("network.http.http3.ecn_report");
            let dgrams = dgrams.map(|mut d| {
                if !ecn_enabled {
                    d.set_tos(Tos::default());
                }
                d
            });

            conn.conn.process_multiple_input(dgrams, Instant::now());

            conn.datagram_size_received.accumulate(sum as u64);
            conn.datagram_segments_received.accumulate(segment_count);
            bytes_read += sum;
            segments_received += segment_count;

            if bytes_read >= MAX_BYTES_READ_PER_PASS {
                warn!(
                    "reached the {MAX_BYTES_READ_PER_PASS} byte receive cap in a single pass; \
                     yielding to deliver buffered events, will continue on the next RecvData"
                );
                break;
            }
        }

        ProcessInputResult {
            result: NS_OK,
            bytes_read: bytes_read.try_into().unwrap_or(u32::MAX),
        }
    });

    span.record("bytes_received", bytes_read);
    span.record("segments_received", segments_received);
    result
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_process_output_and_send_use_nspr_for_io(
    conn: &mut NeqoHttp3Conn,
    context: *mut c_void,
    send_func: SendFunc,
    set_timer_func: SetTimerFunc,
) -> nsresult {
    assert!(conn.socket.is_none(), "NSPR IO path");

    loop {
        match conn.conn.process_output(Instant::now()) {
            Output::Datagram(dg) => {
                let Ok(len) = u32::try_from(dg.len()) else {
                    return NS_ERROR_UNEXPECTED;
                };
                let rv = match dg.destination().ip() {
                    IpAddr::V4(v4) => send_func(
                        context,
                        AF_INET_U16,
                        v4.octets().as_ptr(),
                        dg.destination().port(),
                        dg.as_ptr(),
                        len,
                    ),
                    IpAddr::V6(v6) => send_func(
                        context,
                        AF_INET6_U16,
                        v6.octets().as_ptr(),
                        dg.destination().port(),
                        dg.as_ptr(),
                        len,
                    ),
                };
                if rv != NS_OK {
                    return rv;
                }
            }
            Output::Callback(to) => {
                let timeout = if to.is_zero() {
                    Duration::from_millis(1)
                } else {
                    to
                };
                let Ok(timeout) = u64::try_from(timeout.as_millis()) else {
                    return NS_ERROR_UNEXPECTED;
                };
                set_timer_func(context, timeout);
                break;
            }
            Output::None => {
                set_timer_func(context, u64::MAX);
                break;
            }
        }
    }
    NS_OK
}

#[repr(C)]
pub struct ProcessOutputAndSendResult {
    pub result: nsresult,
    pub bytes_written: u32,
}

/// Process output, retrieving outgoing datagrams from the Neqo state machine
/// and writing them to the socket.
#[no_mangle]
pub extern "C" fn neqo_http3conn_process_output_and_send(
    conn: &mut NeqoHttp3Conn,
    context: *mut c_void,
    set_timer_func: SetTimerFunc,
) -> ProcessOutputAndSendResult {
    let span = tracing::debug_span!(
        "neqo_http3conn_process_output_and_send",
        bytes_sent = tracing::field::Empty,
        segments_sent = tracing::field::Empty
    )
    .entered();

    let mut bytes_written: usize = 0;
    let mut segments_sent: u64 = 0;
    let result = loop {
        let Ok(max_gso_segments) = min(
            static_prefs::pref!("network.http.http3.max_gso_segments")
                .try_into()
                .expect("u32 fit usize"),
            conn.socket
                .as_mut()
                .expect("non NSPR IO")
                .max_gso_segments(),
        )
        .try_into() else {
            error!("Socket return GSO size of 0");
            break NS_ERROR_UNEXPECTED;
        };

        let output = conn
            .buffered_outbound_datagram
            .take()
            .map(OutputBatch::DatagramBatch)
            .unwrap_or_else(|| {
                conn.conn
                    .process_multiple_output(Instant::now(), max_gso_segments)
            });
        match output {
            OutputBatch::DatagramBatch(mut dg) => {
                if !static_prefs::pref!("network.http.http3.ecn_mark") {
                    dg.set_tos(Tos::default());
                }

                if static_prefs::pref!("network.http.http3.block_loopback_ipv6_addr")
                    && matches!(dg.destination(), SocketAddr::V6(addr) if addr.ip().is_loopback())
                {
                    debug!("network.http.http3.block_loopback_ipv6_addr is set, returning NS_ERROR_CONNECTION_REFUSED for localhost IPv6");
                    break NS_ERROR_CONNECTION_REFUSED;
                }

                let send_result = if mock_udp_send_would_block(dg.destination()) {
                    Err(io::Error::from(io::ErrorKind::WouldBlock))
                } else {
                    conn.socket.as_mut().expect("non NSPR IO").send(&dg)
                };
                match send_result {
                    Ok(()) => {}
                    Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                        conn.increment_would_block_tx();
                        if static_prefs::pref!("network.http.http3.pr_poll_write") {
                            debug!("Buffer outbound datagram to be sent once UDP socket has write-availability.");
                            conn.buffered_outbound_datagram = Some(dg);
                            // Propagate WouldBlock error, thus indicating that
                            // the UDP socket should be polled for
                            // write-availability.
                            break NS_BASE_STREAM_WOULD_BLOCK;
                        } else {
                            warn!("dropping datagram as socket would block");
                            break NS_OK;
                        }
                    }
                    Err(e) if e.raw_os_error() == Some(libc::EIO) && dg.num_datagrams() > 1 => {
                        // GSO is unsupported on this send path. quinn-udp has now
                        // disabled GSO for subsequent sends, but the kernel
                        // dropped the current batch. Resend it as individual
                        // datagrams right away, rather than waiting for the QUIC
                        // PTO (~300ms) to retransmit.
                        //
                        // See following resources for details:
                        // - <https://bugzilla.mozilla.org/show_bug.cgi?id=2049334>
                        // - <https://bugzilla.mozilla.org/show_bug.cgi?id=1989895>
                        // - <https://github.com/quinn-rs/quinn/blob/93b6d01605147b9763ee1b1b381a6feb9fcd454e/quinn-udp/src/unix.rs#L345-L349>
                        //
                        // Long term this fallback belongs in quinn-udp, see
                        // <https://github.com/quinn-rs/quinn/issues/2399>.
                        debug!("Failed to send datagram batch size {} with error {e}. Missing GSO support? Resending as individual datagrams.", dg.num_datagrams());
                        let socket = conn.socket.as_mut().expect("non NSPR IO");
                        for single in dg.iter() {
                            let single = datagram::Batch::from(single.to_owned());
                            if let Err(e) = socket.send(&single) {
                                warn!("failed to resend datagram without GSO: {e}");
                                break;
                            }
                        }
                    }
                    Err(e) => {
                        warn!("failed to send datagram: {}", e);
                        break into_nsresult(&e);
                    }
                }
                bytes_written += dg.data().len();
                segments_sent += dg.num_datagrams() as u64;

                // Glean metrics
                conn.datagram_size_sent.accumulate(dg.data().len() as u64);
                conn.datagram_segments_sent
                    .accumulate(dg.num_datagrams() as u64);
                for _ in 0..(dg.data().len() / dg.datagram_size()) {
                    conn.datagram_segment_size_sent
                        .accumulate(dg.datagram_size().get() as u64);
                }
                conn.datagram_segment_size_sent.accumulate(
                    dg.data()
                        .len()
                        .checked_rem(dg.datagram_size().get())
                        .expect("datagram_size is a NonZeroUsize") as u64,
                );
            }
            OutputBatch::Callback(to) => {
                let timeout = if to.is_zero() {
                    Duration::from_millis(1)
                } else {
                    to
                };
                let Ok(timeout) = u64::try_from(timeout.as_millis()) else {
                    break NS_ERROR_UNEXPECTED;
                };
                set_timer_func(context, timeout);
                break NS_OK;
            }
            OutputBatch::None => {
                set_timer_func(context, u64::MAX);
                break NS_OK;
            }
        }
    };

    span.record("bytes_sent", bytes_written);
    span.record("segments_sent", segments_sent);
    ProcessOutputAndSendResult {
        result,
        bytes_written: bytes_written.try_into().unwrap_or(u32::MAX),
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_close(conn: &mut NeqoHttp3Conn, error: u64) {
    conn.conn.close(Instant::now(), error, "");
}

fn is_excluded_header(name: &str) -> bool {
    matches!(
        name,
        "connection"
            | "host"
            | "keep-alive"
            | "proxy-connection"
            | "te"
            | "transfer-encoding"
            | "upgrade"
            | "sec-websocket-key"
    )
}

fn parse_headers(headers: &nsACString) -> Result<Vec<Header>, nsresult> {
    let mut hdrs = Vec::new();
    // this is only used for headers built by Firefox.
    // Firefox supplies all headers already prepared for sending over http1.
    // They need to be split into (name, value) pairs where name is a String
    // and value is a Vec<u8>.

    let headers_bytes: &[u8] = headers;

    // Split on either \r or \n. When splitting "\r\n" sequences, this produces
    // an empty element between them which is filtered out by the is_empty check.
    // This also handles malformed inputs with bare \r or \n.
    for elem in headers_bytes.split(|&b| b == b'\r' || b == b'\n').skip(1) {
        if elem.is_empty() {
            continue;
        }
        if elem.starts_with(b":") {
            // colon headers are for http/2 and 3 and this is http/1
            // input, so that is probably a smuggling attack of some
            // kind.
            continue;
        }

        let colon_pos = match elem.iter().position(|&b| b == b':') {
            Some(pos) => pos,
            None => continue, // No colon, skip this line
        };

        let name_bytes = &elem[..colon_pos];
        // Safe: if colon is at the end, this yields an empty slice
        let value_bytes = &elem[colon_pos + 1..];

        // Header names must be valid UTF-8
        let name = match str::from_utf8(name_bytes) {
            Ok(n) => n.trim().to_lowercase(),
            Err(_) => return Err(NS_ERROR_DOM_INVALID_HEADER_NAME),
        };

        if is_excluded_header(&name) {
            continue;
        }

        // Trim leading and trailing optional whitespace (OWS) from value.
        // Per RFC 9110, OWS is defined as *( SP / HTAB ), i.e., space and tab only.
        let value = value_bytes
            .iter()
            .position(|&b| b != b' ' && b != b'\t')
            .map_or(&value_bytes[0..0], |start| {
                let end = value_bytes
                    .iter()
                    .rposition(|&b| b != b' ' && b != b'\t')
                    .map_or(value_bytes.len(), |pos| pos + 1);
                &value_bytes[start..end]
            })
            .to_vec();

        hdrs.push(Header::new(name, value));
    }
    Ok(hdrs)
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_fetch(
    conn: &mut NeqoHttp3Conn,
    method: &nsACString,
    scheme: &nsACString,
    host: &nsACString,
    path: &nsACString,
    headers: &nsACString,
    stream_id: &mut u64,
    urgency: u8,
    incremental: bool,
) -> nsresult {
    let hdrs = match parse_headers(headers) {
        Err(e) => {
            return e;
        }
        Ok(h) => h,
    };
    let Ok(method_tmp) = str::from_utf8(method) else {
        return NS_ERROR_INVALID_ARG;
    };
    let Ok(scheme_tmp) = str::from_utf8(scheme) else {
        return NS_ERROR_INVALID_ARG;
    };
    let Ok(host_tmp) = str::from_utf8(host) else {
        return NS_ERROR_INVALID_ARG;
    };
    let Ok(path_tmp) = str::from_utf8(path) else {
        return NS_ERROR_INVALID_ARG;
    };
    if urgency >= 8 {
        return NS_ERROR_INVALID_ARG;
    }
    let priority = Priority::new(urgency, incremental);
    match conn.conn.fetch(
        Instant::now(),
        method_tmp,
        (scheme_tmp, host_tmp, path_tmp),
        &hdrs,
        priority,
    ) {
        Ok(id) => {
            *stream_id = id.as_u64();
            NS_OK
        }
        Err(Http3Error::StreamLimit) => NS_BASE_STREAM_WOULD_BLOCK,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_connect(
    conn: &mut NeqoHttp3Conn,
    host: &nsACString,
    headers: &nsACString,
    stream_id: &mut u64,
    urgency: u8,
    incremental: bool,
) -> nsresult {
    let hdrs = match parse_headers(headers) {
        Err(e) => {
            return e;
        }
        Ok(h) => h,
    };
    let Ok(host_tmp) = str::from_utf8(host) else {
        return NS_ERROR_INVALID_ARG;
    };
    if urgency >= 8 {
        return NS_ERROR_INVALID_ARG;
    }
    let priority = Priority::new(urgency, incremental);
    match conn.conn.connect(Instant::now(), host_tmp, &hdrs, priority) {
        Ok(id) => {
            *stream_id = id.as_u64();
            NS_OK
        }
        Err(Http3Error::StreamLimit) => NS_BASE_STREAM_WOULD_BLOCK,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_priority_update(
    conn: &mut NeqoHttp3Conn,
    stream_id: u64,
    urgency: u8,
    incremental: bool,
) -> nsresult {
    if urgency >= 8 {
        return NS_ERROR_INVALID_ARG;
    }
    let priority = Priority::new(urgency, incremental);
    match conn
        .conn
        .priority_update(StreamId::from(stream_id), priority)
    {
        Ok(_) => NS_OK,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

/// # Safety
///
/// Use of raw (i.e. unsafe) pointers as arguments.
#[no_mangle]
pub unsafe extern "C" fn neqo_htttp3conn_send_request_body(
    conn: &mut NeqoHttp3Conn,
    stream_id: u64,
    buf: *const u8,
    len: u32,
    read: &mut u32,
) -> nsresult {
    let array = slice::from_raw_parts(buf, len as usize);
    conn.conn
        .send_data(StreamId::from(stream_id), array, Instant::now())
        .map_or(NS_ERROR_UNEXPECTED, |amount| {
            let Ok(amount) = u32::try_from(amount) else {
                return NS_ERROR_UNEXPECTED;
            };
            *read = amount;
            if amount == 0 {
                NS_BASE_STREAM_WOULD_BLOCK
            } else {
                NS_OK
            }
        })
}

const fn crypto_error_code(err: &nss_rs::Error) -> u64 {
    match err {
        nss_rs::Error::Aead => 1,
        nss_rs::Error::CertificateLoading => 2,
        nss_rs::Error::CreateSslSocket => 3,
        nss_rs::Error::Internal => 5,
        nss_rs::Error::IntegerOverflow => 6,
        nss_rs::Error::InvalidEpoch => 7,
        nss_rs::Error::MixedHandshakeMethod => 8,
        nss_rs::Error::NoDataAvailable => 9,
        nss_rs::Error::Nss { .. } => 10,
        nss_rs::Error::SelfEncrypt => 12,
        nss_rs::Error::TimeTravel => 13,
        nss_rs::Error::UnsupportedCipher => 14,
        nss_rs::Error::UnsupportedVersion => 15,
        nss_rs::Error::String => 16,
        nss_rs::Error::EchRetry(_) => 17,
        nss_rs::Error::CipherInit => 18,
        nss_rs::Error::CertificateDecoding => 19,
        nss_rs::Error::CertificateEncoding => 20,
        nss_rs::Error::InvalidCertificateCompressionID => 21,
        nss_rs::Error::InvalidAlpn => 22,
        nss_rs::Error::AeadTruncated => 23,
        nss_rs::Error::InvalidInput => 24,
        nss_rs::Error::UnsupportedCurve => 25,
        nss_rs::Error::UnsupportedHash => 26,
        nss_rs::Error::InvalidState => 27,
    }
}

// This is only used for telemetry. Therefore we only return error code
// numbers and do not label them. Recording telemetry is easier with a
// number.
#[repr(C)]
pub enum CloseError {
    TransportInternalError,
    TransportInternalErrorOther(u16),
    TransportError(u64),
    CryptoError(u64),
    CryptoAlert(u8),
    PeerAppError(u64),
    PeerError(u64),
    AppError(u64),
    EchRetry,
}

impl From<TransportError> for CloseError {
    fn from(error: TransportError) -> Self {
        #[expect(clippy::match_same_arms, reason = "It's cleaner this way.")]
        match error {
            TransportError::Internal => Self::TransportInternalError,
            TransportError::Crypto(nss_rs::Error::EchRetry(_)) => Self::EchRetry,
            TransportError::Crypto(c) => Self::CryptoError(crypto_error_code(&c)),
            TransportError::CryptoAlert(c) => Self::CryptoAlert(c),
            TransportError::PeerApplication(c) => Self::PeerAppError(c),
            TransportError::Peer(c) => Self::PeerError(c),
            TransportError::None
            | TransportError::IdleTimeout
            | TransportError::ConnectionRefused
            | TransportError::FlowControl
            | TransportError::StreamLimit
            | TransportError::StreamState
            | TransportError::FinalSize
            | TransportError::FrameEncoding
            | TransportError::TransportParameter
            | TransportError::ProtocolViolation
            | TransportError::InvalidToken
            | TransportError::KeysExhausted
            | TransportError::Application
            | TransportError::NoAvailablePath
            | TransportError::CryptoBufferExceeded => Self::TransportError(error.code()),
            TransportError::EchRetry(_) => Self::EchRetry,
            TransportError::AckedUnsentPacket => Self::TransportInternalErrorOther(0),
            TransportError::ConnectionIdLimitExceeded => Self::TransportInternalErrorOther(1),
            TransportError::ConnectionIdsExhausted => Self::TransportInternalErrorOther(2),
            TransportError::ConnectionState => Self::TransportInternalErrorOther(3),
            TransportError::Decrypt => Self::TransportInternalErrorOther(5),
            TransportError::IntegerOverflow => Self::TransportInternalErrorOther(7),
            TransportError::InvalidInput => Self::TransportInternalErrorOther(8),
            TransportError::InvalidMigration => Self::TransportInternalErrorOther(9),
            TransportError::InvalidPacket => Self::TransportInternalErrorOther(10),
            TransportError::InvalidResumptionToken => Self::TransportInternalErrorOther(11),
            TransportError::InvalidRetry => Self::TransportInternalErrorOther(12),
            TransportError::InvalidStreamId => Self::TransportInternalErrorOther(13),
            TransportError::KeysDiscarded(_) => Self::TransportInternalErrorOther(14),
            TransportError::KeysPending(_) => Self::TransportInternalErrorOther(15),
            TransportError::KeyUpdateBlocked => Self::TransportInternalErrorOther(16),
            TransportError::NoMoreData => Self::TransportInternalErrorOther(17),
            TransportError::NotConnected => Self::TransportInternalErrorOther(18),
            TransportError::PacketNumberOverlap => Self::TransportInternalErrorOther(19),
            TransportError::StatelessReset => Self::TransportInternalErrorOther(20),
            TransportError::TooMuchData => Self::TransportInternalErrorOther(21),
            TransportError::UnexpectedMessage => Self::TransportInternalErrorOther(22),
            TransportError::UnknownConnectionId => Self::TransportInternalErrorOther(23),
            TransportError::UnknownFrameType => Self::TransportInternalErrorOther(24),
            TransportError::VersionNegotiation => Self::TransportInternalErrorOther(25),
            TransportError::WrongRole => Self::TransportInternalErrorOther(26),
            TransportError::Qlog => Self::TransportInternalErrorOther(27),
            TransportError::NotAvailable => Self::TransportInternalErrorOther(28),
            TransportError::DisabledVersion => Self::TransportInternalErrorOther(29),
            TransportError::UnknownTransportParameter => Self::TransportInternalErrorOther(30),
            TransportError::TooManyPtos => Self::TransportInternalErrorOther(31),
        }
    }
}

// Keep in sync with `netwerk/metrics.yaml` `http_3_connection_close_reason` metric labels.
#[cfg(not(target_os = "android"))]
const fn transport_error_to_glean_label(error: &TransportError) -> &'static str {
    match error {
        TransportError::None => "NoError",
        TransportError::Internal => "InternalError",
        TransportError::ConnectionRefused => "ConnectionRefused",
        TransportError::FlowControl => "FlowControlError",
        TransportError::StreamLimit => "StreamLimitError",
        TransportError::StreamState => "StreamStateError",
        TransportError::FinalSize => "FinalSizeError",
        TransportError::FrameEncoding => "FrameEncodingError",
        TransportError::TransportParameter => "TransportParameterError",
        TransportError::ProtocolViolation => "ProtocolViolation",
        TransportError::InvalidToken => "InvalidToken",
        TransportError::Application => "ApplicationError",
        TransportError::CryptoBufferExceeded => "CryptoBufferExceeded",
        TransportError::Crypto(_) => "CryptoError",
        TransportError::Qlog => "QlogError",
        TransportError::CryptoAlert(_) => "CryptoAlert",
        TransportError::EchRetry(_) => "EchRetry",
        TransportError::AckedUnsentPacket => "AckedUnsentPacket",
        TransportError::ConnectionIdLimitExceeded => "ConnectionIdLimitExceeded",
        TransportError::ConnectionIdsExhausted => "ConnectionIdsExhausted",
        TransportError::ConnectionState => "ConnectionState",
        TransportError::Decrypt => "DecryptError",
        TransportError::DisabledVersion => "DisabledVersion",
        TransportError::IdleTimeout => "IdleTimeout",
        TransportError::IntegerOverflow => "IntegerOverflow",
        TransportError::InvalidInput => "InvalidInput",
        TransportError::InvalidMigration => "InvalidMigration",
        TransportError::InvalidPacket => "InvalidPacket",
        TransportError::InvalidResumptionToken => "InvalidResumptionToken",
        TransportError::InvalidRetry => "InvalidRetry",
        TransportError::InvalidStreamId => "InvalidStreamId",
        TransportError::KeysDiscarded(_) => "KeysDiscarded",
        TransportError::KeysExhausted => "KeysExhausted",
        TransportError::KeysPending(_) => "KeysPending",
        TransportError::KeyUpdateBlocked => "KeyUpdateBlocked",
        TransportError::NoAvailablePath => "NoAvailablePath",
        TransportError::NoMoreData => "NoMoreData",
        TransportError::NotAvailable => "NotAvailable",
        TransportError::NotConnected => "NotConnected",
        TransportError::PacketNumberOverlap => "PacketNumberOverlap",
        TransportError::PeerApplication(_) => "PeerApplicationError",
        TransportError::Peer(_) => "PeerError",
        TransportError::StatelessReset => "StatelessReset",
        TransportError::TooMuchData => "TooMuchData",
        TransportError::UnexpectedMessage => "UnexpectedMessage",
        TransportError::UnknownConnectionId => "UnknownConnectionId",
        TransportError::UnknownFrameType => "UnknownFrameType",
        TransportError::VersionNegotiation => "VersionNegotiation",
        TransportError::WrongRole => "WrongRole",
        TransportError::UnknownTransportParameter => "UnknownTransportParameter",
        TransportError::TooManyPtos => "TooManyPtos",
    }
}

impl From<neqo_transport::CloseReason> for CloseError {
    fn from(error: neqo_transport::CloseReason) -> Self {
        match error {
            neqo_transport::CloseReason::Transport(c) => c.into(),
            neqo_transport::CloseReason::Application(c) => Self::AppError(c),
        }
    }
}

// Reset a stream with streamId.
#[no_mangle]
pub extern "C" fn neqo_http3conn_cancel_fetch(
    conn: &mut NeqoHttp3Conn,
    stream_id: u64,
    error: u64,
) -> nsresult {
    match conn.conn.cancel_fetch(StreamId::from(stream_id), error) {
        Ok(()) => NS_OK,
        Err(_) => NS_ERROR_INVALID_ARG,
    }
}

// Reset a stream with streamId.
#[no_mangle]
pub extern "C" fn neqo_http3conn_reset_stream(
    conn: &mut NeqoHttp3Conn,
    stream_id: u64,
    error: u64,
) -> nsresult {
    match conn
        .conn
        .stream_reset_send(StreamId::from(stream_id), error)
    {
        Ok(()) => NS_OK,
        Err(_) => NS_ERROR_INVALID_ARG,
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_stream_stop_sending(
    conn: &mut NeqoHttp3Conn,
    stream_id: u64,
    error: u64,
) -> nsresult {
    match conn
        .conn
        .stream_stop_sending(StreamId::from(stream_id), error)
    {
        Ok(()) => NS_OK,
        Err(_) => NS_ERROR_INVALID_ARG,
    }
}

// Close sending side of a stream with stream_id
#[no_mangle]
pub extern "C" fn neqo_http3conn_close_stream(
    conn: &mut NeqoHttp3Conn,
    stream_id: u64,
) -> nsresult {
    match conn
        .conn
        .stream_close_send(StreamId::from(stream_id), Instant::now())
    {
        Ok(()) => NS_OK,
        Err(_) => NS_ERROR_INVALID_ARG,
    }
}

// WebTransport streams can be unidirectional and bidirectional.
// It is mapped to and from neqo's StreamType enum.
#[repr(C)]
pub enum WebTransportStreamType {
    BiDi,
    UniDi,
}

impl From<StreamType> for WebTransportStreamType {
    fn from(t: StreamType) -> Self {
        match t {
            StreamType::BiDi => Self::BiDi,
            StreamType::UniDi => Self::UniDi,
        }
    }
}

impl From<WebTransportStreamType> for StreamType {
    fn from(t: WebTransportStreamType) -> Self {
        match t {
            WebTransportStreamType::BiDi => Self::BiDi,
            WebTransportStreamType::UniDi => Self::UniDi,
        }
    }
}

#[repr(C)]
pub enum SessionCloseReasonExternal {
    Error(u64),
    Status(u16),
    Clean(u32),
}

impl SessionCloseReasonExternal {
    fn new(reason: session::CloseReason, data: &mut ThinVec<u8>) -> Self {
        match reason {
            session::CloseReason::Error(e) => Self::Error(e),
            session::CloseReason::Status(s) => Self::Status(s),
            session::CloseReason::Clean { error, message } => {
                data.extend_from_slice(message.as_ref());
                Self::Clean(error)
            }
        }
    }
}

#[repr(C)]
pub enum WebTransportEventExternal {
    Negotiated(bool),
    Session(u64),
    SessionClosed {
        stream_id: u64,
        reason: SessionCloseReasonExternal,
    },
    NewStream {
        stream_id: u64,
        stream_type: WebTransportStreamType,
        session_id: u64,
    },
    Datagram {
        session_id: u64,
    },
    Draining {
        session_id: u64,
    },
}
#[repr(C)]
pub enum ConnectUdpEventExternal {
    Negotiated(bool),
    Session(u64),
    SessionClosed {
        stream_id: u64,
        reason: SessionCloseReasonExternal,
    },
    Datagram {
        session_id: u64,
    },
}

impl WebTransportEventExternal {
    fn new(event: WebTransportEvent, data: &mut ThinVec<u8>) -> Self {
        match event {
            WebTransportEvent::Negotiated(n) => Self::Negotiated(n),
            WebTransportEvent::NewSession {
                stream_id, status, headers
            } => {
                if convert_h3_to_h1_headers(&headers, data) != NS_OK {
                    data.clear();
                    data.extend_from_slice(b"HTTP/3 ");
                    data.extend_from_slice(status.to_string().as_bytes());
                    data.extend_from_slice(b"\r\n\r\n");
                }
                Self::Session(stream_id.as_u64())
            }
            WebTransportEvent::SessionClosed {
                stream_id, reason, ..
            } => match reason {
                session::CloseReason::Status(status) => {
                    data.extend_from_slice(b"HTTP/3 ");
                    data.extend_from_slice(status.to_string().as_bytes());
                    data.extend_from_slice(b"\r\n\r\n");
                    Self::Session(stream_id.as_u64())
                }
                _ => Self::SessionClosed {
                    stream_id: stream_id.as_u64(),
                    reason: SessionCloseReasonExternal::new(reason, data),
                },
            },
            WebTransportEvent::NewStream {
                stream_id,
                session_id,
            } => Self::NewStream {
                stream_id: stream_id.as_u64(),
                stream_type: stream_id.stream_type().into(),
                session_id: session_id.as_u64(),
            },
            WebTransportEvent::Datagram {
                session_id,
                datagram,
            } => {
                data.extend_from_slice(datagram.as_ref());
                Self::Datagram {
                    session_id: session_id.as_u64(),
                }
            }
            WebTransportEvent::Draining { stream_id } => Self::Draining {
                session_id: stream_id.as_u64(),
            },
        }
    }
}
impl ConnectUdpEventExternal {
    fn new(event: ConnectUdpEvent, data: &mut ThinVec<u8>) -> Self {
        match event {
            ConnectUdpEvent::Negotiated(n) => Self::Negotiated(n),
            ConnectUdpEvent::NewSession {
                stream_id, status, ..
            } => {
                data.extend_from_slice(b"HTTP/3 ");
                data.extend_from_slice(status.to_string().as_bytes());
                data.extend_from_slice(b"\r\n\r\n");
                Self::Session(stream_id.as_u64())
            }
            ConnectUdpEvent::SessionClosed {
                stream_id, reason, ..
            } => match reason {
                session::CloseReason::Status(status) => {
                    data.extend_from_slice(b"HTTP/3 ");
                    data.extend_from_slice(status.to_string().as_bytes());
                    data.extend_from_slice(b"\r\n\r\n");
                    Self::Session(stream_id.as_u64())
                }
                _ => Self::SessionClosed {
                    stream_id: stream_id.as_u64(),
                    reason: SessionCloseReasonExternal::new(reason, data),
                },
            },
            ConnectUdpEvent::Datagram {
                session_id,
                datagram,
            } => {
                data.extend_from_slice(datagram.as_ref());
                Self::Datagram {
                    session_id: session_id.as_u64(),
                }
            }
        }
    }
}

#[repr(C)]
pub enum Http3Event {
    /// A request stream has space for more data to be sent.
    DataWritable {
        stream_id: u64,
    },
    /// A server has sent a `STOP_SENDING` frame.
    StopSending {
        stream_id: u64,
        error: u64,
    },
    HeaderReady {
        stream_id: u64,
        fin: bool,
        interim: bool,
    },
    /// New bytes available for reading.
    DataReadable {
        stream_id: u64,
    },
    /// Peer reset the stream.
    Reset {
        stream_id: u64,
        error: u64,
        local: bool,
    },
    /// A `PushPromise`
    PushPromise {
        push_id: u64,
        request_stream_id: u64,
    },
    /// A push response headers are ready.
    PushHeaderReady {
        push_id: u64,
        fin: bool,
    },
    /// New bytes are available on a push stream for reading.
    PushDataReadable {
        push_id: u64,
    },
    /// A push has been canceled.
    PushCanceled {
        push_id: u64,
    },
    PushReset {
        push_id: u64,
        error: u64,
    },
    RequestsCreatable,
    AuthenticationNeeded,
    ZeroRttRejected,
    ConnectionConnected,
    GoawayReceived,
    ConnectionClosing {
        error: CloseError,
    },
    ConnectionClosed {
        error: CloseError,
    },
    ResumptionToken {
        expire_in: u64, // microseconds
    },
    EchFallbackAuthenticationNeeded,
    WebTransport(WebTransportEventExternal),
    ConnectUdp(ConnectUdpEventExternal),
    NoEvent,
}

fn sanitize_header(mut y: Cow<[u8]>) -> Cow<[u8]> {
    for i in 0..y.len() {
        if matches!(y[i], b'\n' | b'\r' | b'\0') {
            y.to_mut()[i] = b' ';
        }
    }
    y
}

fn convert_h3_to_h1_headers(headers: &[Header], ret_headers: &mut ThinVec<u8>) -> nsresult {
    if headers.iter().filter(|&h| h.name() == ":status").count() != 1 {
        return NS_ERROR_ILLEGAL_VALUE;
    }

    let status_val = headers
        .iter()
        .find(|&h| h.name() == ":status")
        .expect("must be one")
        .value();

    ret_headers.extend_from_slice(b"HTTP/3 ");
    ret_headers.extend_from_slice(status_val);
    ret_headers.extend_from_slice(b"\r\n");

    for hdr in headers.iter().filter(|&h| h.name() != ":status") {
        ret_headers.extend_from_slice(&sanitize_header(Cow::from(hdr.name().as_bytes())));
        ret_headers.extend_from_slice(b": ");
        ret_headers.extend_from_slice(&sanitize_header(Cow::from(hdr.value())));
        ret_headers.extend_from_slice(b"\r\n");
    }
    ret_headers.extend_from_slice(b"\r\n");
    NS_OK
}

#[expect(clippy::too_many_lines, reason = "Nothing to be done about it.")]
#[no_mangle]
pub extern "C" fn neqo_http3conn_event(
    conn: &mut NeqoHttp3Conn,
    ret_event: &mut Http3Event,
    data: &mut ThinVec<u8>,
) -> nsresult {
    while let Some(evt) = conn.conn.next_event() {
        let fe = match evt {
            Http3ClientEvent::DataWritable { stream_id } => Http3Event::DataWritable {
                stream_id: stream_id.as_u64(),
            },
            Http3ClientEvent::OutgoingDatagramSpaceAvailable => Http3Event::NoEvent,
            Http3ClientEvent::StopSending { stream_id, error } => Http3Event::StopSending {
                stream_id: stream_id.as_u64(),
                error,
            },
            Http3ClientEvent::HeaderReady {
                stream_id,
                headers,
                fin,
                interim,
            } => {
                let res = convert_h3_to_h1_headers(&headers, data);
                if res != NS_OK {
                    return res;
                }
                Http3Event::HeaderReady {
                    stream_id: stream_id.as_u64(),
                    fin,
                    interim,
                }
            }
            Http3ClientEvent::DataReadable { stream_id } => Http3Event::DataReadable {
                stream_id: stream_id.as_u64(),
            },
            Http3ClientEvent::Reset {
                stream_id,
                error,
                local,
            } => Http3Event::Reset {
                stream_id: stream_id.as_u64(),
                error,
                local,
            },
            Http3ClientEvent::RequestsCreatable => Http3Event::RequestsCreatable,
            Http3ClientEvent::AuthenticationNeeded => Http3Event::AuthenticationNeeded,
            Http3ClientEvent::ZeroRttRejected => Http3Event::ZeroRttRejected,
            Http3ClientEvent::ResumptionToken(token) => {
                // expiration_time time is Instant, transform it into microseconds it will
                // be valid for. Necko code will add the value to PR_Now() to get the expiration
                // time in PRTime.
                if token.expiration_time() > Instant::now() {
                    let e = (token.expiration_time() - Instant::now()).as_micros();
                    u64::try_from(e).map_or(Http3Event::NoEvent, |expire_in| {
                        data.extend_from_slice(token.as_ref());
                        Http3Event::ResumptionToken { expire_in }
                    })
                } else {
                    Http3Event::NoEvent
                }
            }
            Http3ClientEvent::GoawayReceived => Http3Event::GoawayReceived,
            Http3ClientEvent::StateChange(state) => match state {
                Http3State::Connected => Http3Event::ConnectionConnected,
                Http3State::Closing(reason) => {
                    if let neqo_transport::CloseReason::Transport(
                        TransportError::Crypto(nss_rs::Error::EchRetry(c))
                        | TransportError::EchRetry(c),
                    ) = &reason
                    {
                        data.extend_from_slice(c.as_ref());
                    }

                    #[cfg(not(target_os = "android"))]
                    conn.record_close_reason(&reason);

                    Http3Event::ConnectionClosing {
                        error: reason.into(),
                    }
                }
                Http3State::Closed(error_code) => {
                    if let neqo_transport::CloseReason::Transport(
                        TransportError::Crypto(nss_rs::Error::EchRetry(c))
                        | TransportError::EchRetry(c),
                    ) = &error_code
                    {
                        data.extend_from_slice(c.as_ref());
                    }

                    #[cfg(not(target_os = "android"))]
                    conn.record_close_reason(&error_code);

                    Http3Event::ConnectionClosed {
                        error: error_code.into(),
                    }
                }
                _ => Http3Event::NoEvent,
            },
            Http3ClientEvent::EchFallbackAuthenticationNeeded { public_name } => {
                data.extend_from_slice(public_name.as_ref());
                Http3Event::EchFallbackAuthenticationNeeded
            }
            Http3ClientEvent::WebTransport(e) => {
                Http3Event::WebTransport(WebTransportEventExternal::new(e, data))
            }
            Http3ClientEvent::ConnectUdp(e) => {
                Http3Event::ConnectUdp(ConnectUdpEventExternal::new(e, data))
            }
        };

        if !matches!(fe, Http3Event::NoEvent) {
            *ret_event = fe;
            return NS_OK;
        }
    }

    *ret_event = Http3Event::NoEvent;
    NS_OK
}

// Read response data into buf.
///
/// # Safety
///
/// Marked as unsafe given exposition via FFI i.e. `extern "C"`.
#[no_mangle]
pub unsafe extern "C" fn neqo_http3conn_read_response_data(
    conn: &mut NeqoHttp3Conn,
    stream_id: u64,
    buf: *mut u8,
    len: u32,
    read: &mut u32,
    fin: &mut bool,
) -> nsresult {
    let array = slice::from_raw_parts_mut(buf, len as usize);
    match conn
        .conn
        .read_data(Instant::now(), StreamId::from(stream_id), &mut array[..])
    {
        Ok((amount, fin_recvd)) => {
            let Ok(amount) = u32::try_from(amount) else {
                return NS_ERROR_NET_HTTP3_PROTOCOL_ERROR;
            };
            *read = amount;
            *fin = fin_recvd;
            if (amount == 0) && !fin_recvd {
                NS_BASE_STREAM_WOULD_BLOCK
            } else {
                NS_OK
            }
        }
        Err(Http3Error::InvalidStreamId | Http3Error::Transport(TransportError::NoMoreData)) => {
            NS_ERROR_INVALID_ARG
        }
        Err(_) => NS_ERROR_NET_HTTP3_PROTOCOL_ERROR,
    }
}

#[repr(C)]
pub struct NeqoSecretInfo {
    set: bool,
    version: u16,
    cipher: u16,
    group: u16,
    resumed: bool,
    early_data: bool,
    alpn: nsCString,
    signature_scheme: u16,
    ech_accepted: bool,
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_tls_info(
    conn: &mut NeqoHttp3Conn,
    sec_info: &mut NeqoSecretInfo,
) -> nsresult {
    match conn.conn.tls_info() {
        Some(info) => {
            sec_info.set = true;
            sec_info.version = info.version();
            sec_info.cipher = info.cipher_suite();
            sec_info.group = info.key_exchange();
            sec_info.resumed = info.resumed();
            sec_info.early_data = info.early_data_accepted();
            sec_info.alpn = info.alpn().map_or_else(nsCString::new, nsCString::from);
            sec_info.signature_scheme = info.signature_scheme();
            sec_info.ech_accepted = info.ech_accepted();
            NS_OK
        }
        None => NS_ERROR_NOT_AVAILABLE,
    }
}

#[repr(C)]
pub struct NeqoCertificateInfo {
    certs: ThinVec<ThinVec<u8>>,
    stapled_ocsp_responses_present: bool,
    stapled_ocsp_responses: ThinVec<ThinVec<u8>>,
    signed_cert_timestamp_present: bool,
    signed_cert_timestamp: ThinVec<u8>,
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_peer_certificate_info(
    conn: &mut NeqoHttp3Conn,
    neqo_certs_info: &mut NeqoCertificateInfo,
) -> nsresult {
    let Some(certs_info) = conn.conn.peer_certificate() else {
        return NS_ERROR_NOT_AVAILABLE;
    };

    neqo_certs_info.certs = certs_info.iter().map(ThinVec::from).collect();

    match &mut certs_info.stapled_ocsp_responses() {
        Some(ocsp_val) => {
            neqo_certs_info.stapled_ocsp_responses_present = true;
            neqo_certs_info.stapled_ocsp_responses = ocsp_val
                .iter()
                .map(|ocsp| ocsp.iter().copied().collect())
                .collect();
        }
        None => {
            neqo_certs_info.stapled_ocsp_responses_present = false;
        }
    };

    match certs_info.signed_cert_timestamp() {
        Some(sct_val) => {
            neqo_certs_info.signed_cert_timestamp_present = true;
            neqo_certs_info
                .signed_cert_timestamp
                .extend_from_slice(sct_val);
        }
        None => {
            neqo_certs_info.signed_cert_timestamp_present = false;
        }
    };

    NS_OK
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_authenticated(conn: &mut NeqoHttp3Conn, error: PRErrorCode) {
    conn.conn.authenticated(error.into(), Instant::now());
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_set_resumption_token(
    conn: &mut NeqoHttp3Conn,
    token: &mut ThinVec<u8>,
) -> nsresult {
    match conn.conn.enable_resumption(Instant::now(), token) {
        Ok(_) => NS_OK,
        Err(_) => NS_ERROR_NET_HTTP3_PROTOCOL_ERROR,
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_set_ech_config(
    conn: &mut NeqoHttp3Conn,
    ech_config: &mut ThinVec<u8>,
) {
    _ = conn.conn.enable_ech(ech_config);
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_is_zero_rtt(conn: &mut NeqoHttp3Conn) -> bool {
    conn.conn.state() == Http3State::ZeroRtt
}

#[repr(C)]
#[derive(Default)]
pub struct Http3Stats {
    /// Total packets received, including all the bad ones.
    pub packets_rx: usize,
    /// Duplicate packets received.
    pub dups_rx: usize,
    /// Dropped packets or dropped garbage.
    pub dropped_rx: usize,
    /// The number of packet that were saved for later processing.
    pub saved_datagrams: usize,
    /// Total packets sent.
    pub packets_tx: usize,
    /// Total number of packets that are declared lost.
    pub lost: usize,
    /// Late acknowledgments, for packets that were declared lost already.
    pub late_ack: usize,
    /// Acknowledgments for packets that contained data that was marked
    /// for retransmission when the PTO timer popped.
    pub pto_ack: usize,
    /// Count PTOs. Single PTOs, 2 PTOs in a row, 3 PTOs in row, etc. are counted
    /// separately.
    pub pto_counts: [usize; 16],
    /// The count of WouldBlock errors encountered during receive operations on the UDP socket.
    pub would_block_rx: usize,
    /// The count of WouldBlock errors encountered during transmit operations on the UDP socket.
    pub would_block_tx: usize,
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_get_stats(conn: &mut NeqoHttp3Conn, stats: &mut Http3Stats) {
    let t_stats = conn.conn.transport_stats();
    stats.packets_rx = t_stats.packets_rx;
    stats.dups_rx = t_stats.dups_rx;
    stats.dropped_rx = t_stats.dropped_rx;
    stats.saved_datagrams = t_stats.saved_datagrams;
    stats.packets_tx = t_stats.packets_tx;
    stats.lost = t_stats.lost;
    stats.late_ack = t_stats.late_ack;
    stats.pto_ack = t_stats.pto_ack;
    stats.pto_counts = t_stats.pto_counts;
    stats.would_block_rx = conn.would_block_rx_count();
    stats.would_block_tx = conn.would_block_tx_count();
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_create_session(
    conn: &mut NeqoHttp3Conn,
    host: &nsACString,
    path: &nsACString,
    headers: &nsACString,
    stream_id: &mut u64,
) -> nsresult {
    let hdrs = match parse_headers(headers) {
        Err(e) => {
            return e;
        }
        Ok(h) => h,
    };
    let Ok(host_tmp) = str::from_utf8(host) else {
        return NS_ERROR_INVALID_ARG;
    };
    let Ok(path_tmp) = str::from_utf8(path) else {
        return NS_ERROR_INVALID_ARG;
    };

    match conn.conn.webtransport_create_session(
        Instant::now(),
        ("https", host_tmp, path_tmp),
        &hdrs,
    ) {
        Ok(id) => {
            *stream_id = id.as_u64();
            NS_OK
        }
        Err(Http3Error::StreamLimit) => NS_BASE_STREAM_WOULD_BLOCK,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_connect_udp_create_session(
    conn: &mut NeqoHttp3Conn,
    host: &nsACString,
    path: &nsACString,
    headers: &nsACString,
    stream_id: &mut u64,
) -> nsresult {
    let hdrs = match parse_headers(headers) {
        Err(e) => {
            return e;
        }
        Ok(h) => h,
    };
    let Ok(host_tmp) = str::from_utf8(host) else {
        return NS_ERROR_INVALID_ARG;
    };
    let Ok(path_tmp) = str::from_utf8(path) else {
        return NS_ERROR_INVALID_ARG;
    };

    match conn
        .conn
        .connect_udp_create_session(Instant::now(), ("https", host_tmp, path_tmp), &hdrs)
    {
        Ok(id) => {
            *stream_id = id.as_u64();
            NS_OK
        }
        Err(Http3Error::StreamLimit) => NS_BASE_STREAM_WOULD_BLOCK,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

/// # Safety
///
/// `stats` must be either null or point to a valid, writable
/// `WebTransportSessionStats`.
#[no_mangle]
pub unsafe extern "C" fn neqo_http3conn_webtransport_close_session(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    error: u32,
    message: &nsACString,
    stats: *mut WebTransportSessionStats,
) -> nsresult {
    let Ok(message_tmp) = str::from_utf8(message) else {
        return NS_ERROR_INVALID_ARG;
    };
    match conn.conn.webtransport_close_session(
        StreamId::from(session_id),
        error,
        message_tmp,
        Instant::now(),
    ) {
        Ok(session_stats) => {
            if !stats.is_null() {
                unsafe {
                    let transport_stats = conn.conn.transport_stats();
                    populate_transport_stats(&mut *stats, &transport_stats);

                    (*stats).datagrams_expired_outgoing = session_stats.outgoing_datagrams.expired;
                }
            }
            NS_OK
        }
        Err(_) => NS_ERROR_INVALID_ARG,
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_connect_udp_close_session(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    error: u32,
    message: &nsACString,
) -> nsresult {
    let Ok(message_tmp) = str::from_utf8(message) else {
        return NS_ERROR_INVALID_ARG;
    };
    match conn.conn.connect_udp_close_session(
        StreamId::from(session_id),
        error,
        message_tmp,
        Instant::now(),
    ) {
        Ok(_) => NS_OK,
        Err(_) => NS_ERROR_INVALID_ARG,
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_create_stream(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    stream_type: WebTransportStreamType,
    stream_id: &mut u64,
) -> nsresult {
    match conn
        .conn
        .webtransport_create_stream(StreamId::from(session_id), stream_type.into())
    {
        Ok(id) => {
            *stream_id = id.as_u64();
            NS_OK
        }
        Err(Http3Error::StreamLimit) => NS_BASE_STREAM_WOULD_BLOCK,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_send_datagram(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    data: &mut ThinVec<u8>,
    tracking_id: u64,
    below_high_water_mark: &mut bool,
    send_group_id: u64,
    send_order: i64,
    age_us: u64,
) -> nsresult {
    let id = if tracking_id == 0 {
        None
    } else {
        Some(tracking_id)
    };
    // Gecko's TimeStamp and Instant need not share a clock, so the write time
    // comes across as an age.
    let now = Instant::now();
    let written = now
        .checked_sub(Duration::from_micros(age_us))
        .unwrap_or(now);
    match conn.conn.webtransport_send_datagram(
        StreamId::from(session_id),
        data,
        id,
        written,
        SendGroupId::new(send_group_id),
        send_order,
    ) {
        Ok(outcome) => {
            // A queue that had to evict to make room is full, so apply
            // backpressure regardless of where the high water mark sits.
            *below_high_water_mark = matches!(outcome, DatagramQueueOutcome::Ok);
            NS_OK
        }
        Err(Http3Error::Transport(TransportError::TooMuchData)) => NS_ERROR_NOT_AVAILABLE,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_connect_udp_send_datagram(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    data: &mut ThinVec<u8>,
    tracking_id: u64,
    send_group_id: u64,
    send_order: i64,
) -> nsresult {
    let id = if tracking_id == 0 {
        None
    } else {
        Some(tracking_id)
    };
    // The local neqo2 vendor doesn't yet support per-datagram send group/order
    // prioritization; accept the params from the C++ side but don't forward
    // them until neqo grows the corresponding API.
    let _ = (send_group_id, send_order);
    match conn.conn.connect_udp_send_datagram(
        StreamId::from(session_id),
        data,
        id,
        Instant::now(),
    ) {
        Ok(_) => NS_OK,
        Err(Http3Error::Transport(TransportError::TooMuchData)) => NS_ERROR_NOT_AVAILABLE,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_max_datagram_size(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    result: &mut u64,
) -> nsresult {
    conn.conn
        .webtransport_max_datagram_size(StreamId::from(session_id))
        .map_or(NS_ERROR_UNEXPECTED, |size| {
            *result = size;
            NS_OK
        })
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_set_datagram_max_buffered_datagrams(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    max_buffered_datagrams: u32,
) -> nsresult {
    // Needs neqo's webtransport_set_max_buffered_datagrams.
    let _ = (conn, session_id, max_buffered_datagrams);
    NS_OK
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_set_datagram_max_age(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    max_age: f64,
) -> nsresult {
    // Needs neqo's webtransport_set_datagram_max_age.
    let _ = (conn, session_id, max_age);
    NS_OK
}

/// # Safety
///
/// Use of raw (i.e. unsafe) pointers as arguments.
#[no_mangle]
pub unsafe extern "C" fn neqo_http3conn_webtransport_set_sendorder(
    conn: &mut NeqoHttp3Conn,
    stream_id: u64,
    sendorder: *const i64,
) -> nsresult {
    match conn
        .conn
        .webtransport_set_sendorder(StreamId::from(stream_id), sendorder.as_ref().copied())
    {
        Ok(()) => NS_OK,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

/// # Safety
///
/// Use of raw (i.e. unsafe) pointers as arguments.
#[no_mangle]
pub unsafe extern "C" fn neqo_http3conn_webtransport_set_sendgroup(
    conn: &mut NeqoHttp3Conn,
    stream_id: u64,
    sendgroup_id: u64,
) -> nsresult {
    // sendgroup_id 0 means "no group" (null sendGroup); clear the group assignment.
    if sendgroup_id == 0 {
        return match conn
            .conn
            .webtransport_clear_sendgroup(StreamId::from(stream_id))
        {
            Ok(()) => NS_OK,
            Err(_) => NS_ERROR_UNEXPECTED,
        };
    }
    let Some(&sg_id) = conn.webtransport_send_groups.get(&sendgroup_id) else {
        return NS_ERROR_UNEXPECTED;
    };
    match conn
        .conn
        .webtransport_set_sendgroup(StreamId::from(stream_id), sg_id)
    {
        Ok(()) => NS_OK,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_register_send_group(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    group_id: u64,
) -> nsresult {
    // neqo allocates the send-group id; map the child's provisional id to it.
    match conn
        .conn
        .webtransport_create_send_group(StreamId::from(session_id))
    {
        Ok(neqo_id) => {
            conn.webtransport_send_groups.insert(group_id, neqo_id);
            NS_OK
        }
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}


/// Get the negotiated protocol for a WebTransport session.
#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_session_protocol(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    protocol: &mut nsACString,
) -> nsresult {
    match conn
        .conn
        .webtransport_session_protocol(StreamId::from(session_id))
    {
        Ok(Some(p)) => {
            protocol.assign(&p);
            NS_OK
        }
        Ok(None) => {
            // SAFETY: shrinking the string to length 0 never exposes
            // uninitialized memory.
            unsafe {
                protocol.set_length(0);
            }
            NS_OK
        }
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

/// Set the anticipated concurrent incoming unidirectional streams for a WebTransport session.
#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_set_anticipated_concurrent_incoming_uni_streams(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    value: u16,
) -> nsresult {
    match conn
        .conn
        .webtransport_set_anticipated_incoming_uni_streams(StreamId::from(session_id), value)
    {
        Ok(()) => NS_OK,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

/// Set the anticipated concurrent incoming bidirectional streams for a WebTransport session.
#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_set_anticipated_concurrent_incoming_bidi_streams(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    value: u16,
) -> nsresult {
    match conn
        .conn
        .webtransport_set_anticipated_incoming_bidi_streams(StreamId::from(session_id), value)
    {
        Ok(()) => NS_OK,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

/// Export keying material per RFC 5705/8446.
///
/// # Safety
///
/// Use of raw (i.e. unsafe) pointers as arguments.
/// The `out` buffer must be at least `out_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn neqo_http3conn_export_keying_material(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    label: *const u8,
    label_len: u32,
    context: *const u8,
    context_len: u32,
    out: *mut u8,
    out_len: u32,
) -> nsresult {
    let label_slice = if label.is_null() {
        return NS_ERROR_INVALID_ARG;
    } else {
        slice::from_raw_parts(label, label_len as usize)
    };

    let context_slice = if context.is_null() || context_len == 0 {
        &[]
    } else {
        slice::from_raw_parts(context, context_len as usize)
    };

    if out.is_null() || out_len == 0 {
        return NS_ERROR_INVALID_ARG;
    }

    let out_slice = slice::from_raw_parts_mut(out, out_len as usize);
    match conn.conn.webtransport_export_keying_material(
        StreamId::from(session_id),
        label_slice,
        context_slice,
        out_slice,
    ) {
        Ok(()) => NS_OK,
        Err(_) => NS_ERROR_NOT_CONNECTED,
    }
}

#[repr(C)]
pub struct WebTransportSessionStats {
    // Total bytes sent/received at QUIC transport level (includes framing and
    // retransmissions; neqo does not expose payload-only byte counts separately).
    pub bytes_sent_total: u64,
    pub bytes_received_total: u64,
    // Transport-level details
    pub bytes_acked: u64,
    pub packets_sent: u64,
    pub bytes_lost: u64,
    pub packets_lost: u64,
    pub packets_received: u64,
    // RTT
    pub smoothed_rtt: f64,
    pub rtt_variation: f64,
    pub min_rtt: f64,
    // Congestion-derived
    pub estimated_send_rate: i64,  // bits/sec, -1 means null/unknown
    pub at_send_capacity: bool,
    // Datagram stats
    pub datagrams_expired_outgoing: u64,
    // Firefox does not support WebTransport connection pooling, so this
    // connection-level counter is a valid per-session stand-in.
    pub datagrams_lost_outgoing: u64,
}

// Shared by neqo_http3conn_webtransport_close_session and
// neqo_http3conn_webtransport_session_stats, which both need to translate
// the same connection-level TransportStats into a WebTransportSessionStats.
fn populate_transport_stats(dst: &mut WebTransportSessionStats, transport_stats: &TransportStats) {
    // Transport-level totals (for spec)
    dst.bytes_sent_total = (transport_stats.bytes_acked
        + transport_stats.cc.bytes_in_flight
        + transport_stats.bytes_lost) as u64;
    dst.bytes_received_total = transport_stats.bytes_rx as u64;

    // Transport-level details
    dst.bytes_acked = transport_stats.bytes_acked as u64;
    dst.packets_sent = transport_stats.packets_tx as u64;
    dst.bytes_lost = transport_stats.bytes_lost as u64;
    dst.packets_lost = transport_stats.lost as u64;
    dst.packets_received = transport_stats.packets_rx as u64;

    // RTT stats (convert from Duration to milliseconds)
    dst.smoothed_rtt = transport_stats.rtt.as_secs_f64() * 1000.0;
    dst.rtt_variation = transport_stats.rttvar.as_secs_f64() * 1000.0;
    dst.min_rtt = transport_stats.min_rtt.as_secs_f64() * 1000.0;

    // Congestion-derived stats
    if transport_stats.rtt.as_secs_f64() > 0.0 {
        // estimated_send_rate in bits/sec = (cwnd * 8) / rtt_seconds
        let rtt_seconds = transport_stats.rtt.as_secs_f64();
        dst.estimated_send_rate = ((transport_stats.cc.cwnd as f64 * 8.0) / rtt_seconds) as i64;
    } else {
        dst.estimated_send_rate = -1; // null/unknown
    }
    dst.at_send_capacity = transport_stats.cc.bytes_in_flight >= transport_stats.cc.cwnd;

    dst.datagrams_lost_outgoing = transport_stats.datagram_tx.lost as u64;
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_session_stats(
    conn: &NeqoHttp3Conn,
    session_id: u64,
    stats: &mut WebTransportSessionStats,
) -> nsresult {
    match conn.conn.webtransport_session_stats(StreamId::from(session_id)) {
        Ok(session_stats) => {
            let transport_stats = conn.conn.transport_stats();
            populate_transport_stats(stats, &transport_stats);

            stats.datagrams_expired_outgoing = session_stats.outgoing_datagrams.expired;

            NS_OK
        }
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

/// Fill in only the connection-level part of the stats.
///
/// Used when the session is already gone (neqo tears it down before the
/// close event is drained), so the session-scoped datagram counters are
/// unavailable.
#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_transport_stats(
    conn: &NeqoHttp3Conn,
    stats: &mut WebTransportSessionStats,
) {
    populate_transport_stats(stats, &conn.conn.transport_stats());
}

/// Convert a [`std::io::Error`] into a [`nsresult`].
///
/// Note that this conversion is specific to `neqo_glue`, i.e. does not aim to
/// implement a general-purpose conversion.
/// Treat NS_ERROR_NET_RESET as a generic retryable error for the upper layer.
///
/// Modeled after
/// [`ErrorAccordingToNSPR`](https://searchfox.org/mozilla-central/rev/a965e3c683ecc035dee1de72bd33a8d91b1203ed/netwerk/base/nsSocketTransport2.cpp#164-168).
//
// TODO: Use `non_exhaustive_omitted_patterns_lint` [once stablized](https://github.com/rust-lang/rust/issues/89554).
fn into_nsresult(e: &io::Error) -> nsresult {
    #[expect(clippy::match_same_arms, reason = "It's cleaner this way.")]
    match e.kind() {
        io::ErrorKind::ConnectionRefused => NS_ERROR_CONNECTION_REFUSED,
        io::ErrorKind::ConnectionReset => NS_ERROR_NET_RESET,

        // > We lump the following NSPR codes in with PR_CONNECT_REFUSED_ERROR. We
        // > could get better diagnostics by adding distinct XPCOM error codes for
        // > each of these, but there are a lot of places in Gecko that check
        // > specifically for NS_ERROR_CONNECTION_REFUSED, all of which would need to
        // > be checked.
        //
        // <https://searchfox.org/mozilla-central/rev/a965e3c683ecc035dee1de72bd33a8d91b1203ed/netwerk/base/nsSocketTransport2.cpp#164-168>
        //
        // TODO: `HostUnreachable` and `NetworkUnreachable` available since Rust
        // v1.83.0 only <https://doc.rust-lang.org/std/io/enum.ErrorKind.html>.
        // io::ErrorKind::HostUnreachable | io::ErrorKind::NetworkUnreachable |
        io::ErrorKind::AddrNotAvailable => NS_ERROR_CONNECTION_REFUSED,

        // <https://searchfox.org/mozilla-central/rev/a965e3c683ecc035dee1de72bd33a8d91b1203ed/netwerk/base/nsSocketTransport2.cpp#156>
        io::ErrorKind::ConnectionAborted => NS_ERROR_NET_RESET,

        io::ErrorKind::NotConnected => NS_ERROR_NOT_CONNECTED,
        io::ErrorKind::AddrInUse => NS_ERROR_SOCKET_ADDRESS_IN_USE,
        io::ErrorKind::AlreadyExists => NS_ERROR_FILE_ALREADY_EXISTS,
        io::ErrorKind::WouldBlock => NS_BASE_STREAM_WOULD_BLOCK,

        // TODO: available since Rust v1.83.0 only
        // <https://doc.rust-lang.org/std/io/enum.ErrorKind.html#variant.NotADirectory>
        // io::ErrorKind::NotADirectory => NS_ERROR_FILE_NOT_DIRECTORY,

        // TODO: available since Rust v1.83.0 only
        // <https://doc.rust-lang.org/std/io/enum.ErrorKind.html#variant.IsADirectory>
        // io::ErrorKind::IsADirectory => NS_ERROR_FILE_IS_DIRECTORY,

        // TODO: available since Rust v1.83.0 only
        // <https://doc.rust-lang.org/std/io/enum.ErrorKind.html#variant.DirectoryNotEmpty>
        // io::ErrorKind::DirectoryNotEmpty => NS_ERROR_FILE_DIR_NOT_EMPTY,

        // TODO: available since Rust v1.83.0 only
        // <https://doc.rust-lang.org/std/io/enum.ErrorKind.html#variant.ReadOnlyFilesystem>
        // io::ErrorKind::ReadOnlyFilesystem => NS_ERROR_FILE_READ_ONLY,

        // TODO: nightly-only for now <https://doc.rust-lang.org/std/io/enum.ErrorKind.html#variant.FilesystemLoop>.
        // io::ErrorKind::FilesystemLoop => NS_ERROR_FILE_UNRESOLVABLE_SYMLINK,
        io::ErrorKind::TimedOut => NS_ERROR_NET_TIMEOUT,
        io::ErrorKind::Interrupted => NS_ERROR_NET_INTERRUPT,

        // <https://searchfox.org/mozilla-central/rev/a965e3c683ecc035dee1de72bd33a8d91b1203ed/netwerk/base/nsSocketTransport2.cpp#160-161>
        io::ErrorKind::UnexpectedEof => NS_ERROR_NET_INTERRUPT,

        io::ErrorKind::OutOfMemory => NS_ERROR_OUT_OF_MEMORY,

        // TODO: nightly-only for now <https://doc.rust-lang.org/std/io/enum.ErrorKind.html#variant.InProgress>.
        // io::ErrorKind::InProgress => NS_ERROR_IN_PROGRESS,

        // The errors below are either not relevant for `neqo_glue`, or not
        // defined as `nsresult`.
        io::ErrorKind::NotFound
        | io::ErrorKind::PermissionDenied
        | io::ErrorKind::BrokenPipe
        | io::ErrorKind::InvalidData
        | io::ErrorKind::WriteZero
        | io::ErrorKind::Unsupported
        | io::ErrorKind::Other => NS_ERROR_NET_RESET,

        // TODO: available since Rust v1.83.0 only
        // <https://doc.rust-lang.org/std/io/enum.ErrorKind.html>.
        // io::ErrorKind::NotSeekable
        // | io::ErrorKind::FilesystemQuotaExceeded
        // | io::ErrorKind::FileTooLarge
        // | io::ErrorKind::ResourceBusy
        // | io::ErrorKind::ExecutableFileBusy
        // | io::ErrorKind::Deadlock
        // | io::ErrorKind::TooManyLinks
        // | io::ErrorKind::ArgumentListTooLong
        // | io::ErrorKind::NetworkDown
        // | io::ErrorKind::StaleNetworkFileHandle
        // | io::ErrorKind::StorageFull => NS_ERROR_NET_RESET,

        // TODO: nightly-only for now <https://doc.rust-lang.org/std/io/enum.ErrorKind.html>.
        // io::ErrorKind::CrossesDevices
        // | io::ErrorKind::InvalidFilename
        // | io::ErrorKind::InvalidInput => NS_ERROR_NET_RESET,
        _ => NS_ERROR_NET_RESET,
    }
}

#[repr(C)]
pub struct NeqoEncoder {
    encoder: Encoder,
    refcnt: AtomicRefcnt,
}

impl NeqoEncoder {
    fn new() -> Result<RefPtr<NeqoEncoder>, nsresult> {
        let encoder = Encoder::default();
        let encoder = Box::into_raw(Box::new(NeqoEncoder {
            encoder,
            refcnt: unsafe { AtomicRefcnt::new() },
        }));
        unsafe { Ok(RefPtr::from_raw(encoder).unwrap()) }
    }
}

#[no_mangle]
pub unsafe extern "C" fn neqo_encoder_addref(encoder: &NeqoEncoder) {
    encoder.refcnt.inc();
}

#[no_mangle]
pub unsafe extern "C" fn neqo_encoder_release(encoder: &NeqoEncoder) {
    let rc = encoder.refcnt.dec();
    if rc == 0 {
        drop(Box::from_raw(encoder as *const _ as *mut NeqoEncoder));
    }
}

// xpcom::RefPtr support
unsafe impl RefCounted for NeqoEncoder {
    unsafe fn addref(&self) {
        neqo_encoder_addref(self);
    }
    unsafe fn release(&self) {
        neqo_encoder_release(self);
    }
}

#[no_mangle]
pub extern "C" fn neqo_encoder_new(result: &mut *const NeqoEncoder) {
    *result = ptr::null_mut();
    if let Ok(encoder) = NeqoEncoder::new() {
        encoder.forget(result);
    }
}

#[no_mangle]
pub extern "C" fn neqo_encode_byte(encoder: &mut NeqoEncoder, data: u8) {
    encoder.encoder.encode_byte(data);
}

#[no_mangle]
pub extern "C" fn neqo_encode_varint(encoder: &mut NeqoEncoder, data: u64) {
    encoder.encoder.encode_varint(data);
}

#[no_mangle]
pub extern "C" fn neqo_encode_uint(encoder: &mut NeqoEncoder, n: u32, data: u64) {
    encoder.encoder.encode_uint(n as usize, data);
}

#[no_mangle]
pub unsafe extern "C" fn neqo_encode_buffer(encoder: &mut NeqoEncoder, buf: *const u8, len: u32) {
    let array = slice::from_raw_parts(buf, len as usize);
    encoder.encoder.encode(array);
}

#[no_mangle]
pub unsafe extern "C" fn neqo_encode_vvec(encoder: &mut NeqoEncoder, buf: *const u8, len: u32) {
    let array = slice::from_raw_parts(buf, len as usize);
    encoder.encoder.encode_vvec(array);
}

#[no_mangle]
pub unsafe extern "C" fn neqo_encode_get_data(
    encoder: &mut NeqoEncoder,
    buf: *mut *const u8,
    read: &mut u32,
) {
    let data = encoder.encoder.as_ref();
    *read = data.len() as u32;
    unsafe {
        *buf = data.as_ptr();
    }
}

#[no_mangle]
pub extern "C" fn neqo_encode_varint_len(v: u64) -> usize {
    return Encoder::varint_len(v);
}

#[repr(C)]
pub struct NeqoDecoder {
    decoder: *mut Decoder<'static>,
    refcnt: AtomicRefcnt,
}

impl NeqoDecoder {
    fn new(buf: *const u8, len: u32) -> Result<RefPtr<NeqoDecoder>, nsresult> {
        let slice = unsafe { slice::from_raw_parts(buf, len as usize) };
        let decoder = Box::new(Decoder::new(slice));
        let wrapper = Box::into_raw(Box::new(NeqoDecoder {
            decoder: Box::into_raw(decoder),
            refcnt: unsafe { AtomicRefcnt::new() },
        }));

        unsafe { Ok(RefPtr::from_raw(wrapper).unwrap()) }
    }
}

#[no_mangle]
pub unsafe extern "C" fn neqo_decoder_addref(decoder: &NeqoDecoder) {
    decoder.refcnt.inc();
}

#[no_mangle]
pub unsafe extern "C" fn neqo_decoder_release(decoder: &NeqoDecoder) {
    let rc = decoder.refcnt.dec();
    if rc == 0 {
        unsafe {
            drop(Box::from_raw(decoder.decoder));
            drop(Box::from_raw(decoder as *const _ as *mut NeqoDecoder));
        }
    }
}

// xpcom::RefPtr support
unsafe impl RefCounted for NeqoDecoder {
    unsafe fn addref(&self) {
        neqo_decoder_addref(self);
    }
    unsafe fn release(&self) {
        neqo_decoder_release(self);
    }
}

#[no_mangle]
pub extern "C" fn neqo_decoder_new(buf: *const u8, len: u32, result: &mut *const NeqoDecoder) {
    *result = ptr::null_mut();
    if let Ok(decoder) = NeqoDecoder::new(buf, len) {
        decoder.forget(result);
    }
}

#[no_mangle]
pub unsafe extern "C" fn neqo_decode_uint32(decoder: &mut NeqoDecoder, result: &mut u32) -> bool {
    let decoder = decoder.decoder.as_mut().unwrap();
    if let Some(v) = decoder.decode_uint::<u32>() {
        *result = v;
        return true;
    }
    false
}

#[no_mangle]
pub unsafe extern "C" fn neqo_decode_varint(decoder: &mut NeqoDecoder, result: &mut u64) -> bool {
    let decoder = decoder.decoder.as_mut().unwrap();
    if let Some(v) = decoder.decode_varint() {
        *result = v;
        return true;
    }
    false
}

#[no_mangle]
pub unsafe extern "C" fn neqo_decode(
    decoder: &mut NeqoDecoder,
    n: u32,
    buf: *mut *const u8,
    read: &mut u32,
) -> bool {
    let decoder = decoder.decoder.as_mut().unwrap();
    if let Some(data) = decoder.decode(n as usize) {
        *buf = data.as_ptr();
        *read = data.len() as u32;
        return true;
    }
    false
}

#[no_mangle]
pub unsafe extern "C" fn neqo_decode_remainder(
    decoder: &mut NeqoDecoder,
    buf: *mut *const u8,
    read: &mut u32,
) {
    let decoder = decoder.decoder.as_mut().unwrap();
    let data = decoder.decode_remainder();
    *buf = data.as_ptr();
    *read = data.len() as u32;
}

#[no_mangle]
pub unsafe extern "C" fn neqo_decoder_remaining(decoder: &mut NeqoDecoder) -> u64 {
    let decoder = decoder.decoder.as_mut().unwrap();
    decoder.remaining() as u64
}

#[no_mangle]
pub unsafe extern "C" fn neqo_decoder_offset(decoder: &mut NeqoDecoder) -> u64 {
    let decoder = decoder.decoder.as_mut().unwrap();
    decoder.offset() as u64
}

/// Enables the Apple fast datapath (`sendmsg_x`/`recvmsg_x`) for all
/// subsequently created QUIC sockets. Must only be called after the caller
/// has verified that these private APIs are available and functional.
#[cfg(target_vendor = "apple")]
#[no_mangle]
pub extern "C" fn neqo_glue_enable_apple_fast_path() {
    APPLE_FAST_PATH.store(true, Ordering::Relaxed);
}

/// Inner implementation for [`neqo_glue_probe_apple_fast_path`].
#[cfg(target_vendor = "apple")]
fn probe_apple_fast_path_inner(send_fd: c_int, recv_fd: c_int) -> io::Result<()> {
    use std::os::fd::BorrowedFd;

    use neqo_common::Ecn;
    use rustix::{
        fs::{fcntl_getfl, fcntl_setfl, OFlags},
        net::{getsockname, sockopt::{set_socket_timeout, Timeout}},
    };

    // Wrap a raw fd in neqo_udp::Socket, enable the fast path, restore blocking
    // mode (UdpSocketState::new sets non-blocking), and return the socket's
    // local address.
    let make_socket = |fd: c_int| -> io::Result<(neqo_udp::Socket<BorrowedFd<'static>>, SocketAddr)> {
        let bfd = unsafe { BorrowedFd::borrow_raw(fd) };
        let socket = neqo_udp::Socket::new(bfd)?;
        // SAFETY: The C++ caller has verified via dlsym that the APIs are present.
        unsafe { socket.enable_apple_fast_path() };
        fcntl_setfl(bfd, fcntl_getfl(bfd)? & !OFlags::NONBLOCK)?;
        set_socket_timeout(bfd, Timeout::Recv, Some(Duration::from_secs(1)))?;
        let addr: SocketAddr = getsockname(bfd)?
            .try_into()
            .map_err(|e: rustix::io::Errno| io::Error::from_raw_os_error(e.raw_os_error()))?;
        Ok((socket, addr))
    };
    let (sender, send_addr) = make_socket(send_fd)?;
    let (receiver, recv_addr) = make_socket(recv_fd)?;

    if sender.max_gso_segments() <= 1 {
        return Err(io::Error::other("max_gso_segments not increased"));
    }

    // Send two datagrams with distinct single-byte payloads and ECN codepoints,
    // then receive them across one or more recvmsg_x calls, in any order.
    let mut remaining: Vec<(u8, Ecn)> = vec![(0, Ecn::Ect0), (1, Ecn::Ect1)];
    for &(byte, ecn) in &remaining {
        sender.send(&Datagram::new(send_addr, recv_addr, Tos::from(ecn), vec![byte]).into())?;
    }
    let mut recv_buf = neqo_udp::RecvBuf::default();
    while !remaining.is_empty() {
        for d in receiver.recv(recv_addr, &mut recv_buf)? {
            let &byte = d
                .as_ref()
                .first()
                .ok_or_else(|| io::Error::other("empty datagram"))?;
            let idx = remaining
                .iter()
                .position(|&(b, _)| b == byte)
                .ok_or_else(|| io::Error::other("unexpected datagram payload"))?;
            let (_, expected_ecn) = remaining.swap_remove(idx);
            if Ecn::from(d.tos()) != expected_ecn {
                return Err(io::Error::other("ECN mismatch"));
            }
            if d.source() != send_addr {
                return Err(io::Error::other("source address mismatch"));
            }
        }
    }

    Ok(())
}

/// Tests the Apple fast UDP datapath end-to-end using the same neqo-udp code
/// path used in production. Called on a background thread in the parent
/// process with two pre-created, loopback-bound UDP sockets. Returns `true`
/// only if a datagram with ECN bits set survives the send/receive round-trip
/// through the `sendmsg_x`/`recvmsg_x` APIs.
#[cfg(target_vendor = "apple")]
#[no_mangle]
pub extern "C" fn neqo_glue_probe_apple_fast_path(send_fd: c_int, recv_fd: c_int) -> bool {
    probe_apple_fast_path_inner(send_fd, recv_fd).is_ok()
}

// Test function called from C++ gtest
// Callback signature: fn(user_data, name_ptr, name_len, value_ptr, value_len)
type HeaderCallback = extern "C" fn(*mut c_void, *const u8, usize, *const u8, usize);

#[no_mangle]
pub extern "C" fn neqo_glue_test_parse_headers(
    headers_input: &nsACString,
    callback: HeaderCallback,
    user_data: *mut c_void,
) -> bool {
    match parse_headers(headers_input) {
        Ok(headers) => {
            for header in headers {
                let name_bytes = header.name().as_bytes();
                let value_bytes = header.value();
                callback(
                    user_data,
                    name_bytes.as_ptr(),
                    name_bytes.len(),
                    value_bytes.as_ptr(),
                    value_bytes.len(),
                );
            }
            true
        }
        Err(_) => false,
    }
}
