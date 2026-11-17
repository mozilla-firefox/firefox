// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// The class implementing a QUIC connection.

use std::{
    cell::RefCell,
    cmp::{max, min},
    fmt::{self, Debug, Display, Formatter, Write as _},
    iter, mem,
    net::{IpAddr, SocketAddr},
    num::NonZeroUsize,
    ops::RangeInclusive,
    rc::{Rc, Weak},
    time::{Duration, Instant},
};

use neqo_common::{
    Buffer, Datagram, Decoder, Ecn, Encoder, Role, Tos, datagram,
    event::Provider as EventProvider,
    expect_usize,
    hex::{Hex, HexSnipMiddle, HexWithLen},
    hrtime,
    qlog::Qlog,
    to_u64,
};
use nss::{
    Agent, AntiReplay, AuthenticationStatus, Cipher, Client, Group, HandshakeState, PrivateKey,
    PublicKey, ResumptionToken, SecretAgentInfo, SecretAgentPreInfo, Server, ZeroRttChecker,
    cert::{CertificateCompressor, CertificateInfo},
};
use smallvec::SmallVec;
use strum::IntoEnumIterator as _;
use tracing::{debug, error, info, trace, warn};

use crate::{
    AppError, CloseReason, Error, Res, StreamId,
    addr_valid::{AddressValidation, NewTokenState},
    cc::Phase,
    cid::{
        ConnectionId, ConnectionIdEntry, ConnectionIdGenerator, ConnectionIdManager,
        ConnectionIdRef, ConnectionIdStore,
    },
    crypto::{Crypto, CryptoDxState, Epoch},
    datagram_queue::{DatagramId, DatagramQueueCapacity, DatagramQueueOutcome, DroppedDatagrams},
    ecn,
    events::{ConnectionEvent, ConnectionEvents, OutgoingDatagramOutcome},
    frame::{CloseError, Frame, FrameEncoder as _, FrameType},
    packet,
    path::{Path, PathRef, Paths},
    qlog,
    quic_datagrams::{DATAGRAM_FRAME_TYPE_VARINT_LEN, QuicDatagrams},
    recovery::{self, SendProfile, sent},
    recv_stream,
    rtt::{DEFAULT_INITIAL_RTT, GRANULARITY, RttEstimate},
    saved::SavedDatagrams,
    send_stream::{self, SendStream},
    stateless_reset::Token as Srt,
    stats::{Stats, StatsCell},
    stream_id::{MAX_STREAMS_LIMIT, StreamType},
    streams::{SendGroupId, SendOrder, Streams},
    tparams::{
        self,
        TransportParameterId::{
            self, AckDelayExponent, ActiveConnectionIdLimit, DisableMigration, GreaseQuicBit,
            InitialSourceConnectionId, MaxAckDelay, MaxDatagramFrameSize, MaxUdpPayloadSize,
            MinAckDelay, OriginalDestinationConnectionId, ResetStreamAt, RetrySourceConnectionId,
            StatelessResetToken,
        },
        TransportParameters, TransportParametersHandler,
    },
    tracking::{AckTracker, PacketNumberSpace, RecvdPackets},
    version::{self, Version},
};

mod idle;
pub mod params;
mod state;
#[cfg(any(test, feature = "build-fuzzing-corpus"))]
#[cfg_attr(coverage_nightly, coverage(off))]
pub mod test_internal;

use idle::IdleTimeout;
pub use params::ConnectionParameters;
use params::PreferredAddressConfig;
use state::StateSignaling;
pub use state::{ClosingFrame, State};

pub use crate::send_stream::{RetransmissionPriority, TransmissionPriority};

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum ZeroRttState {
    Init,
    Sending,
    AcceptedClient,
    AcceptedServer,
    Rejected,
}

#[derive(Clone, Debug, PartialEq, Eq)]
/// Type returned from `process()` and `process_output()`. Users are required to
/// call these repeatedly until `Callback` or `None` is returned.
pub enum Output {
    /// Connection requires no action.
    None,
    /// Connection requires the datagram be sent.
    Datagram(Datagram),
    /// Connection requires `process_input()` be called when the `Duration`
    /// elapses.
    Callback(Duration),
}

impl TryFrom<OutputBatch> for Output {
    type Error = ();

    fn try_from(value: OutputBatch) -> Result<Self, Self::Error> {
        match value {
            OutputBatch::None => Ok(Self::None),
            OutputBatch::DatagramBatch(dg) => Ok(Self::Datagram(dg.try_into()?)),
            OutputBatch::Callback(t) => Ok(Self::Callback(t)),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum OutputBatch {
    /// Connection requires no action.
    None,
    /// Connection requires the datagram batch be sent.
    DatagramBatch(datagram::Batch),
    /// Connection requires `process_input()` be called when the `Duration`
    /// elapses.
    Callback(Duration),
}

impl From<Output> for OutputBatch {
    fn from(value: Output) -> Self {
        match value {
            Output::None => Self::None,
            Output::Datagram(dg) => Self::DatagramBatch(datagram::Batch::from(dg)),
            Output::Callback(t) => Self::Callback(t),
        }
    }
}

impl OutputBatch {
    /// Convert into an [`Option<datagram::Batch>`].
    #[must_use]
    pub fn dgram(self) -> Option<datagram::Batch> {
        match self {
            Self::DatagramBatch(dg) => Some(dg),
            _ => None,
        }
    }
}

impl Output {
    /// Convert into an [`Option<Datagram>`].
    #[must_use]
    pub fn dgram(self) -> Option<Datagram> {
        match self {
            Self::Datagram(dg) => Some(dg),
            _ => None,
        }
    }

    /// Get a reference to the Datagram, if any.
    #[must_use]
    pub const fn as_dgram_ref(&self) -> Option<&Datagram> {
        match self {
            Self::Datagram(dg) => Some(dg),
            _ => None,
        }
    }

    /// Ask how long the caller should wait before calling back.
    #[must_use]
    pub const fn callback(&self) -> Duration {
        match self {
            Self::Callback(t) => *t,
            _ => Duration::new(0, 0),
        }
    }
}

impl From<Option<Datagram>> for Output {
    fn from(value: Option<Datagram>) -> Self {
        value.map_or(Self::None, Self::Datagram)
    }
}

/// Used by inner functions like `Connection::output`.
enum SendOptionBatch {
    /// Yes, please send this datagram.
    Yes(datagram::Batch),
    /// Don't send.  If this was blocked on the pacer (the arg is true).
    No(bool),
}

impl Default for SendOptionBatch {
    fn default() -> Self {
        Self::No(false)
    }
}

/// Used by inner functions like `Connection::output`.
enum SendOption {
    /// Yes, please send this datagram.
    Yes,
    /// Don't send.
    No(
        /// Whether this was blocked on the pacer.
        bool,
    ),
}

/// Used by `Connection::preprocess` to determine what to do
/// with an packet before attempting to remove protection.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum PreprocessResult {
    /// End processing and return successfully.
    End,
    /// Stop processing this datagram and move on to the next.
    Next,
    /// Continue and process this packet.
    Continue,
}

/// `AddressValidationInfo` holds information relevant to either
/// responding to address validation (`NewToken`, `Retry`) or generating
/// tokens for address validation (`Server`).
enum AddressValidationInfo {
    None,
    // We are a client and have information from `NEW_TOKEN`.
    NewToken(Vec<u8>),
    // We are a client and have received a `Retry` packet.
    Retry {
        token: Vec<u8>,
        retry_source_cid: ConnectionId,
    },
    // We are a server and can generate tokens.
    Server(Weak<RefCell<AddressValidation>>),
}

impl AddressValidationInfo {
    pub fn token(&self) -> &[u8] {
        match self {
            Self::NewToken(token) | Self::Retry { token, .. } => token,
            _ => &[],
        }
    }

    pub fn generate_new_token(&self, peer_address: SocketAddr, now: Instant) -> Option<Vec<u8>> {
        match self {
            Self::Server(w) => w
                .upgrade()?
                .borrow()
                .generate_new_token(peer_address, now)
                .ok(),
            Self::None => None,
            _ => unreachable!("called a server function on a client"),
        }
    }
}

/// A QUIC Connection
///
/// First, create a new connection using `new_client()` or `new_server()`.
///
/// For the life of the connection, handle activity in the following manner:
/// 1. Perform operations using the `stream_*()` methods.
/// 1. Call `process_input()` when a datagram is received or the timer expires. Obtain information
///    on connection state changes by checking `events()`.
/// 1. Having completed handling current activity, repeatedly call `process_output()` for packets to
///    send, until it returns `Output::Callback` or `Output::None`.
///
/// After the connection is closed (either by calling `close()` or by the
/// remote) continue processing until `state()` returns `Closed`.
pub struct Connection {
    role: Role,
    version: Version,
    state: State,
    tps: Rc<RefCell<TransportParametersHandler>>,
    /// What we are doing with 0-RTT.
    zero_rtt_state: ZeroRttState,
    /// All of the network paths that we are aware of.
    paths: Paths,
    /// This object will generate connection IDs for the connection.
    cid_manager: ConnectionIdManager,
    address_validation: AddressValidationInfo,
    /// The connection IDs that were provided by the peer.
    cids: ConnectionIdStore<Srt>,

    /// The source connection ID that this endpoint uses for the handshake.
    /// Since we need to communicate this to our peer in tparams, setting this
    /// value is part of constructing the struct.
    local_initial_source_cid: ConnectionId,
    /// The source connection ID from the first packet from the other end.
    /// This is checked against the peer's transport parameters.
    remote_initial_source_cid: Option<ConnectionId>,
    /// The destination connection ID from the first packet from the client.
    /// This is checked by the client against the server's transport parameters.
    original_destination_cid: Option<ConnectionId>,

    /// We sometimes save a datagram against the possibility that keys will later
    /// become available.  This avoids reporting packets as dropped during the handshake
    /// when they are either just reordered or we haven't been able to install keys yet.
    /// In particular, this occurs when asynchronous certificate validation happens.
    saved_datagrams: SavedDatagrams,
    /// Some packets were received, but not tracked.
    received_untracked: bool,

    /// This is responsible for the `QuicDatagrams`' handling:
    /// <https://datatracker.ietf.org/doc/html/draft-ietf-quic-datagram>
    quic_datagrams: QuicDatagrams,

    crypto: Crypto,
    acks: AckTracker,
    idle_timeout: IdleTimeout,
    streams: Streams,
    state_signaling: StateSignaling,
    loss_recovery: recovery::Loss,
    events: ConnectionEvents,
    new_token: NewTokenState,
    stats: StatsCell,
    qlog: Qlog,
    /// A session ticket was received without `NEW_TOKEN`,
    /// this is when that turns into an event without `NEW_TOKEN`.
    release_resumption_token_timer: Option<Instant>,
    conn_params: ConnectionParameters,
    hrtime: hrtime::Handle,

    /// For testing purposes it is sometimes necessary to inject frames that wouldn't
    /// otherwise be sent, just to see how a connection handles them.  Inserting them
    /// into packets proper mean that the frames follow the entire processing path.
    #[cfg(any(test, feature = "build-fuzzing-corpus"))]
    test_frame_writer: Option<Box<dyn test_internal::FrameWriter>>,
}

impl Debug for Connection {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(
            f,
            "{:?} Connection: {:?} {:?}",
            self.role,
            self.state,
            self.paths.primary()
        )
    }
}

impl Connection {
    /// A long default for timer resolution, so that we don't tax the
    /// system too hard when we don't need to.
    const LOOSE_TIMER_RESOLUTION: Duration = Duration::from_millis(50);
    /// The SCONE indicator.
    const SCONE_INDICATION: &[u8] = &[0xc8, 0x13];

    /// Create a new QUIC connection with Client role.
    /// # Errors
    /// When NSS fails and an agent cannot be created.
    pub fn new_client<I: Into<String>, A: AsRef<str>>(
        server_name: I,
        protocols: &[A],
        cid_generator: Rc<RefCell<dyn ConnectionIdGenerator>>,
        local_addr: SocketAddr,
        remote_addr: SocketAddr,
        conn_params: ConnectionParameters,
        now: Instant,
    ) -> Res<Self> {
        let dcid = ConnectionId::generate_initial();
        let mut c = Self::new(
            Role::Client,
            Agent::from(Client::new(server_name.into(), conn_params.is_greasing())?),
            cid_generator,
            protocols,
            conn_params,
        )?;
        c.crypto.states_mut().init(
            c.conn_params.get_versions().compatible(),
            Role::Client,
            &dcid,
            c.conn_params.randomize_first_pn_enabled(),
        )?;
        c.original_destination_cid = Some(dcid);
        let path = Path::temporary(
            local_addr,
            remote_addr,
            &c.conn_params,
            Qlog::default(),
            now,
        );
        c.setup_handshake_path(&Rc::new(RefCell::new(path)), now);
        Ok(c)
    }

    /// Create a new QUIC connection with Server role.
    /// # Errors
    /// When NSS fails and an agent cannot be created.
    pub fn new_server<A1: AsRef<str>, A2: AsRef<str>>(
        certs: &[A1],
        protocols: &[A2],
        cid_generator: Rc<RefCell<dyn ConnectionIdGenerator>>,
        conn_params: ConnectionParameters,
    ) -> Res<Self> {
        Self::new(
            Role::Server,
            Agent::from(Server::new(certs)?),
            cid_generator,
            protocols,
            conn_params,
        )
    }

    fn new<P: AsRef<str>>(
        role: Role,
        agent: Agent,
        cid_generator: Rc<RefCell<dyn ConnectionIdGenerator>>,
        protocols: &[P],
        conn_params: ConnectionParameters,
    ) -> Res<Self> {
        // Setup the local connection ID.
        let local_initial_source_cid = cid_generator
            .borrow_mut()
            .generate_cid()
            .ok_or(Error::ConnectionIdsExhausted)?;
        let mut cid_manager =
            ConnectionIdManager::new(cid_generator, local_initial_source_cid.clone());
        let mut tps = conn_params.create_transport_parameter(role, &mut cid_manager)?;
        tps.local_mut()
            .set_bytes(InitialSourceConnectionId, local_initial_source_cid.to_vec());

        let tphandler = Rc::new(RefCell::new(tps));
        let crypto = Crypto::new(
            conn_params.get_versions().initial(),
            &conn_params,
            agent,
            protocols.iter().map(P::as_ref).map(String::from).collect(),
            Rc::clone(&tphandler),
        )?;

        let stats = StatsCell::default();
        let events = ConnectionEvents::default();
        let quic_datagrams = QuicDatagrams::new(conn_params.get_datagram_size(), events.clone());

        let c = Self {
            role,
            version: conn_params.get_versions().initial(),
            state: State::Init,
            paths: Paths::new(&conn_params),
            cid_manager,
            tps: Rc::clone(&tphandler),
            zero_rtt_state: ZeroRttState::Init,
            address_validation: AddressValidationInfo::None,
            local_initial_source_cid,
            remote_initial_source_cid: None,
            original_destination_cid: None,
            saved_datagrams: SavedDatagrams::default(),
            received_untracked: false,
            crypto,
            acks: AckTracker::default(),
            idle_timeout: IdleTimeout::new(conn_params.get_idle_timeout()),
            streams: Streams::new(tphandler, role, events.clone()),
            cids: ConnectionIdStore::default(),
            state_signaling: StateSignaling::Idle,
            loss_recovery: recovery::Loss::new(stats.clone(), conn_params.get_fast_pto()),
            events,
            new_token: NewTokenState::new(role),
            stats,
            qlog: Qlog::disabled(),
            release_resumption_token_timer: None,
            conn_params,
            hrtime: hrtime::Time::get(Self::LOOSE_TIMER_RESOLUTION),
            quic_datagrams,
            #[cfg(any(test, feature = "build-fuzzing-corpus"))]
            test_frame_writer: None,
        };
        c.stats.borrow_mut().init(format!("{c}"));
        Ok(c)
    }

    /// # Errors
    /// When the operation fails.
    pub fn server_enable_0rtt<Z: ZeroRttChecker + 'static>(
        &mut self,
        anti_replay: &AntiReplay,
        zero_rtt_checker: Z,
    ) -> Res<()> {
        self.crypto
            .server_enable_0rtt(Rc::clone(&self.tps), anti_replay, zero_rtt_checker)
    }

    /// # Errors
    /// When the operation fails.
    pub fn set_certificate_compression<T: CertificateCompressor>(&mut self) -> Res<()> {
        self.crypto.tls_mut().set_certificate_compression::<T>()?;
        Ok(())
    }

    /// # Errors
    /// When the operation fails.
    pub fn server_enable_ech(
        &mut self,
        config: u8,
        public_name: &str,
        sk: &PrivateKey,
        pk: &PublicKey,
    ) -> Res<()> {
        self.crypto.server_enable_ech(config, public_name, sk, pk)
    }

    /// Get the active ECH configuration, which is empty if ECH is disabled.
    #[must_use]
    pub fn ech_config(&self) -> &[u8] {
        self.crypto.ech_config()
    }

    /// # Errors
    /// When the operation fails.
    pub fn client_enable_ech<A: AsRef<[u8]>>(&mut self, ech_config_list: A) -> Res<()> {
        self.crypto.client_enable_ech(ech_config_list)
    }

    /// Set or clear the qlog for this connection.
    pub fn set_qlog(&mut self, qlog: Qlog) {
        self.loss_recovery.set_qlog(qlog.clone());
        self.paths.set_qlog(qlog.clone());
        self.qlog = qlog;
    }

    /// Get the qlog (if any) for this connection.
    pub const fn qlog_mut(&mut self) -> &mut Qlog {
        &mut self.qlog
    }

    /// Get the original destination connection id for this connection. This
    /// will always be present for `Role::Client` but not if `Role::Server` is in
    /// `State::Init`.
    #[must_use]
    pub const fn odcid(&self) -> Option<&ConnectionId> {
        self.original_destination_cid.as_ref()
    }

    /// Set a local transport parameter, possibly overriding a default value.
    /// This only sets transport parameters without dealing with other aspects of
    /// setting the value.
    ///
    /// # Errors
    /// When the transport parameter is invalid.
    /// # Panics
    /// This panics if the transport parameter is known to this crate.
    #[cfg(test)]
    pub fn set_local_tparam(
        &self,
        tp: TransportParameterId,
        value: tparams::TransportParameter,
    ) -> Res<()> {
        if *self.state() == State::Init {
            self.tps.borrow_mut().local_mut().set(tp, value);
            Ok(())
        } else {
            error!("Current state: {:?}", self.state());
            error!("Cannot set local tparam when not in an initial connection state");
            Err(Error::ConnectionState)
        }
    }

