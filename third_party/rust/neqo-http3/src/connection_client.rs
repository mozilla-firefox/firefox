// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cell::RefCell,
    fmt::{self, Display, Formatter},
    iter,
    net::SocketAddr,
    num::NonZeroUsize,
    ops::Deref,
    rc::Rc,
    time::Instant,
};

use neqo_common::{
    Datagram, Decoder, Encoder, Header, Role,
    event::Provider as EventProvider,
    hex::{Hex, HexWithLen},
    qlog::Qlog,
};
use neqo_qpack::Stats as QpackStats;
use neqo_transport::{
    AppError, Connection, ConnectionEvent, ConnectionId, ConnectionIdGenerator, Output,
    OutputBatch, Stats as TransportStats, StreamId, StreamType, Version, ZeroRttState,
};
use nss::{AuthenticationStatus, ResumptionToken, SecretAgentInfo, cert::CertificateInfo};
use tracing::{debug, info, trace, warn};

use crate::{
    Error, Http3Parameters, Http3StreamType, NewStreamType, Priority, ReceiveOutput, Res,
    SendGroupId, Stream,
    client_events::{Http3ClientEvent, Http3ClientEvents, WebTransportEvent},
    connection::{Http3Connection, Http3State, RequestDescription},
    features::ConnectType,
    frames::HFrame,
    request_target::RequestTarget,
    settings::HSettings,
};

// Filters send_streams and recv_streams for request streams (Http or ExtendedConnect) with an ID
// greater than or equal to a given ID of the same directionality.
fn id_gte<U>(base: StreamId) -> impl FnMut((&StreamId, &U)) -> Option<StreamId> + 'static
where
    U: Deref<Target: Stream>,
{
    move |(id, s)| {
        (*id >= base
            && !(id.is_bidi() ^ base.is_bidi())
            && matches!(
                s.stream_type(),
                Http3StreamType::Http | Http3StreamType::ExtendedConnect
            ))
        .then_some(*id)
    }
}

const fn alpn_from_quic_version(version: Version) -> &'static str {
    match version {
        Version::Version2 | Version::Version1 => "h3",
        #[cfg(feature = "draft-29")]
        Version::Draft29 => "h3-29",
    }
}

/// # The HTTP/3 client API
///
/// This module implements the HTTP/3 client API. The main implementation of the protocol is in
/// [connection.rs](https://github.com/mozilla/neqo/blob/main/neqo-http3/src/connection.rs) which
/// implements common behavior for the client-side and the server-side. `Http3Client` structure
/// implements the public API and set of functions that differ between the client and the server.
///
/// The API is used for:
/// - create and close an endpoint:
///   - [`Http3Client::new`]
///   - [`Http3Client::new_with_conn`]
///   - [`Http3Client::close`]
/// - configuring an endpoint:
///   - [`Http3Client::authenticated`]
///   - [`Http3Client::enable_ech`]
///   - [`Http3Client::enable_resumption`]
///   - [`Http3Client::set_qlog`]
/// - retrieving information about a connection:
/// - [`Http3Client::peer_certificate`]
///   - [`Http3Client::qpack_encoder_stats`]
///   - [`Http3Client::transport_stats`]
///   - [`Http3Client::state`]
///   - [`Http3Client::tls_info`]
/// - driving HTTP/3 session:
///   - [`Http3Client::process_output`]
///   - [`Http3Client::process_input`]
///   - [`Http3Client::process`]
/// - create requests, send/receive data, and cancel requests:
///   - [`Http3Client::fetch`]
///   - [`Http3Client::send_data`]
///   - [`Http3Client::read_data`]
///   - [`Http3Client::stream_close_send`]
///   - [`Http3Client::cancel_fetch`]
///   - [`Http3Client::stream_reset_send`]
///   - [`Http3Client::stream_stop_sending`]
/// - priority feature:
///   - [`Http3Client::priority_update`]
/// - `WebTransport` feature, see [`crate::webtransport::ClientSession`]
///
/// ## Examples
///
/// ### Fetching a resource
///
/// ```ignore
/// let mut client = Http3Client::new(...);
///
/// // Perform a handshake
/// ...
///
/// let req = client
///     .fetch(
///         Instant::now(),
///         "GET",
///         &("https", "something.com", "/"),
///         &[Header::new("example1", "value1"), Header::new("example1", "value2")],
///         Priority::default(),
///     )
///     .unwrap();
///
/// client.stream_close_send(req).unwrap();
///
/// loop {
///     // exchange packets
///     ...
///
///     while let Some(event) = client.next_event() {
///         match event {
///             Http3ClientEvent::HeaderReady { stream_id, headers, interim, fin } => {
///                 println!("New response headers received for stream {:?} [fin={?}, interim={:?}]: {:?}",
///                     stream_id,
///                     fin,
///                     interim,
///                     headers,
///                 );
///             }
///             Http3ClientEvent::DataReadable { stream_id } => {
///                 println!("New data available on stream {stream_id}");
///                let mut buf = [0; 100];
///                let (amount, fin) = client.read_data(now(), stream_id, &mut buf).unwrap();
///                 println!("Read {:?} bytes from stream {:?} [fin={?}]",
///                     amount,
///                     stream_id,
///                     fin,
///                 );
///             }
///             _ => {
///                 println!("Unhandled event {:?}", event);
///             }
///         }
///     }
/// }
/// ```
///
/// ### Creating a `WebTransport` session
///
/// ```ignore
/// let mut client = Http3Client::new(...);
///
/// // Perform a handshake
/// ...
///
/// // Create a session
/// let wt_session_id = client
///     .webtransport_create_session(now(), &("https", "something.com", "/"), &[])
///     .unwrap();
///
/// loop {
///     // exchange packets
///     ...
///
///     while let Some(event) = client.next_event() {
///         match event {
///             Http3ClientEvent::WebTransport(WebTransportEvent::NewSession{
///                 stream_id,
///                 status,
///                 ..
///             }) => {
///                 println!("The response from the server: WebTransport session ID {:?} status={:?}",
///                     stream_id,
///                     status,
///                 );
///             }
///             _ => {
///                 println!("Unhandled event {:?}", event);
///             }
///         }
///     }
/// }
/// ```
///
/// ### `WebTransport`: create a stream, send and receive data on the stream
///
/// ```ignore
/// const BUF_CLIENT: &[u8] = &[0; 10];
/// // wt_session_id is the session ID of a newly created WebTransport session, see the example above.
///
/// // create a  stream
/// let wt_stream_id = client
///     .webtransport_create_stream(wt_session_id, StreamType::BiDi)
///     .unwrap();
///
/// // send data
/// let data_sent = client.send_data(wt_stream_id, BUF_CLIENT).unwrap();
/// assert_eq!(data_sent, BUF_CLIENT.len());
///
/// // close stream for sending
/// client.stream_close_send(wt_stream_id).unwrap();
///
/// // wait for data from the server
/// loop {
///     // exchange packets
///     ...
///
///     while let Some(event) = client.next_event() {
///         match event {
///             Http3ClientEvent::DataReadable{ stream_id } => {
///                 println!("Data received form the server on WebTransport stream ID {:?}",
///                     stream_id,
///                 );
///                 let mut buf = [0; 100];
///                 let (amount, fin) = client.read_data(now(), stream_id, &mut buf).unwrap();
///                 println!("Read {:?} bytes from stream {:?} [fin={?}]",
///                     amount,
///                     stream_id,
///                     fin,
///                 );
///             }
///             _ => {
///                 println!("Unhandled event {:?}", event);
///             }
///         }
///     }
/// }
/// ```
///
/// ### `WebTransport`: receive a new stream form the server
///
/// ```ignore
/// // wt_session_id is the session ID of a newly created WebTransport session, see the example above.
///
/// // wait for a new stream from the server
/// loop {
///     // exchange packets
///     ...
///
///     while let Some(event) = client.next_event() {
///         match event {
///             Http3ClientEvent::WebTransport(WebTransportEvent::NewStream {
///                 stream_id,
///                 session_id,
///             }) => {
///                 println!("New stream received on session{:?}, stream id={:?} stream type={:?}",
///                     session_id.stream_id(),
///                     stream_id.stream_id(),
///                     stream_id.stream_type()
///                 );
///             }
///             Http3ClientEvent::DataReadable{ stream_id } => {
///                 println!("Data received form the server on WebTransport stream ID {:?}",
///                     stream_id,
///                 );
///                 let mut buf = [0; 100];
///                 let (amount, fin) = client.read_data(now(), stream_id, &mut buf).unwrap();
///                 println!("Read {:?} bytes from stream {:?} [fin={:?}]",
///                     amount,
///                     stream_id,
///                     fin,
///                 );
///             }
///             _ => {
///                 println!("Unhandled event {:?}", event);
///             }
///         }
///     }
/// }
/// ```
pub struct Http3Client {
    conn: Connection,
    base_handler: Http3Connection,
    events: Http3ClientEvents,
    /// A later GOAWAY with this or a higher stream ID rejects no request, if known.
    goaway_noop_from: Option<StreamId>,
}

impl Display for Http3Client {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "Http3 client")
    }
}

impl Http3Client {
    /// # Errors
    ///
    /// Making a `neqo-transport::connection` may produce an error. This can only be a crypto error
    /// if the crypto context can't be created or configured.
    pub fn new<I: Into<String>>(
        server_name: I,
        cid_manager: Rc<RefCell<dyn ConnectionIdGenerator>>,
        local_addr: SocketAddr,
        remote_addr: SocketAddr,
        http3_parameters: Http3Parameters,
        now: Instant,
    ) -> Res<Self> {
        Ok(Self::new_with_conn(
            Connection::new_client(
                server_name,
                &[alpn_from_quic_version(
                    http3_parameters
                        .get_connection_parameters()
                        .get_versions()
                        .initial(),
                )],
                cid_manager,
                local_addr,
                remote_addr,
                http3_parameters.get_connection_parameters().clone(),
                now,
            )?,
            http3_parameters,
        ))
    }

    /// This is a similar function to `new`. In this case, `neqo-transport::connection` has been
    /// already created.
    ///
    /// It is recommended to use `new` instead.
    #[must_use]
    pub fn new_with_conn(c: Connection, http3_parameters: Http3Parameters) -> Self {
        let events = Http3ClientEvents::default();
        let mut base_handler = Http3Connection::new(http3_parameters, Role::Client);
        base_handler.set_features_listener(events.clone());
        Self {
            conn: c,
            events,
            base_handler,
            goaway_noop_from: None,
        }
    }

    pub(crate) const fn connection(&self) -> &Connection {
        &self.conn
    }

    pub(crate) const fn connection_mut(&mut self) -> &mut Connection {
        &mut self.conn
    }

    pub(crate) const fn handler(&self) -> &Http3Connection {
        &self.base_handler
    }

    pub(crate) const fn connection_and_handler(
        &mut self,
    ) -> (&mut Connection, &mut Http3Connection) {
        (&mut self.conn, &mut self.base_handler)
    }

    pub(crate) const fn client_events(&self) -> &Http3ClientEvents {
        &self.events
    }

    /// The function returns the current state of the connection.
    #[must_use]
    pub fn state(&self) -> Http3State {
        self.base_handler.state().clone()
    }

    #[must_use]
    pub fn tls_info(&self) -> Option<&SecretAgentInfo> {
        self.conn.tls_info()
    }

    /// Get the peer's certificate.
    #[must_use]
    pub fn peer_certificate(&self) -> Option<CertificateInfo> {
        self.conn.peer_certificate()
    }

    /// This called when peer certificates have been verified.
    ///
    /// `Http3ClientEvent::AuthenticationNeeded` event is emitted when peer’s certificates are
    /// available and need to be verified. When the verification is completed this function is
    /// called. To inform HTTP/3 session of the verification results.
    pub fn authenticated(&mut self, status: AuthenticationStatus, now: Instant) {
        self.conn.authenticated(status, now);
    }

    pub fn set_qlog(&mut self, qlog: Qlog) {
        self.conn.set_qlog(qlog);
    }

    /// Enable encrypted client hello (ECH).
    ///
    /// # Errors
    ///
    /// Fails when the configuration provided is bad.
    pub fn enable_ech<A: AsRef<[u8]>>(&mut self, ech_config_list: A) -> Res<()> {
        self.conn.client_enable_ech(ech_config_list)?;
        Ok(())
    }

    /// Get the connection id, which is useful for disambiguating connections to
    /// the same origin.
    ///
    /// # Panics
    ///
    /// Never, because clients always have this field.
    #[must_use]
    pub const fn connection_id(&self) -> &ConnectionId {
        self.conn.odcid().expect("Client always has odcid")
    }

    fn encode_resumption_token(&self, token: &ResumptionToken) -> Option<ResumptionToken> {
        self.base_handler.get_settings().map(|settings| {
            let mut enc = Encoder::default();
            settings.encode_frame_contents(&mut enc);
            enc.encode(token.as_ref());
            ResumptionToken::new(enc.into(), token.expiration_time())
        })
    }

    /// This may be call if an application has a resumption token. This must be called before
    /// connection starts.
    ///
    /// The resumption token also contains encoded HTTP/3 settings. The settings will be decoded
    /// and used until the setting are received from the server.
    ///
    /// # Errors
    ///
    /// An error is return if token cannot be decoded or a connection is is a wrong state.
    ///
    /// # Panics
    ///
    /// On closing if the base handler can't handle it (debug only).
    pub fn enable_resumption<A: AsRef<[u8]>>(&mut self, now: Instant, token: A) -> Res<()> {
        if self.base_handler.state() != &Http3State::Initializing {
            return Err(Error::InvalidState);
        }
        let mut dec = Decoder::from(token.as_ref());
        let Some(settings_slice) = dec.decode_vvec() else {
            return Err(Error::InvalidResumptionToken);
        };
        trace!("[{self}]   settings {}", HexWithLen::new(settings_slice));
        let mut dec_settings = Decoder::from(settings_slice);
        let mut settings = HSettings::default();
        Error::map_error(
            settings.decode_frame_contents(&mut dec_settings),
            Error::InvalidResumptionToken,
        )?;
        let tok = dec.decode_remainder();
        trace!("[{self}]   Transport token {}", Hex::new(tok));
        self.conn.enable_resumption(now, tok)?;
        if self.conn.state().closed() {
            let state = self.conn.state().clone();
            let res = self
                .base_handler
                .handle_state_change(&mut self.conn, &state);
            debug_assert_eq!(Ok(true), res);
            return Err(Error::Fatal);
        }
        if self.conn.zero_rtt_state() == ZeroRttState::Sending {
            self.base_handler
                .set_0rtt_settings(&mut self.conn, settings)?;
            self.events
                .connection_state_change(self.base_handler.state().clone());
        }
        Ok(())
    }

    /// Returns a resumption token if one is available, wrapped with the current
    /// H3 settings. Use as a fallback when the `ResumptionToken` event has not
    /// fired before the connection closes (e.g., `NEW_TOKEN` never arrived).
    pub fn take_resumption_token(&mut self, now: Instant) -> Option<ResumptionToken> {
        let transport_token = self.conn.take_resumption_token(now)?;
        self.encode_resumption_token(&transport_token)
    }

    /// This is call to close a connection.
    pub fn close<S>(&mut self, now: Instant, error: AppError, msg: S)
    where
        S: AsRef<str> + Display,
    {
        info!("[{self}] Close the connection error={error} msg={msg}");
        if !matches!(
            self.base_handler.state(),
            Http3State::Closing(_) | Http3State::Closed(_)
        ) {
            self.conn.close(now, error, msg);
            self.base_handler.close(error);
            self.events
                .connection_state_change(self.base_handler.state().clone());
        }
    }

    // API: Request/response

