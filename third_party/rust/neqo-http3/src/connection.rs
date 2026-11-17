// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cell::RefCell,
    fmt::{self, Debug, Display, Formatter},
    mem,
    num::NonZeroUsize,
    rc::Rc,
    time::{Duration, Instant},
};

use neqo_common::{Bytes, Decoder, Header, MessageType, Role};
use neqo_qpack as qpack;
use neqo_transport::{
    AppError, CloseReason, Connection, DatagramQueueCapacity, DatagramQueueOutcome,
    DatagramTracking, State, StreamId, StreamType, ZeroRttState,
    streams::{SendGroupId, SendOrder},
};
use rustc_hash::{FxHashMap as HashMap, FxHashSet as HashSet};
use strum::Display;
use tracing::{debug, error, info, trace, warn};

use crate::{
    CloseType, Error, Http3Parameters, Http3StreamType, HttpRecvStreamEvents, NewStreamType,
    Priority, PriorityHandler, ReceiveOutput, RecvStream, RecvStreamEvents, Res, SendStream,
    SendStreamEvents,
    client_events::Http3ClientEvents,
    control_stream_local::ControlStreamLocal,
    control_stream_remote::ControlStreamRemote,
    features::{
        ConnectType,
        extended_connect::{
            self, ExtendedConnectEvents, ExtendedConnectFeature, ExtendedConnectType,
            TransportPrerequisites,
            send_group::Generator as SendGroupGenerator,
            webtransport_streams::{WebTransportRecvStream, WebTransportSendStream},
        },
    },
    frames::HFrame,
    headers_checks::headers_valid,
    qpack_decoder_receiver::DecoderRecvStream,
    qpack_encoder_receiver::EncoderRecvStream,
    recv_message::{RecvMessage, RecvMessageInfo},
    request_target::RequestTarget,
    send_message::SendMessage,
    settings::{HSettingType, HSettings, HttpZeroRttChecker},
    stream_type_reader::NewStreamHeadReader,
};

/// Number of unidirectional streams HTTP/3 opens for its own use (control, QPACK encoder, QPACK
/// decoder).  Streams of reserved types (RFC 9114, Section 6.2.3), which a peer may also open,
/// are deliberately not counted: how many a given peer opens is a guess.
pub const HTTP3_UNI_CONTROL_STREAMS: u64 = 3;

/// Upper bound on the connection-wide incoming stream limit derived from anticipated
/// `WebTransport` streams.
///
/// Without a cap, an application-controlled value summed across an unbounded number of sessions
/// could ratchet the advertised `MAX_STREAMS` arbitrarily high; the limit is monotonic, so it
/// never comes back down as sessions close.
pub const MAX_ANTICIPATED_INCOMING_STREAMS: u64 = 10_000;

/// A request whose headers are valid.
#[derive(Debug, PartialEq, Eq)]
pub struct RequestDescription {
    headers: Vec<Header>,
    connect_type: Option<ConnectType>,
    priority: Priority,
}

impl RequestDescription {
    /// # Errors
    ///
    /// `InvalidInput` if the method does not match `connect_type` or the headers are not valid.
    pub fn new<T: RequestTarget>(
        method: &str,
        target: &T,
        headers: &[Header],
        connect_type: Option<ConnectType>,
        priority: Priority,
    ) -> Res<Self> {
        info!("Request method={method} target={target:?}");
        match connect_type {
            Some(_) if method != "CONNECT" => {
                warn!("Method {method} with CONNECT type {connect_type:?}");
                return Err(Error::InvalidInput);
            }
            None if method == "CONNECT" => {
                warn!("Method CONNECT without CONNECT type");
                return Err(Error::InvalidInput);
            }
            _ => {}
        }

        let mut final_headers = vec![Header::new(":method", method)];
        if connect_type == Some(ConnectType::Classic) {
            // > The :scheme and :path pseudo-header fields are omitted
            //
            // <https://datatracker.ietf.org/doc/html/rfc9114#section-4.4>
            final_headers.push(Header::new(":authority", target.authority()));
        } else {
            final_headers.extend([
                Header::new(":scheme", target.scheme()?),
                Header::new(":authority", target.authority()),
                Header::new(":path", target.path()?),
            ]);
        }
        if let Some(ConnectType::Extended(protocol)) = connect_type {
            final_headers.push(Header::new(":protocol", protocol.to_string()));
            if protocol == ExtendedConnectType::ConnectUdp {
                final_headers.push(Header::new("capsule-protocol", "?1"));
            }
        }
        final_headers.extend_from_slice(headers);
        headers_valid(&final_headers, MessageType::Request).map_err(|_| Error::InvalidInput)?;
        Ok(Self {
            headers: final_headers,
            connect_type,
            priority,
        })
    }
}

/// Possible actions on an HTTP Extended CONNECT session request.
#[derive(Display)]
pub enum SessionAcceptAction {
    Accept,
    /// Accept the session and include additional headers in the 200 response.
    AcceptWith(Vec<Header>),
    Reject(Vec<Header>),
}

#[derive(Debug)]
enum Http3RemoteSettingsState {
    NotReceived,
    Received(HSettings),
    ZeroRtt(HSettings),
}

/// States:
/// - `Initializing`: this is the state during the QUIC handshake,
/// - `ZeroRtt`: 0-RTT has been enabled and is active
/// - Connected
/// - GoingAway(StreamId): The connection has received a `GOAWAY` frame
/// - Closing(CloseReason): The connection is closed. The closing has been initiated by this end of
///   the connection, e.g., the `CONNECTION_CLOSE` frame has been sent. In this state, the
///   connection waits a certain amount of time to retransmit the `CONNECTION_CLOSE` frame if
///   needed.
/// - Closed(CloseReason): This is the final close state: closing has been initialized by the peer
///   and an ack for the `CONNECTION_CLOSE` frame has been sent or the closing has been initiated by
///   this end of the connection and the ack for the `CONNECTION_CLOSE` has been received or the
///   waiting time has passed.
#[derive(Debug, PartialEq, PartialOrd, Ord, Eq, Clone)]
pub enum Http3State {
    Initializing,
    ZeroRtt,
    Connected,
    GoingAway(StreamId),
    Closing(CloseReason),
    Closed(CloseReason),
}

impl Http3State {
    #[must_use]
    pub const fn active(&self) -> bool {
        matches!(self, Self::Connected | Self::GoingAway(_) | Self::ZeroRtt)
    }
}

