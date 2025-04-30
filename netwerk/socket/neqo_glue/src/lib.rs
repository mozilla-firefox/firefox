/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#![expect(clippy::missing_panics_doc, reason = "OK here")]

#[cfg(feature = "fuzzing")]
use std::time::Duration;
use std::{
    borrow::Cow,
    cell::RefCell,
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
use neqo_common::{
    event::Provider as _, qdebug, qerror, qlog::NeqoQlog, qwarn, Datagram, Decoder, Encoder,
    Header, IpTos, Role,
};
use neqo_crypto::{init, PRErrorCode};
use neqo_http3::{
    features::extended_connect::SessionCloseReason, Error as Http3Error, Http3Client,
    Http3ClientEvent, Http3Parameters, Http3State, Priority, WebTransportEvent,
};
use neqo_transport::{
    stream_id::StreamType, CongestionControlAlgorithm, Connection, ConnectionParameters,
    Error as TransportError, Output, RandomConnectionIdGenerator, StreamId, Version,
};
use nserror::{
    nsresult, NS_BASE_STREAM_WOULD_BLOCK, NS_ERROR_CONNECTION_REFUSED, NS_ERROR_FAILURE,
    NS_ERROR_FILE_ALREADY_EXISTS, NS_ERROR_ILLEGAL_VALUE, NS_ERROR_INVALID_ARG,
    NS_ERROR_NET_HTTP3_PROTOCOL_ERROR, NS_ERROR_NET_INTERRUPT, NS_ERROR_NET_RESET,
    NS_ERROR_NET_TIMEOUT, NS_ERROR_NOT_AVAILABLE, NS_ERROR_NOT_CONNECTED, NS_ERROR_OUT_OF_MEMORY,
    NS_ERROR_SOCKET_ADDRESS_IN_USE, NS_ERROR_UNEXPECTED, NS_OK,
};
use nsstring::{nsACString, nsCString};
use thin_vec::ThinVec;
use uuid::Uuid;
#[cfg(windows)]
use winapi::{
    ctypes::c_int,
    shared::ws2def::{AF_INET, AF_INET6},
};
use xpcom::{interfaces::nsISocketProvider, AtomicRefcnt, RefCounted, RefPtr};

std::thread_local! {
    static RECV_BUF: RefCell<neqo_udp::RecvBuf> = RefCell::new(neqo_udp::RecvBuf::new());
}

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
    buffered_outbound_datagram: Option<Datagram>,

    datagram_segment_size_sent: LocalMemoryDistribution<'static>,
    datagram_segment_size_received: LocalMemoryDistribution<'static>,
    datagram_size_received: LocalMemoryDistribution<'static>,
    datagram_segments_received: LocalCustomDistribution<'static>,
    would_block_counter: WouldBlockCounter,
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
        webtransport_datagram_size: u32,
        provider_flags: u32,
        idle_timeout: u32,
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
                        qerror!("got invalid socked {}", socket);
                        return Err(NS_ERROR_INVALID_ARG);
                    }
                    let raw: RawFd = socket.try_into().map_err(|e| {
                        qerror!("got invalid socked {}: {}", socket, e);
                        NS_ERROR_INVALID_ARG
                    })?;
                    unsafe { BorrowedFd::borrow_raw(raw) }
                };
                #[cfg(windows)]
                let borrowed = {
                    use std::os::windows::io::{BorrowedSocket, RawSocket};
                    if socket as usize == winapi::um::winsock2::INVALID_SOCKET {
                        qerror!("got invalid socked {}", socket);
                        return Err(NS_ERROR_INVALID_ARG);
                    }
                    let raw: RawSocket = socket.try_into().map_err(|e| {
                        qerror!("got invalid socked {}: {}", socket, e);
                        NS_ERROR_INVALID_ARG
                    })?;
                    unsafe { BorrowedSocket::borrow_raw(raw) }
                };
                neqo_udp::Socket::new(borrowed).map_err(|e| {
                    qerror!("failed to initialize socket {}: {}", socket, e);
                    into_nsresult(&e)
                })
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
            0 => CongestionControlAlgorithm::NewReno,
            1 => CongestionControlAlgorithm::Cubic,
            _ => {
                // Unknown preferences; default to Cubic
                CongestionControlAlgorithm::Cubic
            }
        };

        let mut params = ConnectionParameters::default()
            .versions(quic_version, version_list)
            .cc_algorithm(cc_algorithm)
            .max_data(max_data)
            .max_stream_data(StreamType::BiDi, false, max_stream_data)
            .grease(static_prefs::pref!("security.tls.grease_http3_enable"))
            .sni_slicing(static_prefs::pref!("network.http.http3.sni-slicing"))
            .idle_timeout(Duration::from_secs(idle_timeout.into()))
            // Disabled on OpenBSD. See <https://bugzilla.mozilla.org/show_bug.cgi?id=1952304>.
            .pmtud_iface_mtu(cfg!(not(target_os = "openbsd")))
            .mlkem(false);

        // Set a short timeout when fuzzing.
        #[cfg(feature = "fuzzing")]
        if static_prefs::pref!("fuzzing.necko.http3") {
            params = params.idle_timeout(Duration::from_millis(10));
        }

        if webtransport_datagram_size > 0 {
            params = params.datagram_size(webtransport_datagram_size.into());
        }

        let http3_settings = Http3Parameters::default()
            .max_table_size_encoder(max_table_size)
            .max_table_size_decoder(max_table_size)
            .max_blocked_streams(max_blocked_streams)
            .max_concurrent_push_streams(0)
            .connection_parameters(params)
            .webtransport(webtransport)
            .http3_datagram(webtransport);

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

        let mut additional_shares = usize::from(static_prefs::pref!(
            "security.tls.client_hello.send_p256_keyshare"
        ));
        if static_prefs::pref!("security.tls.enable_kyber")
            && static_prefs::pref!("network.http.http3.enable_kyber")
            && (provider_flags & nsISocketProvider::IS_RETRY) == 0
            && (provider_flags & nsISocketProvider::BE_CONSERVATIVE) == 0
        {
            // These operations are infallible when conn.state == State::Init.
            conn.set_groups(&[
                neqo_crypto::TLS_GRP_KEM_MLKEM768X25519,
                neqo_crypto::TLS_GRP_EC_X25519,
                neqo_crypto::TLS_GRP_EC_SECP256R1,
                neqo_crypto::TLS_GRP_EC_SECP384R1,
                neqo_crypto::TLS_GRP_EC_SECP521R1,
            ])
            .map_err(|_| NS_ERROR_UNEXPECTED)?;
            additional_shares += 1;
        }
        // If additional_shares == 2, send mlkem768x25519, x25519, and p256.
        // If additional_shares == 1, send {mlkem768x25519, x25519} or {x25519, p256}.
        // If additional_shares == 0, send x25519.
        conn.send_additional_key_shares(additional_shares)
            .map_err(|_| NS_ERROR_UNEXPECTED)?;

        let mut conn = Http3Client::new_with_conn(conn, http3_settings);

        if !qlog_dir.is_empty() {
            let qlog_dir_conv = str::from_utf8(qlog_dir).map_err(|_| NS_ERROR_INVALID_ARG)?;
            let qlog_path = PathBuf::from(qlog_dir_conv);

            match NeqoQlog::enabled_with_file(
                qlog_path.clone(),
                Role::Client,
                Some("Firefox Client qlog".to_string()),
                Some("Firefox Client qlog".to_string()),
                format!("{}_{}.qlog", origin, Uuid::new_v4()),
            ) {
                Ok(qlog) => conn.set_qlog(qlog),
                Err(e) => {
                    // Emit warnings but to not return an error if qlog initialization
                    // fails.
                    qwarn!(
                        "failed to create NeqoQlog at {}: {}",
                        qlog_path.display(),
                        e
                    );
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
            datagram_size_received: networking::http_3_udp_datagram_size_received.start_buffer(),
            datagram_segments_received: networking::http_3_udp_datagram_segments_received
                .start_buffer(),
            buffered_outbound_datagram: None,
            would_block_counter: WouldBlockCounter::new(),
        }));
        unsafe { RefPtr::from_raw(conn).ok_or(NS_ERROR_NOT_CONNECTED) }
    }

    #[cfg(not(target_os = "android"))]
    fn record_stats_in_glean(&self) {
        use firefox_on_glean::metrics::networking as glean;
        use neqo_common::IpTosEcn;
        use neqo_transport::ecn;

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

        if static_prefs::pref!("network.http.http3.ecn") && stats.frame_rx.handshake_done != 0 {
            let tx_ect0_sum: u64 = stats.ecn_tx.into_values().map(|v| v[IpTosEcn::Ect0]).sum();
            let tx_ce_sum: u64 = stats.ecn_tx.into_values().map(|v| v[IpTosEcn::Ce]).sum();
            if tx_ect0_sum > 0 {
                if let Ok(ratio) = i64::try_from((tx_ce_sum * PRECISION_FACTOR) / tx_ect0_sum) {
                    glean::http_3_ecn_ce_ect0_ratio_sent.accumulate_single_sample_signed(ratio);
                } else {
                    let msg = "Failed to convert ratio to i64 for use with glean";
                    qwarn!("{msg}");
                    debug_assert!(false, "{msg}");
                }
            }
            let rx_ect0_sum: u64 = stats.ecn_rx.into_values().map(|v| v[IpTosEcn::Ect0]).sum();
            let rx_ce_sum: u64 = stats.ecn_rx.into_values().map(|v| v[IpTosEcn::Ce]).sum();
            if rx_ect0_sum > 0 {
                if let Ok(ratio) = i64::try_from((rx_ce_sum * PRECISION_FACTOR) / rx_ect0_sum) {
                    glean::http_3_ecn_ce_ect0_ratio_received.accumulate_single_sample_signed(ratio);
                } else {
                    let msg = "Failed to convert ratio to i64 for use with glean";
                    qwarn!("{msg}");
                    debug_assert!(false, "{msg}");
                }
            }
            for (outcome, value) in stats.ecn_path_validation.into_iter() {
                let Ok(value) = i32::try_from(value) else {
                    let msg = format!("Failed to convert {value} to i32 for use with glean");
                    qwarn!("{msg}");
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

        // Ignore connections into the void.
        if stats.packets_rx != 0 {
            if let Ok(loss) =
                i64::try_from((stats.lost * PRECISION_FACTOR_USIZE) / stats.packets_tx)
            {
                glean::http_3_loss_ratio.accumulate_single_sample_signed(loss);
            } else {
                let msg = "Failed to convert ratio to i64 for use with glean";
                qwarn!("{msg}");
                debug_assert!(false, "{msg}");
            }
        }
    }

    // Noop on Android for now, due to performance regressions.
    // - <https://bugzilla.mozilla.org/show_bug.cgi?id=1898810>
    // - <https://bugzilla.mozilla.org/show_bug.cgi?id=1906664>
    #[cfg(target_os = "android")]
    fn record_stats_in_glean(&self) {}

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
    webtransport_datagram_size: u32,
    provider_flags: u32,
    idle_timeout: u32,
    socket: i64,
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
        webtransport_datagram_size,
        provider_flags,
        idle_timeout,
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
    webtransport_datagram_size: u32,
    provider_flags: u32,
    idle_timeout: u32,
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
        webtransport_datagram_size,
        provider_flags,
        idle_timeout,
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
        IpTos::default(),
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
    let mut bytes_read = 0;

    RECV_BUF.with_borrow_mut(|recv_buf| {
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
                    qwarn!("failed to receive datagrams: {}", e);
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
            let ecn_enabled = static_prefs::pref!("network.http.http3.ecn");
            let dgrams = dgrams.map(|mut d| {
                if !ecn_enabled {
                    d.set_tos(IpTos::default());
                }
                d
            });

            conn.conn.process_multiple_input(dgrams, Instant::now());

            conn.datagram_size_received.accumulate(sum as u64);
            conn.datagram_segments_received.accumulate(segment_count);
            bytes_read += sum;
        }

        ProcessInputResult {
            result: NS_OK,
            bytes_read: bytes_read.try_into().unwrap_or(u32::MAX),
        }
    })
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
    let mut bytes_written: usize = 0;
    loop {
        let output = conn
            .buffered_outbound_datagram
            .take()
            .map(Output::Datagram)
            .unwrap_or_else(|| conn.conn.process_output(Instant::now()));
        match output {
            Output::Datagram(mut dg) => {
                if !static_prefs::pref!("network.http.http3.ecn") {
                    dg.set_tos(IpTos::default());
                }

                if static_prefs::pref!("network.http.http3.block_loopback_ipv6_addr")
                    && matches!(dg.destination(), SocketAddr::V6(addr) if addr.ip().is_loopback())
                {
                    qdebug!("network.http.http3.block_loopback_ipv6_addr is set, returning NS_ERROR_CONNECTION_REFUSED for localhost IPv6");
                    return ProcessOutputAndSendResult {
                        result: NS_ERROR_CONNECTION_REFUSED,
                        bytes_written: 0,
                    };
                }

                match conn.socket.as_mut().expect("non NSPR IO").send(&dg) {
                    Ok(()) => {}
                    Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                        conn.increment_would_block_tx();
                        if static_prefs::pref!("network.http.http3.pr_poll_write") {
                            qdebug!("Buffer outbound datagram to be sent once UDP socket has write-availability.");
                            conn.buffered_outbound_datagram = Some(dg);
                            return ProcessOutputAndSendResult {
                                // Propagate WouldBlock error, thus indicating that
                                // the UDP socket should be polled for
                                // write-availability.
                                result: NS_BASE_STREAM_WOULD_BLOCK,
                                bytes_written: bytes_written.try_into().unwrap_or(u32::MAX),
                            };
                        } else {
                            qwarn!("dropping datagram as socket would block");
                            break;
                        }
                    }
                    Err(e) => {
                        qwarn!("failed to send datagram: {}", e);
                        return ProcessOutputAndSendResult {
                            result: into_nsresult(&e),
                            bytes_written: 0,
                        };
                    }
                }
                bytes_written += dg.len();
                conn.datagram_segment_size_sent.accumulate(dg.len() as u64);
            }
            Output::Callback(to) => {
                let timeout = if to.is_zero() {
                    Duration::from_millis(1)
                } else {
                    to
                };
                let Ok(timeout) = u64::try_from(timeout.as_millis()) else {
                    return ProcessOutputAndSendResult {
                        result: NS_ERROR_UNEXPECTED,
                        bytes_written: 0,
                    };
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

    ProcessOutputAndSendResult {
        result: NS_OK,
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
    // They need to be split into (String, String) pairs.
    match str::from_utf8(headers) {
        Err(_) => {
            return Err(NS_ERROR_INVALID_ARG);
        }
        Ok(h) => {
            for elem in h.split("\r\n").skip(1) {
                if elem.starts_with(':') {
                    // colon headers are for http/2 and 3 and this is http/1
                    // input, so that is probably a smuggling attack of some
                    // kind.
                    continue;
                }
                if elem.is_empty() {
                    continue;
                }
                let hdr_str: Vec<_> = elem.splitn(2, ':').collect();
                let name = hdr_str[0].trim().to_lowercase();
                if is_excluded_header(&name) {
                    continue;
                }
                let value = if hdr_str.len() > 1 {
                    String::from(hdr_str[1].trim())
                } else {
                    String::new()
                };

                hdrs.push(Header::new(name, value));
            }
        }
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
        &(scheme_tmp, host_tmp, path_tmp),
        &hdrs,
        priority,
    ) {
        Ok(id) => {
            *stream_id = id.as_u64();
            NS_OK
        }
        Err(Http3Error::StreamLimitError) => NS_BASE_STREAM_WOULD_BLOCK,
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
        .send_data(StreamId::from(stream_id), array)
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

const fn crypto_error_code(err: &neqo_crypto::Error) -> u64 {
    match err {
        neqo_crypto::Error::AeadError => 1,
        neqo_crypto::Error::CertificateLoading => 2,
        neqo_crypto::Error::CreateSslSocket => 3,
        neqo_crypto::Error::HkdfError => 4,
        neqo_crypto::Error::InternalError => 5,
        neqo_crypto::Error::IntegerOverflow => 6,
        neqo_crypto::Error::InvalidEpoch => 7,
        neqo_crypto::Error::MixedHandshakeMethod => 8,
        neqo_crypto::Error::NoDataAvailable => 9,
        neqo_crypto::Error::NssError { .. } => 10,
        neqo_crypto::Error::OverrunError => 11,
        neqo_crypto::Error::SelfEncryptFailure => 12,
        neqo_crypto::Error::TimeTravelError => 13,
        neqo_crypto::Error::UnsupportedCipher => 14,
        neqo_crypto::Error::UnsupportedVersion => 15,
        neqo_crypto::Error::StringError => 16,
        neqo_crypto::Error::EchRetry(_) => 17,
        neqo_crypto::Error::CipherInitFailure => 18,
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
            TransportError::InternalError => Self::TransportInternalError,
            TransportError::CryptoError(neqo_crypto::Error::EchRetry(_)) => Self::EchRetry,
            TransportError::CryptoError(c) => Self::CryptoError(crypto_error_code(&c)),
            TransportError::CryptoAlert(c) => Self::CryptoAlert(c),
            TransportError::PeerApplicationError(c) => Self::PeerAppError(c),
            TransportError::PeerError(c) => Self::PeerError(c),
            TransportError::NoError
            | TransportError::IdleTimeout
            | TransportError::ConnectionRefused
            | TransportError::FlowControlError
            | TransportError::StreamLimitError
            | TransportError::StreamStateError
            | TransportError::FinalSizeError
            | TransportError::FrameEncodingError
            | TransportError::TransportParameterError
            | TransportError::ProtocolViolation
            | TransportError::InvalidToken
            | TransportError::KeysExhausted
            | TransportError::ApplicationError
            | TransportError::NoAvailablePath
            | TransportError::CryptoBufferExceeded => Self::TransportError(error.code()),
            TransportError::EchRetry(_) => Self::EchRetry,
            TransportError::AckedUnsentPacket => Self::TransportInternalErrorOther(0),
            TransportError::ConnectionIdLimitExceeded => Self::TransportInternalErrorOther(1),
            TransportError::ConnectionIdsExhausted => Self::TransportInternalErrorOther(2),
            TransportError::ConnectionState => Self::TransportInternalErrorOther(3),
            TransportError::DecryptError => Self::TransportInternalErrorOther(5),
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
            TransportError::QlogError => Self::TransportInternalErrorOther(27),
            TransportError::NotAvailable => Self::TransportInternalErrorOther(28),
            TransportError::DisabledVersion => Self::TransportInternalErrorOther(29),
            TransportError::UnknownTransportParameter => Self::TransportInternalErrorOther(30),
        }
    }
}

// Keep in sync with `netwerk/metrics.yaml` `http_3_connection_close_reason` metric labels.
#[cfg(not(target_os = "android"))]
const fn transport_error_to_glean_label(error: &TransportError) -> &'static str {
    match error {
        TransportError::NoError => "NoError",
        TransportError::InternalError => "InternalError",
        TransportError::ConnectionRefused => "ConnectionRefused",
        TransportError::FlowControlError => "FlowControlError",
        TransportError::StreamLimitError => "StreamLimitError",
        TransportError::StreamStateError => "StreamStateError",
        TransportError::FinalSizeError => "FinalSizeError",
        TransportError::FrameEncodingError => "FrameEncodingError",
        TransportError::TransportParameterError => "TransportParameterError",
        TransportError::ProtocolViolation => "ProtocolViolation",
        TransportError::InvalidToken => "InvalidToken",
        TransportError::ApplicationError => "ApplicationError",
        TransportError::CryptoBufferExceeded => "CryptoBufferExceeded",
        TransportError::CryptoError(_) => "CryptoError",
        TransportError::QlogError => "QlogError",
        TransportError::CryptoAlert(_) => "CryptoAlert",
        TransportError::EchRetry(_) => "EchRetry",
        TransportError::AckedUnsentPacket => "AckedUnsentPacket",
        TransportError::ConnectionIdLimitExceeded => "ConnectionIdLimitExceeded",
        TransportError::ConnectionIdsExhausted => "ConnectionIdsExhausted",
        TransportError::ConnectionState => "ConnectionState",
        TransportError::DecryptError => "DecryptError",
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
        TransportError::PeerApplicationError(_) => "PeerApplicationError",
        TransportError::PeerError(_) => "PeerError",
        TransportError::StatelessReset => "StatelessReset",
        TransportError::TooMuchData => "TooMuchData",
        TransportError::UnexpectedMessage => "UnexpectedMessage",
        TransportError::UnknownConnectionId => "UnknownConnectionId",
        TransportError::UnknownFrameType => "UnknownFrameType",
        TransportError::VersionNegotiation => "VersionNegotiation",
        TransportError::WrongRole => "WrongRole",
        TransportError::UnknownTransportParameter => "UnknownTransportParameter",
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
    match conn.conn.stream_close_send(StreamId::from(stream_id)) {
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
    fn new(reason: SessionCloseReason, data: &mut ThinVec<u8>) -> Self {
        match reason {
            SessionCloseReason::Error(e) => Self::Error(e),
            SessionCloseReason::Status(s) => Self::Status(s),
            SessionCloseReason::Clean { error, message } => {
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
}

impl WebTransportEventExternal {
    fn new(event: WebTransportEvent, data: &mut ThinVec<u8>) -> Self {
        match event {
            WebTransportEvent::Negotiated(n) => Self::Negotiated(n),
            WebTransportEvent::Session {
                stream_id, status, ..
            } => {
                data.extend_from_slice(b"HTTP/3 ");
                data.extend_from_slice(status.to_string().as_bytes());
                data.extend_from_slice(b"\r\n\r\n");
                Self::Session(stream_id.as_u64())
            }
            WebTransportEvent::SessionClosed {
                stream_id, reason, ..
            } => match reason {
                SessionCloseReason::Status(status) => {
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
    ret_headers.extend_from_slice(status_val.as_bytes());
    ret_headers.extend_from_slice(b"\r\n");

    for hdr in headers.iter().filter(|&h| h.name() != ":status") {
        ret_headers.extend_from_slice(&sanitize_header(Cow::from(hdr.name().as_bytes())));
        ret_headers.extend_from_slice(b": ");
        ret_headers.extend_from_slice(&sanitize_header(Cow::from(hdr.value().as_bytes())));
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
            Http3ClientEvent::PushPromise {
                push_id,
                request_stream_id,
                headers,
            } => {
                let res = convert_h3_to_h1_headers(&headers, data);
                if res != NS_OK {
                    return res;
                }
                Http3Event::PushPromise {
                    push_id: push_id.into(),
                    request_stream_id: request_stream_id.as_u64(),
                }
            }
            Http3ClientEvent::PushHeaderReady {
                push_id,
                headers,
                fin,
                interim,
            } => {
                if interim {
                    Http3Event::NoEvent
                } else {
                    let res = convert_h3_to_h1_headers(&headers, data);
                    if res != NS_OK {
                        return res;
                    }
                    Http3Event::PushHeaderReady {
                        push_id: push_id.into(),
                        fin,
                    }
                }
            }
            Http3ClientEvent::PushDataReadable { push_id } => Http3Event::PushDataReadable {
                push_id: push_id.into(),
            },
            Http3ClientEvent::PushCanceled { push_id } => Http3Event::PushCanceled {
                push_id: push_id.into(),
            },
            Http3ClientEvent::PushReset { push_id, error } => Http3Event::PushReset {
                push_id: push_id.into(),
                error,
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
                        TransportError::CryptoError(neqo_crypto::Error::EchRetry(c))
                        | TransportError::EchRetry(c),
                    ) = &reason
                    {
                        data.extend_from_slice(c.as_ref());
                    }

                    #[cfg(not(target_os = "android"))]
                    {
                        let glean_label = match &reason {
                            neqo_transport::CloseReason::Application(_) => "Application",
                            neqo_transport::CloseReason::Transport(r) => {
                                transport_error_to_glean_label(r)
                            }
                        };
                        networking::http_3_connection_close_reason
                            .get(glean_label)
                            .add(1);
                    }

                    Http3Event::ConnectionClosing {
                        error: reason.into(),
                    }
                }
                Http3State::Closed(error_code) => {
                    if let neqo_transport::CloseReason::Transport(
                        TransportError::CryptoError(neqo_crypto::Error::EchRetry(c))
                        | TransportError::EchRetry(c),
                    ) = &error_code
                    {
                        data.extend_from_slice(c.as_ref());
                    }
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
        Err(
            Http3Error::InvalidStreamId | Http3Error::TransportError(TransportError::NoMoreData),
        ) => NS_ERROR_INVALID_ARG,
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
) {
    _ = conn.conn.enable_resumption(Instant::now(), token);
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
        &("https", host_tmp, path_tmp),
        &hdrs,
    ) {
        Ok(id) => {
            *stream_id = id.as_u64();
            NS_OK
        }
        Err(Http3Error::StreamLimitError) => NS_BASE_STREAM_WOULD_BLOCK,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_close_session(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    error: u32,
    message: &nsACString,
) -> nsresult {
    let Ok(message_tmp) = str::from_utf8(message) else {
        return NS_ERROR_INVALID_ARG;
    };
    match conn
        .conn
        .webtransport_close_session(StreamId::from(session_id), error, message_tmp)
    {
        Ok(()) => NS_OK,
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
        Err(Http3Error::StreamLimitError) => NS_BASE_STREAM_WOULD_BLOCK,
        Err(_) => NS_ERROR_UNEXPECTED,
    }
}

#[no_mangle]
pub extern "C" fn neqo_http3conn_webtransport_send_datagram(
    conn: &mut NeqoHttp3Conn,
    session_id: u64,
    data: &mut ThinVec<u8>,
    tracking_id: u64,
) -> nsresult {
    let id = if tracking_id == 0 {
        None
    } else {
        Some(tracking_id)
    };
    match conn
        .conn
        .webtransport_send_datagram(StreamId::from(session_id), data, id)
    {
        Ok(()) => NS_OK,
        Err(Http3Error::TransportError(TransportError::TooMuchData)) => NS_ERROR_NOT_AVAILABLE,
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

/// Convert a [`std::io::Error`] into a [`nsresult`].
///
/// Note that this conversion is specific to `neqo_glue`, i.e. does not aim to
/// implement a general-purpose conversion.
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

        // > NSPR's socket code can return these, but they're not worth breaking out
        // > into their own error codes, distinct from NS_ERROR_FAILURE:
        // >
        // > PR_BAD_DESCRIPTOR_ERROR
        // > PR_INVALID_ARGUMENT_ERROR
        //
        // <https://searchfox.org/mozilla-central/rev/a965e3c683ecc035dee1de72bd33a8d91b1203ed/netwerk/base/nsSocketTransport2.cpp#231>
        io::ErrorKind::InvalidInput => NS_ERROR_FAILURE,

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
        | io::ErrorKind::Other => NS_ERROR_FAILURE,

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
        // | io::ErrorKind::StorageFull => NS_ERROR_FAILURE,

        // TODO: nightly-only for now <https://doc.rust-lang.org/std/io/enum.ErrorKind.html>.
        // io::ErrorKind::CrossesDevices | io::ErrorKind::InvalidFilename => NS_ERROR_FAILURE,
        _ => NS_ERROR_FAILURE,
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
pub extern "C" fn neqo_encode_get_data(
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