    /// The function fetches a resource using `method`, `target` and `headers`. A response body
    /// may be added by calling `send_data`. `stream_close_send` must be sent to finish the request
    /// even if request data are not sent.
    ///
    /// # Errors
    ///
    /// If a new stream cannot be created an error will be return.
    ///
    /// # Panics
    ///
    /// `SendMessage` implements `http_stream` so it will not panic.
    pub fn fetch<'t, T>(
        &mut self,
        now: Instant,
        method: &'t str,
        target: &T,
        headers: &'t [Header],
        priority: Priority,
    ) -> Res<StreamId>
    where
        T: RequestTarget,
    {
        if method == "CONNECT" {
            warn!("Invalid method CONNECT in fetch. Use Http3Client::connect instead.");
            return Err(Error::InvalidInput);
        }
        let request = RequestDescription::new(method, target, headers, None, priority)?;
        self.request(&request, now)
    }

    /// The function establishes a classic HTTP CONNECT tunnel on top of this
    /// connection using `target` and `headers`. Data can be send into the
    /// tunnel via [`Http3Client::send_data`] and received from the tunnel via
    /// [`Http3Client::read_data`]. The tunnel can be closed via
    /// [`Http3Client::stream_close_send`].
    ///
    /// # Errors
    ///
    /// If a new stream cannot be created an error will be return.
    pub fn connect<A>(
        &mut self,
        now: Instant,
        authority: A,
        headers: &[Header],
        priority: Priority,
    ) -> Res<StreamId>
    where
        A: AsRef<str>,
    {
        let request = RequestDescription::new(
            "CONNECT",
            &("", authority.as_ref(), ""),
            headers,
            Some(ConnectType::Classic),
            priority,
        )?;
        self.request(&request, now)
    }

    fn request(&mut self, request: &RequestDescription, now: Instant) -> Res<StreamId> {
        let output = self.base_handler.request(
            &mut self.conn,
            Box::new(self.events.clone()),
            Box::new(self.events.clone()),
            request,
            now,
        );
        if let Err(e) = &output
            && e.connection_error()
        {
            self.close(now, e.code(), "");
        }
        output
    }

    /// Send an [`PRIORITY_UPDATE`-frame][1] on next `Http3Client::process_output()` call.
    /// Returns if the priority got changed.
    ///
    /// # Errors
    ///
    /// `InvalidStreamId` if the stream does not exist
    ///
    /// [1]: https://datatracker.ietf.org/doc/html/draft-kazuho-httpbis-priority-04#section-5.2
    pub fn priority_update(&mut self, stream_id: StreamId, priority: Priority) -> Res<bool> {
        self.base_handler.queue_update_priority(stream_id, priority)
    }

    /// An application may cancel a stream(request).
    /// Both sides, the receiving and sending side, sending and receiving side, will be closed.
    ///
    /// # Errors
    ///
    /// An error will be return if a stream does not exist.
    pub fn cancel_fetch(&mut self, stream_id: StreamId, error: AppError) -> Res<()> {
        debug!("[{self}] reset_stream {stream_id} error={error}");
        self.base_handler
            .cancel_fetch(stream_id, error, &mut self.conn)
    }

    /// This is call when application is done sending a request.
    ///
    /// # Errors
    ///
    /// An error will be return if stream does not exist.
    pub fn stream_close_send(&mut self, stream_id: StreamId, now: Instant) -> Res<()> {
        self.base_handler
            .stream_close_send(&mut self.conn, stream_id, now)
    }

    /// # Errors
    ///
    /// An error will be return if a stream does not exist.
    pub fn stream_reset_send(&mut self, stream_id: StreamId, error: AppError) -> Res<()> {
        self.base_handler
            .stream_reset_send(&mut self.conn, stream_id, error)
    }

    /// Commit to reliably delivering the stream data buffered so far. If the stream is later
    /// reset with [`Self::stream_reset_send`], that prefix is still delivered using
    /// `RESET_STREAM_AT` (when the peer supports it). Call this after writing the data to
    /// protect.
    ///
    /// # Errors
    /// `InvalidStreamId` if the stream does not exist, or `NotAvailable` if the peer did not
    /// enable reliable reset.
    pub fn stream_commit(&mut self, stream_id: StreamId, now: Instant) -> Res<()> {
        self.base_handler
            .stream_commit(&mut self.conn, stream_id, now)
    }

    /// # Errors
    ///
    /// An error will be return if a stream does not exist.
    pub fn stream_stop_sending(&mut self, stream_id: StreamId, error: AppError) -> Res<()> {
        self.base_handler
            .stream_stop_sending(&mut self.conn, stream_id, error)
    }

    /// This function is used for regular HTTP requests and `WebTransport` streams.
    /// In the case of regular HTTP requests, the request body is supplied using this function, and
    /// headers are supplied through the `fetch` function.
    ///
    /// # Errors
    ///
    /// `InvalidStreamId` if the stream does not exist,
    /// `AlreadyClosed` if the stream has already been closed.
    /// `TransportStreamDoesNotExist` if the transport stream does not exist (this may happen if
    /// `process_output` has not been called when needed, and HTTP3 layer has not picked up the
    /// info that the stream has been closed.) `InvalidInput` if an empty buffer has been
    /// supplied.
    pub fn send_data(&mut self, stream_id: StreamId, buf: &[u8], now: Instant) -> Res<usize> {
        info!(
            "[{self}] end_data from stream {stream_id} sending {} bytes",
            buf.len()
        );
        self.base_handler
            .send_streams_mut()
            .get_mut(&stream_id)
            .ok_or(Error::InvalidStreamId)?
            .send_data(&mut self.conn, buf, now)
    }

    /// Response data are read directly into a buffer supplied as a parameter of this function to
    /// avoid copying data.
    ///
    /// # Errors
    ///
    /// It returns an error if a stream does not exist or an error happen while reading a stream,
    /// e.g. early close, protocol error, etc.
    pub fn read_data(
        &mut self,
        now: Instant,
        stream_id: StreamId,
        buf: &mut [u8],
    ) -> Res<(usize, bool)> {
        debug!("[{self}] read_data from stream {stream_id}");
        let res = self
            .base_handler
            .read_data(&mut self.conn, stream_id, buf, now);
        if let Err(e) = &res
            && e.connection_error()
        {
            self.close(now, e.code(), "");
        }
        res
    }

    /// This function combines  `process_input` and `process_output` function.
    pub fn process<A: AsRef<[u8]> + AsMut<[u8]>>(
        &mut self,
        dgram: Option<Datagram<A>>,
        now: Instant,
    ) -> Output {
        trace!("[{self}] Process");
        if let Some(d) = dgram {
            self.process_input(d, now);
        }
        self.process_output(now)
    }

    /// The function should be called when there is a new UDP packet available. The function will
    /// handle the packet payload.
    ///
    /// First, the payload will be handled by the QUIC layer. Afterward, `process_http3` will be
    /// called to handle new [`ConnectionEvent`][1]s.
    ///
    /// After this function is called `process_output` should be called to check whether new
    /// packets need to be sent or if a timer needs to be updated.
    ///
    /// [1]: ../neqo_transport/enum.ConnectionEvent.html
    pub fn process_input<A: AsRef<[u8]> + AsMut<[u8]>>(
        &mut self,
        dgram: Datagram<A>,
        now: Instant,
    ) {
        self.process_multiple_input(iter::once(dgram), now);
    }

    pub fn process_multiple_input<
        A: AsRef<[u8]> + AsMut<[u8]>,
        I: IntoIterator<Item = Datagram<A>>,
    >(
        &mut self,
        dgrams: I,
        now: Instant,
    ) {
        let mut dgrams = dgrams.into_iter().peekable();
        trace!("[{self}] Process multiple datagrams");
        if dgrams.peek().is_none() {
            return;
        }
        self.conn.process_multiple_input(dgrams, now);
        self.process_http3(now);
    }

    /// Process HTTP3 layer.
    /// When `process_output`, `process_input`, or `process` is called we must call this function
    /// as well. The functions calls `Http3Client::check_connection_events` to handle events from
    /// the QUC layer and calls `Http3Connection::process_sending` to ensure that HTTP/3 layer
    /// data, e.g. control frames, are sent.
    fn process_http3(&mut self, now: Instant) {
        trace!("[{self}] Process http3 internal");
        match self.base_handler.state() {
            Http3State::ZeroRtt | Http3State::Connected | Http3State::GoingAway(..) => {
                self.base_handler
                    .expire_datagram_queues(&mut self.conn, now);
                let res = self.check_connection_events(now);
                if self.check_result(now, &res) {
                    return;
                }
                let res = self.base_handler.process_sending(&mut self.conn, now);
                self.check_result(now, &res);
            }
            Http3State::Closed { .. } => {}
            _ => {
                let res = self.check_connection_events(now);
                _ = self.check_result(now, &res);
            }
        }
    }

    /// Wrapper around [`Http3Client::process_multiple_output`] that processes a single
    /// output datagram only.
    #[expect(clippy::missing_panics_doc, reason = "see expect()")]
    pub fn process_output(&mut self, now: Instant) -> Output {
        self.process_multiple_output(now, NonZeroUsize::MIN)
            .try_into()
            .expect("max_datagrams is 1")
    }

    /// The function should be called to check if there are new UDP packets to be sent. It should
    /// be called after a new packet is received and processed and after a timer expires (QUIC
    /// needs timers to handle events like PTO detection and timers are not implemented by the neqo
    /// library, but instead must be driven by the application).
    ///
    /// [`Http3Client::process_multiple_output`] can return:
    /// - a [`OutputBatch::DatagramBatch`]: data that should be sent as a UDP payload,
    /// - a [`OutputBatch::Callback`]: the duration of a  timer. `process_output` should be called
    ///   at least after the time expires,
    /// - [`OutputBatch::None`]: this is returned when `Http3Client` is done and can be destroyed.
    ///
    /// The application should call this function repeatedly until a timer value or None is
    /// returned. After that, the application should call the function again if a new UDP packet is
    /// received and processed or the timer value expires.
    ///
    /// The HTTP/3 neqo implementation drives the HTTP/3 and QUIC layers, therefore this function
    /// will call both layers:
    ///  - First it calls HTTP/3 layer processing (`process_http3`) to make sure the layer writes
    ///    data to QUIC layer or cancels streams if needed.
    ///  - Then QUIC layer processing is called - [`Connection::process_output`][3]. This produces a
    ///    packet or a timer value. It may also produce new [`ConnectionEvent`][2]s, e.g. connection
    ///    state-change event.
    ///  - Therefore the HTTP/3 layer processing (`process_http3`) is called again.
    ///
    /// [1]: ../neqo_transport/enum.Output.html
    /// [2]: ../neqo_transport/struct.ConnectionEvents.html
    /// [3]: ../neqo_transport/struct.Connection.html#method.process_output
    pub fn process_multiple_output(
        &mut self,
        now: Instant,
        max_datagrams: NonZeroUsize,
    ) -> OutputBatch {
        trace!("[{self}] Process output");

        // Maybe send() stuff on http3-managed streams
        self.process_http3(now);

        let out = self.conn.process_multiple_output(now, max_datagrams);

        // Update H3 for any transport state changes and events
        self.process_http3(now);

        out
    }

    /// This function takes the provided result and check for an error.
    /// An error results in closing the connection.
    fn check_result<ERR>(&mut self, now: Instant, res: &Res<ERR>) -> bool {
        match &res {
            Err(e) => {
                info!("[{self}] Connection error: {e}");
                self.close(now, e.code(), format!("{e}"));
                true
            }
            _ => false,
        }
    }

    /// This function checks [`ConnectionEvent`][2]s emitted by the QUIC layer, e.g. connection
    /// change state events, new incoming stream data is available, a stream is was reset, etc.
    /// The HTTP/3 layer needs to handle these events. Most of the events are handled by
    /// [`Http3Connection`][1] by calling appropriate functions, e.g. `handle_state_change`,
    /// `handle_stream_reset`, etc. [`Http3Connection`][1] handle functionalities that are common
    /// for the client and server side. Some of the functionalities are specific to the client and
    /// they are handled by `Http3Client`. For example, [`ConnectionEvent::RecvStreamReadable`][3]
    /// event is handled by `Http3Client::handle_stream_readable`. The  function calls
    /// `Http3Connection::handle_stream_readable` and then hands the return value as appropriate
    /// for the client-side.
    ///
    /// [1]: https://github.com/mozilla/neqo/blob/main/neqo-http3/src/connection.rs
    /// [2]: ../neqo_transport/enum.ConnectionEvent.html
    /// [3]: ../neqo_transport/enum.ConnectionEvent.html#variant.RecvStreamReadable
    fn check_connection_events(&mut self, now: Instant) -> Res<()> {
        trace!("[{self}] Check connection events");
        while let Some(e) = self.conn.next_event() {
            debug!("[{self}] check_connection_events - event {e:?}");
            match e {
                ConnectionEvent::NewStream { stream_id } => {
                    // During this event we only add a new stream to the Http3Connection stream
                    // list, with NewStreamHeadReader stream handler.
                    // This function will not read from the stream and try to decode the stream.
                    // RecvStreamReadable  will be emitted after this event and reading, i.e.
                    // decoding of a stream will happen during that event.
                    self.base_handler.add_new_stream(stream_id);
                }
                ConnectionEvent::SendStreamWritable { stream_id } => {
                    if let Some(s) = self.base_handler.send_streams_mut().get_mut(&stream_id) {
                        s.stream_writable();
                    }
                }
                ConnectionEvent::RecvStreamReadable { stream_id } => {
                    self.handle_stream_readable(stream_id, now)?;
                }
                ConnectionEvent::RecvStreamReset {
                    stream_id,
                    app_error,
                } => self
                    .base_handler
                    .handle_stream_reset(stream_id, app_error, &mut self.conn)?,
                ConnectionEvent::SendStreamStopSending {
                    stream_id,
                    app_error,
                } => self.base_handler.handle_stream_stop_sending(
                    stream_id,
                    app_error,
                    &mut self.conn,
                )?,

                ConnectionEvent::SendStreamCreatable { stream_type } => {
                    self.events.new_requests_creatable(stream_type);
                }
                ConnectionEvent::AuthenticationNeeded => self.events.authentication_needed(),
                ConnectionEvent::EchFallbackAuthenticationNeeded { public_name } => {
                    self.events.ech_fallback_authentication_needed(public_name);
                }
                ConnectionEvent::StateChange(state) => {
                    if self
                        .base_handler
                        .handle_state_change(&mut self.conn, &state)?
                    {
                        self.events
                            .connection_state_change(self.base_handler.state().clone());
                    }
                }
                ConnectionEvent::ZeroRttRejected => {
                    self.base_handler.handle_zero_rtt_rejected()?;
                    self.events.zero_rtt_rejected();
                }
                ConnectionEvent::ResumptionToken(token) => {
                    if let Some(t) = self.encode_resumption_token(&token) {
                        self.events.resumption_token(t);
                    }
                }
                ConnectionEvent::Datagram(dgram) => {
                    self.base_handler.handle_datagram(dgram);
                }
                ConnectionEvent::OutgoingDatagramSpaceAvailable { stream_id } => {
                    self.events.datagram_space_available(stream_id);
                }
                ConnectionEvent::SendStreamComplete { .. }
                | ConnectionEvent::OutgoingDatagramOutcome { .. }
                | ConnectionEvent::SconeUpdated(_)
                | ConnectionEvent::PathMigrated { .. } => {}
            }
        }
        Ok(())
    }

    /// This function handled new data available on a stream. It calls
    /// `Http3Client::handle_stream_readable` and handles its response. Reading streams are mostly
    /// handled by [`Http3Connection`][1] because most part of it is common for the client and
    /// server. The following actions need to be handled by the client-specific code:
    ///  - `ReceiveOutput::NewStream(NewStreamType::Http)` - client cannot  receive a
    ///    server-initiated HTTP request,
    ///  - `ReceiveOutput::NewStream(NewStreamType::WebTransportStream(_))` - because
    ///    `Http3ClientEvents`is needed and events handler is specific to the client.
    ///  - `ReceiveOutput::ControlFrames(control_frames)` - some control frame handling differs
    ///    between the  client and the server:
    ///     - `HFrame::PriorityUpdateRequest { .. }` can only be received on the server side,
    ///     - `HFrame::Goaway { stream_id }` needs specific handling by the client by the protocol
    ///       specification.
    ///
    /// [1]: https://github.com/mozilla/neqo/blob/main/neqo-http3/src/connection.rs
    fn handle_stream_readable(&mut self, stream_id: StreamId, now: Instant) -> Res<()> {
        match self
            .base_handler
            .handle_stream_readable(&mut self.conn, stream_id, now)?
        {
            ReceiveOutput::NewStream(NewStreamType::Http(_)) => Err(Error::HttpStreamCreation),
            ReceiveOutput::NewStream(NewStreamType::WebTransportStream(session_id)) => {
                self.base_handler.webtransport_create_stream_remote(
                    StreamId::from(session_id),
                    stream_id,
                    Box::new(self.events.clone()),
                    Box::new(self.events.clone()),
                )?;
                let res =
                    self.base_handler
                        .handle_stream_readable(&mut self.conn, stream_id, now)?;
                debug_assert!(matches!(res, ReceiveOutput::NoOutput));
                Ok(())
            }
            ReceiveOutput::ControlFrames(control_frames) => {
                for f in control_frames {
                    match f {
                        // Server push is not supported. CANCEL_PUSH carries a push ID we never
                        // authorized (H3_ID_ERROR); MAX_PUSH_ID and PRIORITY_UPDATE_PUSH must never
                        // be sent to a client (H3_FRAME_UNEXPECTED).
                        HFrame::CancelPush => Err(Error::HttpId),
                        HFrame::PriorityUpdateRequest { .. }
                        | HFrame::MaxPushId
                        | HFrame::PriorityUpdatePush => Err(Error::HttpFrameUnexpected),
                        HFrame::Goaway { stream_id } => self.handle_goaway(stream_id),
                        _ => {
                            unreachable!(
                                "only Goaway, PriorityUpdateRequest and server-push frames are put into control_frames"
                            );
                        }
                    }?;
                }
                Ok(())
            }
            _ => Ok(()),
        }
    }

    #[cold]
    fn goaway_rejects_nothing(&mut self, goaway_stream_id: StreamId) -> bool {
        let noop_from = *self.goaway_noop_from.get_or_insert_with(|| {
            let h = &self.base_handler;
            let all = StreamId::new(0);
            h.send_streams()
                .iter()
                .filter_map(id_gte(all))
                .chain(h.recv_streams().iter().filter_map(id_gte(all)))
                .max()
                .map_or(all, |mut max| {
                    max.next();
                    max
                })
        });
        if goaway_stream_id >= noop_from {
            return true;
        }
        self.goaway_noop_from = None;
        false
    }

    fn handle_goaway(&mut self, goaway_stream_id: StreamId) -> Res<()> {
        info!("[{self}] handle_goaway {goaway_stream_id}");

        if goaway_stream_id.is_uni() || goaway_stream_id.is_server_initiated() {
            return Err(Error::HttpId);
        }

        let first_goaway = matches!(self.base_handler.state(), Http3State::Connected);
        match self.base_handler.state_mut() {
            Http3State::Connected => {
                self.base_handler
                    .set_state(Http3State::GoingAway(goaway_stream_id));
            }
            Http3State::GoingAway(stream_id) => {
                if goaway_stream_id > *stream_id {
                    return Err(Error::HttpId);
                }
                *stream_id = goaway_stream_id;
            }
            Http3State::Closing(..) | Http3State::Closed(..) => {}
            _ => unreachable!("Should not receive Goaway frame in this state"),
        }

        // Per §4.6 of the WebTransport over HTTP/3 spec, a GOAWAY is "a signal to
        // applications to initiate shutdown for all WebTransport sessions":
        // https://www.ietf.org/archive/id/draft-ietf-webtrans-http3-13.html#name-interaction-with-the-http-3
        //
        // Emit Draining BEFORE resetting streams, because handle_stream_reset below
        // removes sessions with IDs >= goaway_stream_id from the stream tables.
        // Those sessions will also receive a SessionClosed event via the reset path.
        if first_goaway {
            self.base_handler
                .drain_webtransport_sessions()
                .for_each(|session_id| {
                    self.events.push(Http3ClientEvent::WebTransport(
                        WebTransportEvent::Draining {
                            stream_id: session_id,
                        },
                    ));
                });
        } else if self.goaway_rejects_nothing(goaway_stream_id) {
            return Ok(());
        }

        // Issue reset events for streams >= goaway stream id
        let send_ids: Vec<StreamId> = self
            .base_handler
            .send_streams()
            .iter()
            .filter_map(id_gte(goaway_stream_id))
            .collect();
        for id in send_ids {
            // We do not care about streams that are going to be closed.
            drop(self.base_handler.handle_stream_stop_sending(
                id,
                Error::HttpRequestRejected.code(),
                &mut self.conn,
            ));
        }

        let recv_ids: Vec<StreamId> = self
            .base_handler
            .recv_streams()
            .iter()
            .filter_map(id_gte(goaway_stream_id))
            .collect();
        for id in recv_ids {
            // We do not care about streams that are going to be closed.
            drop(self.base_handler.handle_stream_reset(
                id,
                Error::HttpRequestRejected.code(),
                &mut self.conn,
            ));
        }

        if first_goaway {
            self.events.goaway_received();
        }

        Ok(())
    }

    #[must_use]
    pub fn qpack_encoder_stats(&self) -> QpackStats {
        self.base_handler.qpack_encoder().borrow().stats()
    }

    #[must_use]
    pub fn transport_stats(&self) -> TransportStats {
        self.conn.stats()
    }

    /// Create a send group for a WebTransport session, returning its connection-unique ID.
    ///
    /// Send groups allow organizing streams with shared prioritization.
    ///
    /// # Errors
    ///
    /// Returns error if the session ID is invalid or is not a WebTransport session.
    pub fn webtransport_create_send_group(&mut self, session_id: StreamId) -> Res<SendGroupId> {
        self.base_handler.webtransport_create_send_group(session_id)
    }

    /// Validate that a send group belongs to the specified WebTransport session.
    ///
    /// # Errors
    ///
    /// Returns error if the session ID is invalid or is not a WebTransport session.
    pub fn webtransport_validate_send_group(
        &self,
        session_id: StreamId,
        group_id: SendGroupId,
    ) -> Res<bool> {
        self.base_handler
            .webtransport_validate_send_group(session_id, group_id)
    }

    /// Get statistics for a WebTransport session.
    ///
    /// Returns the per-session datagram queue counters; see
    /// [`SessionStats`](crate::features::extended_connect::stats::SessionStats).
    ///
    /// # Errors
    ///
    /// Returns error if the session ID is invalid or is not a WebTransport session.
    pub fn webtransport_session_stats(
        &self,
        session_id: StreamId,
    ) -> Res<crate::features::extended_connect::stats::SessionStats> {
        self.base_handler.webtransport_session_stats(session_id)
    }

    /// Create a WebTransport stream with a send group.
    ///
    /// # Errors
    ///
    /// Returns error if the session ID is invalid, stream creation fails, or send group is invalid.
    pub fn webtransport_create_stream_with_send_group(
        &mut self,
        session_id: StreamId,
        stream_type: StreamType,
        send_group: Option<SendGroupId>,
    ) -> Res<StreamId> {
        self.base_handler
            .webtransport_create_stream_local_with_send_group(
                &mut self.conn,
                session_id,
                stream_type,
                Box::new(self.events.clone()),
                Box::new(self.events.clone()),
                send_group,
            )
    }
}

impl EventProvider for Http3Client {
    type Event = Http3ClientEvent;

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

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::{iter, time::Duration};

    use http::Uri;
    use neqo_common::{Datagram, Decoder, Encoder, event::Provider as _, to_u64};
    use neqo_qpack as qpack;
    use neqo_transport::{
        CloseReason, ConnectionEvent, ConnectionParameters, Error as TransportError,
        INITIAL_LOCAL_MAX_STREAM_DATA, MIN_INITIAL_PACKET_SIZE, Output, State, StreamDataLimit,
        StreamId, StreamType, Version,
    };
    use nss::{AllowZeroRtt, AntiReplay, ResumptionToken};
    use test_fixture::{
        CountingConnectionIdGenerator, DEFAULT_ADDR, DEFAULT_ALPN_H3, DEFAULT_KEYS,
        DEFAULT_SERVER_NAME, anti_replay, default_server_h3, fixture_init, new_server, now,
    };
    use tracing::trace;

    use super::{
        AuthenticationStatus, Connection, Error, HSettings, Header, Http3Client, Http3ClientEvent,
        Http3Parameters, Http3State, Rc, RefCell,
    };
    use crate::{
        Http3Server, Priority, RecvStream as _,
        frames::{HFrame, HFrameType, hframe::MAX_HEADER_BYTES},
        qpack_encoder_receiver::EncoderRecvStream,
        settings::{H3_RESERVED_SETTINGS, HSetting, HSettingType},
    };

    fn assert_closed(client: &Http3Client, expected: &Error) {
        match client.state() {
            Http3State::Closing(err) | Http3State::Closed(err) => {
                assert_eq!(err, CloseReason::Application(expected.code()));
            }
            _ => panic!("Wrong state {:?}", client.state()),
        }
    }

    /// Create a http3 client with default configuration.
    pub fn default_http3_client() -> Http3Client {
        default_http3_client_param(100)
    }

    pub fn default_http3_client_param(max_table_size: u64) -> Http3Client {
        fixture_init();
        Http3Client::new(
            DEFAULT_SERVER_NAME,
            Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
            DEFAULT_ADDR,
            DEFAULT_ADDR,
            Http3Parameters::default()
                .connection_parameters(
                    // Disable compatible upgrade, which complicates tests.
                    ConnectionParameters::default()
                        .versions(Version::default(), vec![Version::default()]),
                )
                .max_table_size_encoder(max_table_size)
                .max_table_size_decoder(max_table_size)
                .max_blocked_streams(100),
            now(),
        )
        .expect("create a default client")
    }

    const CONTROL_STREAM_TYPE: &[u8] = &[0x0];

    // Encoder stream data
    const ENCODER_STREAM_DATA: &[u8] = &[0x2];
    const ENCODER_STREAM_CAP_INSTRUCTION: &[u8] = &[0x3f, 0x45];

    // Encoder stream data with a change capacity instruction(0x3f, 0x45 = change capacity to 100)
    // This data will be send when 0-RTT is used and we already have a max_table_capacity from
    // resumed settings.
    const ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION: &[u8] = &[0x2, 0x3f, 0x45];

    const ENCODER_STREAM_DATA_WITH_CAP_INST_AND_ENCODING_INST: &[u8] = &[
        0x2, 0x3f, 0x45, 0x67, 0xa7, 0xd4, 0xe5, 0x1c, 0x85, 0xb1, 0x1f, 0x86, 0xa7, 0xd7, 0x71,
        0xd1, 0x69, 0x7f,
    ];

    // Decoder stream data
    const DECODER_STREAM_DATA: &[u8] = &[0x3];

    const CLIENT_SIDE_CONTROL_STREAM_ID: StreamId = StreamId::new(2);
    const CLIENT_SIDE_ENCODER_STREAM_ID: StreamId = StreamId::new(6);
    const CLIENT_SIDE_DECODER_STREAM_ID: StreamId = StreamId::new(10);

    const DEFAULT_IDLE_TIMEOUT: Duration = ConnectionParameters::DEFAULT_IDLE_TIMEOUT;

    struct TestServer {
        settings: HFrame,
        conn: Connection,
        control_stream_id: Option<StreamId>,
        encoder: Rc<RefCell<qpack::Encoder>>,
        encoder_receiver: EncoderRecvStream,
        encoder_stream_id: Option<StreamId>,
        decoder_stream_id: Option<StreamId>,
    }

    impl TestServer {
        pub fn new() -> Self {
            Self::new_with_settings(&[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ])
        }

        pub fn new_with_settings(server_settings: &[HSetting]) -> Self {
            fixture_init();
            let max_table_size = server_settings
                .iter()
                .find(|s| s.setting_type == HSettingType::MaxTableCapacity)
                .map_or(100, |s| s.value);
            let max_blocked_streams = u16::try_from(
                server_settings
                    .iter()
                    .find(|s| s.setting_type == HSettingType::BlockedStreams)
                    .map_or(100, |s| s.value),
            )
            .unwrap();
            let qpack = Rc::new(RefCell::new(qpack::Encoder::new(
                &qpack::Settings::default()
                    .max_table_size_encoder(max_table_size)
                    .max_table_size_decoder(max_table_size)
                    .max_blocked_streams(max_blocked_streams),
                true,
            )));
            Self {
                settings: HFrame::Settings {
                    settings: HSettings::new(server_settings),
                },
                conn: default_server_h3(),
                control_stream_id: None,
                encoder: Rc::clone(&qpack),
                encoder_receiver: EncoderRecvStream::new(CLIENT_SIDE_DECODER_STREAM_ID, qpack)
                    .unwrap(),
                encoder_stream_id: None,
                decoder_stream_id: None,
            }
        }

        pub fn new_with_conn(conn: Connection) -> Self {
            let qpack = Rc::new(RefCell::new(qpack::Encoder::new(
                &qpack::Settings::default()
                    .max_table_size_encoder(128)
                    .max_table_size_decoder(128)
                    .max_blocked_streams(0),
                true,
            )));
            Self {
                settings: HFrame::Settings {
                    settings: HSettings::new(&[]),
                },
                conn,
                control_stream_id: None,
                encoder: Rc::clone(&qpack),
                encoder_receiver: EncoderRecvStream::new(CLIENT_SIDE_DECODER_STREAM_ID, qpack)
                    .unwrap(),
                encoder_stream_id: None,
                decoder_stream_id: None,
            }
        }

        pub fn create_qpack_streams(&mut self) {
            // Create a QPACK encoder stream
            self.encoder_stream_id = Some(self.conn.stream_create(StreamType::UniDi).unwrap());
            self.encoder
                .borrow_mut()
                .add_send_stream(self.encoder_stream_id.unwrap())
                .unwrap();
            self.encoder
                .borrow_mut()
                .send_encoder_updates(&mut self.conn)
                .unwrap();

            // Create decoder stream
            self.decoder_stream_id = Some(self.conn.stream_create(StreamType::UniDi).unwrap());
            assert_eq!(
                self.conn
                    .stream_send(self.decoder_stream_id.unwrap(), DECODER_STREAM_DATA)
                    .unwrap(),
                1
            );
        }

        pub fn create_control_stream(&mut self) {
            // Create control stream
            let control = self.conn.stream_create(StreamType::UniDi).unwrap();
            trace!("[TestServer] control stream: {control}");
            self.control_stream_id = Some(control);
            // Send stream type on the control stream.
            assert_eq!(
                self.conn
                    .stream_send(self.control_stream_id.unwrap(), CONTROL_STREAM_TYPE)
                    .unwrap(),
                1
            );

            // Encode a settings frame and send it.
            let mut enc = Encoder::default();
            self.settings.encode(&mut enc);
            assert_eq!(
                self.conn
                    .stream_send(self.control_stream_id.unwrap(), enc.as_ref())
                    .unwrap(),
                enc.len()
            );
        }