    /// `odcid` is their original choice for our CID, which we get from the Retry token.
    /// `remote_cid` is the value from the Source Connection ID field of an incoming packet: what
    /// the peer wants us to use now. `retry_cid` is what we asked them to use when we sent the
    /// Retry.
    pub(crate) fn set_retry_cids(
        &mut self,
        odcid: &ConnectionId,
        remote_cid: ConnectionId,
        retry_cid: &ConnectionId,
    ) {
        debug_assert_eq!(self.role, Role::Server);
        trace!("[{self}] Retry CIDs: odcid={odcid} remote={remote_cid} retry={retry_cid}");
        // We advertise "our" choices in transport parameters.
        self.tps
            .borrow_mut()
            .local_mut()
            .set_bytes(OriginalDestinationConnectionId, odcid.to_vec());
        self.tps
            .borrow_mut()
            .local_mut()
            .set_bytes(RetrySourceConnectionId, retry_cid.to_vec());

        // ...and save their choices for later validation.
        self.remote_initial_source_cid = Some(remote_cid);
    }

    fn retry_sent(&self) -> bool {
        self.tps
            .borrow()
            .local()
            .get_bytes(RetrySourceConnectionId)
            .is_some()
    }

    /// Set ALPN preferences. Strings that appear earlier in the list are given
    /// higher preference.
    /// # Errors
    /// When the operation fails, which is usually due to bad inputs or bad connection state.
    pub fn set_alpn<A: AsRef<[u8]>>(&mut self, protocols: &[A]) -> Res<()> {
        self.crypto.tls_mut().set_alpn(protocols)?;
        Ok(())
    }

    /// Enable a set of ciphers.
    /// # Errors
    /// When the operation fails, which is usually due to bad inputs or bad connection state.
    pub fn set_ciphers(&mut self, ciphers: &[Cipher]) -> Res<()> {
        if self.state != State::Init {
            error!("[{self}] Cannot enable ciphers in state {:?}", self.state);
            return Err(Error::ConnectionState);
        }
        self.crypto.tls_mut().set_ciphers(ciphers)?;
        Ok(())
    }

    /// Enable a set of key exchange groups.
    /// # Errors
    /// When the operation fails, which is usually due to bad inputs or bad connection state.
    pub fn set_groups(&mut self, groups: &[Group]) -> Res<()> {
        if self.state != State::Init {
            error!("[{self}] Cannot enable groups in state {:?}", self.state);
            return Err(Error::ConnectionState);
        }
        self.crypto.tls_mut().set_groups(groups)?;
        Ok(())
    }

    /// Set the number of additional key shares to send in the client hello.
    /// # Errors
    /// When the operation fails, which is usually due to bad inputs or bad connection state.
    pub fn send_additional_key_shares(&mut self, count: usize) -> Res<()> {
        if self.state != State::Init {
            error!("[{self}] Cannot enable groups in state {:?}", self.state);
            return Err(Error::ConnectionState);
        }
        self.crypto.tls_mut().send_additional_key_shares(count)?;
        Ok(())
    }

    fn make_resumption_token(&mut self) -> ResumptionToken {
        debug_assert_eq!(self.role, Role::Client);
        debug_assert!(self.crypto.has_resumption_token());
        // Values less than GRANULARITY are ignored when using the token, so use 0 where needed.
        let rtt = self.paths.primary().map_or_else(
            // If we don't have a path, we don't have an RTT.
            || Duration::from_millis(0),
            |p| {
                let rtt = p.borrow().rtt().estimate();
                if p.borrow().rtt().is_guesstimate() {
                    // When we have no actual RTT sample, do not encode a guestimated RTT larger
                    // than the default initial RTT. (The guess can be very large under lossy
                    // conditions.)
                    if rtt < self.conn_params.get_initial_rtt() {
                        rtt
                    } else {
                        Duration::from_millis(0)
                    }
                } else {
                    rtt
                }
            },
        );

        self.crypto
            .create_resumption_token(
                self.new_token.take_token(),
                self.tps
                    .borrow()
                    .remote_handshake()
                    .as_ref()
                    .expect("should have transport parameters"),
                self.version,
                u64::try_from(rtt.as_millis()).unwrap_or(0),
            )
            .expect("caller checked if a resumption token existed")
    }

    fn confirmed(&self) -> bool {
        self.state == State::Confirmed
    }

    /// Get the simplest PTO calculation for all those cases where we need
    /// a value of this approximate order.  Don't use this for loss recovery,
    /// only use it where a more precise value is not important.
    fn pto(&self) -> Duration {
        self.paths.primary().map_or_else(
            || RttEstimate::new(self.conn_params.get_initial_rtt()).pto(self.confirmed()),
            |p| p.borrow().rtt().pto(self.confirmed()),
        )
    }

    fn create_resumption_token(&mut self, now: Instant) {
        if self.role == Role::Server || self.state < State::Connected {
            return;
        }

        trace!(
            "[{self}] Maybe create resumption token: {} {}",
            self.crypto.has_resumption_token(),
            self.new_token.has_token()
        );

        while self.crypto.has_resumption_token() && self.new_token.has_token() {
            let token = self.make_resumption_token();
            self.events.client_resumption_token(token);
        }

        // If we have a resumption ticket check or set a timer.
        if self.crypto.has_resumption_token() {
            let arm = if let Some(expiration_time) = self.release_resumption_token_timer {
                if expiration_time <= now {
                    let token = self.make_resumption_token();
                    self.events.client_resumption_token(token);
                    self.release_resumption_token_timer = None;

                    // This means that we release one session ticket every 3 PTOs
                    // if no NEW_TOKEN frame is received.
                    self.crypto.has_resumption_token()
                } else {
                    false
                }
            } else {
                true
            };

            if arm {
                self.release_resumption_token_timer = Some(now + 3 * self.pto());
            }
        }
    }

    /// The correct way to obtain a resumption token is to wait for the
    /// `ConnectionEvent::ResumptionToken` event. To emit the event we are waiting for a
    /// resumption token and a `NEW_TOKEN` frame to arrive. Some servers don't send `NEW_TOKEN`
    /// frames and in this case, we wait for 3xPTO before emitting an event. This is especially a
    /// problem for short-lived connections, where the connection is closed before any events are
    /// released. This function retrieves the token, without waiting for a `NEW_TOKEN` frame to
    /// arrive.
    ///
    /// # Panics
    ///
    /// If this is called on a server.
    pub fn take_resumption_token(&mut self, now: Instant) -> Option<ResumptionToken> {
        assert_eq!(self.role, Role::Client);

        self.crypto.has_resumption_token().then(|| {
            let token = self.make_resumption_token();
            if self.crypto.has_resumption_token() {
                self.release_resumption_token_timer = Some(now + 3 * self.pto());
            }
            token
        })
    }

    /// Enable resumption, using a token previously provided.
    /// This can only be called once and only on the client.
    /// After calling the function, it should be possible to attempt 0-RTT
    /// if the token supports that.
    ///
    /// This function starts the TLS stack, which means that any configuration change
    /// to that stack needs to occur prior to calling this.
    ///
    /// # Errors
    /// When the operation fails, which is usually due to bad inputs or bad connection state.
    pub fn enable_resumption<A: AsRef<[u8]>>(&mut self, now: Instant, token: A) -> Res<()> {
        if self.state != State::Init {
            error!("[{self}] set token in state {:?}", self.state);
            return Err(Error::ConnectionState);
        }
        if self.role == Role::Server {
            return Err(Error::ConnectionState);
        }

        info!(
            "[{self}] resumption token {}",
            HexSnipMiddle::new(token.as_ref())
        );
        let mut dec = Decoder::from(token.as_ref());

        let version = Version::try_from(
            dec.decode_uint::<version::Wire>()
                .ok_or(Error::InvalidResumptionToken)?,
        )?;
        trace!("[{self}]   version {version:?}");
        if !self.conn_params.get_versions().all().contains(&version) {
            return Err(Error::DisabledVersion);
        }

        let rtt = Duration::from_millis(dec.decode_varint().ok_or(Error::InvalidResumptionToken)?);
        trace!("[{self}]   RTT {rtt:?}");

        let tp_slice = dec.decode_vvec().ok_or(Error::InvalidResumptionToken)?;
        trace!("[{self}]   transport parameters {}", Hex::new(tp_slice));
        let mut dec_tp = Decoder::from(tp_slice);
        let tp = TransportParameters::decode(Role::Client, &mut dec_tp)
            .map_err(|_| Error::InvalidResumptionToken)?;

        let init_token = dec.decode_vvec().ok_or(Error::InvalidResumptionToken)?;
        trace!("[{self}]   Initial token {}", Hex::new(init_token));

        let tok = dec.decode_remainder();
        trace!("[{self}]   TLS token {}", Hex::new(tok));

        match self.crypto.tls_mut() {
            Agent::Client(c) => {
                let res = c.enable_resumption(tok);
                if let Err(e) = res {
                    self.absorb_error::<Error>(now, Err(Error::from(e)));
                    return Ok(());
                }
            }
            Agent::Server(_) => return Err(Error::WrongRole),
        }

        self.version = version;
        self.crypto.set_version(version);
        self.conn_params.get_versions_mut().set_initial(version);
        self.tps.borrow_mut().set_version(version);
        self.tps.borrow_mut().set_remote_0rtt(Some(tp));
        if !init_token.is_empty() {
            self.address_validation = AddressValidationInfo::NewToken(init_token.to_vec());
        }
        self.paths
            .primary()
            .ok_or(Error::Internal)?
            .borrow_mut()
            .rtt_mut()
            .set_initial(rtt);
        self.set_initial_limits();
        // Start up TLS, which has the effect of setting up all the necessary
        // state for 0-RTT.  This only stages the CRYPTO frames.
        let res = self.client_start(now);
        self.absorb_error(now, res);
        Ok(())
    }

    pub(crate) fn set_validation(&mut self, validation: &Rc<RefCell<AddressValidation>>) {
        trace!("[{self}] Enabling NEW_TOKEN");
        assert_eq!(self.role, Role::Server);
        self.address_validation = AddressValidationInfo::Server(Rc::downgrade(validation));
    }

    /// Send a TLS session ticket AND a `NEW_TOKEN` frame (if possible).
    /// # Errors
    /// When the operation fails, which is usually due to bad inputs or bad connection state.
    pub fn send_ticket(&mut self, now: Instant, extra: &[u8]) -> Res<()> {
        if self.role == Role::Client {
            return Err(Error::WrongRole);
        }

        let tps = &self.tps;
        if let Agent::Server(s) = self.crypto.tls_mut() {
            let mut enc = Encoder::default();
            enc.encode_vvec_with(|enc_inner| {
                tps.borrow().local().encode(enc_inner);
            });
            enc.encode(extra);
            let records = s.send_ticket(now, enc.as_ref())?;
            debug!("[{self}] send session ticket {}", Hex::new(&enc));
            self.crypto.buffer_records(records)?;
        } else {
            unreachable!();
        }

        // If we are able, also send a NEW_TOKEN frame.
        // This should be recording all remote addresses that are valid,
        // but there are just 0 or 1 in the current implementation.
        match self.paths.primary() {
            Some(path) => {
                if path.borrow().is_valid()
                    && let Some(token) = self
                        .address_validation
                        .generate_new_token(path.borrow().remote_address(), now)
                {
                    self.new_token.send_new_token(token);
                }
                Ok(())
            }
            None => Err(Error::NotConnected),
        }
    }

    #[must_use]
    pub fn tls_info(&self) -> Option<&SecretAgentInfo> {
        self.crypto.tls().info()
    }

    /// # Errors
    /// When there is no information to obtain.
    pub fn tls_preinfo(&self) -> Res<SecretAgentPreInfo> {
        Ok(self.crypto.tls().preinfo()?)
    }

    /// Get the peer's certificate chain and other info.
    #[must_use]
    pub fn peer_certificate(&self) -> Option<CertificateInfo> {
        self.crypto.tls().peer_certificate()
    }

    /// Export keying material per RFC 8446 §7.5.
    ///
    /// `label` is the TLS exporter label, not the WebTransport application label.
    /// This can only be called after the handshake is complete.
    /// Per RFC 8446 §7.5, labels SHOULD begin with `"EXPORTER-"`.
    ///
    /// # Errors
    ///
    /// Returns error if connection is not in a connected or closing state,
    /// or export fails.
    pub fn export_keying_material(&self, label: &str, context: &[u8], out: &mut [u8]) -> Res<()> {
        if !(self.state.connected() || self.state.closing()) {
            return Err(Error::NotConnected);
        }
        if out.is_empty() || label.is_empty() {
            return Err(Error::InvalidInput);
        }
        self.crypto
            .tls()
            .export_keying_material(label.as_bytes(), context, out)
            .map_err(Into::into)
    }

    /// Call by application when the peer cert has been verified.
    ///
    /// This panics if there is no active peer.  It's OK to call this
    /// when authentication isn't needed, that will likely only cause
    /// the connection to fail.  However, if no packets have been
    /// exchanged, it's not OK.
    pub fn authenticated(&mut self, status: AuthenticationStatus, now: Instant) {
        debug!("[{self}] Authenticated {status:?}");
        self.crypto.tls_mut().authenticated(status);
        let res = self.handshake(now, self.version, PacketNumberSpace::Handshake, None);
        self.absorb_error(now, res);
        self.process_saved(now);
    }

    /// Get the role of the connection.
    #[must_use]
    pub const fn role(&self) -> Role {
        self.role
    }

    /// Get the state of the connection.
    #[must_use]
    pub const fn state(&self) -> &State {
        &self.state
    }

    /// The QUIC version in use.
    #[must_use]
    pub const fn version(&self) -> Version {
        self.version
    }

    /// Get the 0-RTT state of the connection.
    #[must_use]
    pub const fn zero_rtt_state(&self) -> ZeroRttState {
        self.zero_rtt_state
    }

    /// Get a snapshot of collected statistics.
    #[must_use]
    pub fn stats(&self) -> Stats {
        let mut v = self.stats.borrow().clone();
        v.version = self.version;
        if let Some(p) = self.paths.primary() {
            p.borrow().update_stats(&mut v);
        }
        self.streams.update_stats(&mut v);
        v
    }

    // This function wraps a call to another function and sets the connection state
    // properly if that call fails.
    fn capture_error<T>(
        &mut self,
        path: Option<PathRef>,
        now: Instant,
        frame_type: FrameType,
        res: Res<T>,
    ) -> Res<T> {
        if let Err(v) = &res {
            #[cfg(debug_assertions)]
            let msg = format!("{v:?}");
            #[cfg(not(debug_assertions))]
            let msg = "";
            let error = CloseReason::Transport(v.clone());
            match &self.state {
                State::Closing { error: err, .. }
                | State::Draining { error: err, .. }
                | State::Closed(err) => {
                    warn!("[{self}] Closing again after error {err:?}");
                }
                State::Init => {
                    // We have not even sent anything just close the connection without sending any
                    // error. This may happen when client_start fails.
                    self.set_state(State::Closed(error), now);
                }
                State::WaitInitial | State::WaitVersion => {
                    // We don't have any state yet, so don't bother with
                    // the closing state, just send one CONNECTION_CLOSE.
                    if let Some(path) = path.or_else(|| self.paths.primary()) {
                        self.state_signaling
                            .close(path, error.clone(), frame_type, msg);
                    }
                    self.set_state(State::Closed(error), now);
                }
                _ => match path.or_else(|| self.paths.primary()) {
                    Some(path) => {
                        self.state_signaling
                            .close(path, error.clone(), frame_type, msg);
                        if matches!(v, Error::KeysExhausted) {
                            self.set_state(State::Closed(error), now);
                        } else {
                            self.set_state(
                                State::Closing {
                                    error,
                                    timeout: self.get_closing_period_time(now),
                                },
                                now,
                            );
                        }
                    }
                    None => {
                        self.set_state(State::Closed(error), now);
                    }
                },
            }
        }
        res
    }

    /// For use with `process_input()`. Errors there can be ignored, but this
    /// needs to ensure that the state is updated.
    fn absorb_error<T>(&mut self, now: Instant, res: Res<T>) -> Option<T> {
        self.capture_error(None, now, FrameType::Padding, res).ok()
    }

    fn process_timer(&mut self, now: Instant) {
        match &self.state {
            // Only the client runs timers while waiting for Initial packets.
            State::WaitInitial => debug_assert_eq!(self.role, Role::Client),
            // If Closing or Draining, check if it is time to move to Closed.
            State::Closing { error, timeout } | State::Draining { error, timeout }
                if *timeout <= now =>
            {
                let st = State::Closed(error.clone());
                self.set_state(st, now);
                info!("Closing timer expired");
                return;
            }
            State::Closed(_) => {
                debug!("Timer fired while closed");
                return;
            }
            _ => (),
        }

        let pto = self.pto();
        if self.idle_timeout.expired(now, pto) {
            info!("[{self}] idle timeout expired");
            self.set_state(
                State::Closed(CloseReason::Transport(Error::IdleTimeout)),
                now,
            );
            return;
        }

        if self.state.closing() {
            trace!("[{self}] Closing, not processing other timers");
            return;
        }

        self.streams.cleanup_closed_streams();

        // Not gated on anything else needing to happen: expiry is not a
        // send, so stale datagrams must not wait on some other timer to
        // also be due.
        let min_rtt = self.min_rtt();
        self.quic_datagrams.expire_datagrams(now, min_rtt);

        let res = self.crypto.states_mut().check_key_update(now);
        self.absorb_error(now, res);

        if let Some(path) = self.paths.primary() {
            let lost = self
                .loss_recovery
                .timeout(&path, now, self.crypto.has_handshake_keys());
            self.handle_lost_packets(&lost);
            qlog::packets_lost(&mut self.qlog, &lost, now);
        }

        // Declare the connection broken if too many consecutive PTOs have gone
        // unacknowledged, i.e. the path is a black hole. This closes the connection
        // sooner than, and with a distinct reason from, the idle timeout.
        //
        // We only do this once connected, i.e. after the handshake completes. A
        // black hole during the handshake is the job of happy eyeballs, which races
        // multiple connection attempts and does not need us to give up on any single
        // one early. Requiring `connected()` also guarantees we have an RTT sample,
        // so the PTO backoff we are counting is based on a real estimate.
        if let Some(max_pto) = self.conn_params.get_max_pto()
            && self.state.connected()
            && self.loss_recovery.pto_count() >= max_pto.get()
        {
            info!("[{self}] {max_pto} consecutive PTOs, declaring connection broken");
            self.set_state(
                State::Closed(CloseReason::Transport(Error::TooManyPtos)),
                now,
            );
            return;
        }

        if self.release_resumption_token_timer.is_some() {
            self.create_resumption_token(now);
        }

        if !self
            .paths
            .process_timeout(now, pto, &mut self.stats.borrow_mut())
        {
            info!("[{self}] last available path failed");
            self.absorb_error::<Error>(now, Err(Error::NoAvailablePath));
        }
    }