/// # HTTP/3 core implementation
///
/// This is the core implementation of HTTP/3 protocol. It implements most of the
/// features of the protocol. [`crate::Http3Client`] and
/// [`crate::connection_server::Http3ServerHandler`] implement only client and
/// server side behavior.
///
/// ## Streams
///
/// Each [`Http3Connection`] holds a list of stream handlers. Each send and receive-handler is
/// registered in `send_streams` and `recv_streams`. Unidirectional streams are registered only on
/// one of the lists and bidirectional streams are registered in both lists and the 2 handlers are
/// independent, e.g. one can be closed and removed and second may still be active.
///
/// The only streams that are not registered are the local control stream, local
/// QPACK decoder stream, and local QPACK encoder stream. These streams are
/// send-streams and sending data on this stream is handled a bit differently. This
/// is done in the [`Http3Connection::process_sending`] function, i.e. the control data
/// is sent first and QPACK data is sent after regular stream data is sent because
/// this stream may have new data only after regular streams are handled (TODO we
/// may improve this a bit to send QPACK commands before headers.)
///
/// There are the following types of streams:
/// - [`Http3StreamType::Control`]: there is only a receiver stream of this type and the handler is
///   [`ControlStreamRemote`].
/// - [`Http3StreamType::Decoder`]: there is only a receiver stream of this type and the handler is
///   [`DecoderRecvStream`].
/// - [`Http3StreamType::Encoder`]: there is only a receiver stream of this type and the handler is
///   [`EncoderRecvStream`].
/// - [`Http3StreamType::NewStream`]: there is only a receiver stream of this type and the handler
///   is [`NewStreamHeadReader`].
/// - [`Http3StreamType::Http`]: [`SendMessage`] and [`RecvMessage`] handlers are responsible for
///   this type of streams.
/// - [`Http3StreamType::ExtendedConnect`]: [`extended_connect::session::Session`] is responsible
///   sender and receiver handler.
/// - [`Http3StreamType::WebTransport`]: [`WebTransportSendStream`] and [`WebTransportRecvStream`]
///   are responsible sender and receiver handler.
/// - [`Http3StreamType::Unknown`]: These are all other stream types that are not unknown to the
///   current implementation and should be handled properly by the spec, e.g., in our implementation
///   the streams are reset.
///
/// The streams are registered in `send_streams` and `recv_streams` in following ways depending if
/// they are local or remote:
/// - local streams:
///   - all local stream will be registered with the appropriate handler.
/// - remote streams:
///   - all new incoming streams are registered with [`NewStreamHeadReader`]. This is triggered by
///     [`ConnectionEvent::NewStream`] and [`Http3Connection::add_new_stream`] is called.
///   - reading from a [`NewStreamHeadReader`] stream, via the [`RecvStream::receive`] function,
///     will decode a stream type. [`RecvStream::receive`] will return [`ReceiveOutput::NewStream`]
///     when a stream type has been decoded.  After this point the stream:
///     - will be regegistered with the appropriate handler,
///     - will be canceled if is an unknown stream type or
///     - the connection will fail if it is unallowed stream type (receiving HTTP request on the
///       client-side).
///
/// The output is handled in [`Http3Connection::handle_new_stream`], for control, qpack streams and
/// partially `WebTransport` streams, otherwise the output is handled by [`Http3Client`] and
/// [`Http3ServerHandler`].
///
///
/// ### Receiving data
///
/// Reading from a stream is triggered by [`ConnectionEvent::RecvStreamReadable`] events for the
/// stream. The receive handler is retrieved from `recv_streams` and its [`RecvStream::receive`]
/// function is called.
///
/// Receiving data on [`Http3StreamType::Http`] streams is also triggered by the
/// [`Http3Connection::read_data`] function. [`ConnectionEvent::RecvStreamReadable`] events will
/// trigger reading `HEADERS` frame and frame headers for `DATA` frames which will produce
/// [`Http3ClientEvent`] or [`Http3ServerEvent`] events. The content of `DATA` frames is read by the
/// application using the `read_data` function. The `read_data` function may read frame headers for
/// consecutive `DATA` frames.
///
/// On a [`Http3StreamType::WebTransport`] stream data will be read only by the
/// `Http3Connection::read_data` function. The [`RecvStream::receive`] function only produces an
/// [`Http3ClientEvent`] or [`Http3ServerEvent`] event.
///
/// The [`RecvStream::receive`] and [`Http3Connection::read_data`] functions may detect that the
/// stream is done, e.g. FIN received. In this case, the stream will be removed from the
/// `recv_stream` register, see [`Http3Connection::remove_recv_stream`].
///
/// ### Sending data
///
/// All sender stream handlers have buffers. Data is first written into a buffer before being
/// supplied to the QUIC layer. All data except the `DATA` frame and `WebTransport(_)`’s payload are
/// written into the buffer. This includes stream type byte, e.g. `WEBTRANSPORT_STREAM` as well. In
/// the case of `Http` and `WebTransport(_)` applications can write directly to the QUIC layer using
/// the `send_data` function to avoid copying data. Sending data via the `send_data` function is
/// only possible if there is no buffered data.
///
/// If a stream has buffered data it will be registered in the `streams_with_pending_data` queue and
/// actual sending will be performed in the [`Http3Connection::process_sending`] function call.
/// (This is done in this way, i.e. data is buffered first and then sent, for 2 reasons: in this
/// way, sending will happen in a single function,  therefore error handling and clean up is easier
/// and the QUIC layer may not be able to accept all data and being able to buffer data is required
/// in any case.)
///
/// The `send` and `send_data` functions may detect that the stream is closed and all outstanding
/// data has been transferred to the QUIC layer. In this case, the stream will be removed from the
/// `send_stream` register.
///
/// ### [`ControlStreamRemote`]
///
/// The [`ControlStreamRemote`] handler uses [`FrameReader`] to read and decode frames received on
/// the control frame. The [`RecvStream::receive`] implementation returns
/// [`ReceiveOutput::ControlFrames`] with a list of control frames read (the list may be empty). The
/// control frames are handled by [`Http3Connection`] and/or by [`Http3Client`] and
/// [`Http3ServerHandler`].
///
/// ### [`DecoderRecvStream`] and [`EncoderRecvStream`]
///
/// The [`RecvStream::receive`] implementation of these handlers call corresponding
/// [`RecvStream::receive`] functions of [`qpack::Encoder`] and [`qpack::Decoder`].
///
/// [`DecoderRecvStream`] returns [`ReceiveOutput::UnblockedStreams`] that may contain a list of
/// stream ids that are unblocked by receiving qpack decoder commands. [`Http3Connection`] will
/// handle this output by calling [`RecvStream::receive`] for the listed stream ids.
///
/// [`EncoderRecvStream`] only returns [`ReceiveOutput::NoOutput`].
///
/// Both handlers may return an error that will close the connection.
///
/// ### [`NewStreamHeadReader`]
///
/// A new incoming receiver stream registers a [`NewStreamHeadReader`] handler. This handler reads
/// the first bytes of a stream to detect a stream type. The [`RecvStream::receive`] function
/// returns [`ReceiveOutput::NoOutput`] if a stream type is still not known by reading the available
/// stream data or [`ReceiveOutput::NewStream`]. The handling of the output is explained above.
///
/// ### [`SendMessage`] and [`RecvMessage`]
///
/// [`RecvMessage::receive`] only returns [`ReceiveOutput::NoOutput`]. It also have an event
/// listener of type [`HttpRecvStreamEvents`]. The listener is called when headers are ready, or
/// data is ready, etc.
///
/// For example for [`Http3StreamType::Http`] stream the listener will produce
/// [`Http3ClientEvent::HeaderReady`] and [`Http3ClientEvent::DataReadable`] events.
///
/// ### [`extended_connect::session::Session`]
///
/// An [`extended_connect::session::Session`] is connected to a control stream
/// that is in essence an HTTP transaction. Therefore,
/// [`extended_connect::session::Session`] will internally use a [`SendMessage`]
/// and [`RecvMessage`] handler to handle parsing and sending of HTTP part of
/// the control stream.  When HTTP headers are exchanged,
/// [`extended_connect::session::Session`] will take over handling of stream
/// data. [`extended_connect::session::Session`] sets a [`HttpRecvStreamEvents`]
/// listener as the [`RecvMessage`] event listener.
///
/// `neqo_http3` implements the WebTransport and MASQUE connect-udp HTTP
/// Extended CONNECT protocol using [`extended_connect::session::Session`].
///
/// The WebTransport HTTP Extended CONNECT protocol supports streams.
/// [`WebTransportSendStream`] and [`WebTransportRecvStream`] are associated
/// with a [`extended_connect::session::Session`] and they will be canceled if
/// the session is closed. To be able to do this
/// [`extended_connect::session::Session`] holds a list of its active streams
/// and clean up is done in `remove_extended_connect`.
///
/// ###  [`WebTransportSendStream`] and [`WebTransportRecvStream`]
///
/// WebTransport streams are associated with a session. [`WebTransportSendStream`] and
/// [`WebTransportRecvStream`] hold a reference to the session and are registered in the session
/// upon  creation by [`Http3Connection`]. The [`WebTransportSendStream`] and
/// [`WebTransportRecvStream`]  handlers will be unregistered from the session if they are closed,
/// reset, or canceled.
///
/// The call to function [`RecvStream::receive`] may produce [`Http3ClientEvent::DataReadable`].
/// Actual reading of data is done in the `read_data` function.
///
/// [`Http3ServerEvent`]: crate::Http3ServerEvent
/// [`Http3Server`]: crate::Http3Server
/// [`FrameReader`]: crate::frames::FrameReader
/// [`Http3ClientEvent`]: crate::Http3ClientEvent
/// [`Http3ClientEvent::DataReadable`]: crate::Http3ClientEvent::DataReadable
/// [`Http3ClientEvent::HeaderReady`]: crate::Http3ClientEvent::HeaderReady
/// [`Http3Client`]: crate::connection_client::Http3Client
/// [`Http3ServerEvent::DataReadable`]: crate::Http3ServerEvent
/// [`Http3ServerHandler`]: crate::connection_server::Http3ServerHandler
/// [`ConnectionEvent::RecvStreamReadable`]: neqo_transport::ConnectionEvent::RecvStreamReadable
/// [`ConnectionEvent::NewStream`]: neqo_transport::ConnectionEvent::NewStream
#[derive(Debug)]
pub struct Http3Connection {
    role: Role,
    state: Http3State,
    local_params: Http3Parameters,
    control_stream_local: ControlStreamLocal,
    qpack_encoder: Rc<RefCell<qpack::Encoder>>,
    qpack_decoder: Rc<RefCell<qpack::Decoder>>,
    settings_state: Http3RemoteSettingsState,
    streams_with_pending_data: HashSet<StreamId>,
    send_streams: HashMap<StreamId, Box<dyn SendStream>>,
    recv_streams: HashMap<StreamId, Box<dyn RecvStream>>,
    webtransport: ExtendedConnectFeature,
    connect_udp: ExtendedConnectFeature,
    send_group_generator: SendGroupGenerator,
}

impl Display for Http3Connection {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "Http3 connection")
    }
}

impl Http3Connection {
    /// Create a new connection.
    pub fn new(conn_params: Http3Parameters, role: Role) -> Self {
        Self {
            state: Http3State::Initializing,
            control_stream_local: ControlStreamLocal::default(),
            qpack_encoder: Rc::new(RefCell::new(qpack::Encoder::new(
                conn_params.get_qpack_settings(),
                true,
            ))),
            qpack_decoder: Rc::new(RefCell::new(qpack::Decoder::new(
                conn_params.get_qpack_settings(),
            ))),
            webtransport: ExtendedConnectFeature::new(
                ExtendedConnectType::WebTransport,
                role,
                conn_params.webtransport_enabled(),
            ),
            connect_udp: ExtendedConnectFeature::new(
                ExtendedConnectType::ConnectUdp,
                role,
                conn_params.connect_enabled(),
            ),
            local_params: conn_params,
            settings_state: Http3RemoteSettingsState::NotReceived,
            streams_with_pending_data: HashSet::default(),
            send_streams: HashMap::default(),
            recv_streams: HashMap::default(),
            send_group_generator: SendGroupGenerator::default(),
            role,
        }
    }