        pub fn check_client_control_qpack_streams_no_resumption(&mut self) {
            self.check_client_control_qpack_streams(
                ENCODER_STREAM_DATA,
                EXPECTED_REQUEST_HEADER_FRAME,
                false,
                true,
            );
        }

        pub fn check_control_qpack_request_streams_resumption(
            &mut self,
            expect_encoder_stream_data: &[u8],
            expect_request_header: &[u8],
            expect_request: bool,
        ) {
            self.check_client_control_qpack_streams(
                expect_encoder_stream_data,
                expect_request_header,
                expect_request,
                false,
            );
        }

        // Check that server has received correct settings and qpack streams.
        pub fn check_client_control_qpack_streams(
            &mut self,
            expect_encoder_stream_data: &[u8],
            expect_request_header: &[u8],
            expect_request: bool,
            expect_connected: bool,
        ) {
            let mut connected = false;
            let mut control_stream = false;
            let mut qpack_decoder_stream = false;
            let mut qpack_encoder_stream = false;
            let mut request = false;
            while let Some(e) = self.conn.next_event() {
                match e {
                    ConnectionEvent::NewStream { stream_id }
                    | ConnectionEvent::SendStreamWritable { stream_id } => {
                        if expect_request {
                            assert!(matches!(stream_id.as_u64(), 2 | 6 | 10 | 0));
                        } else {
                            assert!(matches!(stream_id.as_u64(), 2 | 6 | 10));
                        }
                    }
                    ConnectionEvent::RecvStreamReadable { stream_id } => {
                        if stream_id == CLIENT_SIDE_CONTROL_STREAM_ID {
                            self.check_control_stream();
                            control_stream = true;
                        } else if stream_id == CLIENT_SIDE_ENCODER_STREAM_ID {
                            // the qpack encoder stream
                            self.read_and_check_stream_data(
                                stream_id,
                                expect_encoder_stream_data,
                                false,
                            );
                            qpack_encoder_stream = true;
                        } else if stream_id == CLIENT_SIDE_DECODER_STREAM_ID {
                            // the qpack decoder stream
                            self.read_and_check_stream_data(stream_id, DECODER_STREAM_DATA, false);
                            qpack_decoder_stream = true;
                        } else if stream_id == 0 {
                            assert!(expect_request);
                            self.read_and_check_stream_data(stream_id, expect_request_header, true);
                            request = true;
                        } else {
                            panic!("unexpected event");
                        }
                    }
                    ConnectionEvent::StateChange(State::Connected) => connected = true,
                    ConnectionEvent::StateChange(_)
                    | ConnectionEvent::SendStreamCreatable { .. } => {}
                    _ => panic!("unexpected event"),
                }
            }
            assert_eq!(connected, expect_connected);
            assert!(control_stream);
            assert!(qpack_encoder_stream);
            assert!(qpack_decoder_stream);
            assert_eq!(request, expect_request);
        }

        // Check that the control stream contains default values.
        // Expect a SETTINGS frame and some grease.
        // The default test configuration uses:
        //  - max_table_capacity = 100
        //  - max_blocked_streams = 100
        fn check_control_stream(&mut self) {
            let mut buf = [0_u8; 100];
            let (amount, fin) = self
                .conn
                .stream_recv(CLIENT_SIDE_CONTROL_STREAM_ID, &mut buf)
                .unwrap();
            let mut dec = Decoder::from(&buf[..amount]);
            assert_eq!(dec.decode_varint().unwrap(), 0); // control stream type
            assert_eq!(dec.decode_varint().unwrap(), 4); // SETTINGS
            assert_eq!(
                dec.decode_vvec().unwrap(),
                &[
                    1, 0x40, 0x64, 7, 0x40, 0x64, 0xab, 0x60, 0x37, 0x42, 0x00, 0x80, 0xff, 0xd2,
                    0x77, 0x01, 0x33, 0x01
                ]
            );

            assert_eq!((dec.decode_varint().unwrap() - 0x21) % 0x1f, 0); // Grease
            assert!(dec.decode_vvec().unwrap().len() < 8);

            assert_eq!(dec.remaining(), 0);
            assert!(!fin);
        }

        pub fn read_and_check_stream_data(
            &mut self,
            stream_id: StreamId,
            expected_data: &[u8],
            expected_fin: bool,
        ) {
            let mut buf = [0_u8; 100];
            let (amount, fin) = self.conn.stream_recv(stream_id, &mut buf).unwrap();
            assert_eq!(fin, expected_fin);
            assert_eq!(amount, expected_data.len());
            assert_eq!(&buf[..amount], expected_data);
        }

        pub fn encode_headers(
            &mut self,
            stream_id: StreamId,
            headers: &[Header],
            encoder: &mut Encoder,
        ) {
            let header_block =
                self.encoder
                    .borrow_mut()
                    .encode_header_block(&mut self.conn, headers, stream_id);
            let hframe = HFrame::Headers {
                header_block: header_block.as_ref().to_vec(),
            };
            hframe.encode(encoder);
        }
    }

    fn handshake_only(client: &mut Http3Client, server: &mut TestServer) -> Output {
        assert_eq!(client.state(), Http3State::Initializing);
        let out = client.process_output(now());
        let out2 = client.process_output(now());
        assert_eq!(client.state(), Http3State::Initializing);

        assert_eq!(*server.conn.state(), State::Init);
        server.conn.process_input(out.dgram().unwrap(), now());
        let out = server.conn.process(out2.dgram(), now());
        assert_eq!(*server.conn.state(), State::Handshaking);

        let out = client.process(out.dgram(), now());
        let out = server.conn.process(out.dgram(), now());
        let out = client.process(out.dgram(), now());
        let out = server.conn.process(out.dgram(), now());
        assert!(out.as_dgram_ref().is_none());

        let authentication_needed = |e| matches!(e, Http3ClientEvent::AuthenticationNeeded);
        assert!(client.events().any(authentication_needed));
        client.authenticated(AuthenticationStatus::Ok, now());
        out
    }

    // Perform only QUIC transport handshake.
    fn connect_only_transport_with(client: &mut Http3Client, server: &mut TestServer) {
        let out = handshake_only(client, server);

        let out = client.process(out.dgram(), now());
        let connected = |e| matches!(e, Http3ClientEvent::StateChange(Http3State::Connected));
        assert!(client.events().any(connected));

        assert_eq!(client.state(), Http3State::Connected);
        server.conn.process_input(out.dgram().unwrap(), now());
        assert!(server.conn.state().connected());
    }

    // Perform only QUIC transport handshake.
    fn connect_only_transport() -> (Http3Client, TestServer) {
        let mut client = default_http3_client();
        let mut server = TestServer::new();
        connect_only_transport_with(&mut client, &mut server);
        (client, server)
    }

    fn send_and_receive_client_settings(client: &mut Http3Client, server: &mut TestServer) {
        // send and receive client settings
        let out = client.process_output(now());
        server.conn.process_input(out.dgram().unwrap(), now());
        server.check_client_control_qpack_streams_no_resumption();
    }

    // Perform QUIC transport handshake and exchange Http3 settings.
    fn connect_with(client: &mut Http3Client, server: &mut TestServer) {
        connect_only_transport_with(client, server);

        send_and_receive_client_settings(client, server);

        server.create_control_stream();

        server.create_qpack_streams();
        // Send the server's control and qpack streams data.
        let out = server.conn.process(None::<Datagram>, now());
        client.process_input(out.dgram().unwrap(), now());

        // assert no error occurred.
        assert_eq!(client.state(), Http3State::Connected);
    }

    // Perform QUIC transport handshake and exchange Http3 settings.
    fn connect_with_connection_parameters(
        server_conn_params: ConnectionParameters,
    ) -> (Http3Client, TestServer) {
        // connecting with default max_table_size
        let mut client = default_http3_client_param(100);
        let server = Connection::new_server(
            DEFAULT_KEYS,
            DEFAULT_ALPN_H3,
            Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
            server_conn_params,
        )
        .unwrap();
        let mut server = TestServer::new_with_conn(server);
        connect_with(&mut client, &mut server);
        (client, server)
    }

    // Perform QUIC transport handshake and exchange Http3 settings.
    fn connect() -> (Http3Client, TestServer) {
        let mut client = default_http3_client();
        let mut server = TestServer::new();
        connect_with(&mut client, &mut server);
        (client, server)
    }

    // Fetch request fetch("GET", "https", "something.com", "/", headers).
    fn make_request(
        client: &mut Http3Client,
        close_sending_side: bool,
        headers: &[Header],
    ) -> StreamId {
        let request_stream_id = client
            .fetch(
                now(),
                "GET",
                &Uri::from_static("https://something.com/"),
                headers,
                Priority::default(),
            )
            .unwrap();
        if close_sending_side {
            client.stream_close_send(request_stream_id, now()).unwrap();
        }
        request_stream_id
    }

    // For fetch request fetch("GET", "https", "something.com", "/", &[])
    // the following request header frame will be sent:
    const EXPECTED_REQUEST_HEADER_FRAME: &[u8] = &[
        0x01, 0x10, 0x00, 0x00, 0xd1, 0xd7, 0x50, 0x89, 0x41, 0xe9, 0x2a, 0x67, 0x35, 0x53, 0x2e,
        0x43, 0xd3, 0xc1,
    ];

    // For fetch request fetch("GET", "https", "something.com", "/", &[(String::from("myheaders",
    // "myvalue"))]) the following request header frame will be sent:
    const EXPECTED_REQUEST_HEADER_FRAME_VERSION2: &[u8] = &[
        0x01, 0x11, 0x02, 0x80, 0xd1, 0xd7, 0x50, 0x89, 0x41, 0xe9, 0x2a, 0x67, 0x35, 0x53, 0x2e,
        0x43, 0xd3, 0xc1, 0x10,
    ];

    const HTTP_HEADER_FRAME_0: &[u8] = &[0x01, 0x06, 0x00, 0x00, 0xd9, 0x54, 0x01, 0x30];

    // The response header from HTTP_HEADER_FRAME (0x01, 0x06, 0x00, 0x00, 0xd9, 0x54, 0x01, 0x30)
    // are decoded into:
    fn check_response_header_0(header: &[Header]) {
        let expected_response_header_0 = &[
            Header::new(":status", "200"),
            Header::new("content-length", "0"),
        ];
        assert_eq!(header, expected_response_header_0);
    }

    const HTTP_RESPONSE_1: &[u8] = &[
        // headers
        0x01, 0x06, 0x00, 0x00, 0xd9, 0x54, 0x01, 0x37, // the first data frame
        0x0, 0x3, 0x61, 0x62, 0x63, // the second data frame
        0x0, 0x4, 0x64, 0x65, 0x66, 0x67,
    ];

    // The response header from HTTP_RESPONSE_1 (0x01, 0x06, 0x00, 0x00, 0xd9, 0x54, 0x01, 0x36) are
    // decoded into:
    fn check_response_header_1(header: &[Header]) {
        let expected_response_header_1 = &[
            Header::new(":status", "200"),
            Header::new("content-length", "7"),
        ];
        assert_eq!(header, expected_response_header_1);
    }

    const EXPECTED_RESPONSE_DATA_1: &[u8] = &[0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67];

    const HTTP_RESPONSE_2: &[u8] = &[
        // headers
        0x01, 0x06, 0x00, 0x00, 0xd9, 0x54, 0x01, 0x33, // the data frame
        0x0, 0x3, 0x61, 0x62, 0x63,
    ];

    const HTTP_RESPONSE_HEADER_ONLY_2: &[u8] = &[
        // headers
        0x01, 0x06, 0x00, 0x00, 0xd9, 0x54, 0x01, 0x33,
    ];

    const HTTP_RESPONSE_DATA_FRAME_ONLY_2: &[u8] = &[
        // the data frame
        0x0, 0x3, 0x61, 0x62, 0x63,
    ];

    // The response header from HTTP_RESPONSE_2 (0x01, 0x06, 0x00, 0x00, 0xd9, 0x54, 0x01, 0x36) are
    // decoded into:
    fn check_response_header_2(header: &[Header]) {
        let expected_response_header_2 = &[
            Header::new(":status", "200"),
            Header::new("content-length", "3"),
        ];
        assert_eq!(header, expected_response_header_2);
    }

    // The data frame payload from HTTP_RESPONSE_2 is:
    const EXPECTED_RESPONSE_DATA_2_FRAME_1: &[u8] = &[0x61, 0x62, 0x63];

    fn make_request_and_exchange_pkts(
        client: &mut Http3Client,
        server: &mut TestServer,
        close_sending_side: bool,
    ) -> StreamId {
        let request_stream_id = make_request(client, close_sending_side, &[]);

        let out = client.process_output(now());
        server.conn.process_input(out.dgram().unwrap(), now());

        // find the new request/response stream and send frame v on it.
        while let Some(e) = server.conn.next_event() {
            match e {
                ConnectionEvent::NewStream { stream_id } => {
                    assert_eq!(stream_id, request_stream_id);
                    assert_eq!(stream_id.stream_type(), StreamType::BiDi);
                }
                ConnectionEvent::RecvStreamReadable { stream_id } => {
                    if stream_id == CLIENT_SIDE_ENCODER_STREAM_ID {
                        server.read_and_check_stream_data(
                            stream_id,
                            ENCODER_STREAM_CAP_INSTRUCTION,
                            false,
                        );
                    } else {
                        assert_eq!(stream_id, request_stream_id);
                        server.read_and_check_stream_data(
                            stream_id,
                            EXPECTED_REQUEST_HEADER_FRAME,
                            close_sending_side,
                        );
                    }
                }
                _ => {}
            }
        }
        let dgram = server.conn.process_output(now()).dgram();
        if let Some(d) = dgram {
            client.process_input(d, now());
        }
        request_stream_id
    }

    fn connect_and_send_request(close_sending_side: bool) -> (Http3Client, TestServer, StreamId) {
        let (mut client, mut server) = connect();
        let request_stream_id =
            make_request_and_exchange_pkts(&mut client, &mut server, close_sending_side);
        assert_eq!(request_stream_id, 0);

        (client, server, request_stream_id)
    }

    fn server_send_response_and_exchange_packet(
        client: &mut Http3Client,
        server: &mut TestServer,
        stream_id: StreamId,
        response: impl AsRef<[u8]>,
        close_stream: bool,
    ) {
        _ = server
            .conn
            .stream_send(stream_id, response.as_ref())
            .unwrap();
        if close_stream {
            server.conn.stream_close_send(stream_id).unwrap();
        }
        let out = server.conn.process(None::<Datagram>, now());
        let out = client.process(out.dgram(), now());
        drop(server.conn.process(out.dgram(), now()));
    }

    // Client: Test receiving a new control stream and a SETTINGS frame.
    #[test]
    fn client_connect_and_exchange_qpack_and_control_streams() {
        drop(connect());
    }

