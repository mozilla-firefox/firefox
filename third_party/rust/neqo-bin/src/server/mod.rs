// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![expect(
    clippy::unwrap_used,
    clippy::future_not_send,
    clippy::missing_errors_doc,
    clippy::missing_panics_doc,
    reason = "This is example code."
)]

use std::{
    cell::RefCell,
    fmt::Display,
    fs,
    future::poll_fn,
    io,
    net::{SocketAddr, ToSocketAddrs as _},
    num::NonZeroUsize,
    path::{Path, PathBuf},
    pin::Pin,
    process::exit,
    rc::Rc,
    task::{Context, Poll},
    time::{Duration, Instant},
};

use clap::Parser;
use futures::{
    FutureExt as _,
    future::{Either, select, select_all},
};
use neqo_common::{Datagram, hex::Hex};
use neqo_http3::Http3Server;
use neqo_transport::{
    Connection, OutputBatch, RandomConnectionIdGenerator, Version, server::ValidateAddress,
};
use neqo_udp::{DatagramIter, RecvBuf};
use nss::{
    AntiReplay, Cipher, PrivateKey, PublicKey,
    constants::{TLS_AES_128_GCM_SHA256, TLS_AES_256_GCM_SHA384, TLS_CHACHA20_POLY1305_SHA256},
    generate_ech_keys, init_db, random,
};
use thiserror::Error;
use tokio::time::Sleep;
use tracing::{debug, error, info, warn};

use crate::{SharedArgs, contained_path, now, report_stats, send_data::SendData};

const ANTI_REPLAY_WINDOW: Duration = Duration::from_secs(10);

pub mod http09;
pub mod http3;