    /// Listener for non-default feature negotiation. No-op when feature is
    /// disabled. This is currently only used for the
    /// [`crate::features::extended_connect::webtransport_session`] and
    /// [`crate::features::extended_connect::connect_udp_session`] feature.  The
    /// negotiation is done via the `SETTINGS` frame and when the peer's
    /// `SETTINGS` frame has been received the listener will be called.
    pub(crate) fn set_features_listener(&mut self, feature_listener: Http3ClientEvents) {
        self.webtransport.set_listener(feature_listener.clone());
        self.connect_udp.set_listener(feature_listener);
    }

    /// This function creates and initializes, i.e. send stream type, the control and qpack
    /// streams.
    fn initialize_http3_connection(&mut self, conn: &mut Connection) -> Res<()> {
        debug!("[{self}] Initialize the http3 connection");
        self.control_stream_local.create(conn)?;

        self.send_settings();
        self.create_qpack_streams(conn)?;
        Ok(())
    }

    fn send_settings(&mut self) {
        debug!("[{self}] Send settings");
        self.control_stream_local.queue_frame(&HFrame::Settings {
            settings: HSettings::from(&self.local_params),
        });
        self.control_stream_local.queue_frame(&HFrame::Grease);
    }

    /// Save settings for adding to the session ticket.
    pub(crate) fn save_settings(&self) -> Vec<u8> {
        HttpZeroRttChecker::save(&self.local_params)
    }

    fn create_qpack_streams(&self, conn: &mut Connection) -> Res<()> {
        debug!("[{self}] create_qpack_streams");
        self.qpack_encoder
            .borrow_mut()
            .add_send_stream(conn.stream_create(StreamType::UniDi)?)?;
        self.qpack_decoder
            .borrow_mut()
            .add_send_stream(conn.stream_create(StreamType::UniDi)?)?;
        Ok(())
    }

    /// Inform an [`Http3Connection`] that a stream has data to send and that
    /// [`SendStream::send`] should be called for the stream.
    pub(crate) fn stream_has_pending_data(&mut self, stream_id: StreamId) {
        self.streams_with_pending_data.insert(stream_id);
    }

    /// Return true if there is a stream that needs to send data.
    pub(crate) fn has_data_to_send(&self) -> bool {
        !self.streams_with_pending_data.is_empty()
    }