    // Client: Test that the connection will be closed if control stream
    // has been closed.
    #[test]
    fn client_close_control_stream() {
        let (mut client, mut server) = connect();
        server
            .conn
            .stream_close_send(server.control_stream_id.unwrap())
            .unwrap();
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());
        assert_closed(&client, &Error::HttpClosedCriticalStream);
    }

    // Client: Test that the connection will be closed if the local control stream
    // has been reset.
    #[test]
    fn client_reset_control_stream() {
        let (mut client, mut server) = connect();
        server
            .conn
            .stream_reset_send(server.control_stream_id.unwrap(), Error::HttpNone.code())
            .unwrap();
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());
        assert_closed(&client, &Error::HttpClosedCriticalStream);
    }

    // Client: Test that the connection will be closed if the server side encoder stream
    // has been reset.
    #[test]
    fn client_reset_server_side_encoder_stream() {
        let (mut client, mut server) = connect();
        server
            .conn
            .stream_reset_send(server.encoder_stream_id.unwrap(), Error::HttpNone.code())
            .unwrap();
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());
        assert_closed(&client, &Error::HttpClosedCriticalStream);
    }

    // Client: Test that the connection will be closed if the server side decoder stream
    // has been reset.
    #[test]
    fn client_reset_server_side_decoder_stream() {
        let (mut client, mut server) = connect();
        server
            .conn
            .stream_reset_send(server.decoder_stream_id.unwrap(), Error::HttpNone.code())
            .unwrap();
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());
        assert_closed(&client, &Error::HttpClosedCriticalStream);
    }

    // Client: Test that the connection will be closed if the local control stream
    // has received a stop_sending.
    #[test]
    fn client_stop_sending_control_stream() {
        let (mut client, mut server) = connect();
        server
            .conn
            .stream_stop_sending(CLIENT_SIDE_CONTROL_STREAM_ID, Error::HttpNone.code())
            .unwrap();
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());
        assert_closed(&client, &Error::HttpClosedCriticalStream);
    }

    // Client: Test that the connection will be closed if the client side encoder stream
    // has received a stop_sending.
    #[test]
    fn client_stop_sending_encoder_stream() {
        let (mut client, mut server) = connect();
        server
            .conn
            .stream_stop_sending(CLIENT_SIDE_ENCODER_STREAM_ID, Error::HttpNone.code())
            .unwrap();
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());
        assert_closed(&client, &Error::HttpClosedCriticalStream);
    }

    // Client: Test that the connection will be closed if the client side decoder stream
    // has received a stop_sending.
    #[test]
    fn client_stop_sending_decoder_stream() {
        let (mut client, mut server) = connect();
        server
            .conn
            .stream_stop_sending(CLIENT_SIDE_DECODER_STREAM_ID, Error::HttpNone.code())
            .unwrap();
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());
        assert_closed(&client, &Error::HttpClosedCriticalStream);
    }

    // Client: test missing SETTINGS frame
    // (the first frame sent is a garbage frame).
    #[test]
    fn client_missing_settings() {
        let (mut client, mut server) = connect_only_transport();
        // Create server control stream.
        let control_stream = server.conn.stream_create(StreamType::UniDi).unwrap();
        // Send a HEADERS frame instead (which contains garbage).
        let sent = server
            .conn
            .stream_send(control_stream, &[0x0, 0x1, 0x3, 0x0, 0x1, 0x2]);
        assert_eq!(sent, Ok(6));
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());
        assert_closed(&client, &Error::HttpMissingSettings);
    }

    // Client: receiving SETTINGS frame twice causes connection close
    // with error HTTP_UNEXPECTED_FRAME.
    #[test]
    fn client_receive_settings_twice() {
        let (mut client, mut server) = connect();
        // send the second SETTINGS frame.
        let sent = server.conn.stream_send(
            server.control_stream_id.unwrap(),
            &[0x4, 0x6, 0x1, 0x40, 0x64, 0x7, 0x40, 0x64],
        );
        assert_eq!(sent, Ok(8));
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());
        assert_closed(&client, &Error::HttpFrameUnexpected);
    }

    fn test_wrong_frame_on_control_stream(v: &[u8]) {
        let (mut client, mut server) = connect();

        // send a frame that is not allowed on the control stream.
        _ = server
            .conn
            .stream_send(server.control_stream_id.unwrap(), v)
            .unwrap();

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        assert_closed(&client, &Error::HttpFrameUnexpected);
    }

    // send DATA frame on a control stream
    #[test]
    fn data_frame_on_control_stream() {
        test_wrong_frame_on_control_stream(&[0x0, 0x2, 0x1, 0x2]);
    }

    // send HEADERS frame on a control stream
    #[test]
    fn headers_frame_on_control_stream() {
        test_wrong_frame_on_control_stream(&[0x1, 0x2, 0x1, 0x2]);
    }

    // send PRIORITY_UPDATE frame on a control stream to the client
    #[test]
    fn priority_update_request_on_control_stream() {
        test_wrong_frame_on_control_stream(&[0x80, 0x0f, 0x07, 0x00, 0x01, 0x03]);
    }

    // Client: receive unknown stream type
    // This function also tests getting stream id that does not fit into a single byte.
    #[test]
    fn client_received_unknown_stream() {
        let (mut client, mut server) = connect();

        // create a stream with unknown type.
        let new_stream_id = server.conn.stream_create(StreamType::UniDi).unwrap();
        _ = server
            .conn
            .stream_send(new_stream_id, &[0x41, 0x19, 0x4, 0x4, 0x6, 0x0, 0x8, 0x0])
            .unwrap();
        let out = server.conn.process_output(now());
        let out = client.process(out.dgram(), now());
        drop(server.conn.process(out.dgram(), now()));

        // check for stop-sending with Error::HttpStreamCreation.
        let mut stop_sending_event_found = false;
        while let Some(e) = server.conn.next_event() {
            if let ConnectionEvent::SendStreamStopSending {
                stream_id,
                app_error,
            } = e
            {
                stop_sending_event_found = true;
                assert_eq!(stream_id, new_stream_id);
                assert_eq!(app_error, Error::HttpStreamCreation.code());
            }
        }
        assert!(stop_sending_event_found);
        assert_eq!(client.state(), Http3State::Connected);
    }

    // Test wrong frame on req/rec stream
    fn test_wrong_frame_on_request_stream(v: &[u8]) {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(false);

        _ = server.conn.stream_send(request_stream_id, v).unwrap();

        // Generate packet with the above bad h3 input
        let out = server.conn.process_output(now());
        // Process bad input and close the connection.
        drop(client.process(out.dgram(), now()));

        assert_closed(&client, &Error::HttpFrameUnexpected);
    }

    #[test]
    fn settings_frame_on_request_stream() {
        test_wrong_frame_on_request_stream(&[0x4, 0x4, 0x6, 0x4, 0x8, 0x1]);
    }

    #[test]
    fn goaway_frame_on_request_stream() {
        test_wrong_frame_on_request_stream(&[0x7, 0x1, 0x5]);
    }

    #[test]
    fn priority_update_request_on_request_stream() {
        test_wrong_frame_on_request_stream(&[0x80, 0x0f, 0x07, 0x00, 0x01, 0x03]);
    }

    // Server push is not supported. A client that receives push frames or a push stream closes the
    // connection rather than ignoring them, because it never authorized any push.

    // MAX_PUSH_ID must never be sent to a client, so it is H3_FRAME_UNEXPECTED
    // (RFC 9114, Section 7.2.7).
    #[test]
    fn max_push_id_frame_on_control_stream() {
        test_wrong_frame_on_control_stream(&[0x0d, 0x01, 0x05]);
    }

    #[test]
    fn max_push_id_frame_on_request_stream() {
        test_wrong_frame_on_request_stream(&[0x0d, 0x01, 0x05]);
    }

    // Sends `v` to the client on the server's control stream and asserts the connection closes with
    // `expected`.
    fn control_stream_closes_with(v: &[u8], expected: &Error) {
        let (mut client, mut server) = connect();
        _ = server
            .conn
            .stream_send(server.control_stream_id.unwrap(), v)
            .unwrap();
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());
        assert_closed(&client, expected);
    }

    // CANCEL_PUSH carries a push ID we never authorized, so it is H3_ID_ERROR
    // (RFC 9114, Section 7.2.3).
    #[test]
    fn cancel_push_frame_on_control_stream() {
        control_stream_closes_with(&[0x03, 0x01, 0x05], &Error::HttpId);
    }

    // PUSH_PROMISE carries a push ID we never authorized, so it is H3_ID_ERROR
    // (RFC 9114, Section 7.2.5).
    #[test]
    fn push_promise_frame_on_request_stream() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(false);
        _ = server
            .conn
            .stream_send(
                request_stream_id,
                &[0x05, 0x05, 0x04, 0x61, 0x62, 0x63, 0x64],
            )
            .unwrap();
        let out = server.conn.process_output(now());
        client.process_input(out.dgram().unwrap(), now());
        assert_closed(&client, &Error::HttpId);
    }

    // A push stream (unidirectional stream type 0x1) is H3_ID_ERROR for a client that never
    // enabled push (RFC 9114, Section 6.2.2).
    #[test]
    fn client_received_push_stream() {
        let (mut client, mut server) = connect();
        let push_stream = server.conn.stream_create(StreamType::UniDi).unwrap();
        _ = server.conn.stream_send(push_stream, &[0x01, 0x00]).unwrap();
        let out = server.conn.process_output(now());
        client.process_input(out.dgram().unwrap(), now());
        assert_closed(&client, &Error::HttpId);
    }

    // Test reading of a slowly streamed frame. bytes are received one by one
    #[test]
    fn frame_reading() {
        let (mut client, mut server) = connect_only_transport();

        // create a control stream.
        let control_stream = server.conn.stream_create(StreamType::UniDi).unwrap();

        // send the stream type
        let mut sent = server.conn.stream_send(control_stream, &[0x0]);
        assert_eq!(sent, Ok(1));
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        // start sending SETTINGS frame
        sent = server.conn.stream_send(control_stream, &[0x4]);
        assert_eq!(sent, Ok(1));
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        sent = server.conn.stream_send(control_stream, &[0x4]);
        assert_eq!(sent, Ok(1));
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        sent = server.conn.stream_send(control_stream, &[0x6]);
        assert_eq!(sent, Ok(1));
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        sent = server.conn.stream_send(control_stream, &[0x0]);
        assert_eq!(sent, Ok(1));
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        sent = server.conn.stream_send(control_stream, &[0x8]);
        assert_eq!(sent, Ok(1));
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        sent = server.conn.stream_send(control_stream, &[0x0]);
        assert_eq!(sent, Ok(1));
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        assert_eq!(client.state(), Http3State::Connected);
    }

    #[test]
    fn fetch_basic() {
        // Connect exchange headers and send a request. Also check if the correct header frame has
        // been sent.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        // send response - 200  Content-Length: 7
        // with content: 'abcdefg'.
        // The content will be send in 2 DATA frames.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_1,
            true,
        );

        let http_events = client.events().collect::<Vec<_>>();
        assert_eq!(http_events.len(), 2);
        for e in http_events {
            match e {
                Http3ClientEvent::HeaderReady {
                    stream_id,
                    headers,
                    interim,
                    fin,
                } => {
                    assert_eq!(stream_id, request_stream_id);
                    check_response_header_1(&headers);
                    assert!(!fin);
                    assert!(!interim);
                }
                Http3ClientEvent::DataReadable { stream_id } => {
                    assert_eq!(stream_id, request_stream_id);
                    let mut buf = [0_u8; 100];
                    let (amount, fin) = client.read_data(now(), stream_id, &mut buf).unwrap();
                    assert!(fin);
                    assert_eq!(amount, EXPECTED_RESPONSE_DATA_1.len());
                    assert_eq!(&buf[..amount], EXPECTED_RESPONSE_DATA_1);
                }
                _ => {}
            }
        }

        // after this stream will be removed from conn. We will check this by trying to read
        // from the stream and that should fail.
        let mut buf = [0_u8; 100];
        let res = client.read_data(now(), request_stream_id, &mut buf);
        assert_eq!(res.unwrap_err(), Error::InvalidStreamId);

        client.close(now(), 0, "");
    }

    /// Force both endpoints into an idle state.
    /// Do this by opening unidirectional streams at both endpoints and sending
    /// a partial unidirectional stream type (which the receiver has to buffer),
    /// then delivering packets out of order.
    /// This forces the receiver to create an acknowledgment, which will allow
    /// the peer to become idle.
    fn force_idle(client: &mut Http3Client, server: &mut TestServer) {
        // Send a partial unidirectional stream ID.
        // Note that this can't close the stream as that causes the receiver
        // to send `MAX_STREAMS`, which would prevent it from becoming idle.
        fn dgram(c: &mut Connection) -> Datagram {
            let stream = c.stream_create(StreamType::UniDi).unwrap();
            _ = c.stream_send(stream, &[0xc0]).unwrap();
            c.process_output(now()).dgram().unwrap()
        }

        let d1 = dgram(&mut client.conn);
        let d2 = dgram(&mut client.conn);
        server.conn.process_input(d2, now());
        server.conn.process_input(d1, now());
        let d3 = dgram(&mut server.conn);
        let d4 = dgram(&mut server.conn);
        client.process_input(d4, now());
        client.process_input(d3, now());
        let ack = client.process_output(now()).dgram();
        server.conn.process_input(ack.unwrap(), now());
    }

    /// The client should keep a connection alive if it has unanswered requests.
    #[test]
    fn fetch_keep_alive() {
        let (mut client, mut server, _request_stream_id) = connect_and_send_request(true);
        force_idle(&mut client, &mut server);

        assert_eq!(
            client.process_output(now()).callback(),
            DEFAULT_IDLE_TIMEOUT / 2
        );
    }

    // Helper function: read response when a server sends HTTP_RESPONSE_2.
    fn read_response(
        client: &mut Http3Client,
        server: &mut Connection,
        request_stream_id: StreamId,
    ) {
        let out = server.process_output(now());
        client.process(out.dgram(), now());

        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::HeaderReady {
                    stream_id,
                    headers,
                    interim,
                    fin,
                } => {
                    assert_eq!(stream_id, request_stream_id);
                    check_response_header_2(&headers);
                    assert!(!fin);
                    assert!(!interim);
                }
                Http3ClientEvent::DataReadable { stream_id } => {
                    assert_eq!(stream_id, request_stream_id);
                    let mut buf = [0_u8; 100];
                    let (amount, fin) = client.read_data(now(), stream_id, &mut buf).unwrap();
                    assert!(fin);
                    assert_eq!(amount, EXPECTED_RESPONSE_DATA_2_FRAME_1.len());
                    assert_eq!(&buf[..amount], EXPECTED_RESPONSE_DATA_2_FRAME_1);
                }
                _ => {}
            }
        }

        // after this stream will be removed from client. We will check this by trying to read
        // from the stream and that should fail.
        let mut buf = [0_u8; 100];
        let res = client.read_data(now(), request_stream_id, &mut buf);
        assert!(res.is_err());
        assert_eq!(res.unwrap_err(), Error::InvalidStreamId);

        client.close(now(), 0, "");
    }

    // Data sent with a request:
    const REQUEST_BODY: &[u8] = &[0x64, 0x65, 0x66];
    // Corresponding data frame that server will receive.
    const EXPECTED_REQUEST_BODY_FRAME: &[u8] = &[0x0, 0x3, 0x64, 0x65, 0x66];

    // Send a request with the request body.
    #[test]
    fn fetch_with_data() {
        // Connect exchange headers and send a request. Also check if the correct header frame has
        // been sent.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(false);

        // Get DataWritable for the request stream so that we can write the request body.
        let data_writable = |e| matches!(e, Http3ClientEvent::DataWritable { .. });
        assert!(client.events().any(data_writable));
        let sent = client
            .send_data(request_stream_id, REQUEST_BODY, now())
            .unwrap();
        assert_eq!(sent, REQUEST_BODY.len());
        client.stream_close_send(request_stream_id, now()).unwrap();

        let out = client.process_output(now());
        drop(server.conn.process(out.dgram(), now()));

        // find the new request/response stream and send response on it.
        while let Some(e) = server.conn.next_event() {
            match e {
                ConnectionEvent::NewStream { stream_id } => {
                    assert_eq!(stream_id, request_stream_id);
                    assert_eq!(stream_id.stream_type(), StreamType::BiDi);
                }
                ConnectionEvent::RecvStreamReadable { stream_id } => {
                    assert_eq!(stream_id, request_stream_id);

                    // Read request body.
                    let mut buf = [0_u8; 100];
                    let (amount, fin) = server.conn.stream_recv(stream_id, &mut buf).unwrap();
                    assert!(fin);
                    assert_eq!(amount, EXPECTED_REQUEST_BODY_FRAME.len());
                    assert_eq!(&buf[..amount], EXPECTED_REQUEST_BODY_FRAME);

                    // send response - 200  Content-Length: 3
                    // with content: 'abc'.
                    _ = server.conn.stream_send(stream_id, HTTP_RESPONSE_2).unwrap();
                    server.conn.stream_close_send(stream_id).unwrap();
                }
                _ => {}
            }
        }

        read_response(&mut client, &mut server.conn, request_stream_id);
    }

    // Committing the data written on a regular request, then resetting, sends RESET_STREAM_AT so
    // the committed prefix is still reliably delivered.
    #[test]
    fn fetch_commit_then_reset_is_reliable() {
        let (mut client, _server, request_stream_id) = connect_and_send_request(false);

        // Commit the data buffered so far (the request header frame).
        client.stream_commit(request_stream_id, now()).unwrap();

        // Resetting now reliably delivers the committed prefix via RESET_STREAM_AT.
        let before = client.transport_stats().frame_tx;
        client
            .stream_reset_send(request_stream_id, Error::HttpNone.code())
            .unwrap();
        _ = client.process_output(now());
        let after = client.transport_stats().frame_tx;
        assert_eq!(after.reset_stream_at, before.reset_stream_at + 1);
        assert_eq!(after.reset_stream, before.reset_stream);
    }

    // If the data written on a regular request cannot be flushed to the transport (here because
    // the connection send credit is exhausted), commit() fails rather than committing a short
    // prefix. Mirrors `wt_client_stream_commit_blocked_preface_is_safe`.
    #[test]
    fn fetch_commit_blocked_when_flow_control_exhausted() {
        let (mut client, _server) =
            connect_with_connection_parameters(ConnectionParameters::default().max_data(2000));

        // Soak up the connection send credit on one request.
        let filler = make_request(&mut client, false, &[]);
        let buf = [0; 4096];
        while client.send_data(filler, &buf, now()).unwrap() > 0 {}

        // A fresh request cannot flush its buffered headers, so committing fails.
        let blocked = make_request(&mut client, false, &[]);
        assert_eq!(
            client.stream_commit(blocked, now()),
            Err(Error::FlowControlLimit)
        );
        assert_eq!(client.send_data(blocked, &[0; 10], now()), Ok(0));
    }

    // send a request with request body containing request_body. We expect to receive
    // expected_data_frame_header.
    fn fetch_with_data_length_xbytes(request_body: &[u8], expected_data_frame_header: &[u8]) {
        // Connect exchange headers and send a request. Also check if the correct header frame has
        // been sent.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(false);

        // Get DataWritable for the request stream so that we can write the request body.
        let data_writable = |e| matches!(e, Http3ClientEvent::DataWritable { .. });
        assert!(client.events().any(data_writable));
        let sent = client.send_data(request_stream_id, request_body, now());
        assert_eq!(sent, Ok(request_body.len()));

        // Close stream.
        client.stream_close_send(request_stream_id, now()).unwrap();

        // We need to loop a bit until all data has been sent.
        let mut out = client.process_output(now());
        for _i in 0..20 {
            out = server.conn.process(out.dgram(), now());
            out = client.process(out.dgram(), now());
        }

        // check request body is received.
        // Then send a response.
        while let Some(e) = server.conn.next_event() {
            if let ConnectionEvent::RecvStreamReadable { stream_id } = e
                && stream_id == request_stream_id
            {
                // Read the DATA frame.
                let mut buf = vec![1_u8; INITIAL_LOCAL_MAX_STREAM_DATA];
                let (amount, fin) = server.conn.stream_recv(stream_id, &mut buf).unwrap();
                assert!(fin);
                assert_eq!(
                    amount,
                    request_body.len() + expected_data_frame_header.len()
                );

                // Check the DATA frame header
                assert_eq!(
                    &buf[..expected_data_frame_header.len()],
                    expected_data_frame_header
                );

                // Check data.
                assert_eq!(&buf[expected_data_frame_header.len()..amount], request_body);

                // send response - 200  Content-Length: 3
                // with content: 'abc'.
                _ = server.conn.stream_send(stream_id, HTTP_RESPONSE_2).unwrap();
                server.conn.stream_close_send(stream_id).unwrap();
            }
        }

        read_response(&mut client, &mut server.conn, request_stream_id);
    }

    // send a request with 63 bytes. The DATA frame length field will still have 1 byte.
    #[test]
    fn fetch_with_data_length_63bytes() {
        fetch_with_data_length_xbytes(&[0_u8; 63], &[0x0, 0x3f]);
    }

    // send a request with 64 bytes. The DATA frame length field will need 2 byte.
    #[test]
    fn fetch_with_data_length_64bytes() {
        fetch_with_data_length_xbytes(&[0_u8; 64], &[0x0, 0x40, 0x40]);
    }

    // send a request with 16383 bytes. The DATA frame length field will still have 2 byte.
    #[test]
    fn fetch_with_data_length_16383bytes() {
        fetch_with_data_length_xbytes(&[0_u8; 16383], &[0x0, 0x7f, 0xff]);
    }

    // send a request with 16384 bytes. The DATA frame length field will need 4 byte.
    #[test]
    fn fetch_with_data_length_16384bytes() {
        fetch_with_data_length_xbytes(&[0_u8; 16384], &[0x0, 0x80, 0x0, 0x40, 0x0]);
    }

    // Send 2 data frames so that the second one cannot fit into the send_buf and it is only
    // partially sent. We check that the sent data is correct.
    fn fetch_with_two_data_frames(
        first_frame: &[u8],
        expected_first_data_frame_header: &[u8],
        expected_second_data_frame_header: &[u8],
        expected_second_data_frame: &[u8],
    ) {
        // Connect exchange headers and send a request. Also check if the correct header frame has
        // been sent.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(false);

        // Get DataWritable for the request stream so that we can write the request body.
        let data_writable = |e| matches!(e, Http3ClientEvent::DataWritable { .. });
        assert!(client.events().any(data_writable));

        // Send the first frame.
        let sent = client.send_data(request_stream_id, first_frame, now());
        assert_eq!(sent, Ok(first_frame.len()));

        // The second frame cannot fit.
        let sent = client.send_data(
            request_stream_id,
            &vec![0_u8; INITIAL_LOCAL_MAX_STREAM_DATA],
            now(),
        );
        assert_eq!(sent, Ok(expected_second_data_frame.len()));

        // Close stream.
        client.stream_close_send(request_stream_id, now()).unwrap();

        let mut out = client.process_output(now());
        // We need to loop a bit until all data has been sent. Once for every 1K
        // of data.
        for _i in 0..INITIAL_LOCAL_MAX_STREAM_DATA / 1000 {
            out = server.conn.process(out.dgram(), now());
            out = client.process(out.dgram(), now());
        }

        // Check received frames and send a response.
        while let Some(e) = server.conn.next_event() {
            if let ConnectionEvent::RecvStreamReadable { stream_id } = e
                && stream_id == request_stream_id
            {
                // Read DATA frames.
                let mut buf = vec![1_u8; INITIAL_LOCAL_MAX_STREAM_DATA];
                let (amount, fin) = server.conn.stream_recv(stream_id, &mut buf).unwrap();
                assert!(fin);
                assert_eq!(
                    amount,
                    expected_first_data_frame_header.len()
                        + first_frame.len()
                        + expected_second_data_frame_header.len()
                        + expected_second_data_frame.len()
                );

                // Check the first DATA frame header
                let end = expected_first_data_frame_header.len();
                assert_eq!(&buf[..end], expected_first_data_frame_header);

                // Check the first frame data.
                let start = end;
                let end = end + first_frame.len();
                assert_eq!(&buf[start..end], first_frame);

                // Check the second DATA frame header
                let start2 = end;
                let end2 = end + expected_second_data_frame_header.len();
                assert_eq!(&buf[start2..end2], expected_second_data_frame_header);

                // Check the second frame data.
                let start3 = end2;
                let end3 = end2 + expected_second_data_frame.len();
                assert_eq!(&buf[start3..end3], expected_second_data_frame);

                // send response - 200  Content-Length: 3
                // with content: 'abc'.
                _ = server.conn.stream_send(stream_id, HTTP_RESPONSE_2).unwrap();
                server.conn.stream_close_send(stream_id).unwrap();
            }
        }

        read_response(&mut client, &mut server.conn, request_stream_id);
    }

    fn alloc_buffer(size: usize) -> (Vec<u8>, Vec<u8>) {
        let data_frame = HFrame::Data { len: to_u64(size) };
        let mut enc = Encoder::default();
        data_frame.encode(&mut enc);

        (vec![0_u8; size], enc.as_ref().to_vec())
    }

    // Send 2 frames. For the second one we can only send 63 bytes.
    // After the first frame there is exactly 63+2 bytes left in the send buffer.
    #[test]
    fn fetch_two_data_frame_second_63bytes() {
        let (buf, hdr) = alloc_buffer(INITIAL_LOCAL_MAX_STREAM_DATA - 88);
        fetch_with_two_data_frames(&buf, &hdr, &[0x0, 0x3f], &[0_u8; 63]);
    }

    // Send 2 frames. For the second one we can only send 63 bytes.
    // After the first frame there is exactly 63+3 bytes left in the send buffer,
    // but we can only send 63 bytes.
    #[test]
    fn fetch_two_data_frame_second_63bytes_place_for_66() {
        let (buf, hdr) = alloc_buffer(INITIAL_LOCAL_MAX_STREAM_DATA - 89);
        fetch_with_two_data_frames(&buf, &hdr, &[0x0, 0x3f], &[0_u8; 63]);
    }

    // Send 2 frames. For the second one we can only send 64 bytes.
    // After the first frame there is exactly 64+3 bytes left in the send buffer,
    // but we can only send 64 bytes.
    #[test]
    fn fetch_two_data_frame_second_64bytes_place_for_67() {
        let (buf, hdr) = alloc_buffer(INITIAL_LOCAL_MAX_STREAM_DATA - 90);
        fetch_with_two_data_frames(&buf, &hdr, &[0x0, 0x40, 0x40], &[0_u8; 64]);
    }

    // Send 2 frames. For the second one we can only send 16383 bytes.
    // After the first frame there is exactly 16383+3 bytes left in the send buffer.
    #[test]
    fn fetch_two_data_frame_second_16383bytes() {
        let (buf, hdr) = alloc_buffer(INITIAL_LOCAL_MAX_STREAM_DATA - 16409);
        fetch_with_two_data_frames(&buf, &hdr, &[0x0, 0x7f, 0xff], &[0_u8; 16383]);
    }

    // Send 2 frames. For the second one we can only send 16383 bytes.
    // After the first frame there is exactly 16383+4 bytes left in the send buffer, but we can only
    // send 16383 bytes.
    #[test]
    fn fetch_two_data_frame_second_16383bytes_place_for_16387() {
        let (buf, hdr) = alloc_buffer(INITIAL_LOCAL_MAX_STREAM_DATA - 16410);
        fetch_with_two_data_frames(&buf, &hdr, &[0x0, 0x7f, 0xff], &[0_u8; 16383]);
    }

    // Send 2 frames. For the second one we can only send 16383 bytes.
    // After the first frame there is exactly 16383+5 bytes left in the send buffer, but we can only
    // send 16383 bytes.
    #[test]
    fn fetch_two_data_frame_second_16383bytes_place_for_16388() {
        let (buf, hdr) = alloc_buffer(INITIAL_LOCAL_MAX_STREAM_DATA - 16411);
        fetch_with_two_data_frames(&buf, &hdr, &[0x0, 0x7f, 0xff], &[0_u8; 16383]);
    }

    // Send 2 frames. For the second one we can send 16384 bytes.
    // After the first frame there is exactly 16384+5 bytes left in the send buffer, but we can send
    // 16384 bytes.
    #[test]
    fn fetch_two_data_frame_second_16384bytes_place_for_16389() {
        let (buf, hdr) = alloc_buffer(INITIAL_LOCAL_MAX_STREAM_DATA - 16412);
        fetch_with_two_data_frames(&buf, &hdr, &[0x0, 0x80, 0x0, 0x40, 0x0], &[0_u8; 16384]);
    }

    // Test receiving STOP_SENDING with the HttpNoError error code.
    #[test]
    fn stop_sending_early_response() {
        // Connect exchange headers and send a request. Also check if the correct header frame has
        // been sent.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(false);

        // Stop sending with early_response.
        assert_eq!(
            Ok(()),
            server
                .conn
                .stream_stop_sending(request_stream_id, Error::HttpNone.code())
        );

        // send response - 200  Content-Length: 3
        // with content: 'abc'.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_2,
            true,
        );

        let mut stop_sending = false;
        let mut response_headers = false;
        let mut response_body = false;
        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::StopSending { stream_id, error } => {
                    assert_eq!(stream_id, request_stream_id);
                    assert_eq!(error, Error::HttpNone.code());
                    // assert that we cannot send any more request data.
                    assert_eq!(
                        Err(Error::InvalidStreamId),
                        client.send_data(request_stream_id, &[0_u8; 10], now())
                    );
                    stop_sending = true;
                }
                Http3ClientEvent::HeaderReady {
                    stream_id,
                    headers,
                    interim,
                    fin,
                } => {
                    assert_eq!(stream_id, request_stream_id);
                    check_response_header_2(&headers);
                    assert!(!fin);
                    assert!(!interim);
                    response_headers = true;
                }
                Http3ClientEvent::DataReadable { stream_id } => {
                    assert_eq!(stream_id, request_stream_id);
                    let mut buf = [0_u8; 100];
                    let (amount, fin) = client.read_data(now(), stream_id, &mut buf).unwrap();
                    assert!(fin);
                    assert_eq!(amount, EXPECTED_RESPONSE_DATA_2_FRAME_1.len());
                    assert_eq!(&buf[..amount], EXPECTED_RESPONSE_DATA_2_FRAME_1);
                    response_body = true;
                }
                _ => {}
            }
        }
        assert!(response_headers);
        assert!(response_body);
        assert!(stop_sending);

        // after this stream will be removed from client. We will check this by trying to read
        // from the stream and that should fail.
        let mut buf = [0_u8; 100];
        let res = client.read_data(now(), request_stream_id, &mut buf);
        assert!(res.is_err());
        assert_eq!(res.unwrap_err(), Error::InvalidStreamId);

        client.close(now(), 0, "");
    }

    // Server sends stop sending and reset.
    #[test]
    fn stop_sending_other_error_with_reset() {
        // Connect exchange headers and send a request. Also check if the correct header frame has
        // been sent.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(false);

        // Stop sending with RequestRejected.
        assert_eq!(
            Ok(()),
            server
                .conn
                .stream_stop_sending(request_stream_id, Error::HttpRequestRejected.code())
        );
        // also reset with RequestRejected.
        assert_eq!(
            Ok(()),
            server
                .conn
                .stream_reset_send(request_stream_id, Error::HttpRequestRejected.code())
        );

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        let mut reset = false;
        let mut stop_sending = false;
        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::StopSending { stream_id, error } => {
                    assert_eq!(stream_id, request_stream_id);
                    assert_eq!(error, Error::HttpRequestRejected.code());
                    stop_sending = true;
                }
                Http3ClientEvent::Reset {
                    stream_id,
                    error,
                    local,
                } => {
                    assert_eq!(stream_id, request_stream_id);
                    assert_eq!(error, Error::HttpRequestRejected.code());
                    assert!(!local);
                    reset = true;
                }
                Http3ClientEvent::HeaderReady { .. } | Http3ClientEvent::DataReadable { .. } => {
                    panic!("We should not get any headers or data");
                }
                _ => {}
            }
        }

        assert!(reset);
        assert!(stop_sending);

        // after this stream will be removed from client. We will check this by trying to read
        // from the stream and that should fail.
        let mut buf = [0_u8; 100];
        let res = client.read_data(now(), request_stream_id, &mut buf);
        assert!(res.is_err());
        assert_eq!(res.unwrap_err(), Error::InvalidStreamId);

        client.close(now(), 0, "");
    }

    // Server sends stop sending with RequestRejected, but it does not send reset.
    #[test]
    fn stop_sending_other_error_wo_reset() {
        // Connect exchange headers and send a request. Also check if the correct header frame has
        // been sent.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(false);

        // Stop sending with RequestRejected.
        assert_eq!(
            Ok(()),
            server
                .conn
                .stream_stop_sending(request_stream_id, Error::HttpRequestRejected.code())
        );

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        let mut stop_sending = false;

        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::StopSending { stream_id, error } => {
                    assert_eq!(stream_id, request_stream_id);
                    assert_eq!(error, Error::HttpRequestRejected.code());
                    stop_sending = true;
                }
                Http3ClientEvent::Reset { .. } => {
                    panic!("We should not get StopSending");
                }
                Http3ClientEvent::HeaderReady { .. } | Http3ClientEvent::DataReadable { .. } => {
                    panic!("We should not get any headers or data");
                }
                _ => {}
            }
        }

        assert!(stop_sending);

        // after this we can still read from a stream.
        let mut buf = [0_u8; 100];
        let res = client.read_data(now(), request_stream_id, &mut buf);
        assert!(res.is_ok());

        client.close(now(), 0, "");
    }

    // Server sends stop sending and reset. We have some events for that stream already
    // in client.events. The events will be removed.
    #[test]
    fn stop_sending_and_reset_other_error_with_events() {
        // Connect exchange headers and send a request. Also check if the correct header frame has
        // been sent.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(false);

        // send response - 200  Content-Length: 3
        // with content: 'abc'.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_2,
            false,
        );
        // At this moment we have some new events, i.e. a HeadersReady event

        // Send a stop sending and reset.
        assert_eq!(
            Ok(()),
            server
                .conn
                .stream_stop_sending(request_stream_id, Error::HttpRequestCancelled.code())
        );
        assert_eq!(
            Ok(()),
            server
                .conn
                .stream_reset_send(request_stream_id, Error::HttpRequestCancelled.code())
        );

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        let mut reset = false;
        let mut headers = false;

        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::StopSending { stream_id, error } => {
                    assert_eq!(stream_id, request_stream_id);
                    assert_eq!(error, Error::HttpRequestCancelled.code());
                }
                Http3ClientEvent::Reset {
                    stream_id,
                    error,
                    local,
                } => {
                    assert_eq!(stream_id, request_stream_id);
                    assert_eq!(error, Error::HttpRequestCancelled.code());
                    assert!(!local);
                    reset = true;
                }
                // An already-delivered header block survives a reset (a reliable reset relies on
                // this); only data events, which require a read on the torn-down stream, are
                // dropped.
                Http3ClientEvent::HeaderReady { .. } => headers = true,
                Http3ClientEvent::DataReadable { .. } => {
                    panic!("We should not get data after a reset");
                }
                _ => {}
            }
        }

        assert!(reset);
        assert!(headers);

        // after this stream will be removed from client. We will check this by trying to read
        // from the stream and that should fail.
        let mut buf = [0_u8; 100];
        let res = client.read_data(now(), request_stream_id, &mut buf);
        assert!(res.is_err());
        assert_eq!(res.unwrap_err(), Error::InvalidStreamId);

        client.close(now(), 0, "");
    }

    // Server sends stop sending with code that is not HttpNoError.
    // We have some events for that stream already in the client.events.
    // The events will be removed.
    #[test]
    fn stop_sending_other_error_with_events() {
        // Connect exchange headers and send a request. Also check if the correct header frame has
        // been sent.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(false);

        // send response - 200  Content-Length: 3
        // with content: 'abc'.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_2,
            false,
        );
        // At this moment we have some new event, i.e. a HeadersReady event

        // Send a stop sending.
        assert_eq!(
            Ok(()),
            server
                .conn
                .stream_stop_sending(request_stream_id, Error::HttpRequestCancelled.code())
        );

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        let mut stop_sending = false;
        let mut header_ready = false;

        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::StopSending { stream_id, error } => {
                    assert_eq!(stream_id, request_stream_id);
                    assert_eq!(error, Error::HttpRequestCancelled.code());
                    stop_sending = true;
                }
                Http3ClientEvent::Reset { .. } => {
                    panic!("We should not get StopSending");
                }
                Http3ClientEvent::HeaderReady { .. } | Http3ClientEvent::DataReadable { .. } => {
                    header_ready = true;
                }
                _ => {}
            }
        }

        assert!(stop_sending);
        assert!(header_ready);

        // after this, we can sill read data from a stream.
        let mut buf = [0_u8; 100];
        let (amount, fin) = client
            .read_data(now(), request_stream_id, &mut buf)
            .unwrap();
        assert!(!fin);
        assert_eq!(amount, EXPECTED_RESPONSE_DATA_2_FRAME_1.len());
        assert_eq!(&buf[..amount], EXPECTED_RESPONSE_DATA_2_FRAME_1);

        client.close(now(), 0, "");
    }

    // Server sends a reset. We will close sending side as well.
    #[test]
    fn reset_wo_stop_sending() {
        // Connect exchange headers and send a request. Also check if the correct header frame has
        // been sent.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(false);

        // Send a reset.
        assert_eq!(
            Ok(()),
            server
                .conn
                .stream_reset_send(request_stream_id, Error::HttpRequestCancelled.code())
        );

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        let mut reset = false;

        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::StopSending { .. } => {
                    panic!("We should not get StopSending");
                }
                Http3ClientEvent::Reset {
                    stream_id,
                    error,
                    local,
                } => {
                    assert_eq!(stream_id, request_stream_id);
                    assert_eq!(error, Error::HttpRequestCancelled.code());
                    assert!(!local);
                    reset = true;
                }
                Http3ClientEvent::HeaderReady { .. } | Http3ClientEvent::DataReadable { .. } => {
                    panic!("We should not get any headers or data");
                }
                _ => {}
            }
        }

        assert!(reset);

        // after this stream will be removed from client. We will check this by trying to read
        // from the stream and that should fail.
        let mut buf = [0_u8; 100];
        let res = client.read_data(now(), request_stream_id, &mut buf);
        assert!(res.is_err());
        assert_eq!(res.unwrap_err(), Error::InvalidStreamId);

        client.close(now(), 0, "");
    }

    fn test_incomplet_frame(buf: &[u8], error: &Error) {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            buf,
            true,
        );

        while let Some(e) = client.next_event() {
            if let Http3ClientEvent::DataReadable { stream_id } = e {
                assert_eq!(stream_id, request_stream_id);
                let mut buf_res = [0_u8; 100];
                let res = client.read_data(now(), stream_id, &mut buf_res);
                assert!(res.is_err());
                assert_eq!(res.unwrap_err(), Error::HttpFrame);
            }
        }
        assert_closed(&client, error);
    }

    // Incomplete DATA frame
    #[test]
    fn incomplet_data_frame() {
        test_incomplet_frame(&HTTP_RESPONSE_2[..12], &Error::HttpFrame);
    }

    // Incomplete HEADERS frame
    #[test]
    fn incomplet_headers_frame() {
        test_incomplet_frame(&HTTP_RESPONSE_2[..7], &Error::HttpFrame);
    }

    #[test]
    fn incomplet_unknown_frame() {
        test_incomplet_frame(&[0x21], &Error::HttpFrame);
    }

    // test goaway
    #[test]
    fn goaway() {
        let (mut client, mut server) = connect();
        let request_stream_id_1 = make_request(&mut client, false, &[]);
        assert_eq!(request_stream_id_1, 0);
        let request_stream_id_2 = make_request(&mut client, false, &[]);
        assert_eq!(request_stream_id_2, 4);
        let request_stream_id_3 = make_request(&mut client, false, &[]);
        assert_eq!(request_stream_id_3, 8);

        let out = client.process_output(now());
        drop(server.conn.process(out.dgram(), now()));

        _ = server
            .conn
            .stream_send(server.control_stream_id.unwrap(), &[0x7, 0x1, 0x8])
            .unwrap();

        // find the new request/response stream and send frame v on it.
        while let Some(e) = server.conn.next_event() {
            if let ConnectionEvent::RecvStreamReadable { stream_id } = e {
                let mut buf = [0_u8; 100];
                _ = server.conn.stream_recv(stream_id, &mut buf).unwrap();
                if (stream_id == request_stream_id_1) || (stream_id == request_stream_id_2) {
                    // send response - 200  Content-Length: 7
                    // with content: 'abcdefg'.
                    // The content will be send in 2 DATA frames.
                    _ = server.conn.stream_send(stream_id, HTTP_RESPONSE_1).unwrap();
                    server.conn.stream_close_send(stream_id).unwrap();
                }
            }
        }
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        let mut stream_reset = false;
        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::HeaderReady { headers, fin, .. } => {
                    check_response_header_1(&headers);
                    assert!(!fin);
                }
                Http3ClientEvent::DataReadable { stream_id } => {
                    assert!(
                        (stream_id == request_stream_id_1) || (stream_id == request_stream_id_2)
                    );
                    let mut buf = [0_u8; 100];
                    assert_eq!(
                        (EXPECTED_RESPONSE_DATA_1.len(), true),
                        client.read_data(now(), stream_id, &mut buf).unwrap()
                    );
                }
                Http3ClientEvent::Reset {
                    stream_id,
                    error,
                    local,
                } => {
                    assert_eq!(stream_id, request_stream_id_3);
                    assert_eq!(error, Error::HttpRequestRejected.code());
                    assert!(!local);
                    stream_reset = true;
                }
                _ => {}
            }
        }

        assert!(stream_reset);
        assert_eq!(client.state(), Http3State::GoingAway(StreamId::new(8)));

        // Check that a new request cannot be made.
        assert_eq!(
            client.fetch(
                now(),
                "GET",
                &("https", "something.com", "/"),
                &[],
                Priority::default()
            ),
            Err(Error::AlreadyClosed)
        );

        client.close(now(), 0, "");
    }

    #[test]
    fn multiple_goaways() {
        let (mut client, mut server) = connect();
        let request_stream_id_1 = make_request(&mut client, false, &[]);
        assert_eq!(request_stream_id_1, 0);
        let request_stream_id_2 = make_request(&mut client, false, &[]);
        assert_eq!(request_stream_id_2, 4);
        let request_stream_id_3 = make_request(&mut client, false, &[]);
        assert_eq!(request_stream_id_3, 8);

        let out = client.process_output(now());
        drop(server.conn.process(out.dgram(), now()));

        // First send a Goaway frame with an higher number
        _ = server
            .conn
            .stream_send(server.control_stream_id.unwrap(), &[0x7, 0x1, 0x8])
            .unwrap();

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        // Check that there is one reset for stream_id 8
        let mut stream_reset_1 = 0;
        while let Some(e) = client.next_event() {
            if let Http3ClientEvent::Reset {
                stream_id,
                error,
                local,
            } = e
            {
                assert_eq!(stream_id, request_stream_id_3);
                assert_eq!(error, Error::HttpRequestRejected.code());
                assert!(!local);
                stream_reset_1 += 1;
            }
        }

        assert_eq!(stream_reset_1, 1);
        assert_eq!(client.state(), Http3State::GoingAway(StreamId::new(8)));

        // Server sends another GOAWAY frame
        _ = server
            .conn
            .stream_send(server.control_stream_id.unwrap(), &[0x7, 0x1, 0x4])
            .unwrap();

        // Send response for stream 0
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id_1,
            HTTP_RESPONSE_1,
            true,
        );

        let mut stream_reset_2 = 0;
        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::HeaderReady { headers, fin, .. } => {
                    check_response_header_1(&headers);
                    assert!(!fin);
                }
                Http3ClientEvent::DataReadable { stream_id } => {
                    assert_eq!(stream_id, request_stream_id_1);
                    let mut buf = [0_u8; 100];
                    assert_eq!(
                        (EXPECTED_RESPONSE_DATA_1.len(), true),
                        client.read_data(now(), stream_id, &mut buf).unwrap()
                    );
                }
                Http3ClientEvent::Reset {
                    stream_id,
                    error,
                    local,
                } => {
                    assert_eq!(stream_id, request_stream_id_2);
                    assert_eq!(error, Error::HttpRequestRejected.code());
                    assert!(!local);
                    stream_reset_2 += 1;
                }
                _ => {}
            }
        }

        assert_eq!(stream_reset_2, 1);
        assert_eq!(client.state(), Http3State::GoingAway(StreamId::new(4)));
    }

    #[test]
    fn multiple_goaways_single_event() {
        let (mut client, mut server) = connect();
        let goaways = [8, 4, 0]
            .into_iter()
            .flat_map(|id| send_goaway(&mut client, &mut server, StreamId::new(id)))
            .filter(|e| matches!(e, Http3ClientEvent::GoawayReceived))
            .count();
        assert_eq!(goaways, 1);
        assert_eq!(client.state(), Http3State::GoingAway(StreamId::new(0)));
    }

    /// Sends a GOAWAY to the client and returns the client events.
    fn send_goaway(
        client: &mut Http3Client,
        server: &mut TestServer,
        stream_id: StreamId,
    ) -> Vec<Http3ClientEvent> {
        let mut enc = Encoder::default();
        HFrame::Goaway { stream_id }.encode(&mut enc);
        let control_stream_id = server.control_stream_id.unwrap();
        _ = server
            .conn
            .stream_send(control_stream_id, enc.as_ref())
            .unwrap();
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());
        client.events().collect()
    }

    /// The bidi stream IDs >= `base` in `send_streams` and `recv_streams`, in table order.
    fn bidi_ids_from(client: &Http3Client, base: StreamId) -> (Vec<StreamId>, Vec<StreamId>) {
        fn from<'a>(ids: impl Iterator<Item = &'a StreamId>, base: StreamId) -> Vec<StreamId> {
            ids.filter(|id| id.is_bidi() && **id >= base)
                .copied()
                .collect()
        }
        let h = &client.base_handler;
        (
            from(h.send_streams().keys(), base),
            from(h.recv_streams().keys(), base),
        )
    }

    /// The events a GOAWAY raises for rejected requests, in table order.
    fn rejected_events(send: Vec<StreamId>, recv: Vec<StreamId>) -> Vec<Http3ClientEvent> {
        let error = Error::HttpRequestRejected.code();
        send.into_iter()
            .map(|stream_id| Http3ClientEvent::StopSending { stream_id, error })
            .chain(recv.into_iter().map(|stream_id| Http3ClientEvent::Reset {
                stream_id,
                error,
                local: false,
            }))
            .collect()
    }

    fn sorted(mut ids: Vec<StreamId>) -> Vec<StreamId> {
        ids.sort_unstable();
        ids
    }

    #[test]
    fn lower_goaway_rejects_only_new_requests() {
        let (mut client, mut server) = connect();
        let ids: Vec<_> = iter::repeat_with(|| make_request(&mut client, false, &[]))
            .take(20)
            .collect();
        send_goaway(&mut client, &mut server, ids[15]);

        let (send, recv) = bidi_ids_from(&client, ids[5]);
        assert_eq!(sorted(send.clone()), ids[5..15]);
        assert_eq!(sorted(recv.clone()), ids[5..15]);
        let events = send_goaway(&mut client, &mut server, ids[5]);
        assert_eq!(events, rejected_events(send, recv));
        let (send, recv) = bidi_ids_from(&client, ids[0]);
        assert_eq!(sorted(send), ids[..5]);
        assert_eq!(sorted(recv), ids[..5]);
        assert_eq!(client.state(), Http3State::GoingAway(ids[5]));
    }

    #[test]
    fn lower_goaway_skips_ended_requests() {
        let (mut client, mut server) = connect();
        let ids: Vec<_> = iter::repeat_with(|| make_request(&mut client, true, &[]))
            .take(5)
            .collect();
        let out = client.process_output(now());
        drop(server.conn.process(out.dgram(), now()));
        send_goaway(&mut client, &mut server, ids[4]);
        send_goaway(&mut client, &mut server, ids[4]);

        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            ids[3],
            HTTP_RESPONSE_1,
            true,
        );
        let mut buf = [0_u8; 100];
        let read = client.read_data(now(), ids[3], &mut buf).unwrap();
        assert_eq!(read, (EXPECTED_RESPONSE_DATA_1.len(), true));
        client.events().for_each(drop);
        assert_eq!(bidi_ids_from(&client, ids[3]), (vec![], vec![]));

        assert_eq!(send_goaway(&mut client, &mut server, ids[3]), []);

        let (send, recv) = bidi_ids_from(&client, ids[1]);
        assert_eq!(sorted(recv.clone()), ids[1..3]);
        let events = send_goaway(&mut client, &mut server, ids[1]);
        assert_eq!(events, rejected_events(send, recv));
        assert_eq!(bidi_ids_from(&client, ids[0]), (vec![], vec![ids[0]]));
    }

    #[test]
    fn lower_goaway_rejects_request_still_sending() {
        let (mut client, mut server) = connect();
        let ids: Vec<_> = iter::repeat_with(|| make_request(&mut client, false, &[]))
            .take(3)
            .collect();
        let out = client.process_output(now());
        drop(server.conn.process(out.dgram(), now()));
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            ids[2],
            HTTP_RESPONSE_1,
            true,
        );
        let mut buf = [0_u8; 100];
        let read = client.read_data(now(), ids[2], &mut buf).unwrap();
        assert_eq!(read, (EXPECTED_RESPONSE_DATA_1.len(), true));
        send_goaway(&mut client, &mut server, StreamId::new(ids[2].as_u64() + 4));

        let (send, recv) = bidi_ids_from(&client, ids[2]);
        assert_eq!((send.clone(), recv.clone()), (vec![ids[2]], vec![]));
        let events = send_goaway(&mut client, &mut server, ids[2]);
        assert_eq!(events, rejected_events(send, recv));
    }

    #[test]
    fn multiple_goaways_stream_id_increased() {
        let (mut client, mut server) = connect();
        let request_stream_id_1 = make_request(&mut client, false, &[]);
        assert_eq!(request_stream_id_1, 0);
        let request_stream_id_2 = make_request(&mut client, false, &[]);
        assert_eq!(request_stream_id_2, 4);
        let request_stream_id_3 = make_request(&mut client, false, &[]);
        assert_eq!(request_stream_id_3, 8);

        // First send a Goaway frame with a smaller number
        _ = server
            .conn
            .stream_send(server.control_stream_id.unwrap(), &[0x7, 0x1, 0x4])
            .unwrap();

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        assert_eq!(client.state(), Http3State::GoingAway(StreamId::new(4)));

        // Now send a Goaway frame with an higher number
        _ = server
            .conn
            .stream_send(server.control_stream_id.unwrap(), &[0x7, 0x1, 0x8])
            .unwrap();

        let out = server.conn.process_output(now());
        let out = client.process(out.dgram(), now());

        assert_closed(&client, &Error::HttpId);
        server.conn.process_input(out.dgram().unwrap(), now());
        let expected =
            CloseReason::Transport(TransportError::PeerApplication(Error::HttpId.code()));
        assert_eq!(server.conn.state().error(), Some(&expected));
    }

    #[test]
    fn goaway_wrong_stream_id() {
        let (mut client, mut server) = connect();

        _ = server
            .conn
            .stream_send(server.control_stream_id.unwrap(), &[0x7, 0x1, 0x9])
            .unwrap();

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        assert_closed(&client, &Error::HttpId);
    }

    // Close stream before headers.
    #[test]
    fn stream_fin_wo_headers() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);
        // send fin before sending any data.
        server.conn.stream_close_send(request_stream_id).unwrap();

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        // Recv HeaderReady without headers with fin.
        let e = client.events().next().unwrap();
        assert_eq!(
            e,
            Http3ClientEvent::Reset {
                stream_id: request_stream_id,
                error: Error::HttpGeneralProtocolStream.code(),
                local: true,
            }
        );

        // Stream should now be closed and gone
        let mut buf = [0_u8; 100];
        assert_eq!(
            client.read_data(now(), StreamId::new(0), &mut buf),
            Err(Error::InvalidStreamId)
        );
    }

    // Close stream immediately after headers.
    #[test]
    fn stream_fin_after_headers() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_HEADER_ONLY_2,
            true,
        );

        // Recv HeaderReady with headers and fin.
        let e = client.events().next().unwrap();
        if let Http3ClientEvent::HeaderReady {
            stream_id,
            headers,
            interim,
            fin,
        } = e
        {
            assert_eq!(stream_id, request_stream_id);
            check_response_header_2(&headers);
            assert!(fin);
            assert!(!interim);
        } else {
            panic!("wrong event type");
        }

        // Stream should now be closed and gone
        let mut buf = [0_u8; 100];
        assert_eq!(
            client.read_data(now(), StreamId::new(0), &mut buf),
            Err(Error::InvalidStreamId)
        );
    }

    // Send headers, read headers and than close stream.
    // We should get HeaderReady and a DataReadable
    #[test]
    fn stream_fin_after_headers_are_read_wo_data_frame() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);
        // Send some good data without fin
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_HEADER_ONLY_2,
            false,
        );

        // Recv headers without fin
        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::HeaderReady {
                    stream_id,
                    headers,
                    interim,
                    fin,
                } => {
                    assert_eq!(stream_id, request_stream_id);
                    check_response_header_2(&headers);
                    assert!(!fin);
                    assert!(!interim);
                }
                Http3ClientEvent::DataReadable { .. } => {
                    panic!("We should not receive a DataGeadable event!");
                }
                _ => {}
            }
        }

        // ok NOW send fin
        server.conn.stream_close_send(request_stream_id).unwrap();

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        // Recv DataReadable without data with fin
        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::HeaderReady { .. } => {
                    panic!("We should not get another HeaderReady!");
                }
                Http3ClientEvent::DataReadable { stream_id } => {
                    assert_eq!(stream_id, request_stream_id);
                    let mut buf = [0_u8; 100];
                    let res = client.read_data(now(), stream_id, &mut buf);
                    let (len, fin) = res.expect("should read");
                    assert_eq!(0, len);
                    assert!(fin);
                }
                _ => {}
            }
        }

        // Stream should now be closed and gone
        let mut buf = [0_u8; 100];
        assert_eq!(
            client.read_data(now(), StreamId::new(0), &mut buf),
            Err(Error::InvalidStreamId)
        );
    }

    // Send headers and an empty data frame, then close the stream.
    #[test]
    fn stream_fin_after_headers_and_a_empty_data_frame() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        // Send headers.
        _ = server
            .conn
            .stream_send(request_stream_id, HTTP_RESPONSE_HEADER_ONLY_2)
            .unwrap();
        // Send an empty data frame.
        _ = server
            .conn
            .stream_send(request_stream_id, &[0x00, 0x00])
            .unwrap();
        // ok NOW send fin
        server.conn.stream_close_send(request_stream_id).unwrap();

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        // Recv HeaderReady with fin.
        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::HeaderReady {
                    stream_id,
                    headers,
                    interim,
                    fin,
                } => {
                    assert_eq!(stream_id, request_stream_id);
                    check_response_header_2(&headers);
                    assert!(!fin);
                    assert!(!interim);
                }
                Http3ClientEvent::DataReadable { stream_id } => {
                    assert_eq!(stream_id, request_stream_id);
                    let mut buf = [0_u8; 100];
                    assert_eq!(Ok((0, true)), client.read_data(now(), stream_id, &mut buf));
                }
                _ => {}
            }
        }

        // Stream should now be closed and gone
        let mut buf = [0_u8; 100];
        assert_eq!(
            client.read_data(now(), request_stream_id, &mut buf),
            Err(Error::InvalidStreamId)
        );
    }

    // Send headers and an empty data frame. Read headers and then close the stream.
    // We should get a HeaderReady without fin and a DataReadable without data and with fin.
    #[test]
    fn stream_fin_after_headers_an_empty_data_frame_are_read() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);
        // Send some good data without fin
        // Send headers.
        _ = server
            .conn
            .stream_send(request_stream_id, HTTP_RESPONSE_HEADER_ONLY_2)
            .unwrap();
        // Send an empty data frame.
        _ = server
            .conn
            .stream_send(request_stream_id, &[0x00, 0x00])
            .unwrap();

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        // Recv headers without fin
        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::HeaderReady {
                    stream_id,
                    headers,
                    interim,
                    fin,
                } => {
                    assert_eq!(stream_id, request_stream_id);
                    check_response_header_2(&headers);
                    assert!(!fin);
                    assert!(!interim);
                }
                Http3ClientEvent::DataReadable { .. } => {
                    panic!("We should not receive a DataGeadable event!");
                }
                _ => {}
            }
        }

        // ok NOW send fin
        server.conn.stream_close_send(request_stream_id).unwrap();

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        // Recv no data, but do get fin
        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::HeaderReady { .. } => {
                    panic!("We should not get another HeaderReady!");
                }
                Http3ClientEvent::DataReadable { stream_id } => {
                    assert_eq!(stream_id, request_stream_id);
                    let mut buf = [0_u8; 100];
                    let res = client.read_data(now(), stream_id, &mut buf);
                    let (len, fin) = res.expect("should read");
                    assert_eq!(0, len);
                    assert!(fin);
                }
                _ => {}
            }
        }

        // Stream should now be closed and gone
        let mut buf = [0_u8; 100];
        assert_eq!(
            client.read_data(now(), StreamId::new(0), &mut buf),
            Err(Error::InvalidStreamId)
        );
    }

    #[test]
    fn stream_fin_after_a_data_frame() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);
        // Send some good data without fin
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_2,
            false,
        );

        // Recv some good data without fin
        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::HeaderReady {
                    stream_id,
                    headers,
                    interim,
                    fin,
                } => {
                    assert_eq!(stream_id, request_stream_id);
                    check_response_header_2(&headers);
                    assert!(!fin);
                    assert!(!interim);
                }
                Http3ClientEvent::DataReadable { stream_id } => {
                    assert_eq!(stream_id, request_stream_id);
                    let mut buf = [0_u8; 100];
                    let res = client.read_data(now(), stream_id, &mut buf);
                    let (len, fin) = res.expect("should have data");
                    assert_eq!(len, EXPECTED_RESPONSE_DATA_2_FRAME_1.len());
                    assert_eq!(&buf[..len], EXPECTED_RESPONSE_DATA_2_FRAME_1);
                    assert!(!fin);
                }
                _ => {}
            }
        }

        // ok NOW send fin
        server.conn.stream_close_send(request_stream_id).unwrap();
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        // fin without data should generate DataReadable
        let e = client.events().next().unwrap();
        if let Http3ClientEvent::DataReadable { stream_id } = e {
            assert_eq!(stream_id, request_stream_id);
            let mut buf = [0; 100];
            let res = client.read_data(now(), stream_id, &mut buf);
            let (len, fin) = res.expect("should read");
            assert_eq!(0, len);
            assert!(fin);
        } else {
            panic!("wrong event type");
        }

        // Stream should now be closed and gone
        let mut buf = [0_u8; 100];
        assert_eq!(
            client.read_data(now(), StreamId::new(0), &mut buf),
            Err(Error::InvalidStreamId)
        );
    }

    #[test]
    fn multiple_data_frames() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        // Send two data frames with fin
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_1,
            true,
        );

        // Read first frame
        match client.events().nth(1).unwrap() {
            Http3ClientEvent::DataReadable { stream_id } => {
                assert_eq!(stream_id, request_stream_id);
                let mut buf = [0_u8; 100];
                assert_eq!(
                    (EXPECTED_RESPONSE_DATA_1.len(), true),
                    client.read_data(now(), stream_id, &mut buf).unwrap()
                );
            }
            x => {
                panic!("event {x:?}");
            }
        }

        // Stream should now be closed and gone
        let mut buf = [0_u8; 100];
        assert_eq!(
            client.read_data(now(), StreamId::new(0), &mut buf),
            Err(Error::InvalidStreamId)
        );
    }

    #[test]
    fn receive_grease_before_response() {
        // Construct an unknown frame.
        const UNKNOWN_FRAME_LEN: usize = 832;

        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        let mut enc = Encoder::with_capacity(UNKNOWN_FRAME_LEN + 4);
        enc.encode_varint(1028_u64); // Arbitrary type.
        enc.encode_len(UNKNOWN_FRAME_LEN);
        let mut buf: Vec<_> = enc.into();
        buf.resize(UNKNOWN_FRAME_LEN + buf.len(), 0);
        _ = server.conn.stream_send(request_stream_id, &buf).unwrap();

        // Send a headers and a data frame with fin
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_2,
            true,
        );

        // Read first frame
        match client.events().nth(1).unwrap() {
            Http3ClientEvent::DataReadable { stream_id } => {
                assert_eq!(stream_id, request_stream_id);
                let mut buf = [0_u8; 100];
                let (len, fin) = client.read_data(now(), stream_id, &mut buf).unwrap();
                assert_eq!(len, EXPECTED_RESPONSE_DATA_2_FRAME_1.len());
                assert_eq!(&buf[..len], EXPECTED_RESPONSE_DATA_2_FRAME_1);
                assert!(fin);
            }
            x => {
                panic!("event {x:?}");
            }
        }
        // Stream should now be closed and gone
        let mut buf = [0_u8; 100];
        assert_eq!(
            client.read_data(now(), StreamId::new(0), &mut buf),
            Err(Error::InvalidStreamId)
        );
    }

    #[test]
    fn read_frames_header_blocked() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        setup_server_side_encoder(&mut client, &mut server);

        let headers = vec![
            Header::new(":status", "200"),
            Header::new("my-header", "my-header"),
            Header::new("content-length", "3"),
        ];
        let encoded_headers = server.encoder.borrow_mut().encode_header_block(
            &mut server.conn,
            &headers,
            request_stream_id,
        );
        let hframe = HFrame::Headers {
            header_block: encoded_headers.to_vec(),
        };

        // Send the encoder instructions, but delay them so that the stream is blocked on decoding
        // headers.
        let encoder_inst_pkt = server.conn.process_output(now());

        // Send response
        let mut d = Encoder::default();
        hframe.encode(&mut d);
        let d_frame = HFrame::Data { len: 3 };
        d_frame.encode(&mut d);
        d.encode([0x61, 0x62, 0x63]);
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            &d,
            true,
        );

        let header_ready_event = |e| matches!(e, Http3ClientEvent::HeaderReady { .. });
        assert!(!client.events().any(header_ready_event));

        // Let client receive the encoder instructions.
        drop(client.process(encoder_inst_pkt.dgram(), now()));

        let out = server.conn.process_output(now());
        drop(client.process(out.dgram(), now()));
        drop(client.process_output(now()));

        let mut recv_header = false;
        let mut recv_data = false;
        // Now the stream is unblocked and both headers and data will be consumed.
        while let Some(e) = client.next_event() {
            match e {
                Http3ClientEvent::HeaderReady { stream_id, .. } => {
                    assert_eq!(stream_id, request_stream_id);
                    recv_header = true;
                }
                Http3ClientEvent::DataReadable { stream_id } => {
                    recv_data = true;
                    assert_eq!(stream_id, request_stream_id);
                }
                x => {
                    panic!("event {x:?}");
                }
            }
        }
        assert!(recv_header && recv_data);
    }

    #[test]
    fn read_frames_header_blocked_with_fin_after_headers() {
        let (mut hconn, mut server, request_stream_id) = connect_and_send_request(true);

        setup_server_side_encoder(&mut hconn, &mut server);

        let sent_headers = vec![
            Header::new(":status", "200"),
            Header::new("my-header", "my-header"),
            Header::new("content-length", "0"),
        ];
        let encoded_headers = server.encoder.borrow_mut().encode_header_block(
            &mut server.conn,
            &sent_headers,
            request_stream_id,
        );
        let hframe = HFrame::Headers {
            header_block: encoded_headers.to_vec(),
        };

        // Send the encoder instructions, but delay them so that the stream is blocked on decoding
        // headers.
        let encoder_inst_pkt = server.conn.process_output(now());

        let mut d = Encoder::default();
        hframe.encode(&mut d);

        server_send_response_and_exchange_packet(
            &mut hconn,
            &mut server,
            request_stream_id,
            &d,
            true,
        );

        let header_ready_event = |e| matches!(e, Http3ClientEvent::HeaderReady { .. });
        assert!(!hconn.events().any(header_ready_event));

        // Let client receive the encoder instructions.
        let _out = hconn.process(encoder_inst_pkt.dgram(), now());

        let mut recv_header = false;
        // Now the stream is unblocked. After headers we will receive a fin.
        while let Some(e) = hconn.next_event() {
            if let Http3ClientEvent::HeaderReady {
                stream_id,
                headers,
                interim,
                fin,
            } = e
            {
                assert_eq!(stream_id, request_stream_id);
                assert_eq!(headers.as_ref(), sent_headers);
                assert!(fin);
                assert!(!interim);
                recv_header = true;
            } else {
                panic!("event {e:?}");
            }
        }
        assert!(recv_header);
    }

    fn exchange_token(client: &mut Http3Client, server: &mut Connection) -> ResumptionToken {
        server.send_ticket(now(), &[]).expect("can send ticket");
        let out = server.process_output(now());
        assert!(out.as_dgram_ref().is_some());
        client.process_input(out.dgram().unwrap(), now());
        // We do not have a token so we need to wait for a resumption token timer to trigger.
        client.process_output(now() + Duration::from_millis(250));
        assert_eq!(client.state(), Http3State::Connected);
        client
            .events()
            .find_map(|e| {
                if let Http3ClientEvent::ResumptionToken(token) = e {
                    Some(token)
                } else {
                    None
                }
            })
            .unwrap()
    }

    fn start_with_0rtt() -> (Http3Client, TestServer) {
        let (mut client, mut server) = connect();
        let token = exchange_token(&mut client, &mut server.conn);

        let mut client = default_http3_client();

        let server = TestServer::new();

        assert_eq!(client.state(), Http3State::Initializing);
        client
            .enable_resumption(now(), &token)
            .expect("Set resumption token");

        assert_eq!(client.state(), Http3State::ZeroRtt);
        let zerortt_event = |e| matches!(e, Http3ClientEvent::StateChange(Http3State::ZeroRtt));
        assert!(client.events().any(zerortt_event));

        (client, server)
    }

    #[test]
    fn zero_rtt_negotiated() {
        let (mut client, mut server) = start_with_0rtt();

        let out = client.process_output(now());
        let out2 = client.process_output(now());

        assert_eq!(client.state(), Http3State::ZeroRtt);
        assert_eq!(*server.conn.state(), State::Init);
        server.conn.process_input(out.dgram().unwrap(), now());
        let out = server.conn.process(out2.dgram(), now());

        // Check that control and qpack streams are received and a
        // SETTINGS frame has been received.
        // Also qpack encoder stream will send "change capacity" instruction because it has
        // the peer settings already.
        server.check_control_qpack_request_streams_resumption(
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
            EXPECTED_REQUEST_HEADER_FRAME,
            false,
        );

        assert_eq!(*server.conn.state(), State::Handshaking);
        let out = client.process(out.dgram(), now());
        let out = server.conn.process(out.dgram(), now());
        let out = client.process(out.dgram(), now());
        assert_eq!(client.state(), Http3State::Connected);

        drop(server.conn.process(out.dgram(), now()));
        assert!(server.conn.state().connected());

        assert!(client.tls_info().unwrap().resumed());
        assert!(server.conn.tls_info().unwrap().resumed());
    }

    #[test]
    fn zero_rtt_send_request() {
        let (mut client, mut server) = start_with_0rtt();

        let request_stream_id =
            make_request(&mut client, true, &[Header::new("myheaders", "myvalue")]);
        assert_eq!(request_stream_id, 0);

        let out = client.process_output(now());
        let out2 = client.process_output(now());

        assert_eq!(client.state(), Http3State::ZeroRtt);
        assert_eq!(*server.conn.state(), State::Init);
        server.conn.process_input(out.dgram().unwrap(), now());
        let out = server.conn.process(out2.dgram(), now());

        // Check that control and qpack streams are received and a
        // SETTINGS frame has been received.
        // Also qpack encoder stream will send "change capacity" instruction because it has
        // the peer settings already.
        server.check_control_qpack_request_streams_resumption(
            ENCODER_STREAM_DATA_WITH_CAP_INST_AND_ENCODING_INST,
            EXPECTED_REQUEST_HEADER_FRAME_VERSION2,
            true,
        );

        assert_eq!(*server.conn.state(), State::Handshaking);
        let out = client.process(out.dgram(), now());
        let out = server.conn.process(out.dgram(), now());
        let out = client.process(out.dgram(), now());
        assert_eq!(client.state(), Http3State::Connected);
        let out = server.conn.process(out.dgram(), now());
        assert!(server.conn.state().connected());
        let out = client.process(out.dgram(), now());
        assert!(out.dgram().is_none());

        // After the server has been connected, send a response.
        let res = server.conn.stream_send(request_stream_id, HTTP_RESPONSE_2);
        assert_eq!(res, Ok(HTTP_RESPONSE_2.len()));
        server.conn.stream_close_send(request_stream_id).unwrap();

        read_response(&mut client, &mut server.conn, request_stream_id);

        assert!(client.tls_info().unwrap().resumed());
        assert!(server.conn.tls_info().unwrap().resumed());
    }

    #[test]
    fn zero_rtt_before_resumption_token() {
        let mut client = default_http3_client();
        assert!(
            client
                .fetch(
                    now(),
                    "GET",
                    &("https", "something.com", "/"),
                    &[],
                    Priority::default()
                )
                .is_err()
        );
    }

    #[test]
    fn zero_rtt_send_reject() {
        let (mut client, mut server) = connect();
        let token = exchange_token(&mut client, &mut server.conn);

        let mut client = default_http3_client();
        let mut server = Connection::new_server(
            DEFAULT_KEYS,
            DEFAULT_ALPN_H3,
            Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
            ConnectionParameters::default(),
        )
        .unwrap();
        // Using a freshly initialized anti-replay context
        // should result in the server rejecting 0-RTT.
        let ar = AntiReplay::new(now(), test_fixture::ANTI_REPLAY_WINDOW, 1, 3)
            .expect("setup anti-replay");
        server
            .server_enable_0rtt(&ar, AllowZeroRtt {})
            .expect("enable 0-RTT");

        assert_eq!(client.state(), Http3State::Initializing);
        client
            .enable_resumption(now(), &token)
            .expect("Set resumption token");
        let zerortt_event = |e| matches!(e, Http3ClientEvent::StateChange(Http3State::ZeroRtt));
        assert!(client.events().any(zerortt_event));

        // Send ClientHello.
        let client_hs = client.process_output(now());
        assert!(client_hs.as_dgram_ref().is_some());

        // Create a request
        let request_stream_id = make_request(&mut client, false, &[]);
        assert_eq!(request_stream_id, 0);

        let client_0rtt = client.process_output(now());
        assert!(client_0rtt.as_dgram_ref().is_some());

        server.process_input(client_hs.dgram().unwrap(), now());
        let server_hs = server.process(client_0rtt.dgram(), now());
        assert!(server_hs.as_dgram_ref().is_some()); // Should produce ServerHello etc...

        let dgram = client.process(server_hs.dgram(), now()).dgram();
        let dgram = server.process(dgram, now());

        // The server shouldn't receive that 0-RTT data.
        let recvd_stream_evt = |e| matches!(e, ConnectionEvent::NewStream { .. });
        assert!(!server.events().any(recvd_stream_evt));

        // Client should get a rejection.
        let client_out = client.process(dgram.dgram(), now());
        assert!(client_out.as_dgram_ref().is_some());
        let recvd_0rtt_reject = |e| e == Http3ClientEvent::ZeroRttRejected;
        assert!(client.events().any(recvd_0rtt_reject));

        // ...and the client stream should be gone.
        let res = client.stream_close_send(request_stream_id, now());
        assert!(res.is_err());
        assert_eq!(res.unwrap_err(), Error::InvalidStreamId);

        // Client will send Setting frame and open new qpack streams.
        drop(server.process(client_out.dgram(), now()));
        TestServer::new_with_conn(server).check_client_control_qpack_streams_no_resumption();

        // Check that we can send a request and that the stream_id starts again from 0.
        assert_eq!(make_request(&mut client, false, &[]), 0);
    }

    // Connect to a server, get token and reconnect using 0-rtt. Server sends new Settings.
    fn zero_rtt_change_settings(
        original_settings: &[HSetting],
        resumption_settings: &[HSetting],
        expected_client_state: &Http3State,
        expected_encoder_stream_data: &[u8],
    ) {
        let mut client = default_http3_client();
        let mut server = TestServer::new_with_settings(original_settings);
        // Connect and get a token
        connect_with(&mut client, &mut server);
        let token = exchange_token(&mut client, &mut server.conn);

        let mut client = default_http3_client();
        let mut server = TestServer::new_with_settings(resumption_settings);
        assert_eq!(client.state(), Http3State::Initializing);
        client
            .enable_resumption(now(), &token)
            .expect("Set resumption token");
        assert_eq!(client.state(), Http3State::ZeroRtt);
        let out = client.process_output(now());
        let out2 = client.process_output(now());

        assert_eq!(client.state(), Http3State::ZeroRtt);
        assert_eq!(*server.conn.state(), State::Init);
        server.conn.process_input(out.dgram().unwrap(), now());
        let out = server.conn.process(out2.dgram(), now());

        // Check that control and qpack streams and a SETTINGS frame are received.
        // Also qpack encoder stream will send "change capacity" instruction because it has
        // the peer settings already.
        server.check_control_qpack_request_streams_resumption(
            expected_encoder_stream_data,
            EXPECTED_REQUEST_HEADER_FRAME,
            false,
        );

        assert_eq!(*server.conn.state(), State::Handshaking);
        let out = client.process(out.dgram(), now());
        let out = server.conn.process(out.dgram(), now());
        let out = client.process(out.dgram(), now());
        assert_eq!(client.state(), Http3State::Connected);

        drop(server.conn.process(out.dgram(), now()));
        assert!(server.conn.state().connected());

        assert!(client.tls_info().unwrap().resumed());
        assert!(server.conn.tls_info().unwrap().resumed());

        // Send new settings.
        let control_stream = server.conn.stream_create(StreamType::UniDi).unwrap();
        let mut enc = Encoder::default();
        server.settings.encode(&mut enc);
        let mut sent = server.conn.stream_send(control_stream, CONTROL_STREAM_TYPE);
        assert_eq!(sent.unwrap(), CONTROL_STREAM_TYPE.len());
        sent = server.conn.stream_send(control_stream, enc.as_ref());
        assert_eq!(sent.unwrap(), enc.len());

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        assert_eq!(&client.state(), expected_client_state);
        assert!(server.conn.state().connected());
    }

    #[test]
    fn zero_rtt_new_server_setting_are_the_same() {
        // Send a new server settings that are the same as the old one.
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &Http3State::Connected,
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_new_server_setting_omit_max_table() {
        // Send a new server settings without MaxTableCapacity
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &[
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &Http3State::Closing(CloseReason::Application(265)),
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_new_server_setting_omit_blocked_streams() {
        // Send a new server settings without BlockedStreams
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &Http3State::Closing(CloseReason::Application(265)),
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_new_server_setting_omit_header_list_size() {
        // Send a new server settings without MaxHeaderListSize
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
            ],
            &Http3State::Connected,
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_new_server_setting_max_table_size_bigger() {
        // Send a new server settings MaxTableCapacity=200
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 200),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &Http3State::Closing(CloseReason::Application(514)),
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_new_server_setting_max_table_size_smaller() {
        // Send a new server settings MaxTableCapacity=50
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 50),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &Http3State::Closing(CloseReason::Application(265)),
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_new_server_setting_blocked_streams_bigger() {
        // Send a new server settings withBlockedStreams=200
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 200),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &Http3State::Connected,
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_new_server_setting_blocked_streams_smaller() {
        // Send a new server settings withBlockedStreams=50
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 50),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &Http3State::Closing(CloseReason::Application(265)),
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_new_server_setting_max_header_size_bigger() {
        // Send a new server settings with MaxHeaderListSize=20000
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 20000),
            ],
            &Http3State::Connected,
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_new_server_setting_max_headers_size_smaller() {
        // Send the new server settings with MaxHeaderListSize=5000
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 5000),
            ],
            &Http3State::Closing(CloseReason::Application(265)),
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_new_server_setting_h3_datagram_smaller() {
        // The server advertised SETTINGS_H3_DATAGRAM=1 before, and now withholds it.
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
                HSetting::new(HSettingType::EnableH3Datagram, 1),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &Http3State::Closing(CloseReason::Application(265)),
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_new_server_setting_h3_datagram_bigger() {
        // Turning SETTINGS_H3_DATAGRAM on for the resumed connection is allowed.
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
                HSetting::new(HSettingType::EnableH3Datagram, 1),
            ],
            &Http3State::Connected,
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_new_server_setting_webtransport_smaller() {
        // The server advertised SETTINGS_ENABLE_WEBTRANSPORT=1 before, and now sends 0.
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
                HSetting::new(HSettingType::EnableWebTransport, 1),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
                HSetting::new(HSettingType::EnableWebTransport, 0),
            ],
            &Http3State::Closing(CloseReason::Application(265)),
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_extended_connect_disabled() {
        // The server advertised SETTINGS_ENABLE_CONNECT_PROTOCOL=1 before, and now withholds it.
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
                HSetting::new(HSettingType::EnableConnect, 1),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &Http3State::Closing(CloseReason::Application(265)),
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_max_table_size_first_omitted() {
        // send server original settings without MaxTableCapacity
        // send new server setting with MaxTableCapacity
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &Http3State::Connected,
            ENCODER_STREAM_DATA,
        );
    }

    #[test]
    fn zero_rtt_blocked_streams_first_omitted() {
        // Send server original settings without BlockedStreams
        // Send the new server settings with BlockedStreams
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &Http3State::Connected,
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn zero_rtt_max_header_size_first_omitted() {
        // Send server settings without MaxHeaderListSize
        // Send new settings with MaxHeaderListSize.
        zero_rtt_change_settings(
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 10000),
            ],
            &[
                HSetting::new(HSettingType::MaxTableCapacity, 100),
                HSetting::new(HSettingType::BlockedStreams, 100),
                HSetting::new(HSettingType::MaxHeaderListSize, 10000),
            ],
            &Http3State::Closing(CloseReason::Application(265)),
            ENCODER_STREAM_DATA_WITH_CAP_INSTRUCTION,
        );
    }

    #[test]
    fn trailers_with_fin_after_headers() {
        // Make a new connection.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        // Send HEADER frame.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_HEADER_FRAME_0,
            false,
        );

        // Check response headers.
        let mut response_headers = false;
        while let Some(e) = client.next_event() {
            if let Http3ClientEvent::HeaderReady {
                stream_id,
                headers,
                interim,
                fin,
            } = e
            {
                assert_eq!(stream_id, request_stream_id);
                check_response_header_0(&headers);
                assert!(!fin);
                assert!(!interim);
                response_headers = true;
            }
        }
        assert!(response_headers);

        // Send trailers
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_HEADER_FRAME_0,
            true,
        );

        let events: Vec<Http3ClientEvent> = client.events().collect();

        // We already had HeaderReady
        let header_ready: fn(&Http3ClientEvent) -> _ =
            |e| matches!(*e, Http3ClientEvent::HeaderReady { .. });
        assert!(!events.iter().any(header_ready));

        // Check that we have a DataReady event. Reading from the stream will return fin=true.
        let data_readable: fn(&Http3ClientEvent) -> _ =
            |e| matches!(*e, Http3ClientEvent::DataReadable { .. });
        assert!(events.iter().any(data_readable));
        let mut buf = [0_u8; 100];
        let (len, fin) = client
            .read_data(now(), request_stream_id, &mut buf)
            .unwrap();
        assert_eq!(0, len);
        assert!(fin);
    }

    #[test]
    fn trailers_with_later_fin_after_headers() {
        // Make a new connection.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        // Send HEADER frame.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_HEADER_FRAME_0,
            false,
        );

        // Check response headers.
        let mut response_headers = false;
        while let Some(e) = client.next_event() {
            if let Http3ClientEvent::HeaderReady {
                stream_id,
                headers,
                interim,
                fin,
            } = e
            {
                assert_eq!(stream_id, request_stream_id);
                check_response_header_0(&headers);
                assert!(!fin);
                assert!(!interim);
                response_headers = true;
            }
        }
        assert!(response_headers);

        // Send trailers
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_HEADER_FRAME_0,
            false,
        );

        // Check that we do not have a DataReady event.
        let data_readable = |e| matches!(e, Http3ClientEvent::DataReadable { .. });
        assert!(!client.events().any(data_readable));

        server.conn.stream_close_send(request_stream_id).unwrap();

        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        let events: Vec<Http3ClientEvent> = client.events().collect();

        // We already had HeaderReady
        let header_ready: fn(&Http3ClientEvent) -> _ =
            |e| matches!(*e, Http3ClientEvent::HeaderReady { .. });
        assert!(!events.iter().any(header_ready));

        // Check that we have a DataReady event. Reading from the stream will return fin=true.
        let data_readable_fn: fn(&Http3ClientEvent) -> _ =
            |e| matches!(*e, Http3ClientEvent::DataReadable { .. });
        assert!(events.iter().any(data_readable_fn));
        let mut buf = [0_u8; 100];
        let (len, fin) = client
            .read_data(now(), request_stream_id, &mut buf)
            .unwrap();
        assert_eq!(0, len);
        assert!(fin);
    }

    #[test]
    fn data_after_trailers_after_headers() {
        // Make a new connection.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        // Send HEADER frame.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_HEADER_FRAME_0,
            false,
        );

        // Check response headers.
        let mut response_headers = false;
        while let Some(e) = client.next_event() {
            if let Http3ClientEvent::HeaderReady {
                stream_id,
                headers,
                interim,
                fin,
            } = e
            {
                assert_eq!(stream_id, request_stream_id);
                check_response_header_0(&headers);
                assert!(!fin);
                assert!(!interim);
                response_headers = true;
            }
        }
        assert!(response_headers);

        // Send trailers
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_HEADER_FRAME_0,
            false,
        );

        // Check that we do not have a DataReady event.
        let data_readable = |e| matches!(e, Http3ClientEvent::DataReadable { .. });
        assert!(!client.events().any(data_readable));

        // Send Data frame.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            [0x0, 0x3, 0x61, 0x62, 0x63], // a data frame
            false,
        );

        assert_closed(&client, &Error::HttpFrameUnexpected);
    }

    #[test]
    fn transport_stream_readable_event_after_all_data() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(false);

        // Send headers.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_2,
            false,
        );

        // Send an empty data frame and a fin
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            [0x0, 0x0],
            true,
        );

        let mut buf = [0_u8; 100];
        assert_eq!(
            client.read_data(now(), StreamId::new(0), &mut buf),
            Ok((3, true))
        );

        client.process_output(now());
    }

    #[test]
    fn no_data_ready_events_after_fin() {
        // Connect exchange headers and send a request. Also check if the correct header frame has
        // been sent.
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        // send response - 200  Content-Length: 7
        // with content: 'abcdefg'.
        // The content will be send in 2 DATA frames.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_1,
            true,
        );

        let data_readable_event = |e| matches!(e, Http3ClientEvent::DataReadable { stream_id } if stream_id == request_stream_id);
        assert!(client.events().any(data_readable_event));

        let mut buf = [0_u8; 100];
        assert_eq!(
            (EXPECTED_RESPONSE_DATA_1.len(), true),
            client
                .read_data(now(), request_stream_id, &mut buf)
                .unwrap()
        );

        assert!(!client.events().any(data_readable_event));
    }

    #[test]
    fn reading_small_chunks_of_data() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        // send response - 200  Content-Length: 7
        // with content: 'abcdefg'.
        // The content will be send in 2 DATA frames.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_1,
            true,
        );

        let data_readable_event = |e| matches!(e, Http3ClientEvent::DataReadable { stream_id } if stream_id == request_stream_id);
        assert!(client.events().any(data_readable_event));

        let mut buf1 = [0_u8; 1];
        assert_eq!(
            (1, false),
            client
                .read_data(now(), request_stream_id, &mut buf1)
                .unwrap()
        );
        assert!(!client.events().any(data_readable_event));

        // Now read only until the end of the first frame. The firs frame has 3 bytes.
        let mut buf2 = [0_u8; 2];
        assert_eq!(
            (2, false),
            client
                .read_data(now(), request_stream_id, &mut buf2)
                .unwrap()
        );
        assert!(!client.events().any(data_readable_event));

        // Read a half of the second frame.
        assert_eq!(
            (2, false),
            client
                .read_data(now(), request_stream_id, &mut buf2)
                .unwrap()
        );
        assert!(!client.events().any(data_readable_event));

        // Read the rest.
        // Read a half of the second frame.
        assert_eq!(
            (2, true),
            client
                .read_data(now(), request_stream_id, &mut buf2)
                .unwrap()
        );
        assert!(!client.events().any(data_readable_event));
    }

    #[test]
    fn zero_length_data_at_end() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        // send response - 200  Content-Length: 7
        // with content: 'abcdefg'.
        // The content will be send in 2 DATA frames.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_1,
            false,
        );
        // Send a zero-length frame at the end of the stream.
        _ = server.conn.stream_send(request_stream_id, &[0, 0]).unwrap();
        server.conn.stream_close_send(request_stream_id).unwrap();
        let dgram = server.conn.process_output(now()).dgram();
        client.process_input(dgram.unwrap(), now());

        let data_readable_event = |e: &_| matches!(e, Http3ClientEvent::DataReadable { stream_id } if *stream_id == request_stream_id);
        assert_eq!(client.events().filter(data_readable_event).count(), 1);

        let mut buf = [0_u8; 10];
        assert_eq!(
            (7, true),
            client
                .read_data(now(), request_stream_id, &mut buf)
                .unwrap()
        );
        assert!(!client.events().any(|e| data_readable_event(&e)));
    }

    #[test]
    fn stream_blocked_no_remote_encoder_stream() {
        let (mut client, mut server) = connect_only_transport();

        send_and_receive_client_settings(&mut client, &mut server);

        server.create_control_stream();
        // Send the server's control stream data.
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        server.create_qpack_streams();
        let qpack_pkt1 = server.conn.process_output(now());
        // delay delivery of this packet.

        let request_stream_id = make_request(&mut client, true, &[]);
        let out = client.process_output(now());
        drop(server.conn.process(out.dgram(), now()));

        setup_server_side_encoder(&mut client, &mut server);

        let headers = vec![
            Header::new(":status", "200"),
            Header::new("my-header", "my-header"),
            Header::new("content-length", "3"),
        ];
        let encoded_headers = server.encoder.borrow_mut().encode_header_block(
            &mut server.conn,
            &headers,
            request_stream_id,
        );
        let hframe = HFrame::Headers {
            header_block: encoded_headers.to_vec(),
        };

        // Send the encoder instructions,
        let out = server.conn.process_output(now());
        client.process(out.dgram(), now());

        // Send response
        let mut d = Encoder::default();
        hframe.encode(&mut d);
        let d_frame = HFrame::Data { len: 3 };
        d_frame.encode(&mut d);
        d.encode([0x61, 0x62, 0x63]);
        _ = server
            .conn
            .stream_send(request_stream_id, d.as_ref())
            .unwrap();
        server.conn.stream_close_send(request_stream_id).unwrap();

        let out = server.conn.process_output(now());
        drop(client.process(out.dgram(), now()));

        let header_ready_event = |e| matches!(e, Http3ClientEvent::HeaderReady { .. });
        assert!(!client.events().any(header_ready_event));

        // Let client receive the encoder instructions.
        drop(client.process(qpack_pkt1.dgram(), now()));

        assert!(client.events().any(header_ready_event));
    }

    fn setup_server_side_encoder_param(
        client: &mut Http3Client,
        server: &mut TestServer,
        max_blocked_streams: u64,
    ) {
        server
            .encoder
            .borrow_mut()
            .set_max_capacity(max_blocked_streams)
            .unwrap();
        server
            .encoder
            .borrow_mut()
            .set_max_blocked_streams(100)
            .unwrap();
        server
            .encoder
            .borrow_mut()
            .send_encoder_updates(&mut server.conn)
            .unwrap();
        let out = server.conn.process_output(now());
        drop(client.process(out.dgram(), now()));
    }

    fn setup_server_side_encoder(client: &mut Http3Client, server: &mut TestServer) {
        setup_server_side_encoder_param(client, server, 100);
    }

    #[test]
    fn data_readable_in_decoder_blocked_state() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        setup_server_side_encoder(&mut client, &mut server);

        let headers = vec![
            Header::new(":status", "200"),
            Header::new("my-header", "my-header"),
            Header::new("content-length", "0"),
        ];
        let encoded_headers = server.encoder.borrow_mut().encode_header_block(
            &mut server.conn,
            &headers,
            request_stream_id,
        );
        let hframe = HFrame::Headers {
            header_block: encoded_headers.to_vec(),
        };

        // Delay encoder instruction so that the stream will be blocked.
        let encoder_insts = server.conn.process_output(now());

        // Send response headers.
        let mut d = Encoder::default();
        hframe.encode(&mut d);
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            &d,
            false,
        );

        // Headers are blocked waiting for the encoder instructions.
        let header_ready_event = |e| matches!(e, Http3ClientEvent::HeaderReady { .. });
        assert!(!client.events().any(header_ready_event));

        // Now send data frame. This will trigger DataRead event.
        let mut d = Encoder::default();
        hframe.encode(&mut d);
        let d_frame = HFrame::Data { len: 0 };
        d_frame.encode(&mut d);
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            &d,
            true,
        );

        // Now read headers.
        drop(client.process(encoder_insts.dgram(), now()));
    }

    #[test]
    fn qpack_stream_reset() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);
        setup_server_side_encoder(&mut client, &mut server);
        // Cancel request.
        drop(client.cancel_fetch(request_stream_id, Error::HttpRequestCancelled.code()));
        assert_eq!(server.encoder.borrow_mut().stats().stream_cancelled_recv, 0);
        let out = client.process_output(now());
        drop(server.conn.process(out.dgram(), now()));
        drop(server.encoder_receiver.receive(&mut server.conn, now()));
        assert_eq!(server.encoder.borrow_mut().stats().stream_cancelled_recv, 1);
    }

    fn send_headers_using_encoder(
        client: &mut Http3Client,
        server: &mut TestServer,
        request_stream_id: StreamId,
        headers: &[Header],
        data: &[u8],
    ) -> Option<Datagram> {
        let encoded_headers = server.encoder.borrow_mut().encode_header_block(
            &mut server.conn,
            headers,
            request_stream_id,
        );
        let hframe = HFrame::Headers {
            header_block: encoded_headers.to_vec(),
        };

        let out = server.conn.process_output(now());

        // Send response
        let mut d = Encoder::default();
        hframe.encode(&mut d);
        let d_frame = HFrame::Data {
            len: to_u64(data.len()),
        };
        d_frame.encode(&mut d);
        d.encode(data);
        server_send_response_and_exchange_packet(client, server, request_stream_id, &d, true);

        out.dgram()
    }

    #[test]
    fn qpack_stream_reset_recv() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);
        setup_server_side_encoder(&mut client, &mut server);

        // Cancel request.
        server
            .conn
            .stream_reset_send(request_stream_id, Error::HttpRequestCancelled.code())
            .unwrap();
        assert_eq!(server.encoder.borrow_mut().stats().stream_cancelled_recv, 0);
        let out = server.conn.process_output(now());
        let out = client.process(out.dgram(), now());
        drop(server.conn.process(out.dgram(), now()));
        drop(server.encoder_receiver.receive(&mut server.conn, now()));
        assert_eq!(server.encoder.borrow_mut().stats().stream_cancelled_recv, 1);
    }

    #[test]
    fn qpack_stream_reset_during_header_qpack_blocked() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        setup_server_side_encoder(&mut client, &mut server);

        drop(
            send_headers_using_encoder(
                &mut client,
                &mut server,
                request_stream_id,
                &[
                    Header::new(":status", "200"),
                    Header::new("my-header", "my-header"),
                    Header::new("content-length", "3"),
                ],
                &[0x61, 0x62, 0x63],
            )
            .unwrap(),
        );

        let header_ready_event = |e| matches!(e, Http3ClientEvent::HeaderReady { .. });
        assert!(!client.events().any(header_ready_event));

        // Cancel request.
        client
            .cancel_fetch(request_stream_id, Error::HttpRequestCancelled.code())
            .unwrap();

        assert_eq!(server.encoder.borrow_mut().stats().stream_cancelled_recv, 0);
        let out = client.process_output(now());
        drop(server.conn.process(out.dgram(), now()));
        drop(
            server
                .encoder_receiver
                .receive(&mut server.conn, now())
                .unwrap(),
        );
        assert_eq!(server.encoder.borrow_mut().stats().stream_cancelled_recv, 1);
    }

    /// Park a request stream in `DecodingHeaders`, blocked on QPACK encoder instructions, then
    /// reset it.
    ///
    /// Returns two separate datagrams.  The first contains the encoder instructions.
    /// The second contains the header block, the data, and a `RESET_STREAM`.
    fn setup_for_recv_after_reset(
        client: &mut Http3Client,
        server: &mut TestServer,
        request_stream_id: StreamId,
    ) -> (Datagram, Datagram) {
        setup_server_side_encoder(client, server);

        // Flush the request's FIN acknowledgement so that the client's sending side is retired.
        // Otherwise `remove_ended` keeps the receiving side alive for its send counterpart and the
        // transport never forgets the stream.
        if let Some(d) = server.conn.process_output(now()).dgram() {
            client.process_input(d, now());
        }

        // These response headers refer to the dynamic table, so the client blocks on decoding them
        // while the encoder instructions are withheld. A DATA frame follows the headers so that the
        // frame reader has something left to read once decoding is unblocked. The sending side is
        // left open so that it can be reset below.
        let encoded_headers = server.encoder.borrow_mut().encode_header_block(
            &mut server.conn,
            &[
                Header::new(":status", "200"),
                Header::new("my-header", "my-header"),
                Header::new("content-length", "3"),
            ],
            request_stream_id,
        );
        let encoder_instructions = server.conn.process_output(now()).dgram().unwrap();

        let mut d = Encoder::default();
        let headers = HFrame::Headers {
            header_block: encoded_headers.to_vec(),
        };
        headers.encode(&mut d);
        HFrame::Data { len: 3 }.encode(&mut d);
        d.encode(b"abc");
        server_send_response_and_exchange_packet(client, server, request_stream_id, &d, false);

        assert!(
            !client
                .events()
                .any(|e| matches!(e, Http3ClientEvent::HeaderReady { .. })),
            "headers must still be blocked on QPACK"
        );

        server
            .conn
            .stream_reset_send(request_stream_id, Error::HttpRequestCancelled.code())
            .unwrap();
        let reset = server.conn.process_output(now()).dgram().unwrap();

        (encoder_instructions, reset)
    }

    /// The HTTP/3 layer can be asked to read from a stream that the transport has already
    /// forgotten.  Test that this is handled correctly.
    ///
    /// The encoder instructions that unblock header decoding and a `RESET_STREAM` for the blocked
    /// stream arrive in a batch. The transport generates events for available data, then the reset.
    /// As a result, the HTTP/3 layer will attempt to read on a stream that has been removed.
    /// This tests that the handling of that situation is proper.
    ///
    /// This depends on `process_multiple_input`, which defers handling of transport events
    /// at the HTTP/3 layer.  That ensures that the events all arrive in order.
    #[test]
    #[expect(
        clippy::tuple_array_conversions,
        reason = "best to be explicit in this specific case"
    )]
    fn recv_after_stream_removed() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);
        let (encoder_instructions, reset) =
            setup_for_recv_after_reset(&mut client, &mut server, request_stream_id);

        // Encoder instructions ahead of the reset, in a single batch.
        client.process_multiple_input([encoder_instructions, reset], now());

        assert_eq!(client.state(), Http3State::Connected);
        assert!(client.events().any(|e| matches!(
            e,
            Http3ClientEvent::Reset { stream_id, .. } if stream_id == request_stream_id
        )));
    }

    /// The same as [`recv_after_stream_removed`], but with reordered packets.
    /// The encoder instructions arriving after the reset means that the HTTP/3 code does not
    /// try to read, so this needs no special handling.
    #[test]
    #[allow(
        clippy::allow_attributes,
        clippy::tuple_array_conversions,
        reason = "this lint has inconsistent validation, so expect doesn't work"
    )]
    fn recv_after_stream_removed_reordered() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);
        let (encoder_instructions, reset) =
            setup_for_recv_after_reset(&mut client, &mut server, request_stream_id);

        // Reset ahead of the encoder instructions, in a single batch.
        client.process_multiple_input([reset, encoder_instructions], now());

        assert_eq!(client.state(), Http3State::Connected);
        assert!(client.events().any(|e| matches!(
            e,
            Http3ClientEvent::Reset { stream_id, .. } if stream_id == request_stream_id
        )));
    }

    /// A version of [`recv_after_stream_removed`] with event processing interspersed.
    /// This did not produce errors.
    #[test]
    fn recv_after_stream_removed_events_handled() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);
        let (encoder_instructions, reset) =
            setup_for_recv_after_reset(&mut client, &mut server, request_stream_id);

        client.process_input(encoder_instructions, now());
        client.process_input(reset, now());

        assert_eq!(client.state(), Http3State::Connected);
        assert!(client.events().any(|e| matches!(
            e,
            Http3ClientEvent::Reset { stream_id, .. } if stream_id == request_stream_id
        )));
    }

    /// [`recv_after_stream_removed`] with event processing and reordering.
    #[test]
    fn recv_after_stream_removed_events_handled_reordered() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);
        let (encoder_instructions, reset) =
            setup_for_recv_after_reset(&mut client, &mut server, request_stream_id);

        client.process_input(reset, now());
        client.process_input(encoder_instructions, now());

        assert_eq!(client.state(), Http3State::Connected);
        assert!(client.events().any(|e| matches!(
            e,
            Http3ClientEvent::Reset { stream_id, .. } if stream_id == request_stream_id
        )));
    }

    #[test]
    fn qpack_no_stream_cancelled_after_fin() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        setup_server_side_encoder(&mut client, &mut server);

        let encoder_instruct = send_headers_using_encoder(
            &mut client,
            &mut server,
            request_stream_id,
            &[
                Header::new(":status", "200"),
                Header::new("my-header", "my-header"),
                Header::new("content-length", "3"),
            ],
            &[],
        );

        // Exchange encoder instructions
        drop(client.process(encoder_instruct, now()));

        let header_ready_event = |e| matches!(e, Http3ClientEvent::HeaderReady { .. });
        assert!(client.events().any(header_ready_event));
        // After this the recv_stream is in ClosePending state

        // Cancel request.
        client
            .cancel_fetch(request_stream_id, Error::HttpRequestCancelled.code())
            .unwrap();

        assert_eq!(server.encoder.borrow_mut().stats().stream_cancelled_recv, 0);
        let out = client.process_output(now());
        drop(server.conn.process(out.dgram(), now()));
        drop(
            server
                .encoder_receiver
                .receive(&mut server.conn, now())
                .unwrap(),
        );
        assert_eq!(server.encoder.borrow_mut().stats().stream_cancelled_recv, 0);
    }

    #[test]
    fn qpack_stream_reset_before_dynamic_table_capacity() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);
        // Cancel request.
        client
            .cancel_fetch(request_stream_id, Error::HttpRequestCancelled.code())
            .unwrap();
        assert_eq!(server.encoder.borrow_mut().stats().stream_cancelled_recv, 0);
        let out = client.process_output(now());
        drop(server.conn.process(out.dgram(), now()));
        drop(
            server
                .encoder_receiver
                .receive(&mut server.conn, now())
                .unwrap(),
        );
        assert_eq!(server.encoder.borrow_mut().stats().stream_cancelled_recv, 1);
    }

    #[test]
    fn multiple_streams_in_decoder_blocked_state() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        setup_server_side_encoder(&mut client, &mut server);

        let headers = vec![
            Header::new(":status", "200"),
            Header::new("my-header", "my-header"),
            Header::new("content-length", "0"),
        ];
        let encoded_headers = server.encoder.borrow_mut().encode_header_block(
            &mut server.conn,
            &headers,
            request_stream_id,
        );
        let hframe = HFrame::Headers {
            header_block: encoded_headers.to_vec(),
        };

        // Delay encoder instruction so that the stream will be blocked.
        let encoder_insts = server.conn.process_output(now());

        // Send response headers.
        let mut d = Encoder::default();
        hframe.encode(&mut d);
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            &d,
            true,
        );

        // Headers are blocked waiting for the encoder instructions.
        let header_ready_event = |e| matches!(e, Http3ClientEvent::HeaderReady { .. });
        assert!(!client.events().any(header_ready_event));

        // Make another request.
        let request2 = make_request_and_exchange_pkts(&mut client, &mut server, true);
        // Send response headers.
        server_send_response_and_exchange_packet(&mut client, &mut server, request2, &d, true);

        // Headers on the second request are blocked as well are blocked
        // waiting for the encoder instructions.
        assert!(!client.events().any(header_ready_event));

        // Now make the encoder instructions available.
        drop(client.process(encoder_insts.dgram(), now()));

        // Header blocks for both streams should be ready.
        let mut count_responses = 0;
        while let Some(e) = client.next_event() {
            if let Http3ClientEvent::HeaderReady { stream_id, .. } = e {
                assert!((stream_id == request_stream_id) || (stream_id == request2));
                count_responses += 1;
            }
        }
        assert_eq!(count_responses, 2);
    }

    #[test]
    fn reserved_frames() {
        for f in HFrameType::RESERVED {
            let mut enc = Encoder::default();
            enc.encode_varint(*f);
            test_wrong_frame_on_control_stream(enc.as_ref());
            test_wrong_frame_on_request_stream(enc.as_ref());
        }
    }

    #[test]
    fn send_reserved_settings() {
        for s in H3_RESERVED_SETTINGS {
            let (mut client, mut server) = connect_only_transport();
            let control_stream = server.conn.stream_create(StreamType::UniDi).unwrap();
            // Send the control stream type(0x0).
            _ = server
                .conn
                .stream_send(control_stream, CONTROL_STREAM_TYPE)
                .unwrap();
            // Create a settings frame of length 2.
            let mut enc = Encoder::default();
            enc.encode_varint(HFrameType::SETTINGS);
            enc.encode_varint(2_u64);
            // The settings frame contains a reserved settings type and some value (0x1).
            enc.encode_varint(*s);
            enc.encode_varint(1_u64);
            let sent = server.conn.stream_send(control_stream, enc.as_ref());
            assert_eq!(sent, Ok(4));
            let out = server.conn.process_output(now());
            client.process(out.dgram(), now());
            assert_closed(&client, &Error::HttpSettings);
        }
    }

    #[test]
    fn response_w_1xx() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        setup_server_side_encoder(&mut client, &mut server);

        let mut d = Encoder::default();
        let headers1xx: &[Header] = &[Header::new(":status", "103")];
        server.encode_headers(request_stream_id, headers1xx, &mut d);

        let headers200: &[Header] = &[
            Header::new(":status", "200"),
            Header::new("my-header", "my-header"),
            Header::new("content-length", "3"),
        ];
        server.encode_headers(request_stream_id, headers200, &mut d);

        // Send 1xx and 200 headers response.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            &d,
            false,
        );

        // Sending response data.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            HTTP_RESPONSE_DATA_FRAME_ONLY_2,
            true,
        );

        let mut events = client.events().filter_map(|e| {
            if let Http3ClientEvent::HeaderReady {
                stream_id,
                interim,
                headers,
                ..
            } = e
            {
                Some((stream_id, interim, headers))
            } else {
                None
            }
        });
        let (stream_id_1xx_rec, interim1xx_rec, headers1xx_rec) = events.next().unwrap();
        assert_eq!(
            (stream_id_1xx_rec, interim1xx_rec, headers1xx_rec.as_ref()),
            (request_stream_id, true, headers1xx)
        );

        let (stream_id_200_rec, interim200_rec, headers200_rec) = events.next().unwrap();
        assert_eq!(
            (stream_id_200_rec, interim200_rec, headers200_rec.as_ref()),
            (request_stream_id, false, headers200)
        );
        assert!(events.next().is_none());
    }

    #[test]
    fn response_w_too_many_1xx() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        let status = Header::new(":status", "103");
        let max_1xx = MAX_HEADER_BYTES / status.field_size();
        let mut d = Encoder::default();
        server.encode_headers(request_stream_id, &[status], &mut d);
        let response = d.as_ref().repeat(max_1xx);
        send_response_in_packets(&mut client, &mut server, request_stream_id, &response);
        assert_eq!(interim_count(&mut client), max_1xx);
        assert_eq!(client.state(), Http3State::Connected);

        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            &d,
            false,
        );
        assert_closed(&client, &Error::HttpExcessiveLoad);
    }

    fn interim_count(client: &mut Http3Client) -> usize {
        client
            .events()
            .filter(|e| matches!(e, Http3ClientEvent::HeaderReady { interim: true, .. }))
            .count()
    }

    fn send_response_in_packets(
        client: &mut Http3Client,
        server: &mut TestServer,
        stream_id: StreamId,
        response: &[u8],
    ) {
        for chunk in response.chunks(1000) {
            server_send_response_and_exchange_packet(client, server, stream_id, chunk, false);
        }
    }

    fn send_max_size_1xx_from_dynamic_table(
        field: Header,
    ) -> (Http3Client, TestServer, StreamId, Encoder) {
        let status = Header::new(":status", "103");
        let refs = (MAX_HEADER_BYTES - status.field_size()) / field.field_size();
        let headers = [vec![status], vec![field; refs]].concat();
        assert_eq!(
            headers.iter().map(Header::field_size).sum::<usize>(),
            MAX_HEADER_BYTES
        );

        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);
        setup_server_side_encoder(&mut client, &mut server);

        let mut d = Encoder::default();
        server.encode_headers(request_stream_id, &headers, &mut d);
        assert_eq!(server.encoder.borrow().stats().dynamic_table_references, 1);
        let encoder_insts = server.conn.process_output(now());
        drop(client.process(encoder_insts.dgram(), now()));
        send_response_in_packets(&mut client, &mut server, request_stream_id, d.as_ref());
        assert_eq!(interim_count(&mut client), 1);
        assert_eq!(client.state(), Http3State::Connected);
        (client, server, request_stream_id, d)
    }

    #[test]
    fn response_w_1xx_from_dynamic_table() {
        let field = Header::new("x", "x".repeat(48)); // 4854 references fill MAX_HEADER_BYTES.
        let (mut client, mut server, request_stream_id, d) =
            send_max_size_1xx_from_dynamic_table(field);
        send_response_in_packets(&mut client, &mut server, request_stream_id, d.as_ref());
        assert_closed(&client, &Error::HttpExcessiveLoad);
    }

    #[test]
    fn response_w_1xx_stripped_fields_from_dynamic_table() {
        let field = Header::new("host", "x".repeat(45));
        let (mut client, mut server, request_stream_id, d) =
            send_max_size_1xx_from_dynamic_table(field);
        send_response_in_packets(&mut client, &mut server, request_stream_id, d.as_ref());
        assert_closed(&client, &Error::HttpExcessiveLoad);
    }

    #[test]
    fn response_after_max_size_1xx() {
        let field = Header::new("x", "x".repeat(48));
        let (mut client, mut server, request_stream_id, _) =
            send_max_size_1xx_from_dynamic_table(field);
        let mut d = Encoder::default();
        server.encode_headers(request_stream_id, &[Header::new(":status", "200")], &mut d);
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            &d,
            false,
        );
        let is_final = |e| matches!(e, Http3ClientEvent::HeaderReady { interim: false, .. });
        assert!(client.events().any(is_final));
        assert_eq!(client.state(), Http3State::Connected);
    }

    #[test]
    fn response_wo_status() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        setup_server_side_encoder(&mut client, &mut server);

        let mut d = Encoder::default();
        let headers = vec![
            Header::new("my-header", "my-header"),
            Header::new("content-length", "3"),
        ];
        server.encode_headers(request_stream_id, &headers, &mut d);

        // Send response
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            &d,
            false,
        );

        // Stream has been reset because of the malformed headers.
        let e = client.events().next().unwrap();
        assert_eq!(
            e,
            Http3ClientEvent::Reset {
                stream_id: request_stream_id,
                error: Error::HttpMessage.code(),
                local: true,
            }
        );

        let out = client.process_output(now());
        drop(server.conn.process(out.dgram(), now()));

        // Check that server has received a reset.
        let stop_sending_event = |e| {
            matches!(e, ConnectionEvent::SendStreamStopSending {
            stream_id,
            app_error
        } if stream_id == request_stream_id && app_error == Error::HttpMessage.code())
        };
        assert!(server.conn.events().any(stop_sending_event));

        // Stream should now be closed and gone
        let mut buf = [0_u8; 100];
        assert_eq!(
            client.read_data(now(), StreamId::new(0), &mut buf),
            Err(Error::InvalidStreamId)
        );
    }

    // Client: a malformed response also resets a request that flow control keeps unsent.
    #[test]
    fn malformed_response_resets_blocked_request() {
        let (mut client, mut server) = connect_with_connection_parameters(
            ConnectionParameters::default().max_stream_data(StreamDataLimit::BiDiRemote, 1),
        );
        let request_stream_id = make_request(&mut client, true, &[]);
        let has_send_stream =
            |c: &Http3Client| c.handler().send_streams().contains_key(&request_stream_id);
        let out = client.process_output(now());
        server.conn.process_input(out.dgram().unwrap(), now());
        assert!(has_send_stream(&client));

        let mut d = Encoder::default();
        server.encode_headers(request_stream_id, &[Header::new("a", "b")], &mut d);
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            &d,
            false,
        );

        let error = Error::HttpMessage.code();
        let reset = Http3ClientEvent::Reset {
            stream_id: request_stream_id,
            error,
            local: true,
        };
        assert_eq!(client.events().collect::<Vec<_>>(), [reset]);
        assert!(!has_send_stream(&client));
        let reset_stream = ConnectionEvent::RecvStreamReset {
            stream_id: request_stream_id,
            app_error: error,
        };
        assert!(server.conn.events().any(|e| e == reset_stream));
    }

    fn handshake_client_error(client: &mut Http3Client, server: &mut TestServer, error: &Error) {
        let out = handshake_only(client, server);
        client.process(out.dgram(), now());
        assert_closed(client, error);
    }

    /// Client fails to create a control stream, since server does not allow it.
    #[test]
    fn client_control_stream_create_failed() {
        let mut client = default_http3_client();
        let mut server =
            TestServer::new_with_conn(new_server::<CountingConnectionIdGenerator, &str>(
                DEFAULT_ALPN_H3,
                ConnectionParameters::default().max_streams(StreamType::UniDi, 0),
            ));
        handshake_client_error(&mut client, &mut server, &Error::StreamLimit);
    }

    /// 2 streams isn't enough for control and QPACK streams.
    #[test]
    fn client_qpack_stream_create_failed() {
        let mut client = default_http3_client();
        let mut server =
            TestServer::new_with_conn(new_server::<CountingConnectionIdGenerator, &str>(
                DEFAULT_ALPN_H3,
                ConnectionParameters::default().max_streams(StreamType::UniDi, 2),
            ));
        handshake_client_error(&mut client, &mut server, &Error::StreamLimit);
    }

    fn do_malformed_response_test(headers: &[Header]) {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        setup_server_side_encoder(&mut client, &mut server);

        let mut d = Encoder::default();
        server.encode_headers(request_stream_id, headers, &mut d);

        // Send response
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            &d,
            false,
        );

        // Stream has been reset because of the malformed headers.
        let e = client.events().next().unwrap();
        assert_eq!(
            e,
            Http3ClientEvent::Reset {
                stream_id: request_stream_id,
                error: Error::HttpMessage.code(),
                local: true,
            }
        );
    }

    #[test]
    fn malformed_response_pseudo_header_after_regular_header() {
        do_malformed_response_test(&[
            Header::new("content-type", "text/plain"),
            Header::new(":status", "100"),
        ]);
    }

    #[test]
    fn malformed_response_undefined_pseudo_header() {
        do_malformed_response_test(&[Header::new(":status", "200"), Header::new(":cheese", "200")]);
    }

    #[test]
    fn malformed_response_duplicate_pseudo_header() {
        do_malformed_response_test(&[
            Header::new(":status", "200"),
            Header::new(":status", "100"),
            Header::new("content-type", "text/plain"),
        ]);
    }

    #[test]
    fn malformed_response_uppercase_header() {
        do_malformed_response_test(&[
            Header::new(":status", "200"),
            Header::new("content-Type", "text/plain"),
        ]);
    }

    #[test]
    fn malformed_response_connection_specific_header() {
        do_malformed_response_test(&[
            Header::new(":status", "200"),
            Header::new("content-type", "text/plain"),
            Header::new("connection", "close"),
        ]);
    }

    #[test]
    fn malformed_response_excluded_byte_in_header() {
        do_malformed_response_test(&[
            Header::new(":status", "200"),
            Header::new("content:type", "text/plain"),
        ]);
    }

    #[test]
    fn malformed_response_request_header_in_response() {
        do_malformed_response_test(&[
            Header::new(":status", "200"),
            Header::new(":method", "GET"),
            Header::new("content-type", "text/plain"),
        ]);
    }

    fn maybe_authenticate(conn: &mut Http3Client) {
        let authentication_needed = |e| matches!(e, Http3ClientEvent::AuthenticationNeeded);
        if conn.events().any(authentication_needed) {
            conn.authenticated(AuthenticationStatus::Ok, now());
        }
    }

    const MAX_TABLE_SIZE: u64 = 65536;
    const MAX_BLOCKED_STREAMS: u16 = 5;

    fn get_resumption_token(server: &mut Http3Server) -> ResumptionToken {
        let mut client = default_http3_client_param(MAX_TABLE_SIZE);

        let mut datagram = None;
        let is_done = |c: &Http3Client| matches!(c.state(), Http3State::Connected);
        while !is_done(&mut client) {
            maybe_authenticate(&mut client);
            datagram = client.process(datagram, now()).dgram();
            datagram = server.process(datagram, now()).dgram();
        }

        // exchange qpack settings, server will send a token as well.
        datagram = client.process(datagram, now()).dgram();
        datagram = server.process(datagram, now()).dgram();
        drop(client.process(datagram, now()).dgram());

        client
            .events()
            .find_map(|e| {
                if let Http3ClientEvent::ResumptionToken(token) = e {
                    Some(token)
                } else {
                    None
                }
            })
            .unwrap()
    }

    // Test that decoder stream type is always sent before any other instruction also
    // in case when 0RTT is used.
    // A client will send a request that uses the dynamic table. This will trigger a header-ack
    // from a server. We will use stats to check that a header-ack has been received.
    #[test]
    fn zerortt_request_use_dynamic_table() {
        let mut server = Http3Server::new(
            now(),
            DEFAULT_KEYS,
            DEFAULT_ALPN_H3,
            anti_replay(),
            Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
            Http3Parameters::default()
                .max_table_size_encoder(MAX_TABLE_SIZE)
                .max_table_size_decoder(MAX_TABLE_SIZE)
                .max_blocked_streams(MAX_BLOCKED_STREAMS),
            None,
        )
        .unwrap();

        let token = get_resumption_token(&mut server);
        // Make a new connection.
        let mut client = default_http3_client_param(MAX_TABLE_SIZE);
        assert_eq!(client.state(), Http3State::Initializing);
        client
            .enable_resumption(now(), &token)
            .expect("Set resumption token");

        assert_eq!(client.state(), Http3State::ZeroRtt);
        let zerortt_event = |e| matches!(e, Http3ClientEvent::StateChange(Http3State::ZeroRtt));
        assert!(client.events().any(zerortt_event));

        // Make a request that uses the dynamic table.
        _ = make_request(&mut client, true, &[Header::new("myheaders", "myvalue")]);
        // Assert that the request has used dynamic table. That will trigger a header_ack.
        assert_eq!(client.qpack_encoder_stats().dynamic_table_references, 1);

        // Exchange packets until header-ack is received.
        // These many packet exchange is needed, to get a header-ack.
        // TODO this may be optimize at Http3Server.
        let out = client.process_output(now());
        let out = server.process(out.dgram(), now());
        let out = client.process(out.dgram(), now());
        let out = server.process(out.dgram(), now());
        let out = client.process(out.dgram(), now());
        let out = server.process(out.dgram(), now());
        let out = client.process(out.dgram(), now());
        let out = server.process(out.dgram(), now());
        drop(client.process(out.dgram(), now()));

        // The header ack for the first request has been received.
        assert_eq!(client.qpack_encoder_stats().header_acks_recv, 1);
    }

    fn manipulate_conrol_stream(client: &mut Http3Client, stream_id: StreamId) {
        assert_eq!(
            client
                .cancel_fetch(stream_id, Error::HttpNone.code())
                .unwrap_err(),
            Error::InvalidStreamId
        );
        assert_eq!(
            client.stream_close_send(stream_id, now()).unwrap_err(),
            Error::InvalidStreamId
        );
        let mut buf = [0; 2];
        assert_eq!(
            client.send_data(stream_id, &buf, now()).unwrap_err(),
            Error::InvalidStreamId
        );
        assert_eq!(
            client.read_data(now(), stream_id, &mut buf).unwrap_err(),
            Error::InvalidStreamId
        );
    }

    #[test]
    fn manipulate_conrol_streams() {
        let (mut client, server, request_stream_id) = connect_and_send_request(false);
        manipulate_conrol_stream(&mut client, CLIENT_SIDE_CONTROL_STREAM_ID);
        manipulate_conrol_stream(&mut client, CLIENT_SIDE_ENCODER_STREAM_ID);
        manipulate_conrol_stream(&mut client, CLIENT_SIDE_DECODER_STREAM_ID);
        manipulate_conrol_stream(&mut client, server.control_stream_id.unwrap());
        manipulate_conrol_stream(&mut client, server.encoder_stream_id.unwrap());
        manipulate_conrol_stream(&mut client, server.decoder_stream_id.unwrap());
        client
            .cancel_fetch(request_stream_id, Error::HttpNone.code())
            .unwrap();
    }

    #[test]
    fn priority_update_during_full_buffer() {
        // set a lower MAX_DATA on the server side to restrict the data the client can send
        let (mut client, mut server) = connect_with_connection_parameters(
            ConnectionParameters::default().max_data(MIN_INITIAL_PACKET_SIZE.try_into().unwrap()),
        );

        let request_stream_id = make_request_and_exchange_pkts(&mut client, &mut server, false);
        let data_writable = |e| matches!(e, Http3ClientEvent::DataWritable { .. });
        assert!(client.events().any(data_writable));
        // Send a lot of data to reach the flow control limit
        client
            .send_data(request_stream_id, &[0; 2000], now())
            .unwrap();

        // now queue a priority_update packet for that stream
        assert!(
            client
                .priority_update(request_stream_id, Priority::new(6, false))
                .unwrap()
        );

        let md_before = server.conn.stats().frame_tx.max_data;

        // sending the http request and most most of the request data
        let out = client.process_output(now());
        let out = server.conn.process(out.dgram(), now());

        // the server responses with an ack, but the max_data didn't change
        assert_eq!(md_before, server.conn.stats().frame_tx.max_data);

        let out = client.process(out.dgram(), now());
        let out = server.conn.process(out.dgram(), now());

        // the server increased the max_data during the second read if that isn't the case
        // in the future and therefore this asserts fails, the request data on stream 0 could be
        // read to cause a max_update frame
        assert_eq!(md_before + 1, server.conn.stats().frame_tx.max_data);

        // make sure that the server didn't receive a priority_update on client control stream
        // (stream_id 2) yet
        let mut buf = [0; 32];
        assert_eq!(
            server.conn.stream_recv(StreamId::new(2), &mut buf),
            Ok((0, false))
        );

        // the client now sends the priority update
        let out = client.process(out.dgram(), now());
        server.conn.process_input(out.dgram().unwrap(), now());

        // check that the priority_update arrived at the client control stream
        let num_read = server.conn.stream_recv(StreamId::new(2), &mut buf).unwrap();
        assert_eq!(b"\x80\x0f\x07\x00\x04\x00\x75\x3d\x36", &buf[0..num_read.0]);
    }

    #[test]
    fn error_request_stream() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        setup_server_side_encoder(&mut client, &mut server);

        let headers = vec![
            Header::new(":status", "200"),
            Header::new(":method", "GET"), // <- invalid
            Header::new("my-header", "my-header"),
            Header::new("content-length", "3"),
        ];
        let encoded_headers = server.encoder.borrow_mut().encode_header_block(
            &mut server.conn,
            &headers,
            request_stream_id,
        );
        let hframe = HFrame::Headers {
            header_block: encoded_headers.to_vec(),
        };

        // Send the encoder instructions, but delay them so that the stream is blocked on decoding
        // headers.
        let encoder_inst_pkt = server.conn.process_output(now());

        // Send response
        let mut d = Encoder::default();
        hframe.encode(&mut d);
        let d_frame = HFrame::Data { len: 3 };
        d_frame.encode(&mut d);
        d.encode(b"abc");
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            &d,
            true,
        );

        // Let client receive the encoder instructions.
        client.process_input(encoder_inst_pkt.dgram().unwrap(), now());

        let reset_event = |e| matches!(e, Http3ClientEvent::Reset { stream_id, .. } if stream_id == request_stream_id);
        assert!(client.events().any(reset_event));
    }

    #[test]
    fn response_w_101() {
        let (mut client, mut server, request_stream_id) = connect_and_send_request(true);

        setup_server_side_encoder(&mut client, &mut server);

        let mut d = Encoder::default();
        let headers1xx = &[Header::new(":status", "101")];
        server.encode_headers(request_stream_id, headers1xx, &mut d);

        // Send 101 response.
        server_send_response_and_exchange_packet(
            &mut client,
            &mut server,
            request_stream_id,
            &d,
            false,
        );

        // Stream has been reset because of the 101 response.
        let e = client.events().next().unwrap();
        assert_eq!(
            e,
            Http3ClientEvent::Reset {
                stream_id: request_stream_id,
                error: Error::HttpGeneralProtocolStream.code(),
                local: true,
            }
        );
    }

    // Client needs to gracefully handle out-of-order STOP_SENDING and STREAM frame arrivals.
    fn client_stop_sending_and_stream_test(separate_packets: bool, stop_sending_first: bool) {
        let (mut client, mut server, stream_id) = connect_and_send_request(false);

        let send_stop_sending = |s: &mut TestServer| {
            s.conn
                .stream_stop_sending(stream_id, Error::HttpRequestCancelled.code())
                .unwrap();
        };
        let send_response = |s: &mut TestServer| {
            _ = s.conn.stream_send(stream_id, HTTP_RESPONSE_2).unwrap();
            s.conn.stream_close_send(stream_id).unwrap();
        };

        if stop_sending_first {
            send_stop_sending(&mut server);
        } else {
            send_response(&mut server);
        }
        if separate_packets {
            client.process(server.conn.process_output(now()).dgram(), now());
        }
        if stop_sending_first {
            send_response(&mut server);
        } else {
            send_stop_sending(&mut server);
        }
        client.process(server.conn.process_output(now()).dgram(), now());

        let events: Vec<_> = client.events().collect();
        assert!(events.iter().any(|e| matches!(
            e, Http3ClientEvent::StopSending { stream_id: id, .. } if *id == stream_id
        )));
        assert!(events.iter().any(|e| matches!(
            e, Http3ClientEvent::HeaderReady { stream_id: id, .. } if *id == stream_id
        )));

        // The stream is dead; any attempt to send on it should fail.
        assert_eq!(
            client.stream_close_send(stream_id, now()),
            Err(Error::InvalidStreamId)
        );
    }

    #[test]
    fn client_stop_sending_and_stream_combinations() {
        for separate_packets in [false, true] {
            for stop_sending_first in [false, true] {
                client_stop_sending_and_stream_test(separate_packets, stop_sending_first);
            }
        }
    }
}