#[derive(Debug, Error)]
pub enum Error {
    #[error("invalid argument: {0}")]
    Argument(&'static str),
    #[error(transparent)]
    Http3(#[from] neqo_http3::Error),
    #[error(transparent)]
    Io(#[from] io::Error),
    #[error(transparent)]
    Qlog(#[from] qlog::Error),
    #[error(transparent)]
    Transport(#[from] neqo_transport::Error),
    #[error(transparent)]
    Crypto(#[from] nss::Error),
}

pub type Res<T> = Result<T, Error>;

#[derive(Debug, Parser)]
#[command(author, version, about, long_about = None)]
pub struct Args {
    #[command(flatten)]
    shared: SharedArgs,

    /// List of IP:port to listen on
    #[arg(default_value = "[::]:4433")]
    hosts: Vec<String>,

    #[arg(short = 'd', long)]
    /// NSS database directory [default: `$TEST_FIXTURE_DB` or the bundled NSS test DB].
    db: Option<PathBuf>,

    #[arg(short = 'k', long, default_value = "key")]
    /// Name of key from NSS database.
    key: String,

    #[arg(name = "retry", long)]
    /// Force a retry
    retry: bool,

    #[arg(name = "ech", long)]
    /// Enable encrypted client hello (ECH).
    /// This generates a new set of ECH keys when it is invoked.
    /// The resulting configuration is printed to stdout in hexadecimal format.
    ech: bool,
}

#[cfg(any(test, feature = "bench"))]
impl Default for Args {
    fn default() -> Self {
        Self {
            shared: SharedArgs::default(),
            hosts: vec!["[::]:12345".to_string()],
            db: None,
            key: "key".to_string(),
            retry: false,
            ech: false,
        }
    }
}

impl Args {
    #[must_use]
    pub const fn get_shared(&self) -> &SharedArgs {
        &self.shared
    }

    fn get_ciphers(&self) -> Vec<Cipher> {
        self.shared
            .ciphers
            .iter()
            .filter_map(|c| match c.as_str() {
                "TLS_AES_128_GCM_SHA256" => Some(TLS_AES_128_GCM_SHA256),
                "TLS_AES_256_GCM_SHA384" => Some(TLS_AES_256_GCM_SHA384),
                "TLS_CHACHA20_POLY1305_SHA256" => Some(TLS_CHACHA20_POLY1305_SHA256),
                _ => None,
            })
            .collect::<Vec<_>>()
    }

    fn listen_addresses(&self) -> Vec<SocketAddr> {
        self.hosts
            .iter()
            .filter_map(|host| host.to_socket_addrs().ok())
            .flatten()
            .chain(self.shared.quic_parameters.preferred_address_v4())
            .chain(self.shared.quic_parameters.preferred_address_v6())
            .collect()
    }

    fn now(&self) -> Instant {
        if self.shared.qns_test.is_some() {
            // When NSS starts its anti-replay it blocks any acceptance of 0-RTT for a
            // single period.  This ensures that an attacker that is able to force a
            // server to reboot is unable to use that to flush the anti-replay buffers
            // and have something replayed.
            //
            // However, this is a massive inconvenience for us when we are testing.
            // As we can't initialize `AntiReplay` in the past (see `neqo_common::time`
            // for why), fast forward time here so that the connections get times from
            // in the future.
            //
            // This is NOT SAFE.  Don't do this.
            now() + ANTI_REPLAY_WINDOW
        } else {
            now()
        }
    }

    #[cfg(any(test, feature = "bench"))]
    pub fn set_qlog_dir(&mut self, dir: PathBuf) {
        self.shared.qlog_dir = Some(dir);
    }

    pub fn set_hosts(&mut self, hosts: Vec<String>) {
        self.hosts = hosts;
    }

    pub fn update_for_tests(&mut self) {
        if let Some(testcase) = self.shared.qns_test.as_ref() {
            if self.shared.quic_parameters.quic_version.is_empty() {
                // Quic Interop Runner expects the server to support `Version1`
                // only. Exceptions are testcases `versionnegotiation` (not yet
                // implemented) and `v2`.
                if testcase != "v2" {
                    self.shared.quic_parameters.quic_version = vec![Version::Version1];
                }
            } else {
                warn!("Both -V and --qns-test were set. Ignoring testcase specific versions");
            }

            // These are the default for all tests except http3.
            self.shared.alpn = String::from("hq-interop");
            // TODO: More options to deduplicate with client?
            match testcase.as_str() {
                "http3" => {
                    self.shared.alpn = String::from("h3");
                }
                "zerortt" => self.shared.quic_parameters.max_streams_bidi = 100,
                "handshake" | "transfer" | "resumption" | "multiconnect" | "v2" | "ecn" => {}
                "connectionmigration" => {
                    if self.shared.quic_parameters.preferred_address().is_none() {
                        error!("No preferred addresses set for connectionmigration test");
                        exit(127);
                    }
                }
                "chacha20" => {
                    self.shared.ciphers.clear();
                    self.shared
                        .ciphers
                        .extend_from_slice(&[String::from("TLS_CHACHA20_POLY1305_SHA256")]);
                }
                "retry" => self.retry = true,
                _ => exit(127),
            }
        }
    }
}

/// Reports the stats of closed connections.
pub(super) struct StatsReporter {
    enabled: bool,
    /// Where to append to, or `None` to log.
    path: Option<PathBuf>,
}

impl StatsReporter {
    pub(super) const fn new(enabled: bool, path: Option<PathBuf>) -> Self {
        Self { enabled, path }
    }

    /// Reports `conn`'s stats. Callers report each connection once.
    pub(super) fn report(&self, conn: &RefCell<Connection>) {
        if self.enabled {
            report_stats(&conn.borrow().stats(), self.path.as_deref());
        }
    }
}

/// Abstracts the common configuration methods shared by [`neqo_transport::server::Server`]
/// and [`Http3Server`], enabling [`configure_server`] to work with both.
pub(super) trait ServerConfig {
    fn set_ciphers(&mut self, ciphers: &[Cipher]);
    fn set_qlog_dir(&mut self, dir: Option<PathBuf>);
    fn set_validation(&self, v: ValidateAddress);
    fn enable_ech(&mut self, config: u8, public_name: &str, sk: &PrivateKey, pk: &PublicKey);
    fn ech_config(&self) -> &[u8];
}

impl ServerConfig for neqo_transport::server::Server {
    fn set_ciphers(&mut self, ciphers: &[Cipher]) {
        self.set_ciphers(ciphers);
    }
    fn set_qlog_dir(&mut self, dir: Option<PathBuf>) {
        self.set_qlog_dir(dir);
    }
    fn set_validation(&self, v: ValidateAddress) {
        self.set_validation(v);
    }
    fn enable_ech(&mut self, config: u8, public_name: &str, sk: &PrivateKey, pk: &PublicKey) {
        self.enable_ech(config, public_name, sk, pk)
            .expect("ECH configuration failed");
    }
    fn ech_config(&self) -> &[u8] {
        self.ech_config()
    }
}

impl ServerConfig for Http3Server {
    fn set_ciphers(&mut self, ciphers: &[Cipher]) {
        self.set_ciphers(ciphers);
    }
    fn set_qlog_dir(&mut self, dir: Option<PathBuf>) {
        self.set_qlog_dir(dir);
    }
    fn set_validation(&self, v: ValidateAddress) {
        self.set_validation(v);
    }
    fn enable_ech(&mut self, config: u8, public_name: &str, sk: &PrivateKey, pk: &PublicKey) {
        self.enable_ech(config, public_name, sk, pk)
            .expect("ECH configuration failed");
    }
    fn ech_config(&self) -> &[u8] {
        self.ech_config()
    }
}

/// Apply common [`Args`]-driven configuration to any server that implements [`ServerConfig`].
pub(super) fn configure_server(server: &mut impl ServerConfig, args: &Args) {
    server.set_ciphers(&args.get_ciphers());
    server.set_qlog_dir(args.shared.qlog_dir.clone());
    if args.retry {
        server.set_validation(ValidateAddress::Always);
    }
    if args.ech {
        let (sk, pk) = generate_ech_keys().expect("should create ECH keys");
        server.enable_ech(random::<1>()[0], "public.example", &sk, &pk);
        info!("ECHConfigList: {}", Hex::new(server.ech_config()));
    }
}

/// Generate a response [`SendData`] for a given request path.
///
/// In QNS test mode, reads the corresponding file from `/www/`. Returns `Err`
/// if the file cannot be read (caller sends a 404) or if the path contains
/// `..` components.
/// In non-QNS mode, trims `/` from the path, parses the remainder as a byte
/// count, and generates that many zero bytes. If parsing fails, sends the
/// path bytes instead.
pub(super) fn response_for_path(path: &str, is_qns_test: bool) -> Result<SendData, ()> {
    if is_qns_test {
        let Some(file_path) = contained_path(Path::new("/www"), path.trim_matches('/')) else {
            error!("Rejecting path outside /www: {path}");
            return Err(());
        };
        // Reading the whole file is fine for QNS test mode.
        fs::read(file_path).map(SendData::from).map_err(|e| {
            error!("Failed to read {path}: {e}");
        })
    } else {
        Ok(path
            .trim_matches('/')
            .parse::<usize>()
            .map_or_else(|_| path.into(), SendData::zeroes))
    }
}

#[expect(clippy::module_name_repetitions, reason = "This is OK.")]
pub trait HttpServer: Display {
    fn process_multiple<'a, D: IntoIterator<Item = Datagram<&'a mut [u8]>>>(
        &mut self,
        dgrams: D,
        now: Instant,
        max_datagrams: NonZeroUsize,
    ) -> OutputBatch;
    fn process_events(&mut self, now: Instant);
    fn has_events(&self) -> bool;
    /// Enables an [`HttpServer`] to drive asynchronous operations.
    ///
    /// Needed in Firefox's HTTP/3 proxy test server implementation to drive TCP
    /// and UDP sockets to the proxy target.
    ///
    /// <https://github.com/mozilla-firefox/firefox/blob/main/netwerk/test/http3server/src/main.rs>
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<()> {
        Poll::Pending
    }
}

pub struct Runner<S> {
    now: Box<dyn Fn() -> Instant>,
    server: S,
    timeout: Option<Pin<Box<Sleep>>>,
    sockets: Vec<crate::udp::Socket>,
    recv_buf: RecvBuf,
}

impl<S: HttpServer + Unpin> Runner<S> {
    #[must_use]
    pub fn new(server: S, now: Box<dyn Fn() -> Instant>, sockets: Vec<crate::udp::Socket>) -> Self {
        Self {
            now,
            server,
            timeout: None,
            sockets,
            recv_buf: RecvBuf::default(),
        }
    }

    #[must_use]
    pub fn local_addresses(&self) -> Vec<SocketAddr> {
        self.sockets
            .iter()
            .map(crate::udp::Socket::local_addr)
            .collect()
    }

    /// Tries to find a socket, but then just falls back to sending from the first.
    fn find_socket(
        sockets: &mut [crate::udp::Socket],
        addr: SocketAddr,
    ) -> &mut crate::udp::Socket {
        let index = sockets
            .iter()
            .position(|socket| socket.local_addr() == addr)
            .unwrap_or_default();
        &mut sockets[index]
    }

    // Free function (i.e. not taking `&mut self: ServerRunner`) to be callable by
    // `ServerRunner::read_and_process` while holding a reference to
    // `ServerRunner::recv_buf`.
    async fn process_inner(
        server: &mut S,
        timeout: &mut Option<Pin<Box<Sleep>>>,
        sockets: &mut [crate::udp::Socket],
        now: &dyn Fn() -> Instant,
        mut input_dgrams: Option<DatagramIter<'_>>,
    ) -> Result<(), io::Error> {
        // Each socket has a maximum number of GSO segments it can handle. When
        // calling `server.process_multiple` we don't know which socket will be
        // used. Take the smallest maximum GSO segments from all sockets to
        // ensure that we don't send more segments than any socket can handle.
        //
        // Ideally we would have a way to know which socket will be used. Likely
        // not worth it for a test-only server implementation which is mostly
        // used with a single socket only.
        let smallest_max_gso_segments = sockets
            .iter()
            .map(crate::udp::Socket::max_gso_segments)
            .min()
            .expect("At least one socket must be present")
            .try_into()
            .inspect_err(|_| error!("Socket return GSO size of 0"))
            .map_err(|_| io::Error::from(io::ErrorKind::Unsupported))?;

        loop {
            match server.process_multiple(
                input_dgrams.take().into_iter().flatten(),
                now(),
                smallest_max_gso_segments,
            ) {
                OutputBatch::DatagramBatch(dgram) => {
                    let socket = Self::find_socket(sockets, dgram.source());
                    let mut retry_on_reset = true;
                    loop {
                        // Optimistically attempt sending datagram. In case the
                        // OS buffer is full, wait till socket is writable then
                        // try again.
                        match socket.send(&dgram) {
                            Ok(()) => break,
                            Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                                socket.writable().await?;
                                // Now try again.
                            }
                            Err(e)
                                if e.raw_os_error() == Some(libc::EIO)
                                    && dgram.num_datagrams() > 1 =>
                            {
                                info!(
                                    "`libc::sendmsg` failed with {e}; quinn-udp will halt segmentation offload"
                                );
                                // Drop the packets and let QUIC handle retransmission.
                                break;
                            }
                            Err(e) if e.kind() == io::ErrorKind::ConnectionReset => {
                                // See `read_and_process`. This call consumes the pending error,
                                // so retry once, then let QUIC handle retransmission.
                                debug!("Ignoring {e} on send, a peer probably closed its socket");
                                if !retry_on_reset {
                                    break;
                                }
                                retry_on_reset = false;
                            }
                            e @ Err(_) => return e,
                        }
                    }
                }
                OutputBatch::Callback(new_timeout) => {
                    debug!("Setting timeout of {new_timeout:?}");
                    *timeout = Some(Box::pin(tokio::time::sleep(new_timeout)));
                    break;
                }
                OutputBatch::None => break,
            }
        }
        Ok(())
    }

    async fn read_and_process(&mut self, sockets_index: usize) -> Result<(), io::Error> {
        loop {
            let socket = &mut self.sockets[sockets_index];
            let input_dgrams = match socket.recv(&mut self.recv_buf) {
                Ok(Some(input_dgrams)) => input_dgrams,
                Ok(None) => break,
                Err(e) if e.kind() == io::ErrorKind::ConnectionReset => {
                    // Windows reports an earlier send's ICMP "port unreachable" as
                    // `WSAECONNRESET` here. This socket serves all connections, so keep going,
                    // like quinn does.
                    //
                    // <https://github.com/quinn-rs/quinn/blob/35fe3379205ed2ace0e6a858f60f3a8a2ff6510e/quinn/src/endpoint.rs#L925-L929>
                    debug!("Ignoring {e} on receive, a peer probably closed its socket");
                    continue;
                }
                Err(e) => return Err(e),
            };

            Self::process_inner(
                &mut self.server,
                &mut self.timeout,
                &mut self.sockets,
                &self.now,
                Some(input_dgrams),
            )
            .await?;
        }

        Ok(())
    }

    async fn process(&mut self) -> Result<(), io::Error> {
        Self::process_inner(
            &mut self.server,
            &mut self.timeout,
            &mut self.sockets,
            &self.now,
            None,
        )
        .await
    }

    // Wait for any of the sockets to be readable or the timeout to fire.
    async fn ready(&mut self) -> Result<Ready, io::Error> {
        let sockets_ready = select_all(
            self.sockets
                .iter()
                .map(|socket| Box::pin(socket.readable())),
        )
        .map(|(res, inx, _)| match res {
            Ok(()) => Ok(Ready::Socket(inx)),
            Err(e) => Err(e),
        });

        let timeout_ready = self
            .timeout
            .as_mut()
            .map_or_else(|| Either::Right(futures::future::pending()), Either::Left)
            .map(|()| Ok(Ready::Timeout));

        let server_ready = poll_fn(|cx| HttpServer::poll(Pin::new(&mut self.server), cx))
            .map(|()| Ok(Ready::Server));

        select(
            select(sockets_ready, timeout_ready).map(|either| either.factor_first().0),
            server_ready,
        )
        .map(|either| either.factor_first().0)
        .await
    }

    pub async fn run(mut self) -> Res<()> {
        loop {
            self.server.process_events((self.now)());
            self.process().await?;

            if self.server.has_events() {
                continue;
            }

            match self.ready().await? {
                Ready::Socket(sockets_index) => {
                    self.read_and_process(sockets_index).await?;
                }
                Ready::Timeout => {
                    self.timeout = None;
                    self.process().await?;
                }
                Ready::Server => {
                    // Processing server at top of the loop.
                }
            }
        }
    }
}

enum Ready {
    Socket(usize),
    Timeout,
    Server,
}

#[expect(clippy::type_complexity, reason = "pinned and boxed future")]
pub fn run(
    mut args: Args,
) -> Res<(
    Pin<Box<dyn Future<Output = Res<()>> + 'static>>,
    Vec<SocketAddr>,
)> {
    neqo_common::log::init(args.shared.level_filter());
    args.update_for_tests();
    assert!(!args.key.is_empty(), "Need at least one key");

    init_db(args.db.take().unwrap_or_else(nss_test_fixture::db_path))?;

    let hosts = args.listen_addresses();
    if hosts.is_empty() {
        error!("No valid hosts defined");
        return Err(io::Error::new(io::ErrorKind::InvalidInput, "No hosts").into());
    }
    let sockets: Vec<crate::udp::Socket> = hosts
        .into_iter()
        .map(|host| {
            let socket = crate::udp::Socket::bind(host)?;
            info!("Server waiting for connection on: {}", socket.local_addr());

            Ok(socket)
        })
        .collect::<Result<_, io::Error>>()?;

    // Note: this is the exception to the case where we use `Args::now`.
    let anti_replay = AntiReplay::new(now(), ANTI_REPLAY_WINDOW, 7, 14)?;
    let cid_mgr = Rc::new(RefCell::new(RandomConnectionIdGenerator::new(10)));

    if args.shared.alpn == "h3" {
        let runner = Runner::new(
            http3::HttpServer::new(&args, anti_replay, cid_mgr),
            Box::new(move || args.now()),
            sockets,
        );
        let local_addrs = runner.local_addresses();
        Ok((Box::pin(runner.run()), local_addrs))
    } else {
        let runner = Runner::new(
            http09::HttpServer::new(&args, anti_replay, cid_mgr)?,
            Box::new(move || args.now()),
            sockets,
        );
        let local_addrs = runner.local_addresses();
        Ok((Box::pin(runner.run()), local_addrs))
    }
}

/// Test helpers shared by `http09::tests` and `http3::tests`.
#[cfg(test)]
pub(super) mod test_support {
    use std::fs;

    use neqo_transport::{Connection, INITIAL_LOCAL_MAX_STREAM_DATA};
    use test_fixture::{DEFAULT_ALPN, ProcessServer, default_client, handshake_with_server, now};

    use super::Args;
    use crate::temp_dir::TempDir;

    /// The inner server [`reported_on_close`] drives.
    pub(super) trait StatsServer: super::HttpServer {
        fn transport(&mut self) -> &mut dyn ProcessServer;
    }

    /// [`Args`] for the ALPN that [`default_client`] offers.
    pub(super) fn alpn_args() -> Args {
        let mut args = Args::default();
        args.shared.alpn = DEFAULT_ALPN[0].to_string();
        args
    }

    /// Connect a [`default_client`] to `server`.
    pub(super) fn connect<S: StatsServer>(server: &mut S) -> Connection {
        let mut client = default_client();
        handshake_with_server(&mut client, server.transport());
        server.process_events(now());
        let out = server.transport().process(None, now()); // NewSessionTicket
        client.process_input(out.dgram().unwrap(), now());
        client
    }

    /// Send `client`'s next datagram to `server` and handle the events.
    pub(super) fn deliver<S: StatsServer>(server: &mut S, client: &mut Connection) {
        let out = client.process_output(now());
        _ = server.transport().process(out.dgram(), now());
        server.process_events(now());
    }

    /// A path whose response exceeds the client's stream flow-control window.
    pub(super) fn blocked_path() -> String {
        format!("/{}", INITIAL_LOCAL_MAX_STREAM_DATA + 1)
    }

    /// Connect to `make`'s server and close, returning the records it wrote.
    pub(super) fn reported_on_close<S: StatsServer>(make: impl FnOnce(&Args) -> S) -> usize {
        let dir = TempDir::new();
        let file = dir.path().join("stats.json");

        let mut args = alpn_args();
        args.shared.stats_file = Some(file.clone());
        let mut server = make(&args);

        let mut client = connect(&mut server);
        assert!(!file.exists(), "must only report on close");

        client.close(now(), 0, "bye");
        deliver(&mut server, &mut client);
        server.process_events(now()); // Again: a repeat would append a line.
        fs::read_to_string(&file).map_or(0, |s| s.lines().count())
    }
}

#[cfg(test)]
mod tests {
    use std::fmt;

    use neqo_common::{Tos, datagram};
    use test_fixture::{default_client, fixture_init};
    use tokio::time::timeout;

    use super::*;

    #[derive(Default)]
    struct MockServer {
        batches: Vec<datagram::Batch>,
        destinations: Vec<SocketAddr>,
    }

    impl Display for MockServer {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            write!(f, "MockServer")
        }
    }

    impl HttpServer for MockServer {
        fn process_multiple<'a, D: IntoIterator<Item = Datagram<&'a mut [u8]>>>(
            &mut self,
            dgrams: D,
            _now: Instant,
            _max_datagrams: NonZeroUsize,
        ) -> OutputBatch {
            for d in dgrams {
                self.destinations.push(d.destination());
            }
            self.batches
                .pop()
                .map_or(OutputBatch::None, OutputBatch::DatagramBatch)
        }

        fn process_events(&mut self, _now: Instant) {}

        fn has_events(&self) -> bool {
            false
        }
    }

    /// Expect a peer closing its socket not to stop the server serving everyone else.
    ///
    /// Only Windows surfaces the resulting ICMP error, as `WSAECONNRESET`. Elsewhere this
    /// still covers the send and receive loops.
    #[tokio::test]
    async fn ignore_connection_reset() -> Result<(), io::Error> {
        let socket = crate::udp::Socket::bind("127.0.0.1:0")?;
        let local_addr = socket.local_addr();

        let closed = std::net::UdpSocket::bind("127.0.0.1:0")?;
        let closed_addr = closed.local_addr()?;
        drop(closed);

        let mut runner = Runner::new(MockServer::default(), Box::new(now), vec![socket]);

        // Draw an ICMP "port unreachable" from the closed port.
        for _ in 0..10 {
            runner.server.batches.push(
                Datagram::new(local_addr, closed_addr, Tos::default(), b"hello".to_vec()).into(),
            );
            runner.process().await?;
        }

        // Whatever that did to the socket, this datagram from a live peer has to arrive.
        std::net::UdpSocket::bind("127.0.0.1:0")?.send_to(b"ping", local_addr)?;
        timeout(Duration::from_secs(10), async {
            while runner.server.destinations.is_empty() {
                if let Ready::Socket(i) = runner.ready().await? {
                    runner.read_and_process(i).await?;
                }
            }
            Ok::<_, io::Error>(())
        })
        .await
        .map_err(|_| io::Error::new(io::ErrorKind::TimedOut, "server stopped serving"))??;

        assert!(runner.server.destinations.iter().all(|d| *d == local_addr));

        Ok(())
    }

    #[test]
    fn reports_nothing_without_the_stats_flag() {
        fixture_init();
        let dir = crate::temp_dir::TempDir::new();
        let file = dir.path().join("stats.json");
        StatsReporter::new(false, Some(file.clone())).report(&RefCell::new(default_client()));
        assert!(!file.exists());
    }

    #[test]
    fn response_for_path_qns_not_found() {
        // Non-existent file should return Err.
        let result = response_for_path("/no_such_file_xyz", true);
        assert!(result.is_err());
    }

    #[test]
    fn response_for_path_non_qns_count() {
        let data = response_for_path("/1000", false).expect("should succeed");
        assert_eq!(data.len(), 1000);
    }

    #[test]
    fn response_for_path_non_qns_non_numeric_sends_path_bytes() {
        // Non-numeric path falls back to sending the path bytes themselves.
        let data = response_for_path("/hello", false).expect("should succeed");
        assert_eq!(data.len(), "/hello".len());
    }

    #[test]
    fn response_for_path_qns_dotdot_rejected() {
        // Paths with ".." components must be rejected in QNS mode to prevent
        // directory traversal outside /www/.
        for path in ["/../etc/passwd", "/foo/../etc/passwd", "/.."] {
            assert!(response_for_path(path, true).is_err(), "path: {path}");
        }
    }
}