    /// This function calls the `send` function for all streams that have data to send. If a stream
    /// has data to send it will be added to the `streams_with_pending_data` list.
    ///
    /// Control and QPACK streams are handled differently and are never added to the list.
    fn send_non_control_streams(&mut self, conn: &mut Connection, now: Instant) -> Res<()> {
        let to_send = mem::take(&mut self.streams_with_pending_data);
        #[expect(
            clippy::iter_over_hash_type,
            reason = "OK to loop over active streams in an undefined order."
        )]
        for stream_id in to_send {
            let done = if let Some(s) = &mut self.send_streams.get_mut(&stream_id) {
                s.send(conn, now)?;
                if s.has_data_to_send() {
                    self.streams_with_pending_data.insert(stream_id);
                }
                s.done()
            } else {
                false
            };
            if done {
                self.remove_send_stream(stream_id, conn);
            }
        }
        Ok(())
    }

    /// Call `send` for all streams that need to send data. See explanation for the main structure
    /// for more details.
    pub(crate) fn process_sending(&mut self, conn: &mut Connection, now: Instant) -> Res<()> {
        // check if control stream has data to send.
        self.control_stream_local
            .send(conn, &mut self.recv_streams, now)?;

        self.send_non_control_streams(conn, now)?;

        self.qpack_decoder.borrow_mut().send(conn)?;
        match self.qpack_encoder.borrow_mut().send_encoder_updates(conn) {
            Ok(())
            | Err(neqo_qpack::Error::EncoderStreamBlocked | neqo_qpack::Error::DynamicTableFull) => {
            }
            Err(e) => return Err(Error::Qpack(e)),
        }
        Ok(())
    }

    /// We have a resumption token which remembers previous settings. Update the setting.
    pub(crate) fn set_0rtt_settings(
        &mut self,
        conn: &mut Connection,
        settings: HSettings,
    ) -> Res<()> {
        self.initialize_http3_connection(conn)?;
        self.set_qpack_settings(&settings)?;
        self.settings_state = Http3RemoteSettingsState::ZeroRtt(settings);
        self.state = Http3State::ZeroRtt;
        Ok(())
    }

    /// Returns the settings for a connection. This is used for creating a resumption token.
    pub(crate) fn get_settings(&self) -> Option<HSettings> {
        if let Http3RemoteSettingsState::Received(settings) = &self.settings_state {
            Some(settings.clone())
        } else {
            None
        }
    }

    /// This is called when a [`neqo_transport::ConnectionEvent::NewStream`]
    /// event is received.  This registers the stream with a
    /// [`NewStreamHeadReader`] handler.
    pub(crate) fn add_new_stream(&mut self, stream_id: StreamId) {
        trace!("[{self}] A new stream: {stream_id}");
        self.recv_streams.insert(
            stream_id,
            Box::new(NewStreamHeadReader::new(stream_id, self.role)),
        );
    }

    /// The function calls [`RecvStream::receive`] for a stream. It also deals
    /// with the outcome of a read by calling
    /// [`Http3Connection::handle_stream_manipulation_output`].
    fn stream_receive(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        now: Instant,
    ) -> Res<ReceiveOutput> {
        trace!("[{self}] Readable stream {stream_id}");

        if let Some(recv_stream) = self.recv_streams.get_mut(&stream_id) {
            let res = recv_stream.receive(conn, now);
            return self
                .handle_stream_manipulation_output(res, stream_id, conn)
                .map(|(output, _)| output);
        }
        Ok(ReceiveOutput::NoOutput)
    }

    fn handle_unblocked_streams(
        &mut self,
        unblocked_streams: Vec<StreamId>,
        conn: &mut Connection,
        now: Instant,
    ) -> Res<()> {
        for stream_id in unblocked_streams {
            debug!("[{self}] Stream {stream_id} is unblocked");
            if let Some(r) = self.recv_streams.get_mut(&stream_id) {
                let res = r
                    .http_stream()
                    .ok_or(Error::HttpInternal(10))?
                    .header_unblocked(conn, now);
                let res = self.handle_stream_manipulation_output(res, stream_id, conn)?;
                debug_assert!(matches!(res, (ReceiveOutput::NoOutput, _)));
            }
        }
        Ok(())
    }

    /// This function handles reading from all streams, i.e. control, qpack, request/response
    /// stream and unidi stream that still do not have a type.
    /// The function cannot handle:
    /// 1) a `Http` or `WebTransportStream(_)` stream
    /// 2) frames `PriorityUpdateRequest` or `Goaway` must be handled by `Http3Client`/`Server`.
    ///
    /// The function returns `ReceiveOutput`.
    pub(crate) fn handle_stream_readable(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        now: Instant,
    ) -> Res<ReceiveOutput> {
        let mut output = self.stream_receive(conn, stream_id, now)?;

        if let ReceiveOutput::NewStream(stream_type) = output {
            output = self.handle_new_stream(conn, stream_type, stream_id, now)?;
        }

        match output {
            ReceiveOutput::UnblockedStreams(unblocked_streams) => {
                self.handle_unblocked_streams(unblocked_streams, conn, now)?;
                Ok(ReceiveOutput::NoOutput)
            }
            ReceiveOutput::ControlFrames(control_frames) => {
                let mut rest = Vec::new();
                for cf in control_frames {
                    if let Some(not_handled) = self.handle_control_frame(conn, cf)? {
                        rest.push(not_handled);
                    }
                }
                Ok(ReceiveOutput::ControlFrames(rest))
            }
            ReceiveOutput::NewStream(
                NewStreamType::Http(_) | NewStreamType::WebTransportStream(_),
            )
            | ReceiveOutput::NoOutput => Ok(output),
            ReceiveOutput::NewStream(_) => {
                unreachable!("NewStream should have been handled already")
            }
        }
    }

    /// This is called when a RESET frame has been received.
    pub(crate) fn handle_stream_reset(
        &mut self,
        stream_id: StreamId,
        app_error: AppError,
        conn: &mut Connection,
    ) -> Res<()> {
        info!("[{self}] Handle a stream reset stream_id={stream_id} app_err={app_error}");

        // TODO: https://github.com/mozilla/neqo/issues/4322
        self.close_recv(stream_id, CloseType::ResetRemote(app_error), conn)
    }

    pub(crate) fn handle_stream_stop_sending(
        &mut self,
        stream_id: StreamId,
        app_error: AppError,
        conn: &mut Connection,
    ) -> Res<()> {
        info!("[{self}] Handle stream_stop_sending stream_id={stream_id} app_err={app_error}");

        if self.send_stream_is_critical(stream_id) {
            return Err(Error::HttpClosedCriticalStream);
        }

        self.close_send(stream_id, CloseType::ResetRemote(app_error), conn);
        Ok(())
    }

    /// This is called when `neqo_transport::Connection` state has been change to take proper
    /// actions in the HTTP3 layer.
    pub(crate) fn handle_state_change(
        &mut self,
        conn: &mut Connection,
        state: &State,
    ) -> Res<bool> {
        debug!("[{self}] Handle state change {state:?}");
        match state {
            State::Handshaking => {
                if self.role == Role::Server
                    && conn.zero_rtt_state() == ZeroRttState::AcceptedServer
                {
                    self.state = Http3State::ZeroRtt;
                    self.initialize_http3_connection(conn)?;
                    Ok(true)
                } else {
                    Ok(false)
                }
            }
            State::Connected => {
                debug_assert!(matches!(
                    self.state,
                    Http3State::Initializing | Http3State::ZeroRtt
                ));
                if self.state == Http3State::Initializing {
                    self.initialize_http3_connection(conn)?;
                }
                self.state = Http3State::Connected;
                Ok(true)
            }
            State::Closing { error, .. } | State::Draining { error, .. } => {
                if matches!(self.state, Http3State::Closing(_) | Http3State::Closed(_)) {
                    Ok(false)
                } else {
                    self.state = Http3State::Closing(error.clone());
                    Ok(true)
                }
            }
            State::Closed(error) => {
                if matches!(self.state, Http3State::Closed(_)) {
                    Ok(false)
                } else {
                    self.state = Http3State::Closed(error.clone());
                    Ok(true)
                }
            }
            _ => Ok(false),
        }
    }

    /// This is called when 0RTT has been reset to clear `send_streams`, `recv_streams` and
    /// settings.
    pub(crate) fn handle_zero_rtt_rejected(&mut self) -> Res<()> {
        if self.state == Http3State::ZeroRtt {
            self.state = Http3State::Initializing;
            self.control_stream_local = ControlStreamLocal::default();
            self.qpack_encoder = Rc::new(RefCell::new(qpack::Encoder::new(
                self.local_params.get_qpack_settings(),
                true,
            )));
            self.qpack_decoder = Rc::new(RefCell::new(qpack::Decoder::new(
                self.local_params.get_qpack_settings(),
            )));
            self.settings_state = Http3RemoteSettingsState::NotReceived;
            self.streams_with_pending_data.clear();
            // TODO: investigate whether this code can automatically retry failed transactions.
            // No extended-CONNECT session can be among these, so there is no
            // per-session datagram queue on the `Connection` to drop: neither
            // feature is negotiated in 0-RTT (`set_0rtt_settings` never enables
            // them), and creating a session fails with `Unavailable` until one is.
            self.send_streams.clear();
            self.recv_streams.clear();
            Ok(())
        } else {
            debug_assert!(false, "Zero rtt rejected in the wrong state");
            Err(Error::HttpInternal(3))
        }
    }

    pub(crate) fn handle_datagram(&mut self, datagram: Vec<u8>) {
        let mut decoder = Decoder::new(&datagram);
        let Some(id) = decoder.decode_varint() else {
            debug!("[{self}] handle_datagram: failed to decode session ID");
            return;
        };
        let varint_len = decoder.offset();

        let Some(stream) = self
            .recv_streams
            .get_mut(&StreamId::from(id * 4))
            .and_then(|s| s.extended_connect_session())
        else {
            debug!("[{self}] handle_datagram for unknown extended connect session");
            return;
        };

        stream.borrow().datagram(Bytes::new(datagram, varint_len));
    }

    fn check_stream_exists(&self, stream_type: Http3StreamType) -> Res<()> {
        if self
            .recv_streams
            .values()
            .any(|c| c.stream_type() == stream_type)
        {
            Err(Error::HttpStreamCreation)
        } else {
            Ok(())
        }
    }

    /// If the new stream is a control or QPACK stream, this function creates a proper handler
    /// and perform a read.
    /// if the new stream is a `Http` or `WebTransportStream(_)` stream, the function
    /// returns `ReceiveOutput::NewStream(_)` and the caller will handle it.
    /// If the stream is of a unknown type the stream will be closed.
    fn handle_new_stream(
        &mut self,
        conn: &mut Connection,
        stream_type: NewStreamType,
        stream_id: StreamId,
        now: Instant,
    ) -> Res<ReceiveOutput> {
        match stream_type {
            NewStreamType::Control => {
                self.check_stream_exists(Http3StreamType::Control)?;
                self.recv_streams
                    .insert(stream_id, Box::new(ControlStreamRemote::new(stream_id)));
            }

            NewStreamType::Decoder => {
                debug!("[{self}] A new remote qpack encoder stream {stream_id}");
                self.check_stream_exists(Http3StreamType::Decoder)?;
                self.recv_streams.insert(
                    stream_id,
                    Box::new(DecoderRecvStream::new(
                        stream_id,
                        Rc::clone(&self.qpack_decoder),
                    )?),
                );
            }
            NewStreamType::Encoder => {
                debug!("[{self}] A new remote qpack decoder stream {stream_id}");
                self.check_stream_exists(Http3StreamType::Encoder)?;
                self.recv_streams.insert(
                    stream_id,
                    Box::new(EncoderRecvStream::new(
                        stream_id,
                        Rc::clone(&self.qpack_encoder),
                    )?),
                );
            }
            NewStreamType::Http(_) => {
                info!("[{self}] A new http stream {stream_id}");
            }
            NewStreamType::WebTransportStream(session_id) => {
                let session_exists = self
                    .send_streams
                    .get(&StreamId::from(session_id))
                    .is_some_and(|s| s.stream_type() == Http3StreamType::ExtendedConnect);
                if !session_exists {
                    conn.stream_stop_sending(stream_id, Error::HttpStreamCreation.code())?;
                    return Ok(ReceiveOutput::NoOutput);
                }
                // Set incoming WebTransport streams to be fair (share bandwidth).
                // We may call this with an invalid stream ID, so ignore that error.
                match conn.stream_fairness(stream_id, true) {
                    Ok(()) | Err(neqo_transport::Error::InvalidStreamId) => (),
                    Err(e) => return Err(Error::from(e)),
                }
                info!("[{self}] A new WebTransport stream {stream_id} for session {session_id}");
            }
            NewStreamType::Unknown => {
                conn.stream_stop_sending(stream_id, Error::HttpStreamCreation.code())?;
            }
        }

        match stream_type {
            NewStreamType::Control | NewStreamType::Decoder | NewStreamType::Encoder => {
                self.stream_receive(conn, stream_id, now)
            }
            NewStreamType::Http(_) | NewStreamType::WebTransportStream(_) => {
                Ok(ReceiveOutput::NewStream(stream_type))
            }
            NewStreamType::Unknown => Ok(ReceiveOutput::NoOutput),
        }
    }

    /// This is called when an application closes the connection.
    pub fn close(&mut self, error: AppError) {
        debug!("[{self}] Close connection error {error:?}");
        self.state = Http3State::Closing(CloseReason::Application(error));
        if (!self.send_streams.is_empty() || !self.recv_streams.is_empty()) && (error == 0) {
            debug!("close(0) called when streams still active");
        }
        self.send_streams.clear();
        self.recv_streams.clear();
    }

    /// This function will not handle the output of the function completely, but only
    /// handle the indication that a stream is closed. There are 2 cases:
    ///  - an error occurred or
    ///  - the stream is done, i.e. the second value in `output` tuple is true if the stream is done
    ///    and can be removed from the `recv_streams`
    ///
    /// How it is handling `output`:
    ///  - if the stream is done, it removes the stream from `recv_streams`
    ///  - if the stream is not done and there is no error, return `output` and the caller will
    ///    handle it.
    ///  - in case of an error:
    ///    - if it is only a stream error and the stream is not critical, send `STOP_SENDING` and
    ///      `RESET_STREAM` frames, remove the stream from `recv_streams` and `send_streams`, and
    ///      inform the listener that the stream has been reset.
    ///    - otherwise this is a connection error. In this case, propagate the error to the caller
    ///      that will handle it properly.
    fn handle_stream_manipulation_output<U>(
        &mut self,
        output: Res<(U, bool)>,
        stream_id: StreamId,
        conn: &mut Connection,
    ) -> Res<(U, bool)>
    where
        U: Default,
    {
        match &output {
            Ok((_, true)) => {
                self.remove_recv_stream(stream_id, conn);
            }
            Ok((_, false)) => {}
            Err(e) => {
                if e.stream_reset_error() && !self.recv_stream_is_critical(stream_id) {
                    drop(conn.stream_stop_sending(stream_id, e.code()));
                    self.close_recv(stream_id, CloseType::LocalError(e.code()), conn)?;
                    self.close_send(stream_id, CloseType::LocalError(e.code()), conn);
                    drop(conn.stream_reset_send(stream_id, e.code()));
                    return Ok((U::default(), false));
                }
            }
        }
        output
    }

    fn create_bidi_transport_stream(&self, conn: &mut Connection) -> Res<StreamId> {
        // Requests cannot be created when a connection is in states: Initializing, GoingAway,
        // Closing and Closed.
        match self.state() {
            Http3State::GoingAway(..) | Http3State::Closing(..) | Http3State::Closed(..) => {
                return Err(Error::AlreadyClosed);
            }
            Http3State::Initializing => return Err(Error::Unavailable),
            _ => {}
        }

        let id = conn.stream_create(StreamType::BiDi)?;
        conn.stream_keep_alive(id, true)?;
        Ok(id)
    }

    pub fn request(
        &mut self,
        conn: &mut Connection,
        send_events: Box<dyn SendStreamEvents>,
        recv_events: Box<dyn HttpRecvStreamEvents>,
        request: &RequestDescription,
        now: Instant,
    ) -> Res<StreamId> {
        let stream_id = self.create_bidi_transport_stream(conn)?;

        let stream_type = if request.connect_type.is_some() {
            Http3StreamType::ExtendedConnect
        } else {
            Http3StreamType::Http
        };

        let mut send_message = SendMessage::new(
            MessageType::Request,
            stream_type,
            stream_id,
            Rc::clone(&self.qpack_encoder),
            send_events,
        );

        send_message
            .http_stream()
            .ok_or(Error::Internal)?
            .send_headers(&request.headers, conn)?;

        self.add_streams(
            stream_id,
            Box::new(send_message),
            Box::new(RecvMessage::new(
                &RecvMessageInfo {
                    message_type: MessageType::Response,
                    stream_type,
                    stream_id,
                    first_frame_type: None,
                },
                Rc::clone(&self.qpack_decoder),
                recv_events,
                PriorityHandler::new(request.priority),
            )),
        );

        // Call immediately send so that at least headers get sent. This will make Firefox faster,
        // since it can send request body immediately in most cases and does not need to do
        // a complete process loop.
        self.send_streams
            .get_mut(&stream_id)
            .ok_or(Error::InvalidStreamId)?
            .send(conn, now)?;
        Ok(stream_id)
    }

    /// Stream data are read directly into a buffer supplied as a parameter of this function to
    /// avoid copying data.
    ///
    /// # Errors
    ///
    /// It returns an error if a stream does not exist or an error happens while reading a stream,
    /// e.g. early close, protocol error, etc.
    pub fn read_data(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        buf: &mut [u8],
        now: Instant,
    ) -> Res<(usize, bool)> {
        debug!("[{self}] read_data from stream {stream_id}");
        let res = self
            .recv_streams
            .get_mut(&stream_id)
            .ok_or(Error::InvalidStreamId)?
            .read_data(conn, buf, now);
        self.handle_stream_manipulation_output(res, stream_id, conn)
    }

    /// This is called when an application resets a stream.
    /// The application reset will close both sides.
    pub fn stream_reset_send(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        error: AppError,
    ) -> Res<()> {
        debug!("[{self}] Reset sending side of stream {stream_id} error={error}");

        if self.send_stream_is_critical(stream_id) {
            return Err(Error::InvalidStreamId);
        }

        self.close_send(stream_id, CloseType::ResetApp(error), conn);
        conn.stream_reset_send(stream_id, error)?;
        Ok(())
    }

    /// Commit to reliably delivering the stream data buffered so far. If the stream is
    /// subsequently reset, that prefix is delivered using `RESET_STREAM_AT`, if possible.
    /// See [`neqo_transport::Connection::stream_commit`].
    ///
    /// # Errors
    /// `InvalidStreamId` if the stream does not exist, or a transport error when the commitment
    /// is rejected (e.g. the peer did not enable reliable reset).
    pub fn stream_commit(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        now: Instant,
    ) -> Res<()> {
        trace!("[{self}] Commit reliable size on stream {stream_id}");
        // The request is routed through the send stream so that any data still buffered in the
        // HTTP/3 layer is flushed to the transport before the commitment is made.
        self.send_streams
            .get_mut(&stream_id)
            .ok_or(Error::InvalidStreamId)?
            .commit(conn, now)
    }

    pub fn stream_stop_sending(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        error: AppError,
    ) -> Res<()> {
        debug!("[{self}] Send stop sending for stream {stream_id} error={error}");
        if self.recv_stream_is_critical(stream_id) {
            return Err(Error::InvalidStreamId);
        }

        self.close_recv(stream_id, CloseType::ResetApp(error), conn)?;

        // Stream may be already be closed and we may get an error here, but we do not care.
        conn.stream_stop_sending(stream_id, error)?;
        Ok(())
    }

    /// Set the stream `SendOrder`.
    ///
    /// # Errors
    ///
    /// Returns `InvalidStreamId` if the stream id doesn't exist
    pub fn stream_set_sendorder(
        conn: &mut Connection,
        stream_id: StreamId,
        sendorder: Option<SendOrder>,
    ) -> Res<()> {
        conn.stream_sendorder(stream_id, sendorder)
            .map_err(|_| Error::InvalidStreamId)
    }

    /// Set the stream [`SendGroupId`].
    ///
    /// # Errors
    ///
    /// Returns [`Error::InvalidStreamId`] if the stream id doesn't exist,
    /// [`Error::Unavailable`] if the stream doesn't support send groups, or
    /// [`Error::InvalidState`] if the group is not registered with the session.
    pub fn stream_set_sendgroup(&mut self, stream_id: StreamId, sendgroup: SendGroupId) -> Res<()> {
        let send_stream = self
            .send_streams
            .get_mut(&stream_id)
            .ok_or(Error::InvalidStreamId)?;
        send_stream.set_send_group(sendgroup)
    }

    /// Clear the stream [`SendGroupId`].
    ///
    /// # Errors
    ///
    /// Returns [`Error::InvalidStreamId`] if the stream id doesn't exist, or
    /// [`Error::Unavailable`] if the stream doesn't support send groups.
    pub fn stream_clear_sendgroup(&mut self, stream_id: StreamId) -> Res<()> {
        let send_stream = self
            .send_streams
            .get_mut(&stream_id)
            .ok_or(Error::InvalidStreamId)?;
        send_stream.clear_send_group()
    }

    /// Set the stream Fairness.   Fair streams will share bandwidth with other
    /// streams of the same sendOrder group (or the unordered group).  Unfair streams
    /// will give bandwidth preferentially to the lowest streamId with data to send.
    ///
    /// # Errors
    ///
    /// Returns `InvalidStreamId` if the stream id doesn't exist
    pub fn stream_set_fairness(
        conn: &mut Connection,
        stream_id: StreamId,
        fairness: bool,
    ) -> Res<()> {
        conn.stream_fairness(stream_id, fairness)
            .map_err(|_| Error::InvalidStreamId)
    }

    pub fn cancel_fetch(
        &mut self,
        stream_id: StreamId,
        error: AppError,
        conn: &mut Connection,
    ) -> Res<()> {
        info!("[{self}] cancel_fetch {stream_id} error={error}");
        let send_stream = self.send_streams.get(&stream_id);
        let recv_stream = self.recv_streams.get(&stream_id);
        match (send_stream, recv_stream) {
            (None, None) => return Err(Error::InvalidStreamId),
            (Some(s), None) => {
                if !matches!(
                    s.stream_type(),
                    Http3StreamType::Http | Http3StreamType::ExtendedConnect
                ) {
                    return Err(Error::InvalidStreamId);
                }
                // Stream may be already be closed and we may get an error here, but we do not care.
                drop(self.stream_reset_send(conn, stream_id, error));
            }
            (None, Some(s)) => {
                if !matches!(
                    s.stream_type(),
                    Http3StreamType::Http | Http3StreamType::ExtendedConnect
                ) {
                    return Err(Error::InvalidStreamId);
                }

                // Stream may be already be closed and we may get an error here, but we do not care.
                drop(self.stream_stop_sending(conn, stream_id, error));
            }
            (Some(s), Some(r)) => {
                debug_assert_eq!(s.stream_type(), r.stream_type());
                if !matches!(
                    s.stream_type(),
                    Http3StreamType::Http | Http3StreamType::ExtendedConnect
                ) {
                    return Err(Error::InvalidStreamId);
                }
                // Stream may be already be closed and we may get an error here, but we do not care.
                drop(self.stream_reset_send(conn, stream_id, error));
                // Stream may be already be closed and we may get an error here, but we do not care.
                drop(self.stream_stop_sending(conn, stream_id, error));
            }
        }
        Ok(())
    }

    /// This is called when an application wants to close the sending side of a stream.
    pub fn stream_close_send(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        now: Instant,
    ) -> Res<()> {
        debug!("[{self}] Close the sending side for stream {stream_id}");
        debug_assert!(self.state.active());
        let send_stream = self
            .send_streams
            .get_mut(&stream_id)
            .ok_or(Error::InvalidStreamId)?;
        // The following function may return InvalidStreamId from the transport layer if the stream
        // has been closed already. It is ok to ignore it here.
        drop(send_stream.close(conn, now));
        if send_stream.done() {
            self.remove_send_stream(stream_id, conn);
        } else if send_stream.has_data_to_send() {
            self.streams_with_pending_data.insert(stream_id);
        }
        Ok(())
    }

    pub fn extended_connect_create_session<T>(
        &mut self,
        conn: &mut Connection,
        events: Box<dyn ExtendedConnectEvents>,
        target: &T,
        headers: &[Header],
        connect_type: ExtendedConnectType,
    ) -> Res<StreamId>
    where
        T: RequestTarget,
    {
        let request = RequestDescription::new(
            "CONNECT",
            target,
            headers,
            Some(ConnectType::Extended(connect_type)),
            Priority::default(),
        )?;
        let id = self.create_bidi_transport_stream(conn)?;

        let extended_conn = Rc::new(RefCell::new(extended_connect::session::Session::new(
            id,
            events,
            self.role,
            Rc::clone(&self.qpack_encoder),
            Rc::clone(&self.qpack_decoder),
            connect_type,
        )));
        self.add_streams(
            id,
            Box::new(Rc::clone(&extended_conn)),
            Box::new(Rc::clone(&extended_conn)),
        );

        extended_conn
            .borrow_mut()
            .send_request(&request.headers, conn)?;
        self.streams_with_pending_data.insert(id);
        Ok(id)
    }

    pub(crate) fn extended_connect_session_accept(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        events: Box<dyn ExtendedConnectEvents>,
        accept_res: &SessionAcceptAction,
        connect_type: ExtendedConnectType,
        now: Instant,
    ) -> Res<()> {
        let mut recv_stream = self.recv_streams.get_mut(&stream_id);
        if let Some(r) = &mut recv_stream
            && !r
                .http_stream()
                .ok_or(Error::InvalidStreamId)?
                .extended_connect_wait_for_response()
        {
            return Err(Error::InvalidStreamId);
        }

        let send_stream = self.send_streams.get_mut(&stream_id);
        conn.stream_keep_alive(stream_id, true)?;

        match (send_stream, recv_stream, accept_res) {
            (None, None, _) => Err(Error::InvalidStreamId),
            (None, Some(_), _) | (Some(_), None, _) => {
                // Stream is in an inconsistent state (one direction exists, the other doesn't).
                self.cancel_fetch(stream_id, Error::HttpRequestRejected.code(), conn)?;
                Err(Error::InvalidState)
            }
            (Some(s), Some(_r), SessionAcceptAction::Reject(headers)) => {
                let sent = s
                    .http_stream()
                    .ok_or(Error::InvalidStreamId)?
                    .send_headers(headers, conn);
                if sent.is_ok() {
                    drop(self.stream_close_send(conn, stream_id, now));
                    // TODO issue 1294: add a timer to clean up the recv_stream if the peer does not
                    // do that in a short time.
                    self.streams_with_pending_data.insert(stream_id);
                } else {
                    self.reject_extended_connect(conn, stream_id);
                }
                sent
            }
            (Some(_), Some(_), SessionAcceptAction::Accept) => {
                self.do_accept_extended_connect(conn, stream_id, events, connect_type, &[])
            }
            (Some(_), Some(_), SessionAcceptAction::AcceptWith(extra)) => {
                self.do_accept_extended_connect(conn, stream_id, events, connect_type, extra)
            }
        }
    }

    fn do_accept_extended_connect(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
        events: Box<dyn ExtendedConnectEvents>,
        connect_type: ExtendedConnectType,
        extra_headers: &[Header],
    ) -> Res<()> {
        let mut response_headers = vec![Header::new(":status", "200")];
        if connect_type == ExtendedConnectType::ConnectUdp {
            response_headers.push(Header::new("capsule-protocol", "?1"));
        }
        response_headers.extend_from_slice(extra_headers);

        let sent = self
            .send_streams
            .get_mut(&stream_id)
            .ok_or(Error::InvalidStreamId)?
            .http_stream()
            .ok_or(Error::InvalidStreamId)?
            .send_headers(&response_headers, conn);
        if sent.is_ok() {
            let extended_conn = Rc::new(RefCell::new(
                extended_connect::session::Session::new_with_http_streams(
                    stream_id,
                    events,
                    self.role,
                    self.recv_streams
                        .remove(&stream_id)
                        .ok_or(Error::Internal)?,
                    self.send_streams
                        .remove(&stream_id)
                        .ok_or(Error::Internal)?,
                    connect_type,
                )?,
            ));
            self.add_streams(
                stream_id,
                Box::new(Rc::clone(&extended_conn)),
                Box::new(extended_conn),
            );
            self.streams_with_pending_data.insert(stream_id);
        } else {
            self.reject_extended_connect(conn, stream_id);
            return sent;
        }
        Ok(())
    }

    /// Unlike `cancel_fetch`, this allows the send and receive streams to still differ in type.
    fn reject_extended_connect(&mut self, conn: &mut Connection, stream_id: StreamId) {
        let error = Error::HttpRequestRejected.code();
        if let Some(mut s) = self.send_streams.remove(&stream_id) {
            s.handle_stop_sending(CloseType::ResetApp(error));
        }
        if let Some(mut s) = self.recv_streams.remove(&stream_id) {
            drop(s.reset(CloseType::ResetApp(error)));
        }
        drop(conn.stream_reset_send(stream_id, error));
        drop(conn.stream_stop_sending(stream_id, error));
    }

    /// Invoked when GOAWAY is received. It flags all open WebTransport
    /// sessions as draining and returns any sessions that were newly
    /// marked as draining
    pub(crate) fn drain_webtransport_sessions(&self) -> impl Iterator<Item = StreamId> + use<'_> {
        self.recv_streams.iter().filter_map(|(id, s)| {
            let sess = s.extended_connect_session()?;
            let mut s = sess.borrow_mut();
            (s.connect_type() == ExtendedConnectType::WebTransport && s.set_draining())
                .then_some(*id)
        })
    }

    /// Get the negotiated protocol for a WebTransport session.
    ///
    /// Returns `Ok(None)` if no protocol was negotiated.
    /// Returns an error if the session does not exist or is not an extended CONNECT session.
    pub(crate) fn webtransport_session_protocol(
        &self,
        session_id: StreamId,
    ) -> Res<Option<String>> {
        let stream = self
            .send_streams
            .get(&session_id)
            .filter(|s| s.stream_type() == Http3StreamType::ExtendedConnect)
            .ok_or(Error::InvalidStreamId)?;
        Ok(stream.session_protocol())
    }

    /// Mint a connection-unique send-group ID and register it with a WebTransport session.
    ///
    /// # Errors
    /// Returns error if session doesn't exist or is not a WebTransport session.
    pub(crate) fn webtransport_create_send_group(
        &mut self,
        session_id: StreamId,
    ) -> Res<SendGroupId> {
        let group_id = self.send_group_generator.next_id();
        self.recv_streams
            .get_mut(&session_id)
            .filter(|s| s.stream_type() == Http3StreamType::ExtendedConnect)
            .ok_or(Error::InvalidStreamId)?
            .register_send_group(group_id)?;
        Ok(group_id)
    }

    /// Validate that a send group belongs to the specified WebTransport session.
    ///
    /// # Errors
    /// Returns error if session doesn't exist or is not a WebTransport session.
    pub(crate) fn webtransport_validate_send_group(
        &self,
        session_id: StreamId,
        group_id: SendGroupId,
    ) -> Res<bool> {
        self.recv_streams
            .get(&session_id)
            .filter(|s| s.stream_type() == Http3StreamType::ExtendedConnect)
            .map(|s| s.validate_send_group(group_id))
            .ok_or(Error::InvalidStreamId)
    }

    /// Look up a `WebTransport` session by its CONNECT stream ID.
    ///
    /// # Errors
    /// Returns `InvalidStreamId` if the session doesn't exist or is not a `WebTransport`
    /// session. Extended CONNECT sessions of other protocols (connect-udp) share the same
    /// stream tables, so every `webtransport_*` entry point must filter on the type.
    pub(crate) fn webtransport_session(
        &self,
        session_id: StreamId,
    ) -> Res<Rc<RefCell<extended_connect::session::Session>>> {
        let session = self.get_extended_connect_session(session_id)?;
        let is_webtransport = session.borrow().connect_type() == ExtendedConnectType::WebTransport;
        if is_webtransport {
            Ok(session)
        } else {
            Err(Error::InvalidStreamId)
        }
    }

    /// Get statistics for a WebTransport session.
    ///
    /// # Errors
    /// Returns error if session doesn't exist or is not a WebTransport session.
    pub(crate) fn webtransport_session_stats(
        &self,
        session_id: StreamId,
    ) -> Res<extended_connect::stats::SessionStats> {
        let Some(stats) = self.webtransport_session(session_id)?.borrow().stats() else {
            debug_assert!(false, "a WebTransport session should always have stats");
            return Err(Error::InvalidStreamId);
        };
        Ok(stats)
    }

    /// Recompute the connection's incoming limit for `stream_type` as the sum over open
    /// `WebTransport` sessions; see
    /// [`crate::webtransport::ClientSession::webtransport_set_anticipated_incoming_uni_streams`].
    /// Walks every receive stream: call per session, not per stream.
    ///
    /// # Errors
    /// Returns `InvalidStreamId` if the session does not exist, is not a `WebTransport`
    /// session, or is already closing.
    pub(crate) fn webtransport_set_anticipated_incoming_streams(
        &self,
        conn: &mut Connection,
        session_id: StreamId,
        stream_type: StreamType,
        value: u16,
    ) -> Res<()> {
        let session = self.webtransport_session(session_id)?;
        if session.borrow().is_closing() {
            return Err(Error::InvalidStreamId);
        }
        session
            .borrow_mut()
            .set_anticipated_incoming(stream_type, value);

        let total: u64 = self
            .recv_streams
            .values()
            .filter_map(|s| {
                let session = s.extended_connect_session()?;
                let session = session.borrow();
                (!session.is_closing()
                    && session.connect_type() == ExtendedConnectType::WebTransport)
                    .then(|| u64::from(session.anticipated_incoming(stream_type)))
            })
            .sum::<u64>()
            .min(MAX_ANTICIPATED_INCOMING_STREAMS);

        let total_including_h3 = match stream_type {
            // HTTP/3 opens its own unidirectional streams (control and the two QPACK
            // streams) out of the same limit, so they have to be added on top.
            StreamType::UniDi => total + HTTP3_UNI_CONTROL_STREAMS,
            StreamType::BiDi => total,
        };
        debug!(
            "[{self}] Session {session_id} anticipates {value} incoming {stream_type:?} streams, connection total {total_including_h3}"
        );
        conn.set_remote_max_streams(stream_type, total_including_h3);
        Ok(())
    }

    pub(crate) fn extended_connect_close_session(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        connect_type: ExtendedConnectType,
        error: u32,
        message: &str,
        now: Instant,
    ) -> Res<Rc<RefCell<extended_connect::session::Session>>> {
        let session = self.get_extended_connect_session(session_id)?;
        if session.borrow().connect_type() != connect_type {
            return Err(Error::InvalidStreamId);
        }
        let send_stream = self
            .send_streams
            .get_mut(&session_id)
            .filter(|s| s.stream_type() == Http3StreamType::ExtendedConnect)
            .ok_or(Error::InvalidStreamId)?;

        send_stream.close_with_message(conn, error, message, now)?;
        if send_stream.done() {
            self.remove_send_stream(session_id, conn);
        } else if send_stream.has_data_to_send() {
            self.streams_with_pending_data.insert(session_id);
        }
        Ok(session)
    }

    fn get_extended_connect_session(
        &self,
        session_id: StreamId,
    ) -> Res<Rc<RefCell<extended_connect::session::Session>>> {
        self.recv_streams
            .get(&session_id)
            .ok_or(Error::InvalidStreamId)?
            .extended_connect_session()
            .ok_or(Error::InvalidStreamId)
    }

    pub(crate) fn validate_extended_connect_session(
        &self,
        session_id: StreamId,
    ) -> Res<Rc<RefCell<extended_connect::session::Session>>> {
        let wt = self.get_extended_connect_session(session_id)?;
        if !wt.borrow().is_active() {
            return Err(Error::InvalidStreamId);
        }
        Ok(wt)
    }

    pub(crate) fn webtransport_create_stream_local(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        stream_type: StreamType,
        send_events: Box<dyn SendStreamEvents>,
        recv_events: Box<dyn RecvStreamEvents>,
    ) -> Res<StreamId> {
        self.webtransport_create_stream_local_with_send_group(
            conn,
            session_id,
            stream_type,
            send_events,
            recv_events,
            None,
        )
    }

    pub(crate) fn webtransport_create_stream_local_with_send_group(
        &mut self,
        conn: &mut Connection,
        session_id: StreamId,
        stream_type: StreamType,
        send_events: Box<dyn SendStreamEvents>,
        recv_events: Box<dyn RecvStreamEvents>,
        send_group: Option<SendGroupId>,
    ) -> Res<StreamId> {
        trace!(
            "Create new WebTransport stream session={session_id} type={stream_type:?} send_group={send_group:?}"
        );

        let wt = self.validate_extended_connect_session(session_id)?;

        if let Some(group_id) = send_group
            && !self.webtransport_validate_send_group(session_id, group_id)?
        {
            return Err(Error::InvalidState);
        }

        let stream_id = conn.stream_create(stream_type)?;
        // Set outgoing WebTransport streams to be fair (share bandwidth)
        conn.stream_fairness(stream_id, true)?;
        // Register the stream with its send group in the transport scheduler so that
        // sendOrder is evaluated per-group and groups get fair bandwidth allocation.
        if let Some(group_id) = send_group {
            conn.stream_sendgroup(stream_id, Some(group_id))?;
        }

        self.webtransport_create_stream_internal(
            wt,
            stream_id,
            (send_events, recv_events),
            true,
            send_group,
        )?;
        Ok(stream_id)
    }

    pub(crate) fn webtransport_create_stream_remote(
        &mut self,
        session_id: StreamId,
        stream_id: StreamId,
        send_events: Box<dyn SendStreamEvents>,
        recv_events: Box<dyn RecvStreamEvents>,
    ) -> Res<()> {
        trace!("Create new WebTransport stream session={session_id} stream_id={stream_id}");

        let wt = self.get_extended_connect_session(session_id)?;

        self.webtransport_create_stream_internal(
            wt,
            stream_id,
            (send_events, recv_events),
            false,
            None,
        )?;
        Ok(())
    }

    fn webtransport_create_stream_internal(
        &mut self,
        webtransport_session: Rc<RefCell<extended_connect::session::Session>>,
        stream_id: StreamId,
        events: (Box<dyn SendStreamEvents>, Box<dyn RecvStreamEvents>),
        local: bool,
        send_group: Option<SendGroupId>,
    ) -> Res<()> {
        if webtransport_session.borrow().connect_type() != ExtendedConnectType::WebTransport {
            return Err(Error::InvalidStreamId);
        }
        let (send_events, recv_events) = events;
        webtransport_session.borrow_mut().add_stream(stream_id)?;
        if stream_id.stream_type() == StreamType::UniDi {
            if local {
                self.send_streams.insert(
                    stream_id,
                    Box::new(WebTransportSendStream::new(
                        stream_id,
                        send_events,
                        webtransport_session,
                        true,
                        send_group,
                    )),
                );
            } else {
                self.recv_streams.insert(
                    stream_id,
                    Box::new(WebTransportRecvStream::new(
                        stream_id,
                        recv_events,
                        webtransport_session,
                    )),
                );
            }
        } else {
            self.add_streams(
                stream_id,
                Box::new(WebTransportSendStream::new(
                    stream_id,
                    send_events,
                    Rc::clone(&webtransport_session),
                    local,
                    send_group,
                )),
                Box::new(WebTransportRecvStream::new(
                    stream_id,
                    recv_events,
                    webtransport_session,
                )),
            );
        }
        Ok(())
    }

    #[expect(
        clippy::too_many_arguments,
        reason = "mirrors send_datagram's own params plus session_id/conn"
    )]
    pub(crate) fn extended_connect_send_datagram<I: Into<DatagramTracking>>(
        &self,
        session_id: StreamId,
        conn: &mut Connection,
        buf: &[u8],
        id: I,
        now: Instant,
        send_group_id: SendGroupId,
        send_order: SendOrder,
    ) -> Res<DatagramQueueOutcome> {
        self.validate_extended_connect_session(session_id)?
            .borrow_mut()
            .send_datagram(conn, buf, id, now, send_group_id, send_order)
    }

    pub(crate) fn webtransport_session_set_max_buffered_datagrams(
        &self,
        conn: &mut Connection,
        session_id: StreamId,
        mark: Option<NonZeroUsize>,
    ) -> Res<()> {
        self.webtransport_session(session_id)?
            .borrow()
            .set_max_buffered_datagrams(conn, mark);
        Ok(())
    }

    pub(crate) fn webtransport_session_set_datagram_max_age(
        &self,
        conn: &mut Connection,
        session_id: StreamId,
        max_age: Option<Duration>,
        now: Instant,
    ) -> Res<()> {
        self.webtransport_session(session_id)?
            .borrow_mut()
            .set_datagram_max_age(conn, max_age, now);
        Ok(())
    }

    /// See [`crate::webtransport::ClientSession::webtransport_datagram_queue_capacity`].
    ///
    /// # Errors
    /// Returns `InvalidStreamId` if the session does not exist or is not a `WebTransport`
    /// session.
    pub(crate) fn webtransport_datagram_queue_capacity(
        &self,
        conn: &Connection,
        session_id: StreamId,
    ) -> Res<DatagramQueueCapacity> {
        Ok(self
            .webtransport_session(session_id)?
            .borrow()
            .datagram_queue_capacity(conn))
    }

    /// Record expired, sent and too-big datagram counts for each active
    /// extended-CONNECT session.  Returns the number expired.
    pub(crate) fn expire_datagram_queues(&self, conn: &mut Connection, now: Instant) -> u64 {
        self.recv_streams
            .values()
            .filter_map(|s| s.extended_connect_session())
            .filter(|s| s.borrow().is_active())
            .map(|s| {
                let mut s = s.borrow_mut();
                s.report_sent_datagrams(conn);
                s.expire_datagrams(conn, now)
            })
            .sum()
    }

    /// Frames whose handling is specific to the client and server (`Goaway`,
    /// `PriorityUpdateRequest`, and the unsupported server-push control frames) are given to the
    /// specific client/server handler.
    fn handle_control_frame(&mut self, conn: &Connection, f: HFrame) -> Res<Option<HFrame>> {
        debug!("[{self}] Handle a control frame {f:?}");
        if !matches!(f, HFrame::Settings { .. })
            && !matches!(
                self.settings_state,
                Http3RemoteSettingsState::Received { .. }
            )
        {
            return Err(Error::HttpMissingSettings);
        }
        match f {
            HFrame::Settings { settings } => {
                self.handle_settings(conn, settings)?;
                Ok(None)
            }
            HFrame::Goaway { .. }
            | HFrame::PriorityUpdateRequest { .. }
            | HFrame::CancelPush
            | HFrame::MaxPushId
            | HFrame::PriorityUpdatePush => Ok(Some(f)),
            _ => Err(Error::HttpFrameUnexpected),
        }
    }

    fn set_qpack_settings(&self, settings: &HSettings) -> Res<()> {
        let mut qpe = self.qpack_encoder.borrow_mut();
        qpe.set_max_capacity(settings.get(HSettingType::MaxTableCapacity))?;
        qpe.set_max_blocked_streams(settings.get(HSettingType::BlockedStreams))?;
        Ok(())
    }

    fn handle_settings(&mut self, conn: &Connection, new_settings: HSettings) -> Res<()> {
        debug!("[{self}] Handle SETTINGS frame");
        let prereqs = TransportPrerequisites::new(
            conn.remote_datagram_size() > 0,
            conn.peer_supports_reliable_stream_reset(),
        );
        match &self.settings_state {
            Http3RemoteSettingsState::NotReceived => {
                self.set_qpack_settings(&new_settings)?;
                self.webtransport.handle_settings(&new_settings, &prereqs);
                self.connect_udp.handle_settings(&new_settings, &prereqs);
                self.settings_state = Http3RemoteSettingsState::Received(new_settings);
                Ok(())
            }
            Http3RemoteSettingsState::ZeroRtt(settings) => {
                self.webtransport.handle_settings(&new_settings, &prereqs);
                self.connect_udp.handle_settings(&new_settings, &prereqs);
                let mut qpack_changed = false;
                for st in &[
                    HSettingType::MaxHeaderListSize,
                    HSettingType::MaxTableCapacity,
                    HSettingType::BlockedStreams,
                    HSettingType::EnableConnect,
                    // [RFC 9297, Section 2.1.1](https://www.rfc-editor.org/rfc/rfc9297.html#section-2.1.1)
                    // requires a client that stored SETTINGS_H3_DATAGRAM with its
                    // 0-RTT state to terminate the connection with H3_SETTINGS_ERROR
                    // if the server's new value is smaller than the stored one.
                    // [draft-ietf-webtrans-http3, Section 3.2](https://datatracker.ietf.org/doc/html/draft-ietf-webtrans-http3#section-3.2)
                    // states the same rule for the WebTransport setting.
                    HSettingType::EnableWebTransport,
                    HSettingType::EnableH3Datagram,
                ] {
                    let zero_rtt_value = settings.get(*st);
                    let new_value = new_settings.get(*st);
                    if zero_rtt_value == new_value {
                        continue;
                    }
                    if zero_rtt_value > new_value {
                        error!(
                            "[{self}] The new({new_value}) and the old value({zero_rtt_value}) of setting {st:?} do not match"
                        );
                        return Err(Error::HttpSettings);
                    }

                    match st {
                        HSettingType::MaxTableCapacity => {
                            if zero_rtt_value != 0 {
                                return Err(Error::Qpack(neqo_qpack::Error::DecoderStream));
                            }
                            qpack_changed = true;
                        }
                        HSettingType::BlockedStreams => qpack_changed = true,
                        HSettingType::MaxHeaderListSize
                        | HSettingType::EnableWebTransport
                        | HSettingType::EnableH3Datagram
                        | HSettingType::EnableConnect => (),
                    }
                }
                if qpack_changed {
                    debug!("[{self}] Settings after zero rtt differ");
                    self.set_qpack_settings(&(new_settings))?;
                }
                self.settings_state = Http3RemoteSettingsState::Received(new_settings);
                Ok(())
            }
            Http3RemoteSettingsState::Received { .. } => Err(Error::HttpFrameUnexpected),
        }
    }

    /// Adds a new send and receive stream.
    pub(crate) fn add_streams(
        &mut self,
        stream_id: StreamId,
        send_stream: Box<dyn SendStream>,
        recv_stream: Box<dyn RecvStream>,
    ) {
        if send_stream.has_data_to_send() {
            self.streams_with_pending_data.insert(stream_id);
        }
        self.send_streams.insert(stream_id, send_stream);
        self.recv_streams.insert(stream_id, recv_stream);
    }

    pub(crate) fn queue_control_frame(&mut self, frame: &HFrame) {
        self.control_stream_local.queue_frame(frame);
    }

    pub(crate) fn queue_update_priority(
        &mut self,
        stream_id: StreamId,
        priority: Priority,
    ) -> Res<bool> {
        let stream = self
            .recv_streams
            .get_mut(&stream_id)
            .ok_or(Error::InvalidStreamId)?
            .http_stream()
            .ok_or(Error::InvalidStreamId)?;

        if stream.maybe_update_priority(priority)? {
            self.control_stream_local.queue_update_priority(stream_id);
            Ok(true)
        } else {
            Ok(false)
        }
    }

    fn recv_stream_is_critical(&self, stream_id: StreamId) -> bool {
        self.recv_streams.get(&stream_id).is_some_and(|r| {
            matches!(
                r.stream_type(),
                Http3StreamType::Control | Http3StreamType::Encoder | Http3StreamType::Decoder
            )
        })
    }

    fn send_stream_is_critical(&self, stream_id: StreamId) -> bool {
        self.qpack_encoder
            .borrow()
            .local_stream_id()
            .iter()
            .chain(self.qpack_decoder.borrow().local_stream_id().iter())
            .chain(self.control_stream_local.stream_id().iter())
            .any(|id| stream_id == *id)
    }

    fn close_send(&mut self, stream_id: StreamId, close_type: CloseType, conn: &mut Connection) {
        if let Some(mut s) = self.remove_send_stream(stream_id, conn) {
            s.handle_stop_sending(close_type);
        }
    }

    fn close_recv(
        &mut self,
        stream_id: StreamId,
        close_type: CloseType,
        conn: &mut Connection,
    ) -> Res<()> {
        if let Some(mut s) = self.remove_recv_stream(stream_id, conn) {
            s.reset(close_type)?;
        }
        Ok(())
    }

    fn remove_extended_connect(
        &mut self,
        wt: &Rc<RefCell<extended_connect::session::Session>>,
        conn: &mut Connection,
    ) {
        wt.borrow_mut().drop_queued_datagrams(conn);

        let (recv, send) = wt.borrow_mut().take_sub_streams();

        #[expect(
            clippy::iter_over_hash_type,
            reason = "OK to loop over active streams in an undefined order."
        )]
        for id in recv {
            trace!("Remove the extended connect sub receiver stream {id}");
            // Use CloseType::ResetRemote so that an event will be sent. CloseType::LocalError would
            // have the same effect.
            if let Some(mut s) = self.recv_streams.remove(&id) {
                drop(s.reset(CloseType::ResetRemote(Error::HttpRequestCancelled.code())));
            }
            drop(conn.stream_stop_sending(id, Error::HttpRequestCancelled.code()));
        }
        #[expect(
            clippy::iter_over_hash_type,
            reason = "OK to loop over active streams in an undefined order."
        )]
        for id in send {
            trace!("Remove the extended connect sub send stream {id}");
            if let Some(mut s) = self.send_streams.remove(&id) {
                s.handle_stop_sending(CloseType::ResetRemote(Error::HttpRequestCancelled.code()));
            }
            drop(conn.stream_reset_send(id, Error::HttpRequestCancelled.code()));
        }
    }

    fn remove_recv_stream(
        &mut self,
        stream_id: StreamId,
        conn: &mut Connection,
    ) -> Option<Box<dyn RecvStream>> {
        let stream = self.recv_streams.remove(&stream_id);
        if let Some(s) = &stream
            && s.stream_type() == Http3StreamType::ExtendedConnect
        {
            self.send_streams.remove(&stream_id)?;
            if let Some(wt) = s.extended_connect_session() {
                self.remove_extended_connect(&wt, conn);
            }
        }
        stream
    }

    fn remove_send_stream(
        &mut self,
        stream_id: StreamId,
        conn: &mut Connection,
    ) -> Option<Box<dyn SendStream>> {
        let stream = self.send_streams.remove(&stream_id);
        if let Some(s) = &stream
            && s.stream_type() == Http3StreamType::ExtendedConnect
            && let Some(wt) = self
                .recv_streams
                .remove(&stream_id)?
                .extended_connect_session()
        {
            self.remove_extended_connect(&wt, conn);
        }
        stream
    }

    pub const fn webtransport_enabled(&self) -> bool {
        self.webtransport.enabled()
    }

    pub const fn connect_udp_enabled(&self) -> bool {
        self.connect_udp.enabled()
    }

    #[must_use]
    pub const fn state(&self) -> &Http3State {
        &self.state
    }

    pub fn set_state(&mut self, state: Http3State) {
        self.state = state;
    }

    #[must_use]
    pub const fn state_mut(&mut self) -> &mut Http3State {
        &mut self.state
    }

    #[must_use]
    pub const fn qpack_encoder(&self) -> &Rc<RefCell<qpack::Encoder>> {
        &self.qpack_encoder
    }

    #[must_use]
    pub const fn qpack_decoder(&self) -> &Rc<RefCell<qpack::Decoder>> {
        &self.qpack_decoder
    }

    #[must_use]
    pub fn send_streams(&self) -> &HashMap<StreamId, Box<dyn SendStream>> {
        &self.send_streams
    }

    #[must_use]
    pub fn send_streams_mut(&mut self) -> &mut HashMap<StreamId, Box<dyn SendStream>> {
        &mut self.send_streams
    }

    #[must_use]
    pub fn recv_streams(&self) -> &HashMap<StreamId, Box<dyn RecvStream>> {
        &self.recv_streams
    }

    #[must_use]
    pub fn recv_streams_mut(&mut self) -> &mut HashMap<StreamId, Box<dyn RecvStream>> {
        &mut self.recv_streams
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use http::Uri;

    use crate::{Error, Priority, connection::RequestDescription, features::ConnectType};

    #[test]
    fn request_description_connect_without_connect_type() {
        let request = RequestDescription::new(
            "CONNECT",
            &Uri::from_static("https://example.com"),
            &[],
            None,
            Priority::default(),
        );
        assert_eq!(request, Err(Error::InvalidInput));
    }

    #[test]
    fn request_description_connect_type_without_connect() {
        let request = RequestDescription::new(
            "GET",
            &Uri::from_static("https://example.com"),
            &[],
            Some(ConnectType::Classic),
            Priority::default(),
        );
        assert_eq!(request, Err(Error::InvalidInput));
    }
}