    /// Whether the given [`ConnectionIdRef`] is a valid local [`ConnectionId`].
    #[must_use]
    pub fn is_valid_local_cid(&self, cid: ConnectionIdRef) -> bool {
        self.cid_manager.is_valid(cid)
    }

    /// Process a new input datagram on the connection.
    pub fn process_input<A: AsRef<[u8]> + AsMut<[u8]>>(&mut self, d: Datagram<A>, now: Instant) {
        self.process_multiple_input(iter::once(d), now);
    }

    /// Process new input datagrams on the connection.
    pub fn process_multiple_input<
        A: AsRef<[u8]> + AsMut<[u8]>,
        I: IntoIterator<Item = Datagram<A>>,
    >(
        &mut self,
        dgrams: I,
        now: Instant,
    ) {
        let mut dgrams = dgrams.into_iter().peekable();
        if dgrams.peek().is_none() {
            return;
        }

        // Snapshot timer type before ACKs can alter loss state.
        if let Some(path) = self.paths.primary() {
            self.loss_recovery.note_timeout_type(&path.borrow(), now);
        }
        for d in dgrams {
            self.input(d, now, now);
        }
        self.process_saved(now);
        self.streams.cleanup_closed_streams();
    }

    /// Get the time that we next need to be called back, relative to `now`.
    fn next_delay(&mut self, now: Instant, paced: bool) -> Duration {
        trace!("[{self}] Get callback delay {now:?}");

        // Only one timer matters when closing...
        if let State::Closing { timeout, .. } | State::Draining { timeout, .. } = self.state {
            self.hrtime.update(Self::LOOSE_TIMER_RESOLUTION);
            return timeout.duration_since(now);
        }

        let mut delays = SmallVec::<[_; 7]>::new();
        // The anti-amplification limit can hold back an ACK on an unvalidated path.
        let ack_blocked =
            |t: &Instant| *t <= now && self.paths.primary().is_some_and(|p| !p.borrow().is_valid());
        if let Some(ack_time) = self.acks.ack_time(now).filter(|t| !ack_blocked(t)) {
            trace!("[{self}] Delayed ACK timer {ack_time:?}");
            delays.push(ack_time);
        }

        if let Some(p) = self.paths.primary() {
            let path = p.borrow();
            let rtt = path.rtt();
            let pto = rtt.pto(self.confirmed());

            let idle_time = self.idle_timeout.expiry(now, pto);
            trace!("[{self}] Idle timer {idle_time:?}");
            delays.push(idle_time);

            if self.streams.need_keep_alive()
                && let Some(keep_alive_time) = self.idle_timeout.next_keep_alive(now, pto)
            {
                trace!("[{self}] Keep alive timer {keep_alive_time:?}");
                delays.push(keep_alive_time);
            }

            if let Some(lr_time) = self.loss_recovery.next_timeout(&path) {
                trace!("[{self}] Loss recovery timer {lr_time:?}");
                delays.push(lr_time);
            }

            if paced && let Some(pace_time) = path.sender().next_paced(rtt.estimate()) {
                trace!("[{self}] Pacing timer {pace_time:?}");
                delays.push(pace_time);
            }

            if let Some(path_time) = self.paths.next_timeout(pto) {
                trace!("[{self}] Path probe timer {path_time:?}");
                delays.push(path_time);
            }
        }

        if let Some(key_update_time) = self.crypto.states().update_time() {
            trace!("[{self}] Key update timer {key_update_time:?}");
            delays.push(key_update_time);
        }

        if let Some(dgram_time) = self.quic_datagrams.next_datagram_expiry(self.min_rtt()) {
            trace!("[{self}] Datagram expiry timer {dgram_time:?}");
            delays.push(dgram_time);
        }

        // `release_resumption_token_timer` is not considered here, because
        // it is not important enough to force the application to set a
        // timeout for it  It is expected that other activities will
        // drive it.

        let earliest = delays.into_iter().min().expect("at least one delay");
        // TODO(agrover, mt) - need to analyze and fix #47
        // rather than just clamping to zero here.
        debug_assert!(earliest > now);
        let delay = earliest.saturating_duration_since(now);
        debug!("[{self}] delay duration {delay:?}");
        self.hrtime.update(delay / 4);
        delay
    }

    /// Wrapper around [`Connection::process_multiple_output`] that processes a
    /// single output datagram only.
    #[expect(clippy::missing_panics_doc, reason = "see expect()")]
    #[must_use = "Output of the process_output function must be handled"]
    pub fn process_output(&mut self, now: Instant) -> Output {
        self.process_multiple_output(now, NonZeroUsize::MIN)
            .try_into()
            .expect("max_datagrams is 1")
    }

    /// Get output packets, as a result of receiving packets, or actions taken
    /// by the application.
    /// Returns datagrams to send, and how long to wait before calling again
    /// even if no incoming packets.
    #[must_use = "OutputBatch of the process_multiple_output function must be handled"]
    pub fn process_multiple_output(
        &mut self,
        now: Instant,
        max_datagrams: NonZeroUsize,
    ) -> OutputBatch {
        trace!("[{self}] process_output {:?} {now:?}", self.state);

        match (&self.state, self.role) {
            (State::Init, Role::Client) => {
                let res = self.client_start(now);
                self.absorb_error(now, res);
            }
            (State::Init | State::WaitInitial, Role::Server) => {
                return OutputBatch::None;
            }
            _ => {
                self.process_timer(now);
            }
        }

        match self.output(now, max_datagrams) {
            SendOptionBatch::Yes(dgram) => OutputBatch::DatagramBatch(dgram),
            SendOptionBatch::No(paced) => match self.state {
                State::Init | State::Closed(_) => OutputBatch::None,
                State::Closing { timeout, .. } | State::Draining { timeout, .. } => {
                    OutputBatch::Callback(timeout.duration_since(now))
                }
                _ => OutputBatch::Callback(self.next_delay(now, paced)),
            },
        }
    }

    /// A test-only output function that uses the provided writer to
    /// pack something extra into the output.
    #[cfg(any(test, feature = "build-fuzzing-corpus"))]
    pub fn test_write_frames<W>(&mut self, writer: W, now: Instant) -> Output
    where
        W: test_internal::FrameWriter + 'static,
    {
        self.test_frame_writer = Some(Box::new(writer));
        let res = self.process_output(now);
        self.test_frame_writer = None;
        res
    }

    /// Wrapper around [`Connection::process_multiple`], processing a single
    /// input and single output datagram only.
    #[expect(clippy::missing_panics_doc, reason = "see expect()")]
    #[must_use = "Output of the process function must be handled"]
    pub fn process<A: AsRef<[u8]> + AsMut<[u8]>>(
        &mut self,
        dgram: Option<Datagram<A>>,
        now: Instant,
    ) -> Output {
        self.process_multiple(dgram, now, NonZeroUsize::MIN)
            .try_into()
            .expect("max_datagrams is 1")
    }

    /// Process input and generate output.
    #[must_use = "OutputBatch of the process_multiple function must be handled"]
    pub fn process_multiple<A: AsRef<[u8]> + AsMut<[u8]>>(
        &mut self,
        dgram: Option<Datagram<A>>,
        now: Instant,
        max_datagrams: NonZeroUsize,
    ) -> OutputBatch {
        if let Some(d) = dgram {
            // Snapshot timer type before ACKs can alter loss state.
            if let Some(path) = self.paths.primary() {
                self.loss_recovery.note_timeout_type(&path.borrow(), now);
            }
            self.input(d, now, now);
            self.process_saved(now);
        }
        let output = self.process_multiple_output(now, max_datagrams);
        #[cfg(feature = "build-fuzzing-corpus")]
        if self.test_frame_writer.is_none()
            && let OutputBatch::DatagramBatch(batch) = &output
        {
            for dgram in batch.iter() {
                neqo_common::write_item_to_fuzzing_corpus("packet", &dgram);
            }
        }
        output
    }

    fn handle_retry(&mut self, packet: &packet::Public, now: Instant) -> Res<()> {
        info!("[{self}] received Retry");
        if matches!(self.address_validation, AddressValidationInfo::Retry { .. }) {
            self.stats.borrow_mut().pkt_dropped("Extra Retry");
            return Ok(());
        }
        if packet.token().is_empty() {
            self.stats.borrow_mut().pkt_dropped("Retry without a token");
            return Ok(());
        }
        let odcid = self
            .original_destination_cid
            .as_ref()
            .ok_or(Error::InvalidRetry)?;
        if !packet.is_valid_retry(odcid) {
            self.stats
                .borrow_mut()
                .pkt_dropped("Retry with bad integrity tag");
            return Ok(());
        }
        // RFC 9000, Section 17.2.5.2: a client MUST discard a Retry packet that
        // carries a Source Connection ID identical to the Destination Connection ID
        // of its Initial. The Retry integrity key is public, so this comparison is
        // one of the few checks that constrains an off-path injected Retry.
        if packet.scid() == *odcid {
            self.stats
                .borrow_mut()
                .pkt_dropped("Retry with SCID matching our Initial DCID");
            return Ok(());
        }
        // At this point, we should only have the connection ID that we generated.
        // Update to the one that the server prefers.
        let Some(path) = self.paths.primary() else {
            self.stats
                .borrow_mut()
                .pkt_dropped("Retry without an existing path");
            return Ok(());
        };

        path.borrow_mut().set_remote_cid(packet.scid());

        let retry_scid = ConnectionId::from(packet.scid());
        info!(
            "[{self}] Valid Retry received, token={} scid={retry_scid}",
            Hex::new(packet.token())
        );

        let lost_packets = self.loss_recovery.retry(&path, now);
        self.handle_lost_packets(&lost_packets);

        self.crypto.reset_early_resend();
        self.crypto.states_mut().init(
            self.conn_params.get_versions().compatible(),
            self.role,
            &retry_scid,
            false, // don't randomize on Retry
        )?;
        self.address_validation = AddressValidationInfo::Retry {
            token: packet.token().to_vec(),
            retry_source_cid: retry_scid,
        };
        Ok(())
    }

    fn discard_keys(&mut self, space: PacketNumberSpace, now: Instant) {
        if self.crypto.discard(space) {
            debug!("[{self}] Drop packet number space {space}");
            if let Some(path) = self.paths.primary() {
                self.loss_recovery.discard(&path, space, now);
            }
            self.acks.drop_space(space);
        }
    }

    fn is_stateless_reset(&self, path: &PathRef, d: &[u8]) -> bool {
        // If the connection is connected, then the reset token will be invalid.
        if !self.state.connected() {
            return false;
        }
        // If the datagram is too small, don't try.
        d.last_chunk()
            .is_some_and(|token| path.borrow().is_stateless_reset(&Srt::new(*token)))
    }

    fn check_stateless_reset(
        &mut self,
        path: &PathRef,
        d: &[u8],
        first: bool,
        now: Instant,
    ) -> Res<()> {
        if first && self.is_stateless_reset(path, d) {
            // Failing to process a packet in a datagram might
            // indicate that there is a stateless reset present.
            debug!(
                "[{self}] Stateless reset: {}",
                Hex::new(&d[d.len() - Srt::LEN..])
            );
            self.state_signaling.reset();
            self.set_state(
                State::Draining {
                    error: CloseReason::Transport(Error::StatelessReset),
                    timeout: self.get_closing_period_time(now),
                },
                now,
            );
            Err(Error::StatelessReset)
        } else {
            Ok(())
        }
    }

    /// Process any saved datagrams that might be available for processing.
    fn process_saved(&mut self, now: Instant) {
        while let Some(epoch) = self.saved_datagrams.available() {
            debug!("[{self}] process saved for epoch {epoch:?}");
            debug_assert!(
                self.crypto
                    .states_mut()
                    .rx_hp(self.version, epoch)
                    .is_some()
            );
            for saved in self.saved_datagrams.take_saved() {
                trace!("[{self}] input saved @{:?}: {:?}", saved.t, saved.d);
                self.input(saved.d, saved.t, now);
            }
        }
    }

    /// In case a datagram arrives that we can only partially process, save any
    /// part that we don't have keys for.
    #[expect(
        clippy::needless_pass_by_value,
        reason = "To consume an owned datagram below."
    )]
    fn save_datagram(
        &mut self,
        epoch: Epoch,
        d: Datagram<impl AsRef<[u8]>>,
        remaining: usize,
        now: Instant,
    ) {
        let d = Datagram::new(
            d.source(),
            d.destination(),
            d.tos(),
            d[d.len() - remaining..].to_vec(),
        );
        self.saved_datagrams.save(epoch, d, now);
        self.stats.borrow_mut().saved_datagrams += 1;
        // We already counted the datagram as received in [`input_path`]. We
        // will do so again when we (re-)process it, so reduce the count now.
        self.stats.borrow_mut().packets_rx -= 1;
    }

    /// Perform version negotiation.
    fn version_negotiation(&mut self, supported: &[version::Wire], now: Instant) -> Res<()> {
        debug_assert_eq!(self.role, Role::Client);

        if let Some(version) = self.conn_params.get_versions().preferred(supported) {
            assert_ne!(self.version, version);

            info!("[{self}] Version negotiation: trying {version:?}");
            let path = self.paths.primary().ok_or(Error::NoAvailablePath)?;
            let local_addr = path.borrow().local_address();
            let remote_addr = path.borrow().remote_address();
            let conn_params = self
                .conn_params
                .clone()
                .versions(version, self.conn_params.get_versions().all().to_vec());
            let mut c = Self::new_client(
                self.crypto.server_name().ok_or(Error::VersionNegotiation)?,
                self.crypto.protocols(),
                self.cid_manager.generator(),
                local_addr,
                remote_addr,
                conn_params,
                now,
            )?;
            c.conn_params
                .get_versions_mut()
                .set_initial(self.conn_params.get_versions().initial());
            mem::swap(self, &mut c);
            qlog::client_version_information_negotiated(
                &mut self.qlog,
                self.conn_params.get_versions().all(),
                supported,
                version,
                now,
            );
            Ok(())
        } else {
            info!("[{self}] Version negotiation: failed with {supported:?}");
            // This error goes straight to closed.
            self.set_state(
                State::Closed(CloseReason::Transport(Error::VersionNegotiation)),
                now,
            );
            Err(Error::VersionNegotiation)
        }
    }

    /// Perform any processing that we might have to do on packets prior to
    /// attempting to remove protection.
    #[expect(clippy::too_many_lines, reason = "Yeah, it's a work in progress.")]
    fn preprocess_packet(
        &mut self,
        packet: &packet::Public,
        path: &PathRef,
        dcid: Option<&ConnectionId>,
        now: Instant,
    ) -> Res<PreprocessResult> {
        if dcid.is_some_and(|d| d != &packet.dcid()) {
            self.stats
                .borrow_mut()
                .pkt_dropped("Coalesced packet has different DCID");
            return Ok(PreprocessResult::Next);
        }

        match (packet.packet_type(), &self.state, &self.role) {
            // RFC 9000, Section 17.2.2: a server MUST set the Token Length field of an
            // Initial to 0, and a client that receives an Initial with a non-zero Token
            // Length MUST discard the packet or close with PROTOCOL_VIOLATION. Discarding
            // is preferred here: the token length sits in the unprotected header, so an
            // off-path injection must not be able to tear down the connection.
            (packet::Type::Initial, _, Role::Client) if !packet.token().is_empty() => {
                self.stats
                    .borrow_mut()
                    .pkt_dropped("Client received an Initial with a token");
                return Ok(PreprocessResult::Next);
            }
            (packet::Type::Initial, State::Init, Role::Server) => {
                let version = packet.version().ok_or(Error::ProtocolViolation)?;
                if !packet.is_valid_initial()
                    || !self.conn_params.get_versions().all().contains(&version)
                {
                    self.stats.borrow_mut().pkt_dropped("Invalid Initial");
                    return Ok(PreprocessResult::Next);
                }
                info!(
                    "[{self}] Received valid Initial packet with scid {:?} dcid {:?}",
                    packet.scid(),
                    packet.dcid()
                );
                // Record the client's selected CID so that it can be accepted until
                // the client starts using a real connection ID.
                let dcid = ConnectionId::from(packet.dcid());
                self.crypto.states_mut().init_server(
                    version,
                    &dcid,
                    self.conn_params.randomize_first_pn_enabled(),
                )?;
                self.original_destination_cid = Some(dcid);
                self.set_state(State::WaitInitial, now);

                // We need to make sure that we set this transport parameter.
                // This has to happen prior to processing the packet so that
                // the TLS handshake has all it needs.
                if !self.retry_sent() {
                    self.tps
                        .borrow_mut()
                        .local_mut()
                        .set_bytes(OriginalDestinationConnectionId, packet.dcid().to_vec());
                }
            }
            (packet::Type::VersionNegotiation, State::WaitInitial, Role::Client) => {
                if let Ok(versions) = packet.supported_versions() {
                    if versions.is_empty()
                        || versions.contains(&self.version().wire_version())
                        || versions.contains(&0)
                        || &packet.scid() != self.odcid().ok_or(Error::Internal)?
                        || matches!(self.address_validation, AddressValidationInfo::Retry { .. })
                    {
                        // Ignore VersionNegotiation packets that contain the current version.
                        // Or don't have the right connection ID.
                        // Or are received after a Retry.
                        self.stats.borrow_mut().pkt_dropped("Invalid VN");
                    } else {
                        self.version_negotiation(&versions, now)?;
                    }
                } else {
                    self.stats.borrow_mut().pkt_dropped("VN with no versions");
                }
                return Ok(PreprocessResult::End);
            }
            (packet::Type::Retry, State::WaitInitial, Role::Client) => {
                self.handle_retry(packet, now)?;
                return Ok(PreprocessResult::Next);
            }
            (packet::Type::Handshake | packet::Type::Short, State::WaitInitial, Role::Client)
                // Can't be processed yet, but hints that our Initial was lost: resend CRYPTO early
                // (Section 6.2.3 of RFC 9002) and mark all 0-RTT data as lost.
                if dcid.is_none()
                    && self.cid_manager.is_valid(packet.dcid())
                    && !self.saved_datagrams.is_either_full()
                => {
                    self.crypto.resend_unacked_early();
                    self.resend_0rtt(now);
                }
            (
                packet::Type::VersionNegotiation | packet::Type::Retry | packet::Type::OtherVersion,
                ..,
            ) => {
                self.stats
                    .borrow_mut()
                    .pkt_dropped(format!("{:?}", packet.packet_type()));
                return Ok(PreprocessResult::Next);
            }
            _ => {}
        }

        let res = match self.state {
            State::Init => {
                self.stats
                    .borrow_mut()
                    .pkt_dropped("Received while in Init state");
                PreprocessResult::Next
            }
            State::WaitInitial => PreprocessResult::Continue,
            State::WaitVersion | State::Handshaking | State::Connected | State::Confirmed => {
                if self.cid_manager.is_valid(packet.dcid()) {
                    if self.role == Role::Server && packet.packet_type() == packet::Type::Handshake
                    {
                        // Server has received a Handshake packet -> discard Initial keys and states
                        self.discard_keys(PacketNumberSpace::Initial, now);
                    }
                    PreprocessResult::Continue
                } else {
                    self.stats
                        .borrow_mut()
                        .pkt_dropped(format!("Invalid DCID {:?}", packet.dcid()));
                    PreprocessResult::Next
                }
            }
            State::Closing { .. } => {
                // Don't bother processing the packet. Instead ask to get a
                // new close frame.
                //
                // > In the closing state, an endpoint retains only enough
                // > information to generate a packet containing a
                // > CONNECTION_CLOSE frame and to identify packets as belonging
                // > to the connection. An endpoint in the closing state sends a
                // > packet containing a CONNECTION_CLOSE frame in response to any
                // > incoming packet that it attributes to the connection.
                //
                // <https://www.rfc-editor.org/rfc/rfc9000.html#section-10.2.1-2>
                if self.cid_manager.is_valid(packet.dcid()) && !self.paths.is_temporary(path) {
                    self.state_signaling.send_close();
                } else {
                    self.stats
                        .borrow_mut()
                        .pkt_dropped("Unattributed packet while closing");
                }
                PreprocessResult::Next
            }
            State::Draining { .. } | State::Closed(..) => {
                // Do nothing.
                self.stats
                    .borrow_mut()
                    .pkt_dropped(format!("State {:?}", self.state));
                PreprocessResult::Next
            }
        };
        Ok(res)
    }

    /// After a Initial, Handshake, `ZeroRtt`, or Short packet is successfully processed.
    fn postprocess_packet(
        &mut self,
        path: &PathRef,
        tos: Tos,
        remote: SocketAddr,
        packet: &packet::Decrypted,
        migrate: bool,
        now: Instant,
    ) {
        let ecn_mark = Ecn::from(tos);
        let mut stats = self.stats.borrow_mut();
        stats.ecn_rx[packet.packet_type()] += ecn_mark;
        if let Some(last_ecn_mark) = stats.ecn_last_mark.filter(|&last_ecn_mark| {
            last_ecn_mark != ecn_mark && stats.ecn_rx_transition[last_ecn_mark][ecn_mark].is_none()
        }) {
            stats.ecn_rx_transition[last_ecn_mark][ecn_mark] =
                Some((packet.packet_type(), packet.pn()));
        }

        stats.ecn_last_mark = Some(ecn_mark);
        drop(stats);
        let space = PacketNumberSpace::from(packet.packet_type());
        if let Some(space) = self.acks.get_mut(space) {
            *space.ecn_marks() += ecn_mark;
        } else {
            trace!("Not tracking ECN for dropped packet number space");
        }

        if self.state == State::WaitInitial {
            self.start_handshake(path, packet, now);
        }

        if matches!(self.state, State::WaitInitial | State::WaitVersion) {
            let new_state = if self.has_version() {
                State::Handshaking
            } else {
                State::WaitVersion
            };
            self.set_state(new_state, now);
            if self.role == Role::Server && self.state == State::Handshaking {
                self.zero_rtt_state = if self.crypto.enable_0rtt(self.version) == Ok(true) {
                    debug!("[{self}] Accepted 0-RTT");
                    ZeroRttState::AcceptedServer
                } else {
                    ZeroRttState::Rejected
                };
            }
        }

        if self.state.connected() {
            self.handle_migration(path, remote, migrate, now);
        } else if self.validates_path(packet) {
            // We only allow one path during setup, so apply handshake
            // path validation to this path.
            path.borrow_mut().set_valid(now);
        }

        // Update SCONE signal.
        if let Some(rate) = path.borrow_mut().update_scone(now, packet.scone()) {
            debug!("[{self}] SCONE rate updated to {rate:x?}");
            self.events.scone_updated(rate);
        }
    }

    /// Take a datagram as input.  This reports an error if the packet was bad.
    /// This takes two times: when the datagram was received, and the current time.
    fn input(
        &mut self,
        d: Datagram<impl AsRef<[u8]> + AsMut<[u8]>>,
        received: Instant,
        now: Instant,
    ) {
        if self.state < State::Confirmed
            && self
                .paths
                .primary()
                .is_some_and(|p| !p.borrow().received_on(d.destination(), d.source()))
        {
            self.stats
                .borrow_mut()
                .pkt_dropped("Address changed during handshake");
            return;
        }
        // First determine the path.
        let path = self
            .paths
            .find_path(d.destination(), d.source(), &self.conn_params, now);
        path.borrow_mut().add_received(d.len());
        let res = self.input_path(&path, d, received);
        _ = self.capture_error(Some(path), now, FrameType::Padding, res);
    }

    fn input_path(
        &mut self,
        path: &PathRef,
        mut d: Datagram<impl AsRef<[u8]> + AsMut<[u8]>>,
        now: Instant,
    ) -> Res<()> {
        trace!("[{self}] {} input {}", path.borrow(), Hex::new(&d));
        let tos = d.tos();
        let remote = d.source();
        let mut slc = d.as_mut();
        self.stats.borrow_mut().bytes_rx += slc.len();
        let mut dcid = None;
        let pto = path.borrow().rtt().pto(self.confirmed());

        // Handle each packet in the datagram.
        while !slc.is_empty() {
            self.stats.borrow_mut().packets_rx += 1;
            self.stats.borrow_mut().dscp_rx[tos.into()] += 1;
            let slc_len = slc.len();
            let (packet, remainder) =
                match packet::Public::decode(slc, &*self.cid_manager.decoder()) {
                    Ok((packet, remainder)) => {
                        #[cfg(feature = "build-fuzzing-corpus")]
                        neqo_common::write_item_to_fuzzing_corpus("packet", packet.data());
                        (packet, remainder)
                    }
                    Err(e) => {
                        info!("[{self}] Garbage packet: {e}");
                        self.stats.borrow_mut().pkt_dropped("Garbage packet");
                        break;
                    }
                };
            match self.preprocess_packet(&packet, path, dcid.as_ref(), now)? {
                PreprocessResult::Continue => (),
                PreprocessResult::Next => break,
                PreprocessResult::End => return Ok(()),
            }

            trace!("[{self}] Received unverified packet {packet:?}");

            let packet_len = packet.len();
            match packet.decrypt(self.crypto.states_mut(), now + pto) {
                Ok(payload) => {
                    // OK, we have a valid packet.
                    let pn = payload.pn();
                    self.log_packet(
                        packet::MetaData::new_in(path, tos, packet_len, &payload, self.version),
                        now,
                    );

                    #[cfg(feature = "build-fuzzing-corpus")]
                    if payload.packet_type() == packet::Type::Initial {
                        let target = if self.role == Role::Client {
                            "server_initial"
                        } else {
                            "client_initial"
                        };
                        neqo_common::write_item_to_fuzzing_corpus(target, &payload[..]);
                    }

                    let space = PacketNumberSpace::from(payload.packet_type());
                    if let Some(space) = self.acks.get_mut(space) {
                        if space.is_duplicate(pn) {
                            debug!("Duplicate packet {space}-{pn}");
                            self.stats.borrow_mut().dups_rx += 1;
                        } else {
                            match self.process_packet(path, tos, &payload, now) {
                                Ok(migrate) => {
                                    self.idle_timeout.on_packet_received(now);
                                    self.postprocess_packet(
                                        path, tos, remote, &payload, migrate, now,
                                    );
                                }
                                Err(e) => {
                                    self.ensure_error_path(path, &payload, now);
                                    return Err(e);
                                }
                            }
                        }
                    } else {
                        debug!(
                            "[{self}] Received packet {space} for untracked space {}",
                            payload.pn()
                        );
                        return Err(Error::ProtocolViolation);
                    }
                    dcid = Some(ConnectionId::from(payload.dcid()));
                }
                Err(e) => {
                    match e.error {
                        Error::KeysPending(epoch) => {
                            // This packet can't be decrypted because we don't have the keys yet.
                            // Don't check this packet for a stateless reset, just return.
                            let remaining = slc_len;
                            self.save_datagram(epoch, d, remaining, now);
                            return Ok(());
                        }
                        // Exhausting read keys is fatal. So is a packet that
                        // authenticated but broke the protocol (e.g. reserved
                        // bits set), which closes the connection.
                        Error::KeysExhausted | Error::ProtocolViolation => {
                            return Err(e.error);
                        }
                        Error::KeysDiscarded(epoch) => self.handle_keys_discarded(epoch),
                        _ => (),
                    }
                    // Decryption failure, or not having keys is not fatal.
                    // If the state isn't available, or we can't decrypt the packet, drop
                    // the rest of the datagram on the floor, but don't generate an error.
                    self.check_stateless_reset(path, e.data, dcid.is_none(), now)?;
                    self.stats.borrow_mut().pkt_dropped("Decryption failure");
                    qlog::packet_dropped(&mut self.qlog, &e, now);
                    dcid = Some(e.dcid);
                }
            }
            slc = remainder;
        }
        self.check_stateless_reset(path, &d, dcid.is_none(), now)?;
        Ok(())
    }

    /// Handle receiving a packet for which keys have been discarded.
    fn handle_keys_discarded(&mut self, epoch: Epoch) {
        // Client: receiving undecryptable Initial packets while waiting
        // indicates server's Initial was lost. Probe with Handshake.
        self.received_untracked |= self.role == Role::Client && epoch == Epoch::Initial;

        // Server: receiving undecryptable Handshake packets while Confirmed
        // indicates the client hasn't received HANDSHAKE_DONE. Resend it.
        if self.role == Role::Server && epoch == Epoch::Handshake && self.state == State::Confirmed
        {
            self.state_signaling.handshake_done();
        }
    }

    /// Process a packet.  Returns true if the packet might initiate migration.
    fn process_packet(
        &mut self,
        path: &PathRef,
        tos: Tos,
        packet: &packet::Decrypted,
        now: Instant,
    ) -> Res<bool> {
        (!packet.is_empty())
            .then_some(())
            .ok_or(Error::ProtocolViolation)?;

        // TODO(ekr@rtfm.com): Have the server blow away the initial
        // crypto state if this fails? Otherwise, we will get a panic
        // on the assert for doesn't exist.
        // OK, we have a valid packet.

        // Get the next packet number we'll send, for ACK verification.
        // This is used by `input_frame` to verify that ACKs don't acknowledge unsent packets.
        let next_pn = self
            .crypto
            .states()
            .select_tx(self.version, PacketNumberSpace::from(packet.packet_type()))
            .map_or(0, |(_, tx)| tx.next_pn());

        let mut ack_eliciting = false;
        let mut probing = true;
        let mut d = Decoder::from(&packet[..]);
        while d.remaining() > 0 {
            #[cfg(feature = "build-fuzzing-corpus")]
            let pos = d.offset();
            let f = Frame::decode(&mut d)?;
            #[cfg(feature = "build-fuzzing-corpus")]
            neqo_common::write_item_to_fuzzing_corpus("frame", &packet[pos..d.offset()]);
            ack_eliciting |= f.ack_eliciting();
            probing &= f.path_probing();
            let t = f.get_type();
            if let Err(e) = self.input_frame(
                path,
                packet.version(),
                packet.packet_type(),
                f,
                next_pn,
                now,
            ) {
                self.capture_error(Some(Rc::clone(path)), now, t, Err(e))?;
            }
        }

        let largest_received = if let Some(space) = self
            .acks
            .get_mut(PacketNumberSpace::from(packet.packet_type()))
        {
            let largest = space.set_received(
                now,
                packet.pn(),
                ack_eliciting,
                &mut self.stats.borrow_mut(),
            )?;
            if ack_eliciting && Ecn::from(tos) == Ecn::Ce {
                space.immediate_ack(now);
            }
            largest
        } else {
            debug!(
                "[{self}] processed a {:?} packet without tracking it",
                packet.packet_type(),
            );
            // This was a valid packet that caused the same packet number to be
            // discarded.  This happens when the client discards the Initial packet
            // number space after receiving the ServerHello.  Remember this so
            // that we guarantee that we send a Handshake packet.
            self.received_untracked = true;
            // We don't migrate during the handshake, so return false.
            false
        };

        Ok(largest_received && !probing)
    }

    /// During connection setup, the first path needs to be setup.
    /// This uses the connection IDs that were provided during the handshake
    /// to setup that path.
    fn setup_handshake_path(&mut self, path: &PathRef, now: Instant) {
        self.paths.make_permanent(
            path,
            Some(self.local_initial_source_cid.clone()),
            // Ideally we know what the peer wants us to use for the remote CID.
            // But we will use our own guess if necessary.
            ConnectionIdEntry::initial_remote(
                self.remote_initial_source_cid
                    .as_ref()
                    .or(self.original_destination_cid.as_ref())
                    .expect("have either remote_initial_source_cid or original_destination_cid")
                    .clone(),
            ),
            now,
            &mut self.stats.borrow_mut(),
        );
        if self.role == Role::Client {
            path.borrow_mut().set_valid(now);
        }
    }

    /// If the path isn't permanent, assign it a connection ID to make it so.
    fn ensure_permanent(&mut self, path: &PathRef, now: Instant) -> Res<()> {
        if self.paths.is_temporary(path) {
            // If there isn't a connection ID to use for this path, the packet
            // will be processed, but it won't be attributed to a path.  That means
            // no path probes or PATH_RESPONSE.  But it's not fatal.
            match self.cids.next() {
                Some(cid) => {
                    self.paths
                        .make_permanent(path, None, cid, now, &mut self.stats.borrow_mut());
                    Ok(())
                }
                None if self
                    .paths
                    .primary()
                    .is_some_and(|p| p.borrow().remote_cid().is_none_or(|id| id.is_empty())) =>
                {
                    self.paths.make_permanent(
                        path,
                        None,
                        ConnectionIdEntry::empty_remote(),
                        now,
                        &mut self.stats.borrow_mut(),
                    );
                    Ok(())
                }
                None => {
                    trace!("[{self}] Unable to make path permanent: {}", path.borrow());
                    Err(Error::InvalidMigration)
                }
            }
        } else {
            Ok(())
        }
    }

    /// Whether `packet` validates its path during the handshake (RFC 9000, Section 8.1).
    fn validates_path(&self, packet: &packet::Decrypted) -> bool {
        !self.state.connected()
            && self.role != Role::Client
            && (packet.packet_type() == packet::Type::Handshake
                || (packet.dcid().len() >= 8 && packet.dcid() == self.local_initial_source_cid))
    }

    /// After an error, a permanent path is needed to send the `CONNECTION_CLOSE`.
    /// This attempts to ensure that this exists.  As the connection is now
    /// temporary, there is no reason to do anything special here.
    fn ensure_error_path(&mut self, path: &PathRef, packet: &packet::Decrypted, now: Instant) {
        if self.validates_path(packet) {
            path.borrow_mut().set_valid(now);
        }
        if self.paths.is_temporary(path) {
            // First try to fill in handshake details.
            if packet.packet_type() == packet::Type::Initial {
                self.remote_initial_source_cid = Some(ConnectionId::from(packet.scid()));
                self.setup_handshake_path(path, now);
            } else {
                // Otherwise try to get a usable connection ID.
                drop(self.ensure_permanent(path, now));
            }
        }
    }

    fn start_handshake(&mut self, path: &PathRef, packet: &packet::Decrypted, now: Instant) {
        trace!("[{self}] starting handshake");
        debug_assert_eq!(packet.packet_type(), packet::Type::Initial);
        self.remote_initial_source_cid = Some(ConnectionId::from(packet.scid()));

        if self.role == Role::Server {
            let Some(original_destination_cid) = self.original_destination_cid.as_ref() else {
                debug!("[{self}] No original destination DCID");
                return;
            };
            self.cid_manager.add_odcid(original_destination_cid.clone());
            // Make a path on which to run the handshake.
            self.setup_handshake_path(path, now);
        } else {
            debug!("[{self}] Changing to use Server CID={}", packet.scid());
            debug_assert!(path.borrow().is_primary());
            path.borrow_mut().set_remote_cid(packet.scid());
        }
    }

    /// Migrate to the provided path.
    /// Either local or remote address (but not both) may be provided as `None` to have
    /// the address from the current primary path used.
    /// If `force` is true, then migration is immediate.
    /// Otherwise, migration occurs after the path is probed successfully.
    /// Either way, the path is probed and will be abandoned if the probe fails.
    ///
    /// # Errors
    ///
    /// Fails if this is not a client, not confirmed, the peer disabled connection migration, or
    /// there are not enough connection IDs available to use.
    pub fn migrate(
        &mut self,
        local: Option<SocketAddr>,
        remote: Option<SocketAddr>,
        force: bool,
        now: Instant,
    ) -> Res<()> {
        if self.role != Role::Client {
            return Err(Error::InvalidMigration);
        }
        if !matches!(self.state(), State::Confirmed) {
            return Err(Error::InvalidMigration);
        }
        if self.tps.borrow().remote().get_empty(DisableMigration) {
            return Err(Error::InvalidMigration);
        }

        // Fill in the blanks, using the current primary path.
        if local.is_none() && remote.is_none() {
            // Pointless migration is pointless.
            return Err(Error::InvalidMigration);
        }

        let path = self.paths.primary().ok_or(Error::InvalidMigration)?;
        let local = local.unwrap_or_else(|| path.borrow().local_address());
        let remote = remote.unwrap_or_else(|| path.borrow().remote_address());

        if mem::discriminant(&local.ip()) != mem::discriminant(&remote.ip()) {
            // Can't mix address families.
            return Err(Error::InvalidMigration);
        }
        let (local_ip, remote_ip) = (local.ip().to_canonical(), remote.ip().to_canonical());
        if local.port() == 0 || remote_ip.is_unspecified() || remote.port() == 0 {
            // All but the local address need to be specified.
            return Err(Error::InvalidMigration);
        }
        if (local_ip.is_loopback() ^ remote_ip.is_loopback()) && !local_ip.is_unspecified() {
            // Block attempts to migrate to a path with loopback on only one end, unless the local
            // address is unspecified.
            return Err(Error::InvalidMigration);
        }

        let path = self.paths.find_path(local, remote, &self.conn_params, now);
        self.ensure_permanent(&path, now)?;
        info!(
            "[{self}] Migrate to {} probe {}",
            path.borrow(),
            if force { "now" } else { "after" }
        );
        if self
            .paths
            .migrate(&path, force, now, &mut self.stats.borrow_mut())
        {
            self.path_migrated(&path);
        }
        Ok(())
    }

    fn path_migrated(&mut self, path: &PathRef) {
        self.loss_recovery.migrate();
        let p = path.borrow();
        self.events
            .path_migrated(p.local_address(), p.remote_address());
    }

    fn migrate_to_preferred_address(&mut self, now: Instant) -> Res<()> {
        let spa: Option<(tparams::PreferredAddress, ConnectionIdEntry<Srt>)> = if matches!(
            self.conn_params.get_preferred_address(),
            PreferredAddressConfig::Disabled
        ) {
            debug!("[{self}] Preferred address is disabled");
            None
        } else {
            self.tps.borrow_mut().remote().get_preferred_address()
        };
        if let Some((addr, cid)) = spa {
            // The connection ID isn't special, so just save it.
            self.cids.add_remote(cid)?;

            // The preferred address doesn't dictate what the local address is, so this
            // has to use the existing address.  So only pay attention to a preferred
            // address from the same family as is currently in use. More thought will
            // be needed to work out how to get addresses from a different family.
            let prev = self
                .paths
                .primary()
                .ok_or(Error::NoAvailablePath)?
                .borrow()
                .remote_address();
            let remote = match prev.ip() {
                IpAddr::V4(_) => addr.ipv4().map(SocketAddr::V4),
                IpAddr::V6(_) => addr.ipv6().map(SocketAddr::V6),
            };

            if let Some(remote) = remote {
                // Ignore preferred address that move to loopback from non-loopback.
                // `migrate` doesn't enforce this rule.
                if !prev.ip().to_canonical().is_loopback()
                    && remote.ip().to_canonical().is_loopback()
                {
                    warn!("[{self}] Ignoring a move to a loopback address: {remote}");
                    return Ok(());
                }

                if self.migrate(None, Some(remote), false, now).is_err() {
                    warn!("[{self}] Ignoring bad preferred address: {remote}");
                }
            } else {
                warn!("[{self}] Unable to migrate to a different address family");
            }
        } else {
            debug!("[{self}] No preferred address to migrate to");
        }
        Ok(())
    }

    fn handle_migration(
        &mut self,
        path: &PathRef,
        remote: SocketAddr,
        migrate: bool,
        now: Instant,
    ) {
        if !migrate {
            return;
        }
        if self.role == Role::Client {
            return;
        }

        if self.ensure_permanent(path, now).is_ok() {
            let was_primary = path.borrow().is_primary();
            self.paths
                .handle_migration(path, remote, now, &mut self.stats.borrow_mut());
            if !was_primary {
                self.path_migrated(path);
            }
        } else {
            info!(
                "[{self}] {} Peer migrated, but no connection ID available",
                path.borrow()
            );
        }
    }

    fn output(&mut self, now: Instant, max_datagrams: NonZeroUsize) -> SendOptionBatch {
        trace!("[{self}] output {now:?}");
        let res = match &self.state {
            State::Init
            | State::WaitInitial
            | State::WaitVersion
            | State::Handshaking
            | State::Connected
            | State::Confirmed => self.paths.select_path().map_or_else(
                || Ok(SendOptionBatch::default()),
                |path| {
                    let res = self.output_dgram_batch_on_path(&path, now, None, max_datagrams);
                    if matches!(res, Ok(SendOptionBatch::No(false)))
                        && self.state.connected()
                        && !path.borrow().is_valid()
                        && path.borrow().has_probe()
                        && path.borrow_mut().block_probe(now)
                    {
                        // The probe does not fit the anti-amplification limit; try another path.
                        return Ok(self.output(now, max_datagrams));
                    }
                    self.capture_error(Some(path), now, FrameType::Padding, res)
                },
            ),
            State::Closing { .. } | State::Draining { .. } | State::Closed(_) => {
                self.state_signaling.close_frame().map_or_else(
                    || Ok(SendOptionBatch::default()),
                    |details| {
                        let path = Rc::clone(details.path());
                        // In some error cases, we will not be able to make a new, permanent path.
                        // For example, if we run out of connection IDs and the error results from
                        // a packet on a new path, we avoid sending (and the privacy risk) rather
                        // than reuse a connection ID.
                        let res = if path.borrow().is_temporary() {
                            error!("[{self}] Attempting to close with a temporary path");
                            Err(Error::Internal)
                        } else {
                            self.output_dgram_batch_on_path(
                                &path,
                                now,
                                Some(&details),
                                max_datagrams,
                            )
                        };
                        self.capture_error(Some(path), now, FrameType::Padding, res)
                    },
                )
            }
        };
        res.unwrap_or_default()
    }

    #[expect(clippy::too_many_arguments, reason = "no easy way to simplify")]
    fn build_packet_header<'a>(
        path: &Path,
        epoch: Epoch,
        encoder: Encoder<&'a mut Vec<u8>>,
        tx: &CryptoDxState,
        address_validation: &AddressValidationInfo,
        version: Version,
        grease_quic_bit: bool,
        limit: usize,
        largest_acknowledged: Option<packet::Number>,
    ) -> (
        packet::Type,
        packet::Builder<&'a mut Vec<u8>>,
        packet::Number,
    ) {
        let pt = packet::Type::from(epoch);
        let mut builder = if pt == packet::Type::Short {
            debug!("Building Short dcid {:?}", path.remote_cid());
            packet::Builder::short(encoder, tx.key_phase(), path.remote_cid(), limit)
        } else {
            debug!(
                "Building {pt:?} dcid {:?} scid {:?}",
                path.remote_cid(),
                path.local_cid(),
            );
            packet::Builder::long(
                encoder,
                pt,
                version,
                path.remote_cid(),
                path.local_cid(),
                limit,
            )
        };
        if builder.remaining() > 0 {
            builder.scramble(grease_quic_bit);
            if pt == packet::Type::Initial {
                builder.initial_token(address_validation.token());
            }
        }

        let pn = tx.next_pn(); // Deliberately not closed at 2^62-1 (RFC 9000 Section 12.3).
        let unacked_range = largest_acknowledged.map_or_else(|| pn + 1, |la| (pn - la) << 1);
        // Count how many bytes in this range are non-zero.
        // The conversion is safe because the maximum value is 8.
        let pn_len = size_of::<packet::Number>() - expect_usize(unacked_range.leading_zeros() / 8);
        assert!(
            pn_len > 0,
            "pn_len can't be zero as unacked_range should be > 0, pn {pn}, largest_acknowledged {largest_acknowledged:?}, tx {tx}"
        );
        // TODO(mt) also use `4*path CWND/path MTU` to set a minimum length.
        builder.pn(pn, pn_len);

        (pt, builder, pn)
    }

    fn can_grease_quic_bit(&self) -> bool {
        let tph = self.tps.borrow();
        tph.remote_handshake()
            .as_ref()
            .is_some_and(|r| r.get_empty(GreaseQuicBit))
    }

    /// Write the frames that are exchanged in the application data space.
    /// The order of calls here determines the relative priority of frames.
    fn write_appdata_frames(
        &mut self,
        builder: &mut packet::Builder<&mut Vec<u8>>,
        tokens: &mut recovery::Tokens,
        now: Instant,
        full_mtu: bool,
    ) {
        let rtt = self.paths.primary().map_or_else(
            || RttEstimate::new(self.conn_params.get_initial_rtt()).estimate(),
            |p| p.borrow().rtt().estimate(),
        );

        let stats = &mut self.stats.borrow_mut();
        let frame_stats = &mut stats.frame_tx;
        if self.role == Role::Server
            && let Some(t) = self.state_signaling.write_done(builder)
        {
            tokens.push(t);
            frame_stats.handshake_done += 1;
        }

        self.streams
            .write_frames(TransmissionPriority::Critical, builder, tokens, frame_stats);
        if builder.is_full() {
            return;
        }

        self.streams
            .write_maintenance_frames(builder, tokens, frame_stats, now, rtt);
        if builder.is_full() {
            return;
        }

        self.streams.write_frames(
            TransmissionPriority::Important,
            builder,
            tokens,
            frame_stats,
        );
        if builder.is_full() {
            return;
        }

        // NEW_CONNECTION_ID, RETIRE_CONNECTION_ID, and ACK_FREQUENCY.
        self.cid_manager.write_frames(builder, tokens, frame_stats);
        if builder.is_full() {
            return;
        }

        self.paths.write_frames(builder, tokens, frame_stats);
        if builder.is_full() {
            return;
        }

        for prio in [TransmissionPriority::High, TransmissionPriority::Normal] {
            self.streams
                .write_frames(prio, builder, tokens, &mut stats.frame_tx);
            if builder.is_full() {
                return;
            }
        }

        // Datagrams are best-effort and unreliable.  Let streams starve them for now.
        self.quic_datagrams
            .write_frames(builder, tokens, stats, full_mtu);
        if builder.is_full() {
            return;
        }

        // CRYPTO here only includes NewSessionTicket, plus NEW_TOKEN.
        // Both of these are only used for resumption and so can be relatively low priority.
        let frame_stats = &mut stats.frame_tx;
        self.crypto.write_frame(
            PacketNumberSpace::ApplicationData,
            self.conn_params.sni_slicing_enabled(),
            builder,
            tokens,
            frame_stats,
        );
        if builder.is_full() {
            return;
        }

        self.new_token.write_frames(builder, tokens, frame_stats);
        if builder.is_full() {
            return;
        }

        self.streams
            .write_frames(TransmissionPriority::Low, builder, tokens, frame_stats);
    }

    // Maybe send a probe.  Return true if the packet was ack-eliciting.
    fn maybe_probe<B: Buffer>(
        &mut self,
        path: &PathRef,
        force_probe: bool,
        builder: &mut packet::Builder<B>,
        ack_end: usize,
        tokens: &mut recovery::Tokens,
        now: Instant,
    ) -> bool {
        let untracked = self.received_untracked && !self.state.connected();
        self.received_untracked = false;

        // Anything written after an ACK already elicits acknowledgment.
        // If we need to probe and nothing has been written, send a PING.
        if builder.len() > ack_end {
            return true;
        }

        let pto = path.borrow().rtt().pto(self.confirmed());
        let mut probe = if untracked && builder.packet_empty() || force_probe {
            // If we received an untracked packet and we aren't probing already
            // or the PTO timer fired: probe.
            true
        } else if !builder.packet_empty() {
            // The packet only contains an ACK.  Check whether we want to
            // force an ACK with a PING so we can stop tracking packets.
            self.loss_recovery.should_probe(pto, now)
        } else {
            false
        };

        if self.streams.need_keep_alive() {
            // We need to keep the connection alive, including sending a PING
            // again. If a PING is already scheduled (i.e. `probe` is `true`)
            // piggy back on it. If not, schedule one.
            probe |= self.idle_timeout.send_keep_alive(now, pto, tokens);
        }

        if probe {
            // Nothing ack-eliciting and we need to probe; send PING.
            debug_assert_ne!(builder.remaining(), 0);
            builder.encode_frame(FrameType::Ping, |_| {});
            let stats = &mut self.stats.borrow_mut().frame_tx;
            stats.ping += 1;
        }
        probe
    }

    /// Whether this packet should be a PMTUD probe.
    fn should_probe_pmtud(
        &self,
        path: &PathRef,
        space: PacketNumberSpace,
        profile: &SendProfile,
        coalesced: bool,
    ) -> bool {
        let path = path.borrow();
        space == PacketNumberSpace::ApplicationData
            && self.state.connected()
            && !coalesced // Only send PMTUD probes using non-coalesced packets.
            && path.is_primary()
            && !profile.ack_only() // We could only send ACKs.
            && path.can_probe_pmtud()
    }

    fn is_small_initial(&self, space: PacketNumberSpace, profile: &SendProfile) -> bool {
        self.role == Role::Server
            && space == PacketNumberSpace::Initial
            && profile.limit() < packet::MIN_INITIAL_PACKET_SIZE
    }

    /// Write frames to the provided builder.  Returns a list of tokens used for
    /// tracking loss or acknowledgment and whether any frame was ACK eliciting.
    fn write_frames(
        &mut self,
        path: &PathRef,
        space: PacketNumberSpace,
        profile: &SendProfile,
        builder: &mut packet::Builder<&mut Vec<u8>>,
        pmtud_probe: bool,
        now: Instant,
    ) -> (recovery::Tokens, bool) {
        let mut tokens = recovery::Tokens::new();
        let primary = path.borrow().is_primary();
        let mut ack_eliciting = false;

        if primary {
            let stats = &mut self.stats.borrow_mut().frame_tx;
            self.acks.write_frame(
                space,
                now,
                path.borrow().rtt().estimate(),
                builder,
                &mut tokens,
                stats,
            );
        }
        let ack_end = builder.len();

        // Avoid sending path validation probes until the handshake completes,
        // but send them even when we don't have space.
        let full_mtu = profile.full_mtu();
        // PMTUD must not record a success for a packet that was never probe-sized.
        debug_assert!(
            !pmtud_probe || (primary && full_mtu && space == PacketNumberSpace::ApplicationData)
        );
        if space == PacketNumberSpace::ApplicationData && self.state.connected() {
            // Path validation probes should only be padded if the full MTU is available.
            // The probing code needs to know so it can track that.
            if path.borrow_mut().write_frames(
                builder,
                &mut self.stats.borrow_mut().frame_tx,
                full_mtu,
                now,
            ) && full_mtu
            {
                builder.enable_padding(true);
            }
        }

        // ACKs only if CC limited or a server Initial is too small (RFC 9000, Section 14.1).
        if profile.ack_only() || self.is_small_initial(space, profile) {
            return (tokens, false);
        }

        if primary {
            if space == PacketNumberSpace::ApplicationData {
                if pmtud_probe {
                    path.borrow_mut().pmtud_mut().send_probe(
                        builder,
                        &mut tokens,
                        &mut self.stats.borrow_mut(),
                    );
                    ack_eliciting = true;
                }
                self.write_appdata_frames(builder, &mut tokens, now, full_mtu);
            } else {
                let stats = &mut self.stats.borrow_mut().frame_tx;
                self.crypto.write_frame(
                    space,
                    self.conn_params.sni_slicing_enabled(),
                    builder,
                    &mut tokens,
                    stats,
                );
            }

            #[cfg(test)]
            if let Some(w) = &mut self.test_frame_writer {
                assert!(!builder.is_full(), "test_frame_writer set on full packet");
                w.write_frames(builder);
            }
        }

        // Maybe send a probe now, either to probe for losses or to keep the connection live.
        let force_probe = profile.should_probe(space);
        ack_eliciting |= self.maybe_probe(path, force_probe, builder, ack_end, &mut tokens, now);
        // If this is not the primary path, this should be ack-eliciting.
        debug_assert!(primary || ack_eliciting);

        (tokens, ack_eliciting)
    }

    fn write_closing_frames<B: Buffer>(
        &mut self,
        close: &ClosingFrame,
        builder: &mut packet::Builder<B>,
        space: PacketNumberSpace,
        now: Instant,
        path: &PathRef,
        tokens: &mut recovery::Tokens,
    ) {
        if builder.remaining() > ClosingFrame::MIN_LENGTH + RecvdPackets::USEFUL_ACK_LEN {
            // Include an ACK frame with the CONNECTION_CLOSE.
            let limit = builder.limit();
            builder.set_limit(limit - ClosingFrame::MIN_LENGTH);
            self.acks.immediate_ack(space, now);
            self.acks.write_frame(
                space,
                now,
                path.borrow().rtt().estimate(),
                builder,
                tokens,
                &mut self.stats.borrow_mut().frame_tx,
            );
            builder.set_limit(limit);
        }
        // CloseReason::Application is only allowed at 1RTT.
        let sanitized = if space == PacketNumberSpace::ApplicationData {
            None
        } else {
            close.sanitize()
        };
        sanitized.as_ref().unwrap_or(close).write_frame(builder);
        self.stats.borrow_mut().frame_tx.connection_close += 1;
    }

    /// Build batch of datagrams to be sent on the provided path.
    fn output_dgram_batch_on_path(
        &mut self,
        path: &PathRef,
        now: Instant,
        mut closing_frame: Option<&ClosingFrame>,
        max_datagrams: NonZeroUsize,
    ) -> Res<SendOptionBatch> {
        let packet_tos = path.borrow().tos();
        let mut send_buffer = Vec::new();
        let mut max_datagram_size = None;
        let mut num_datagrams = 0;
        let mtu = path.borrow().plpmtu();
        let address_family_max_mtu = path.borrow().pmtud().address_family_max_mtu();

        loop {
            if max_datagrams.get() <= num_datagrams {
                break;
            }
            if num_datagrams != 0 && !path.borrow().is_primary() {
                break; // A probe or closing datagram on a non-primary path is never batched.
            }
            if path.borrow().can_probe_pmtud() && num_datagrams != 0 {
                // Next datagram will be larger due to PMTUD probing.  GSO
                // requires that all datagrams in a batch are of equal size.
                // Only the last datagram can be smaller. Given that this would
                // not be the first datagram, close the batch early to uphold
                // the above GSO requirement.
                break;
            }

            let send_buffer_len_before = send_buffer.len();

            // Check if we can fit another PMTUD sized datagram into the batch.
            if max_datagram_size.is_some_and(|datagram_size| {
                // GSO requires that all datagrams in a batch are of equal size.
                // The last datagram can be smaller. The datagrams already in
                // the batch are each `datagram_size` large. The next datagram
                // can be up to `mtu` large. Break in case the next could be
                // larger than the ones already in the batch.
                datagram_size < mtu
                // GSO allows total datagram batch size up to the address family
                // max MTU. If the next datagram could exceed that limit, break.
                //
                // See for example Linux kernel:
                // https://github.com/torvalds/linux/blob/fb4d33ab452ea254e2c319bac5703d1b56d895bf/include/linux/netdevice.h#L2402
                || address_family_max_mtu - send_buffer.len() < mtu
            }) {
                break;
            }

            match self.output_dgram_on_path(
                path,
                now,
                closing_frame.take(),
                Encoder::new_borrowed_vec(&mut send_buffer),
                packet_tos,
            )? {
                SendOption::Yes => {
                    debug_assert_eq!(
                        mtu,
                        path.borrow().plpmtu(),
                        "MTU does not change within batch"
                    );
                    num_datagrams += 1;
                    let datagram_size = send_buffer.len() - send_buffer_len_before;
                    let max_datagram_size = *max_datagram_size.get_or_insert(datagram_size);

                    // GSO requires that all datagrams in a batch are of equal
                    // size. Only the last datagram can be smaller.
                    debug_assert!(datagram_size <= max_datagram_size);
                    if datagram_size < max_datagram_size {
                        // This packet was smaller. Make sure it is the last by
                        // breaking the loop.
                        break;
                    }
                }
                SendOption::No(paced) => {
                    if num_datagrams == 0 {
                        debug_assert!(send_buffer.is_empty());
                        return Ok(SendOptionBatch::No(paced));
                    }
                    break;
                }
            }
        }

        debug_assert!(!send_buffer.is_empty());
        let batch = path.borrow_mut().datagram_batch(
            send_buffer,
            packet_tos,
            num_datagrams,
            max_datagram_size.ok_or(Error::Internal)?,
            &mut self.stats.borrow_mut(),
        );

        Ok(SendOptionBatch::Yes(batch))
    }

    /// Build a datagram, possibly from multiple packets (for different PN
    /// spaces) and each containing 1+ frames.
    #[expect(clippy::too_many_lines, reason = "Yeah, that's just the way it is.")]
    fn output_dgram_on_path(
        &mut self,
        path: &PathRef,
        now: Instant,
        closing_frame: Option<&ClosingFrame>,
        mut encoder: Encoder<&mut Vec<u8>>,
        packet_tos: Tos,
    ) -> Res<SendOption> {
        let mut initial_sent = None;
        let mut needs_padding = false;
        let grease_quic_bit = self.can_grease_quic_bit();
        let version = self.version();

        // Determine how we are sending packets (PTO, etc..).
        let mut profile = self.loss_recovery.send_profile(&path.borrow(), now);
        // Everything counts against the anti-amplification limit (RFC 9000, Section 8.1).
        profile.cap(path.borrow().amplification_limit());
        debug!("[{self}] output_dgram_on_path send_profile {profile:?}");

        // Frames for different epochs must go in different packets, but then these
        // packets can go in a single datagram
        for space in PacketNumberSpace::iter() {
            // Other than closing frames, only path probes are sent on a non-primary path.
            if closing_frame.is_none()
                && !path.borrow().is_primary()
                && space != PacketNumberSpace::ApplicationData
            {
                continue;
            }
            let coalesced = !encoder.is_empty();
            let pmtud_probe = self.should_probe_pmtud(path, space, &profile, coalesced);

            // Ensure we have tx crypto state for this epoch, or skip it.
            let Some((epoch, tx)) = self.crypto.states_mut().select_tx_mut(self.version, space)
            else {
                continue;
            };
            let aead_expansion = tx.expansion();

            let header_start = encoder.len();

            // Configure the limits and padding for this packet.
            let Some(limit) = (if pmtud_probe {
                needs_padding = true;
                debug_assert!(path.borrow().pmtud().probe_size() >= profile.limit());
                Some(path.borrow().pmtud().probe_size())
            } else {
                profile.limit().checked_sub(
                    if space == PacketNumberSpace::Initial && self.conn_params.scone_enabled() {
                        // Reserve some space for the SCONE indication in an Initial.
                        // This reduces the amount available for building the packet,
                        // but we'll pad to `profile.limit()` when padding.
                        // This will not reserve space for the indication if packets
                        // are coalesced (with Handshake or 0-RTT). That's too bad.
                        Self::SCONE_INDICATION.len()
                    } else {
                        0
                    },
                )
            })
            .and_then(|limit| limit.checked_sub(aead_expansion)) else {
                // Too small for any packet, including those in later spaces.
                break;
            };

            let (pt, mut builder, pn) = Self::build_packet_header(
                &path.borrow(),
                epoch,
                encoder,
                tx,
                &self.address_validation,
                version,
                grease_quic_bit,
                limit,
                self.loss_recovery.largest_acknowledged_pn(space),
            );
            // The builder will set the limit to 0 if there isn't enough space for the header.
            if builder.is_full() {
                encoder = builder.abort();
                break;
            }

            builder.enable_padding(needs_padding);
            if builder.is_full() {
                encoder = builder.abort();
                break;
            }

            // Add frames to the packet.
            let payload_start = builder.len();
            let (mut tokens, mut ack_eliciting) = (recovery::Tokens::new(), false);
            if let Some(close) = closing_frame {
                self.write_closing_frames(close, &mut builder, space, now, path, &mut tokens);
            } else {
                (tokens, ack_eliciting) =
                    self.write_frames(path, space, &profile, &mut builder, pmtud_probe, now);
            }
            if builder.packet_empty() {
                // Nothing to include in this packet.
                encoder = builder.abort();

                continue;
            }

            // Padding is only enabled for full MTU packets or after an Initial.
            let padded = (ack_eliciting || initial_sent.is_some()) && builder.pad();

            if packet_tos.is_ecn_marked() {
                tokens.push(recovery::Token::EcnEct0);
            }

            self.log_packet(
                packet::MetaData::new_out(
                    path,
                    pt,
                    pn,
                    builder.len() + aead_expansion,
                    &builder.as_ref()[payload_start..],
                    packet_tos,
                    self.version,
                ),
                now,
            );

            self.stats.borrow_mut().packets_tx += 1;
            // Track which packet types are sent with which ECN codepoints. For
            // coalesced packets, this increases the counts for each packet type
            // contained in the coalesced packet. This is per Section 13.4.1 of
            // RFC 9000.
            self.stats.borrow_mut().ecn_tx[pt] += Ecn::from(packet_tos);
            let tx = self
                .crypto
                .states_mut()
                .tx_mut(self.version, epoch)
                .ok_or(Error::Internal)?;
            encoder = builder.build(tx)?;
            self.crypto.states_mut().auto_update()?;

            if ack_eliciting {
                self.idle_timeout.on_packet_sent(now);
            }
            let sent = sent::Packet::new(
                pt,
                pn,
                now,
                ack_eliciting,
                tokens,
                encoder.len() - header_start,
            )
            .with_padding(padded);
            if !pt.is_long() {
                if padded {
                    self.stats.borrow_mut().frame_tx.padding += 1;
                }
                // Padding after a short header packet would join its ciphertext.
                needs_padding = false;
                self.loss_recovery.on_packet_sent(path, sent, now);
            } else if pt == packet::Type::Initial && (self.role == Role::Client || ack_eliciting) {
                // Packets containing Initial packets might need padding, and we want to
                // track that padding along with the Initial packet.  So defer tracking.
                initial_sent = Some(sent);
                needs_padding = true;
            } else {
                if pt.is_long() && self.role == Role::Client && initial_sent.is_none() {
                    // Disable padding for any long header packet if the UDP packet doesn't include
                    // an Initial packet.
                    needs_padding = false;
                }
                self.loss_recovery.on_packet_sent(path, sent, now);
            }

            if space == PacketNumberSpace::Handshake {
                if self.role == Role::Client {
                    // We're sending a Handshake packet, so we can discard Initial keys.
                    self.discard_keys(PacketNumberSpace::Initial, now);
                } else if self.role == Role::Server && self.state == State::Confirmed {
                    // We could discard handshake keys in set_state,
                    // but wait until after sending an ACK.
                    self.discard_keys(PacketNumberSpace::Handshake, now);
                }
            }

            // If the client has more CRYPTO data queued up, do not coalesce if
            // this packet is an Initial. Without this, 0-RTT packets could be
            // coalesced with the first Initial, which some server (e.g., ours)
            // do not support, because they may not save packets they can't
            // decrypt yet.
            if self.role == Role::Client
                && space == PacketNumberSpace::Initial
                && !self.crypto.streams_mut().is_empty(space)
            {
                break;
            }
        }

        if encoder.is_empty() {
            debug!("TX blocked, profile={profile:?}");
            Ok(SendOption::No(profile.paced()))
        } else {
            // Perform additional padding for Initial packets as necessary.
            if let Some(mut initial) = initial_sent.take() {
                if needs_padding {
                    self.pad_initial(&mut encoder, &mut initial, &profile);
                }
                self.loss_recovery.on_packet_sent(path, initial, now);
            }
            path.borrow_mut().add_sent(encoder.len());
            Ok(SendOption::Yes)
        }
    }

    fn pad_initial(
        &self,
        encoder: &mut Encoder<&mut Vec<u8>>,
        initial: &mut sent::Packet,
        profile: &SendProfile,
    ) {
        if encoder.len() >= profile.limit() {
            return;
        }

        debug!(
            "[{self}] pad Initial from {} to {}",
            encoder.len(),
            profile.limit()
        );
        let pad_amount = profile.limit() - encoder.len();
        initial.track_padding(pad_amount);
        if self.conn_params.scone_enabled() {
            // This ensures that the last bytes are a SCONE indication, if there is enough space.
            // This is not tracked, other than for congestion control (above)
            if pad_amount >= Self::SCONE_INDICATION.len() {
                encoder.pad_to(
                    profile.limit() - Self::SCONE_INDICATION.len() + 1,
                    Self::SCONE_INDICATION[0],
                );
                encoder.encode(&Self::SCONE_INDICATION[1..]);
            } else {
                encoder.pad_to(profile.limit(), Self::SCONE_INDICATION[0]);
            }
        } else {
            encoder.pad_to(profile.limit(), 0);
        }
    }

    /// # Errors
    /// When connection state is not valid.
    pub fn initiate_key_update(&mut self) -> Res<()> {
        if self.state == State::Confirmed {
            let la = self
                .loss_recovery
                .largest_acknowledged_pn(PacketNumberSpace::ApplicationData);
            info!("[{self}] Initiating key update");
            self.crypto.states_mut().initiate_key_update(la)
        } else {
            Err(Error::KeyUpdateBlocked)
        }
    }

    #[cfg(test)]
    #[must_use]
    pub fn get_epochs(&self) -> (Option<usize>, Option<usize>) {
        self.crypto.states().get_epochs()
    }

    fn client_start(&mut self, now: Instant) -> Res<()> {
        debug!("[{self}] client_start");
        debug_assert_eq!(self.role, Role::Client);
        if let Some(path) = self.paths.primary() {
            qlog::connection_started(&mut self.qlog, &path, now);
            qlog::recovery_parameters_set(
                &mut self.qlog,
                path.borrow().plpmtu(),
                self.conn_params.get_congestion_control(),
                now,
            );
            qlog::congestion_state_updated(
                &mut self.qlog,
                None,
                Phase::SlowStart.into(),
                None,
                now,
            );
        }
        qlog::client_version_information_initiated(
            &mut self.qlog,
            self.conn_params.get_versions(),
            now,
        );

        self.handshake(now, self.version, PacketNumberSpace::Initial, None)?;
        self.set_state(State::WaitInitial, now);
        self.zero_rtt_state = if self.crypto.enable_0rtt(self.version)? {
            debug!("[{self}] Enabled 0-RTT");
            ZeroRttState::Sending
        } else {
            ZeroRttState::Init
        };
        Ok(())
    }

    fn get_closing_period_time(&self, now: Instant) -> Instant {
        // Spec says close time should be at least PTO times 3.
        now + (self.pto() * 3)
    }

    /// Close the connection.
    pub fn close<A: AsRef<str>>(&mut self, now: Instant, app_error: AppError, msg: A) {
        let error = CloseReason::Application(app_error);
        let timeout = self.get_closing_period_time(now);
        match self.paths.primary() {
            Some(path) => {
                self.state_signaling
                    .close(path, error.clone(), FrameType::Padding, msg);
                self.set_state(State::Closing { error, timeout }, now);
            }
            None => {
                self.set_state(State::Closed(error), now);
            }
        }
    }

    fn set_initial_limits(&mut self) {
        self.streams.set_initial_limits();
        let peer_timeout = self
            .tps
            .borrow()
            .remote()
            .get_integer(TransportParameterId::IdleTimeout);
        if peer_timeout > 0 {
            self.idle_timeout
                .set_peer_timeout(Duration::from_millis(peer_timeout));
        }

        self.quic_datagrams
            .set_remote_datagram_size(self.tps.borrow().remote().get_integer(MaxDatagramFrameSize));
    }

    #[must_use]
    pub fn is_stream_id_allowed(&self, stream_id: StreamId) -> bool {
        self.streams.is_stream_id_allowed(stream_id)
    }

    /// Process the final set of transport parameters.
    fn process_tps(&mut self, now: Instant) -> Res<()> {
        self.validate_cids()?;
        self.validate_versions()?;
        {
            let tps = self.tps.borrow();
            let remote = tps.remote_handshake().ok_or(Error::TransportParameter)?;

            // If the peer provided a preferred address, then we have to be a client
            // and they have to be using a non-empty connection ID.
            if remote.get_preferred_address().is_some()
                && (self.role == Role::Server
                    || self
                        .remote_initial_source_cid
                        .as_ref()
                        .ok_or(Error::UnknownConnectionId)?
                        .is_empty())
            {
                return Err(Error::TransportParameter);
            }

            let path = self.paths.primary().ok_or(Error::NoAvailablePath)?;
            if let Some(token) = remote.get_bytes(StatelessResetToken) {
                let reset_token = Srt::try_from(token).map_err(|_| Error::TransportParameter)?;
                path.borrow_mut().set_reset_token(reset_token);
            }

            // We cap this transport parameter to usize::MAX on decode, so this is safe.
            let max_udp_payload = expect_usize(remote.get_integer(MaxUdpPayloadSize));
            path.borrow_mut()
                .pmtud_mut()
                .set_peer_max_udp_payload(max_udp_payload);
            self.stats.borrow_mut().pmtud_peer_max_udp_payload = Some(max_udp_payload);

            let max_ad = Duration::from_millis(remote.get_integer(MaxAckDelay));
            let min_ad = if remote.has_value(MinAckDelay) {
                let min_ad = Duration::from_micros(remote.get_integer(MinAckDelay));
                if min_ad > max_ad {
                    return Err(Error::TransportParameter);
                }
                Some(min_ad)
            } else {
                None
            };
            path.borrow_mut()
                .set_ack_delay(max_ad, min_ad, self.conn_params.get_ack_ratio());

            let max_active_cids = remote.get_integer(ActiveConnectionIdLimit);
            self.cid_manager.set_limit(max_active_cids);
        }
        self.set_initial_limits();
        qlog::connection_tparams_set(&mut self.qlog, &self.tps.borrow(), now);
        Ok(())
    }

    fn validate_cids(&self) -> Res<()> {
        let tph = self.tps.borrow();
        let remote_tps = tph.remote_handshake().ok_or(Error::TransportParameter)?;

        let tp = remote_tps.get_bytes(InitialSourceConnectionId);
        if self
            .remote_initial_source_cid
            .as_ref()
            .map(ConnectionId::as_cid_ref)
            != tp.map(ConnectionIdRef::from)
        {
            warn!(
                "[{self}] ISCID test failed: self cid {:?} != tp cid {:?}",
                self.remote_initial_source_cid,
                tp.map(Hex::new),
            );
            return Err(Error::ProtocolViolation);
        }

        if self.role == Role::Client {
            let tp = remote_tps.get_bytes(OriginalDestinationConnectionId);
            if self
                .original_destination_cid
                .as_ref()
                .map(ConnectionId::as_cid_ref)
                != tp.map(ConnectionIdRef::from)
            {
                warn!(
                    "[{self}] ODCID test failed: self cid {:?} != tp cid {:?}",
                    self.original_destination_cid,
                    tp.map(Hex::new),
                );
                return Err(Error::ProtocolViolation);
            }

            let tp = remote_tps.get_bytes(RetrySourceConnectionId);
            let expected = if let AddressValidationInfo::Retry {
                retry_source_cid, ..
            } = &self.address_validation
            {
                Some(retry_source_cid.as_cid_ref())
            } else {
                None
            };
            if expected != tp.map(ConnectionIdRef::from) {
                warn!(
                    "[{self}] RSCID test failed. self cid {expected:?} != tp cid {:?}",
                    tp.map(Hex::new),
                );
                return Err(Error::ProtocolViolation);
            }
        }

        Ok(())
    }

    /// Validate the `version_negotiation` transport parameter from the peer.
    fn validate_versions(&self) -> Res<()> {
        let tph = self.tps.borrow();
        let remote_tps = tph.remote_handshake().ok_or(Error::TransportParameter)?;
        // `current` and `other` are the value from the peer's transport parameters.
        // We're checking that these match our expectations.
        if let Some((current, other)) = remote_tps.get_versions() {
            trace!(
                "[{self}] validate_versions: current={:x} chosen={current:x} other={other:x?}",
                self.version.wire_version(),
            );
            if self.role == Role::Server {
                // 1. A server acts on transport parameters, with validation
                // of `current` happening in the transport parameter handler.
                // All we need to do is confirm that the transport parameter
                // was provided.
                Ok(())
            } else if self.version().wire_version() != current {
                info!("[{self}] validate_versions: current version mismatch");
                Err(Error::VersionNegotiation)
            } else if self
                .conn_params
                .get_versions()
                .initial()
                .is_compatible(self.version)
            {
                // 2. The current version is compatible with what we attempted.
                // That's a compatible upgrade and that's OK.
                Ok(())
            } else {
                // 3. The initial version we attempted isn't compatible.  Check that
                // the one we would have chosen is compatible with this one.
                let mut all_versions = other.to_owned();
                all_versions.push(current);
                if self
                    .conn_params
                    .get_versions()
                    .preferred(&all_versions)
                    .ok_or(Error::VersionNegotiation)?
                    .is_compatible(self.version)
                {
                    Ok(())
                } else {
                    info!("[{self}] validate_versions: failed");
                    Err(Error::VersionNegotiation)
                }
            }
        } else if self.version != Version::Version1 && !self.version.is_draft() {
            info!("[{self}] validate_versions: missing extension");
            Err(Error::VersionNegotiation)
        } else {
            Ok(())
        }
    }

    const fn has_version(&self) -> bool {
        self.crypto.has_handshake_keys()
    }

    /// Commit to a particular version.
    fn compatible_upgrade(&mut self, packet_version: Version) -> Res<()> {
        if !matches!(self.state, State::WaitInitial | State::WaitVersion) {
            return Ok(());
        }

        let v = if self.role == Role::Client {
            packet_version
        } else {
            let version = self.tps.borrow().version();
            let dcid = self
                .original_destination_cid
                .as_ref()
                .ok_or(Error::ProtocolViolation)?;
            // No need to randomize the starting packet number; that's already taken care of.
            self.crypto.states_mut().init_server(version, dcid, false)?;
            version
        };

        self.crypto.confirm_version(v)?;
        // OK, it's all confirmed.
        if self.version != v {
            debug!("[{self}] Compatible upgrade {:?} ==> {v:?}", self.version);
            self.version = v;
        }
        Ok(())
    }

    fn handshake(
        &mut self,
        now: Instant,
        packet_version: Version,
        space: PacketNumberSpace,
        data: Option<&[u8]>,
    ) -> Res<()> {
        trace!(
            "[{self}] Handshake space={space} data: {:?}",
            data.as_ref().map(HexWithLen::new),
        );

        let was_authentication_pending =
            *self.crypto.tls().state() == HandshakeState::AuthenticationPending;
        let try_update = data.is_some();
        match self
            .crypto
            .handshake(now, space, data)
            .map_err(|e| self.tps.borrow_mut().take_error().unwrap_or(e))?
        {
            HandshakeState::Authenticated(_) | HandshakeState::InProgress => (),
            HandshakeState::AuthenticationPending => {
                if !was_authentication_pending {
                    self.events.authentication_needed();
                }
            }
            HandshakeState::EchFallbackAuthenticationPending(public_name) => self
                .events
                .ech_fallback_authentication_needed(public_name.clone()),
            HandshakeState::Complete(_) => {
                if !self.state.connected() {
                    self.set_connected(now)?;
                }
            }
            _ => {
                error!("Crypto state should not be new or failed after successful handshake");
                return Err(Error::Crypto(nss::Error::Internal));
            }
        }

        // There is a chance that this could be called less often, but getting the
        // conditions right is a little tricky, so call whenever CRYPTO data is used.
        if try_update {
            // We have transport parameters, it's go time.
            if self.state < State::Connected && self.tps.borrow().remote_handshake().is_some() {
                self.set_initial_limits();
            } else if self.role == Role::Server && self.crypto.tls().has_secret(Epoch::Handshake) {
                warn!("[{self}] No transport parameters, closing connection");
                return Err(Error::CryptoAlert(109)); // missing_extension
            }
            if self.crypto.tls().has_secret(Epoch::Handshake) {
                self.compatible_upgrade(packet_version)?;
            }
            if self.crypto.install_keys()? {
                self.saved_datagrams.make_available(Epoch::Handshake);
            }
        }

        Ok(())
    }

    fn set_confirmed(&mut self, now: Instant) -> Res<()> {
        self.set_state(State::Confirmed, now);
        if self.conn_params.pmtud_enabled() {
            self.paths
                .primary()
                .ok_or(Error::Internal)?
                .borrow_mut()
                .pmtud_mut()
                .start(now, &mut self.stats.borrow_mut());
        }
        self.paths.start_ecn(&mut self.stats.borrow_mut());
        Ok(())
    }

    #[expect(clippy::too_many_lines, reason = "Yep, but it's a nice big match.")]
    fn input_frame(
        &mut self,
        path: &PathRef,
        packet_version: Version,
        packet_type: packet::Type,
        frame: Frame,
        next_pn: packet::Number,
        now: Instant,
    ) -> Res<()> {
        if !frame.is_allowed(packet_type) {
            info!("frame not allowed: {frame:?} {packet_type:?}");
            return Err(Error::ProtocolViolation);
        }
        let space = PacketNumberSpace::from(packet_type);
        if frame.is_stream() {
            return self
                .streams
                .input_frame(&frame, &mut self.stats.borrow_mut().frame_rx);
        }
        match frame {
            Frame::Padding(length) => {
                self.stats.borrow_mut().frame_rx.padding += usize::from(length);
            }
            Frame::Ping => {
                // A PING is likely a PTO probe, so resend any CRYPTO the peer may lack.
                self.stats.borrow_mut().frame_rx.ping += 1;
                self.crypto.resend_unacked(space);
                // Send an ACK immediately if we might not otherwise do so.
                self.acks.immediate_ack(space, now);
            }
            Frame::Ack {
                largest_acknowledged,
                ack_delay,
                first_ack_range,
                ack_ranges,
                ecn_count,
            } => {
                // Ensure that the largest acknowledged packet number was actually sent.
                // (If we ever start using non-contiguous packet numbers, we need to check all the
                // packet numbers in the ACKed ranges.)
                if largest_acknowledged >= next_pn {
                    warn!("Largest ACKed {largest_acknowledged} was never sent");
                    return Err(Error::AckedUnsentPacket);
                }

                let ranges =
                    Frame::decode_ack_frame(largest_acknowledged, first_ack_range, &ack_ranges)?;
                self.handle_ack(space, ranges, ecn_count.as_ref(), ack_delay, now)?;
            }
            Frame::Crypto { offset, data } => {
                trace!(
                    "[{self}] Crypto frame on space={space} offset={offset}: {d}",
                    d = HexSnipMiddle::new(data),
                );
                self.stats.borrow_mut().frame_rx.crypto += 1;
                let duplicate = self
                    .crypto
                    .streams_mut()
                    .inbound_frame(space, offset, data)?;

                if self.role == Role::Client
                    && space == PacketNumberSpace::Initial
                    && packet_version != self.version
                {
                    // If the server has switched versions, switch to that version.
                    // This is an assumption, but very often a good one.
                    // This function does nothing if we already have a version.
                    self.compatible_upgrade(packet_version)?;
                }

                let mut buf = Vec::new();
                if self.crypto.streams().data_ready(space)
                    && self.crypto.streams_mut().read_to_end(space, &mut buf)? > 0
                {
                    self.handshake(now, packet_version, space, Some(&buf))?;
                    self.create_resumption_token(now);
                } else if duplicate && space == PacketNumberSpace::Initial {
                    self.crypto.resend_unacked_early(); // See Section 6.2.3 of RFC 9002.
                    self.resend_0rtt(now);
                }
            }
            Frame::NewToken { token } => {
                if self.role == Role::Server || !self.state.connected() {
                    // > Clients MUST NOT send NEW_TOKEN frames. A server MUST
                    // > treat receipt of a NEW_TOKEN frame as a connection error of
                    // > type PROTOCOL_VIOLATION.
                    //
                    // <https://www.rfc-editor.org/rfc/rfc9000.html#name-new_token-frames>
                    return Err(Error::ProtocolViolation);
                }
                self.stats.borrow_mut().frame_rx.new_token += 1;
                self.new_token.save_token(token.to_vec());
                self.create_resumption_token(now);
            }
            Frame::NewConnectionId {
                sequence_number,
                connection_id,
                stateless_reset_token,
                retire_prior,
            } => {
                self.stats.borrow_mut().frame_rx.new_connection_id += 1;
                if self
                    .remote_initial_source_cid
                    .as_ref()
                    .is_some_and(|cid| cid.is_empty())
                {
                    return Err(Error::ProtocolViolation);
                }
                self.cids.add_remote(ConnectionIdEntry::new(
                    sequence_number,
                    ConnectionId::from(connection_id),
                    stateless_reset_token,
                ))?;
                self.paths.retire_cids(retire_prior, &mut self.cids);
                let too_many = if self.cids.len() >= ConnectionIdManager::ACTIVE_LIMIT {
                    Some("received too many active connection IDs")
                } else if self.paths.retire_queue_len() > ConnectionIdManager::MAX_RETIRE_QUEUE {
                    // `MAX_RETIRE_QUEUE` is the actual allowed maximum.  A single call to
                    // `retire_cids` above can add at most `ACTIVE_LIMIT` entries, so the
                    // queue can only ever temporarily exceed the bound by that.
                    Some("too many connection IDs pending retirement")
                } else {
                    None
                };
                if let Some(msg) = too_many {
                    info!("[{self}] {msg}");
                    return Err(Error::ConnectionIdLimitExceeded);
                }
            }
            Frame::RetireConnectionId { sequence_number } => {
                self.stats.borrow_mut().frame_rx.retire_connection_id += 1;
                self.cid_manager.retire(sequence_number)?;
            }
            Frame::PathChallenge { data } => {
                self.stats.borrow_mut().frame_rx.path_challenge += 1;
                // If we were challenged, try to make the path permanent.
                // Report an error if we don't have enough connection IDs.
                self.ensure_permanent(path, now)?;
                path.borrow_mut().challenged(data);
                // A PATH_CHALLENGE indicates the peer sees a different path,
                // so start PMTUD to discover any MTU changes.
                if self.conn_params.pmtud_enabled() {
                    path.borrow_mut()
                        .pmtud_mut()
                        .start(now, &mut self.stats.borrow_mut());
                }
            }
            Frame::PathResponse { data } => {
                self.stats.borrow_mut().frame_rx.path_response += 1;
                let primary = self
                    .paths
                    .path_response(data, now, &mut self.stats.borrow_mut());
                if let Some(primary) = primary {
                    self.path_migrated(&primary);
                }
            }
            Frame::ConnectionClose {
                error_code,
                frame_type,
                reason_phrase,
            } => {
                self.stats.borrow_mut().frame_rx.connection_close += 1;
                info!(
                    "[{self}] ConnectionClose received. Error code: {error_code:?} frame type {frame_type:x} reason {reason_phrase}"
                );
                let (detail, frame_type) = if let CloseError::Application(_) = error_code {
                    // Use a transport error here because we want to send
                    // NO_ERROR in this case.
                    (
                        Error::PeerApplication(error_code.code()),
                        FrameType::ConnectionCloseApplication,
                    )
                } else {
                    (
                        Error::Peer(error_code.code()),
                        FrameType::ConnectionCloseTransport,
                    )
                };
                let error = CloseReason::Transport(detail);
                self.state_signaling
                    .drain(Rc::clone(path), error.clone(), frame_type, "");
                self.set_state(
                    State::Draining {
                        error,
                        timeout: self.get_closing_period_time(now),
                    },
                    now,
                );
            }
            Frame::HandshakeDone => {
                self.stats.borrow_mut().frame_rx.handshake_done += 1;
                if self.role == Role::Server
                    || !self.state.connected()
                    // The server lacks our Finished until we have sent it.
                    || !self.crypto.streams().sent_all(PacketNumberSpace::Handshake)
                {
                    return Err(Error::ProtocolViolation);
                }
                if self.confirmed() {
                    return Ok(());
                }
                self.set_confirmed(now)?;
                self.discard_keys(PacketNumberSpace::Handshake, now);
                self.migrate_to_preferred_address(now)?;
            }
            Frame::AckFrequency {
                seqno,
                tolerance,
                delay,
                ignore_order,
            } => {
                self.stats.borrow_mut().frame_rx.ack_frequency += 1;
                let delay = Duration::from_micros(delay);
                if delay < GRANULARITY {
                    return Err(Error::ProtocolViolation);
                }
                self.acks
                    .ack_freq(seqno, tolerance - 1, delay, ignore_order);
            }
            Frame::Datagram {
                data,
                len_field_len,
                ..
            } => {
                self.stats.borrow_mut().frame_rx.datagram += 1;
                self.quic_datagrams.handle_datagram(data, len_field_len)?;
            }
            _ => unreachable!("All other frames are for streams"),
        }

        Ok(())
    }

    /// Given a set of `sent::Packet` instances, ensure that the source of the packet
    /// is told that they are lost.  This gives the frame generation code a chance
    /// to retransmit the frame as needed.
    fn handle_lost_packets(&mut self, lost_packets: &[sent::Packet]) {
        for lost in lost_packets {
            let space = lost.space();
            for token in lost.tokens() {
                debug!("[{self}] Lost: {token:?}");
                match token {
                    recovery::Token::Ack(_) => {
                        // If we lost an ACK frame during the handshake, send another one.
                        if space != PacketNumberSpace::ApplicationData {
                            self.acks.immediate_ack(space, lost.time_sent());
                        }
                    }
                    recovery::Token::Crypto(ct) => self.crypto.lost(space, ct),
                    recovery::Token::HandshakeDone => self.state_signaling.handshake_done(),
                    recovery::Token::NewToken(seqno) => self.new_token.lost(*seqno),
                    recovery::Token::NewConnectionId(ncid) => self.cid_manager.lost(ncid),
                    recovery::Token::RetireConnectionId(seqno) => {
                        self.paths.lost_retire_cid(*seqno);
                    }
                    recovery::Token::AckFrequency(rate) => self.paths.lost_ack_frequency(*rate),
                    recovery::Token::KeepAlive => self.idle_timeout.lost_keep_alive(),
                    recovery::Token::Stream(stream_token) => self.streams.lost(stream_token),
                    recovery::Token::Datagram(dgram_tracker) => {
                        self.events
                            .datagram_outcome(dgram_tracker, OutgoingDatagramOutcome::Lost);
                        self.stats.borrow_mut().datagram_tx.lost += 1;
                    }
                    recovery::Token::EcnEct0 => self.paths.lost_ecn(&mut self.stats.borrow_mut()),
                    // PMTUD probe loss is handled by the PMTUD state machine.
                    recovery::Token::PmtudProbe(_) => (),
                }
            }
        }
    }

    fn decode_ack_delay(&self, v: u64) -> Res<Duration> {
        // If we have remote transport parameters, use them.
        // Otherwise, ack delay should be zero (because it's the handshake).
        self.tps.borrow().remote_handshake().map_or_else(
            || Ok(Duration::default()),
            |r| {
                let exponent = u32::try_from(r.get_integer(AckDelayExponent))?;
                // ACK_DELAY_EXPONENT > 20 is invalid per RFC9000. We already checked that in
                // TransportParameter::decode.
                let corrected = if v.leading_zeros() >= exponent {
                    v << exponent
                } else {
                    u64::MAX
                };
                Ok(Duration::from_micros(corrected))
            },
        )
    }

    fn handle_ack<R>(
        &mut self,
        space: PacketNumberSpace,
        ack_ranges: R,
        ack_ecn: Option<&ecn::Count>,
        ack_delay: u64,
        now: Instant,
    ) -> Res<()>
    where
        R: IntoIterator<Item = RangeInclusive<packet::Number>, IntoIter: ExactSizeIterator> + Debug,
    {
        debug!("[{self}] Rx ACK space={space}, ranges={ack_ranges:?}");

        let Some(path) = self.paths.primary() else {
            return Ok(());
        };
        let (acked_packets, lost_packets) = self.loss_recovery.on_ack_received(
            &path,
            space,
            ack_ranges,
            ack_ecn,
            self.decode_ack_delay(ack_delay)?,
            now,
        );
        let largest_acknowledged = acked_packets.first().map(sent::Packet::pn);
        qlog::packets_acked(&mut self.qlog, space, &acked_packets, now);
        let mut bytes_acked = 0;
        for acked in acked_packets {
            bytes_acked += acked.len();
            for token in acked.tokens() {
                match token {
                    recovery::Token::Stream(stream_token) => self.streams.acked(stream_token),
                    recovery::Token::Ack(at) => self.acks.acked(space, at),
                    recovery::Token::Crypto(ct) => self.crypto.acked(space, ct),
                    recovery::Token::NewToken(seqno) => self.new_token.acked(*seqno),
                    recovery::Token::NewConnectionId(entry) => self.cid_manager.acked(entry),
                    recovery::Token::RetireConnectionId(seqno) => {
                        self.paths.acked_retire_cid(*seqno);
                    }
                    recovery::Token::AckFrequency(rate) => self.paths.acked_ack_frequency(*rate),
                    recovery::Token::KeepAlive => self.idle_timeout.ack_keep_alive(),
                    recovery::Token::Datagram(dgram_tracker) => self
                        .events
                        .datagram_outcome(dgram_tracker, OutgoingDatagramOutcome::Acked),
                    recovery::Token::EcnEct0 => self.paths.acked_ecn(),
                    // We don't care about these being ACK'ed
                    recovery::Token::HandshakeDone | recovery::Token::PmtudProbe(_) => (),
                }
            }
        }
        self.handle_lost_packets(&lost_packets);
        qlog::packets_lost(&mut self.qlog, &lost_packets, now);
        let mut stats = self.stats.borrow_mut();
        stats.bytes_acked += bytes_acked;
        stats.frame_rx.ack += 1;
        if let Some(largest_acknowledged) = largest_acknowledged {
            stats.frame_rx.largest_acknowledged =
                max(stats.frame_rx.largest_acknowledged, largest_acknowledged);
        }
        Ok(())
    }

    /// Tell 0-RTT packets that they were "lost".
    fn resend_0rtt(&mut self, now: Instant) {
        if let Some(path) = self.paths.primary() {
            let dropped = self.loss_recovery.drop_0rtt(&path, now);
            self.handle_lost_packets(&dropped);
        }
    }

    /// When the server rejects 0-RTT we need to drop a bunch of stuff.
    fn client_0rtt_rejected(&mut self, now: Instant) {
        if !matches!(self.zero_rtt_state, ZeroRttState::Sending) {
            return;
        }
        debug!("[{self}] 0-RTT rejected");
        self.resend_0rtt(now);
        self.streams.zero_rtt_rejected();
        self.crypto.states_mut().discard_0rtt_keys();
        self.events.client_0rtt_rejected();
    }

    fn set_connected(&mut self, now: Instant) -> Res<()> {
        debug!("[{self}] TLS connection complete");
        if self
            .crypto
            .tls()
            .info()
            .map(SecretAgentInfo::alpn)
            .is_none()
        {
            warn!("[{self}] No ALPN, closing connection");
            // 120 = no_application_protocol
            return Err(Error::CryptoAlert(120));
        }
        if self.role == Role::Server {
            // Remove the randomized client CID from the list of acceptable CIDs.
            self.cid_manager.remove_odcid();
            // Mark the path as validated, if it isn't already.
            let path = self.paths.primary().ok_or(Error::NoAvailablePath)?;
            path.borrow_mut().set_valid(now);
            // Generate a qlog event that the server connection started.
            qlog::connection_started(&mut self.qlog, &path, now);
            qlog::recovery_parameters_set(
                &mut self.qlog,
                path.borrow().plpmtu(),
                self.conn_params.get_congestion_control(),
                now,
            );
            qlog::congestion_state_updated(
                &mut self.qlog,
                None,
                Phase::SlowStart.into(),
                None,
                now,
            );
        } else {
            self.zero_rtt_state = if self
                .crypto
                .tls()
                .info()
                .ok_or(Error::Internal)?
                .early_data_accepted()
            {
                ZeroRttState::AcceptedClient
            } else {
                self.client_0rtt_rejected(now);
                ZeroRttState::Rejected
            };
        }

        // Setting application keys has to occur after 0-RTT rejection.
        let pto = self.pto();
        self.crypto
            .install_application_keys(self.version, now + pto)?;
        self.process_tps(now)?;
        self.set_state(State::Connected, now);
        self.create_resumption_token(now);
        self.saved_datagrams.make_available(Epoch::ApplicationData);
        self.stats.borrow_mut().resumed =
            self.crypto.tls().info().ok_or(Error::Internal)?.resumed();
        if self.role == Role::Server {
            self.state_signaling.handshake_done();
            self.set_confirmed(now)?;
        }
        info!("[{self}] Connection established");
        Ok(())
    }

    fn set_state(&mut self, state: State, now: Instant) {
        if state > self.state {
            debug!("[{self}] State change from {:?} -> {state:?}", self.state);
            let old_state = self.state.clone();
            self.state = state.clone();
            if self.state.closed() {
                self.streams.clear_streams();
            }
            self.events.connection_state_change(state);
            qlog::connection_state_updated(&mut self.qlog, &old_state, &self.state, now);
            if let State::Closed(reason) = &self.state {
                qlog::connection_closed(&mut self.qlog, reason, now);
            }
        } else if mem::discriminant(&state) != mem::discriminant(&self.state) {
            // Only tolerate a regression in state if the new state is closing
            // and the connection is already closed.
            debug_assert!(matches!(
                state,
                State::Closing { .. } | State::Draining { .. }
            ));
            debug_assert!(self.state.closed());
        }
    }

    /// Create a stream.
    /// Returns new stream id
    ///
    /// # Errors
    ///
    /// `ConnectionState` if the connection stat does not allow to create streams.
    /// `StreamLimitError` if we are limited by server's stream concurrence.
    pub fn stream_create(&mut self, st: StreamType) -> Res<StreamId> {
        // Can't make streams while closing, otherwise rely on the stream limits.
        match self.state {
            State::Closing { .. } | State::Draining { .. } | State::Closed { .. } => {
                return Err(Error::ConnectionState);
            }
            State::WaitInitial | State::Handshaking
                if self.role == Role::Client && self.zero_rtt_state != ZeroRttState::Sending =>
            {
                return Err(Error::ConnectionState);
            }
            // In all other states, trust that the stream limits are correct.
            _ => (),
        }

        self.streams.stream_create(st)
    }

    /// Raise the maximum number of concurrent incoming streams of `stream_type`.
    ///
    /// This is a local receive window: the cumulative stream count advertised
    /// in `MAX_STREAMS` (RFC 9000, Section 4.6) advances by it as the peer's
    /// streams are retired.  Raise-only: a `max` not greater than the current
    /// window has no effect.  Shrinking the window would also be legal, taking
    /// effect as streams retire, but no caller needs it.  A `max` above
    /// [`MAX_STREAMS_LIMIT`] is clamped to it.
    pub fn set_remote_max_streams(&mut self, stream_type: StreamType, max: u64) {
        debug_assert!(max <= MAX_STREAMS_LIMIT, "max_streams is too large");
        self.streams.set_remote_max_streams(stream_type, max);
    }

    /// Set the priority of a stream.
    ///
    /// # Errors
    ///
    /// `InvalidStreamId` the stream does not exist.
    pub fn stream_priority(
        &mut self,
        stream_id: StreamId,
        transmission: TransmissionPriority,
        retransmission: RetransmissionPriority,
    ) -> Res<()> {
        self.streams
            .get_send_stream_mut(stream_id)?
            .set_priority(transmission, retransmission);
        Ok(())
    }

    /// Set the `SendOrder` of a stream.  Re-enqueues to keep the ordering correct
    ///
    /// # Errors
    /// When the stream does not exist.
    pub fn stream_sendorder(
        &mut self,
        stream_id: StreamId,
        sendorder: Option<SendOrder>,
    ) -> Res<()> {
        self.streams.set_sendorder(stream_id, sendorder)
    }

    /// Set the Fairness of a stream
    ///
    /// # Errors
    /// When the stream does not exist.
    pub fn stream_fairness(&mut self, stream_id: StreamId, fairness: bool) -> Res<()> {
        self.streams.set_fairness(stream_id, fairness)
    }

    /// Assign a stream to a send group for per-group sendOrder namespacing and fair
    /// bandwidth allocation between groups per the WebTransport spec.
    ///
    /// # Errors
    /// When the stream does not exist.
    pub fn stream_sendgroup(
        &mut self,
        stream_id: StreamId,
        group_id: Option<SendGroupId>,
    ) -> Res<()> {
        self.streams.set_sendgroup(stream_id, group_id)
    }

    /// # Errors
    /// When the stream does not exist.
    pub fn send_stream_stats(&self, stream_id: StreamId) -> Res<send_stream::Stats> {
        self.streams
            .get_send_stream(stream_id)
            .map(SendStream::stats)
    }

    /// # Errors
    /// When the stream does not exist.
    pub fn recv_stream_stats(&mut self, stream_id: StreamId) -> Res<recv_stream::Stats> {
        let stream = self.streams.get_recv_stream_mut(stream_id)?;

        Ok(stream.stats())
    }

    /// Send data on a stream.
    /// Returns how many bytes were successfully sent. Could be less
    /// than total, based on receiver credit space available, etc.
    ///
    /// # Errors
    ///
    /// `InvalidStreamId` the stream does not exist,
    /// `InvalidInput` if length of `data` is zero,
    /// `FinalSizeError` if the stream has already been closed.
    pub fn stream_send(&mut self, stream_id: StreamId, data: &[u8]) -> Res<usize> {
        self.streams.get_send_stream_mut(stream_id)?.send(data)
    }

    /// Send all data or nothing on a stream. May cause `DATA_BLOCKED` or
    /// `STREAM_DATA_BLOCKED` frames to be sent.
    /// Returns true if data was successfully sent, otherwise false.
    ///
    /// # Errors
    ///
    /// `InvalidStreamId` the stream does not exist,
    /// `InvalidInput` if length of `data` is zero,
    /// `FinalSizeError` if the stream has already been closed.
    pub fn stream_send_atomic(&mut self, stream_id: StreamId, data: &[u8]) -> Res<bool> {
        let val = self
            .streams
            .get_send_stream_mut(stream_id)?
            .send_atomic(data);
        if let Ok(val) = val {
            debug_assert!(
                val == 0 || val == data.len(),
                "Unexpected value {val} when trying to send {} bytes atomically",
                data.len()
            );
        }
        val.map(|v| v == data.len())
    }

    /// Bytes that `stream_send()` is guaranteed to accept for sending.
    /// i.e. that will not be blocked by flow credits or send buffer max
    /// capacity.
    /// # Errors
    /// When the stream ID is invalid.
    pub fn stream_avail_send_space(&self, stream_id: StreamId) -> Res<usize> {
        Ok(self.streams.get_send_stream(stream_id)?.avail())
    }

    /// Set low watermark for [`ConnectionEvent::SendStreamWritable`] event.
    ///
    /// Stream emits a [`crate::ConnectionEvent::SendStreamWritable`] event
    /// when:
    /// - the available sendable bytes increased to or above the watermark
    /// - and was previously below the watermark.
    ///
    /// Default value is `1`. In other words
    /// [`crate::ConnectionEvent::SendStreamWritable`] is emitted whenever the
    /// available sendable bytes was previously at `0` and now increased to `1`
    /// or more.
    ///
    /// Use this when your protocol needs at least `watermark` amount of available
    /// sendable bytes to make progress.
    ///
    /// # Errors
    /// When the stream ID is invalid.
    pub fn stream_set_writable_event_low_watermark(
        &mut self,
        stream_id: StreamId,
        watermark: NonZeroUsize,
    ) -> Res<()> {
        self.streams
            .set_writable_event_low_watermark(stream_id, watermark)
    }

    /// Close the stream. Enqueued data will be sent.
    /// # Errors
    /// When the stream ID is invalid.
    pub fn stream_close_send(&mut self, stream_id: StreamId) -> Res<()> {
        self.streams.get_send_stream_mut(stream_id)?.close();
        Ok(())
    }

    /// Commit to reliably delivering all stream data buffered so far: if the stream is later
    /// reset via [`Self::stream_reset_send`], that prefix is still delivered using
    /// `RESET_STREAM_AT`. Call this after writing the data to be protected.
    /// # Errors
    /// When the stream ID is invalid, the peer did not enable reliable reset
    /// ([`Error::NotAvailable`]), or the stream has already been reset ([`Error::StreamState`]).
    pub fn stream_commit(&mut self, stream_id: StreamId) -> Res<()> {
        // Get the stream first: it cannot exist without remote transport parameters, so
        // `remote()` below won't panic.
        let stream = self.streams.get_send_stream_mut(stream_id)?;
        if !self.tps.borrow().remote().get_empty(ResetStreamAt) {
            return Err(Error::NotAvailable);
        }
        stream.commit()
    }

    /// Abandon transmission of in-flight and future stream data.
    ///
    /// If a reliable prefix was committed via [`Self::stream_commit`] and the peer advertised
    /// support for reliable reset, the committed prefix is delivered using `RESET_STREAM_AT`;
    /// otherwise a plain `RESET_STREAM` is sent.
    /// # Errors
    /// When the stream ID is invalid.
    pub fn stream_reset_send(&mut self, stream_id: StreamId, err: AppError) -> Res<()> {
        self.streams.get_send_stream_mut(stream_id)?.reset(err);
        Ok(())
    }

    /// Read buffered data from stream. bool says whether read bytes includes
    /// the final data on stream.
    ///
    /// # Errors
    ///
    /// `InvalidStreamId` if the stream does not exist.
    /// `NoMoreData` if data and fin bit were previously read by the application.
    pub fn stream_recv(&mut self, stream_id: StreamId, data: &mut [u8]) -> Res<(usize, bool)> {
        self.streams.recv(stream_id, data)
    }

    /// Application is no longer interested in this stream.
    /// # Errors
    /// When the stream ID is invalid.
    pub fn stream_stop_sending(&mut self, stream_id: StreamId, err: AppError) -> Res<()> {
        self.streams.stop_sending(stream_id, err)
    }

    /// Increases `max_stream_data` for a `stream_id`.
    ///
    /// # Errors
    ///
    /// Returns `InvalidStreamId` if a stream does not exist or the receiving
    /// side is closed.
    pub fn set_stream_max_data(&mut self, stream_id: StreamId, max_data: u64) -> Res<()> {
        let stream = self.streams.get_recv_stream_mut(stream_id)?;

        stream.set_stream_max_data(max_data);
        Ok(())
    }

    /// Mark a receive stream as being important enough to keep the connection alive
    /// (if `keep` is `true`) or no longer important (if `keep` is `false`).  If any
    /// stream is marked this way, PING frames will be used to keep the connection
    /// alive, even when there is no activity.
    ///
    /// # Errors
    ///
    /// Returns `InvalidStreamId` if a stream does not exist or the receiving
    /// side is closed.
    pub fn stream_keep_alive(&mut self, stream_id: StreamId, keep: bool) -> Res<()> {
        self.streams.keep_alive(stream_id, keep)
    }

    #[must_use]
    pub const fn remote_datagram_size(&self) -> u64 {
        self.quic_datagrams.remote_datagram_size()
    }

    /// Whether the peer advertised support for reliable stream reset (`RESET_STREAM_AT`).
    /// Returns `false` until peer transport parameters are available.
    #[must_use]
    pub fn peer_supports_reliable_stream_reset(&self) -> bool {
        let tps = self.tps.borrow();
        tps.remote_handshake()
            .or_else(|| tps.remote_0rtt())
            .is_some_and(|tp| tp.get_empty(ResetStreamAt))
    }

    /// Returns the current max size of a datagram that can fit into a packet.
    /// The value will change over time depending on the encoded size of the
    /// packet number, ack frames, etc.
    ///
    /// # Errors
    /// The function returns `NotAvailable` if datagrams are not enabled.
    /// # Panics
    /// Basically never, because that unwrap won't fail.
    pub fn max_datagram_size(&self) -> Res<u64> {
        let max_dgram_frame_size = self.quic_datagrams.remote_datagram_size();
        if max_dgram_frame_size == 0 {
            return Err(Error::NotAvailable);
        }
        let version = self.version();
        let Some((epoch, tx)) = self
            .crypto
            .states()
            .select_tx(self.version, PacketNumberSpace::ApplicationData)
        else {
            return Err(Error::NotAvailable);
        };
        let path = self.paths.primary().ok_or(Error::NotAvailable)?;
        let mtu = path.borrow().plpmtu();
        let mut buffer = Vec::new();
        let encoder = Encoder::new_borrowed_vec(&mut buffer);

        let (_, builder, _) = Self::build_packet_header(
            &path.borrow(),
            epoch,
            encoder,
            tx,
            &self.address_validation,
            version,
            false,
            usize::MAX,
            self.loss_recovery
                .largest_acknowledged_pn(PacketNumberSpace::ApplicationData),
        );

        let data_len_possible = to_u64(
            mtu.saturating_sub(tx.expansion() + builder.len() + DATAGRAM_FRAME_TYPE_VARINT_LEN),
        );
        Ok(min(
            data_len_possible,
            max_dgram_frame_size.saturating_sub(to_u64(DATAGRAM_FRAME_TYPE_VARINT_LEN)),
        ))
    }

    /// The primary path's minimum RTT estimate, from which `QuicDatagrams`
    /// derives the default outgoing-datagram max-age.
    fn min_rtt(&self) -> Duration {
        self.paths
            .primary()
            .map_or(DEFAULT_INITIAL_RTT, |p| p.borrow().rtt().minimum())
    }

    /// Enqueue a datagram on `session`'s outgoing queue: byte-budgeted,
    /// max-buffered-tracked, and scheduled by `send_group_id`/
    /// `send_order` and age. `session` is opaque to `Connection` — nothing
    /// here requires it to be a real stream, only a stable per-caller tag
    /// (in practice, an Extended CONNECT session's control-stream `StreamId`).
    /// `send_group_id` of `SendGroupId::new(0)` means ungrouped.
    ///
    /// The *path* MTU is applied later, at packet-build time, exactly like
    /// queued stream data.
    ///
    /// # Errors
    ///
    /// Returns `TooMuchData` if the DATAGRAM frame containing `data` would
    /// exceed [`Self::remote_datagram_size`], including any datagram while
    /// that is `0` (the peer sent no `max_datagram_frame_size` and so does
    /// not support DATAGRAM frames at all).
    pub fn enqueue_datagram(
        &mut self,
        session: StreamId,
        data: Vec<u8>,
        id: Option<DatagramId>,
        now: Instant,
        send_group_id: SendGroupId,
        send_order: SendOrder,
    ) -> Res<DatagramQueueOutcome> {
        let min_rtt = self.min_rtt();
        self.quic_datagrams.enqueue_datagram(
            session,
            data,
            id,
            now,
            send_group_id,
            send_order,
            min_rtt,
        )
    }

    /// See `DatagramQueue::set_max_buffered_datagrams`.
    pub fn set_max_buffered_datagrams(&mut self, session: StreamId, mark: Option<NonZeroUsize>) {
        self.quic_datagrams
            .set_max_buffered_datagrams(session, mark);
    }

    /// See `DatagramQueue::set_max_age`. Unlike the inner engine's own
    /// method, does not take a default max-age: this connection's own RTT
    /// estimate supplies it.
    pub fn set_datagram_max_age(
        &mut self,
        session: StreamId,
        max_age: Option<Duration>,
        now: Instant,
    ) {
        let min_rtt = self.min_rtt();
        self.quic_datagrams
            .set_datagram_max_age(session, max_age, now, min_rtt);
    }

    /// See `DatagramQueue::capacity`.
    #[must_use]
    pub fn datagram_queue_capacity(&self, session: StreamId) -> DatagramQueueCapacity {
        self.quic_datagrams.datagram_queue_capacity(session)
    }

    /// Expire stale datagrams on a single session's queue and return how many
    /// of its datagrams have expired since the last call, whichever sweep
    /// shed them, for a caller that counts expiries per session.
    ///
    /// This is the second of two sweeps over the same queues. `process_timer`
    /// runs the first on the connection's own schedule and never reports what
    /// it shed; this one lets a caller attribute expiries to a session and
    /// pick them up on its own tick, before or after `process_output`. The
    /// count accumulates on the queue, so the order of the two within a tick
    /// changes nothing: every expiry is reported exactly once, by whoever
    /// drains it. Each call re-derives the RTT-based default max-age, which
    /// is one `min_rtt` per session per tick.
    pub fn expire_session_datagrams(&mut self, session: StreamId, now: Instant) -> u64 {
        let min_rtt = self.min_rtt();
        self.quic_datagrams
            .expire_session_datagrams(session, now, min_rtt)
    }

    /// [`Self::expire_session_datagrams`] without the sweep: only return the
    /// count accumulated so far.
    pub fn take_session_expired_datagrams(&mut self, session: StreamId) -> u64 {
        self.quic_datagrams.take_session_expired_count(session)
    }

    /// Remove `session`'s queue, e.g. because the session is closing. Returns
    /// how many datagrams were still queued, and the sent, expiry and
    /// too-big counts not yet taken.
    pub fn drop_session_datagrams(&mut self, session: StreamId) -> DroppedDatagrams {
        self.quic_datagrams.drop_session_datagrams(session)
    }

    /// The number of `session`'s datagrams written into a QUIC packet since
    /// the last call.  Not a delivery signal: the packet can still be lost,
    /// and a DATAGRAM frame is never retransmitted.  Tracked datagrams get
    /// their delivery outcome via [`ConnectionEvent::OutgoingDatagramOutcome`].
    ///
    /// [`ConnectionEvent::OutgoingDatagramOutcome`]: crate::ConnectionEvent::OutgoingDatagramOutcome
    pub fn take_session_sent_datagrams(&mut self, session: StreamId) -> u64 {
        self.quic_datagrams.take_session_sent_count(session)
    }

    /// The number of `session`'s datagrams dropped at packet-build time since
    /// the last call, for not fitting the path MTU.  Enqueueing only checks
    /// the peer's `max_datagram_frame_size`, which can exceed the path MTU.
    pub fn take_session_too_big_datagrams(&mut self, session: StreamId) -> u64 {
        self.quic_datagrams.take_session_too_big_count(session)
    }

    /// Return the PLMTU of the primary path.
    ///
    /// # Panics
    ///
    /// The function panics if there is no primary path. (Should be fine for test usage.)
    #[cfg(test)]
    #[must_use]
    pub fn plpmtu(&self) -> usize {
        self.paths.primary().unwrap().borrow().plpmtu()
    }

    fn log_packet(&mut self, meta: packet::MetaData, now: Instant) {
        if tracing::enabled!(tracing::Level::DEBUG) {
            let mut s = String::new();
            let mut d = Decoder::from(meta.payload());
            while d.remaining() > 0 {
                let Ok(f) = Frame::decode(&mut d) else {
                    s.push_str(" [broken]...");
                    break;
                };
                let x = f.dump();
                if !x.is_empty() {
                    _ = write!(&mut s, "\n  {} {x}", meta.direction());
                }
            }
            debug!("[{self}] {meta}{s}");
        }

        qlog::packet_io(&mut self.qlog, meta, now);
    }
}

impl EventProvider for Connection {
    type Event = ConnectionEvent;

    /// Return true if there are outstanding events.
    fn has_events(&self) -> bool {
        self.events.has_events()
    }

    /// Get events that indicate state changes on the connection. This method
    /// correctly handles cases where handling one event can obsolete
    /// previously-queued events, or cause new events to be generated.
    fn next_event(&mut self) -> Option<Self::Event> {
        self.events.next_event()
    }
}

impl Display for Connection {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "{:?} ", self.role)?;
        if let Some(cid) = self.odcid() {
            Display::fmt(&cid, f)
        } else {
            write!(f, "...")
        }
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests;
