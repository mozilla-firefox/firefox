// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![cfg_attr(coverage_nightly, feature(coverage_attribute))]

/*!
# The HTTP/3 protocol

This crate implements [RFC9114](https://datatracker.ietf.org/doc/html/rfc9114).

The implementation depends on:
 - [neqo-transport](../neqo_transport/index.html) --- implements the QUIC protocol
   ([RFC9000](https://www.rfc-editor.org/info/rfc9000)) and
 - [neqo-qpack](../neqo_qpack/index.html) --- implements QPACK
   ([RFC9204](https://www.rfc-editor.org/info/rfc9204));

## Features

Both client and server-side HTTP/3 protocols are implemented, although the server-side
implementation is not meant to be used in production and its only purpose is to facilitate testing
of the client-side code.

__`WebTransport`__
WebTransport is supported
and can be enabled using [`Http3Parameters`](struct.Http3Parameters.html).

## Interaction with an application

### Driving HTTP/3  session

The crate does not create an OS level UDP socket, it produces, i.e. encodes, data that should be
sent as a payload in a UDP packet and consumes data received on the UDP socket. For example,
[`std::net::UdpSocket`] or [`mio::net::UdpSocket`](https://crates.io/crates/mio)
could be used for creating UDP sockets.

The application is responsible for creating a socket, polling the socket, and sending and receiving
data from the socket.

In addition to receiving data HTTP/3 session’s actions may be triggered when a certain amount of
time passes, e.g. after a certain amount of time data may be considered lost and should be
retransmitted, packet pacing requires a timer, etc. The implementation does not use timers, but
instead informs the application when processing needs to be triggered.


The core functions for driving HTTP/3 sessions are:
 - __On the client-side__ :
   - [`process_output`](struct.Http3Client.html#method.process_output) used for producing UDP
     payload. If a payload is not produced this function returns a callback time, e.g. the time when
     [`process_output`](struct.Http3Client.html#method.process_output) should be called again.
   - [`process_input`](struct.Http3Client.html#method.process_input)  used consuming UDP payload.
   - [`process`](struct.Http3Client.html#method.process) combines the 2 functions into one, i.e. it
     consumes UDP payload if available and produces some UDP payload to be sent or returns a
     callback time.
- __On the server-side__ only [`process`](struct.Http3Server.html#method.process) is
  available.

An example interaction with a socket:

```ignore
let socket = match UdpSocket::bind(local_addr) {
    Err(e) => {
        eprintln!("Unable to bind UDP socket: {}", e);
    }
    Ok(s) => s,
};
let mut client = Http3Client::new(...);

...

// process_output can return 3 values, data to be sent, time duration when process_output should
// be called, and None when Http3Client is done.
match client.process_output(Instant::now()) {
    Output::Datagram(dgram) => {
        // Send dgram on a socket.
        socket.send_to(&dgram[..], dgram.destination())

    }
    Output::Callback(duration) => {
        // the client is idle for “duration”, set read timeout on the socket to this value and
        // poll the socket for reading in the meantime.
        socket.set_read_timeout(Some(duration)).unwrap();
    }
    Output::None => {
        // client is done.
    }
};

...

// Reading new data coming for the network.
match socket.recv_from(&mut buf[..]) {
     Ok((sz, remote)) => {
        let d = Datagram::new(remote, *local_addr, &buf[..sz]);
        client.process_input(d, Instant::now());
    }
    Err(err) => {
         eprintln!("UDP error: {}", err);
    }
}
 ```

### HTTP/3 session events

[`Http3Client`](struct.Http3Client.html) and [`Http3Server`](struct.Http3Server.html) produce
events that can be obtain by calling
[`next_event`](neqo_common/event/trait.Provider.html#tymethod.next_event). The events are of type
[`Http3ClientEvent`](enum.Http3ClientEvent.html) and
[`Http3ServerEvent`](enum.Http3ServerEvent.html) respectively. They are informing the application
when the connection changes state, when new data is received on a stream, etc.

```ignore
...

while let Some(event) = client.next_event() {
    match event {
        Http3ClientEvent::DataReadable { stream_id } => {
            println!("New data available on stream {}", stream_id);
        }
        Http3ClientEvent::StateChange(Http3State::Connected) => {
            println!("Http3 session is in state Connected now");
        }
        _ => {
            println!("Unhandled event {:?}", event);
        }
    }
}
```



*/

mod buffered_send_stream;
mod client_events;
mod conn_params;
pub mod connect_udp;
mod connection;
mod connection_client;
mod connection_server;
mod control_stream_local;
mod control_stream_remote;
pub mod features;
#[cfg(fuzzing)]
pub mod frames;
#[cfg(not(fuzzing))]
mod frames;
mod headers_checks;
mod priority;
mod qlog;
mod qpack_decoder_receiver;
mod qpack_encoder_receiver;
mod recv_message;
mod request_target;
mod send_message;
mod server;
mod server_connection_events;
mod server_events;
mod settings;
mod stream_type_reader;
pub mod webtransport;

use std::{cell::RefCell, fmt::Debug, rc::Rc, time::Instant};

use buffered_send_stream::BufferedStream;
pub use client_events::{ConnectUdpEvent, Http3ClientEvent, WebTransportEvent};
pub use conn_params::Http3Parameters;
pub use connection::{Http3State, MAX_ANTICIPATED_INCOMING_STREAMS, SessionAcceptAction};
pub use connection_client::Http3Client;
use frames::HFrame;
pub use headers_checks::{
    connect_authority_valid, request_authority_valid, request_path_valid, request_scheme_valid,
};
pub use neqo_common::Header;
use neqo_common::MessageType;
use neqo_qpack::Error as QpackError;
use neqo_transport::{AppError, Connection, Error as TransportError, recv_stream, send_stream};
pub use neqo_transport::{
    Output, StreamId,
    streams::{SendGroupId, SendOrder},
};
pub use priority::Priority;
pub use server::Http3Server;
pub use server_events::{Http3OrWebTransportStream, Http3ServerEvent};
#[cfg(fuzzing)]
pub use settings::HSettings;
use stream_type_reader::NewStreamType;
use thiserror::Error;

use crate::{features::extended_connect, priority::PriorityHandler};

type Res<T> = Result<T, Error>;

#[derive(Clone, Debug, PartialEq, Eq, Error)]
pub enum Error {
    #[error("HTTP no error")]
    HttpNone,
    #[error("HTTP general protocol error")]
    HttpGeneralProtocol,
    // This is the same as the above but it should only close a stream not a
    // connection.
    #[error("HTTP protocol error on stream")]
    HttpGeneralProtocolStream,
    // When using this error, you need to provide a value that is unique, which
    // will allow the specific error to be identified.  This will be validated in CI.
    #[error("HTTP internal error: {0}")]
    HttpInternal(u16),
    #[error("HTTP stream creation error")]
    HttpStreamCreation,
    #[error("HTTP closed critical stream")]
    HttpClosedCriticalStream,
    #[error("HTTP unexpected frame")]
    HttpFrameUnexpected,
    #[error("HTTP frame error")]
    HttpFrame,
    #[error("HTTP excessive load")]
    HttpExcessiveLoad,
    #[error("HTTP ID error")]
    HttpId,
    #[error("HTTP settings error")]
    HttpSettings,
    #[error("HTTP missing settings")]
    HttpMissingSettings,
    #[error("HTTP request rejected")]
    HttpRequestRejected,
    #[error("HTTP request cancelled")]
    HttpRequestCancelled,
    #[error("HTTP request incomplete")]
    HttpRequestIncomplete,
    #[error("HTTP connect error")]
    HttpConnect,
    #[error("HTTP version fallback")]
    HttpVersionFallback,
    #[error("HTTP message error")]
    HttpMessage,
    #[error("QPACK error: {0}")]
    Qpack(#[source] neqo_qpack::Error),

    // Internal errors from here.
    #[error("Already closed")]
    AlreadyClosed,
    #[error("Already initialized")]
    AlreadyInitialized,
    #[error("Fatal error")]
    Fatal,
    #[error("Flow control limit reached")]
    FlowControlLimit,
    #[error("HTTP GOAWAY received")]
    HttpGoaway,
    #[error("Internal error")]
    Internal,
    #[error("Invalid header")]
    InvalidHeader,
    #[error("Invalid input")]
    InvalidInput,
    #[error("Invalid request target")]
    InvalidRequestTarget,
    #[error("Invalid resumption token")]
    InvalidResumptionToken,
    #[error("Invalid state")]
    InvalidState,
    #[error("Invalid stream ID")]
    InvalidStreamId,
    #[error("Not enough data")]
    NotEnoughData,
    #[error("Stream limit reached")]
    StreamLimit,
    #[error("Transport error: {0}")]
    Transport(#[source] TransportError),
    #[error("Transport stream does not exist")]
    TransportStreamDoesNotExist,
    #[error("Operation unavailable")]
    Unavailable,
    #[error("Unexpected condition")]
    Unexpected,
}

impl Error {
    #[must_use]
    pub const fn code(&self) -> AppError {
        match self {
            Self::HttpNone => 0x100,
            Self::HttpGeneralProtocol | Self::HttpGeneralProtocolStream => 0x101,
            Self::HttpInternal(..) => 0x102,
            Self::HttpStreamCreation => 0x103,
            Self::HttpClosedCriticalStream => 0x104,
            Self::HttpFrameUnexpected => 0x105,
            Self::HttpFrame => 0x106,
            Self::HttpExcessiveLoad => 0x107,
            Self::HttpId => 0x108,
            Self::HttpSettings => 0x109,
            Self::HttpMissingSettings => 0x10a,
            Self::HttpRequestRejected => 0x10b,
            Self::HttpRequestCancelled => 0x10c,
            Self::HttpRequestIncomplete => 0x10d,
            Self::HttpMessage | Self::InvalidHeader => 0x10e,
            Self::HttpConnect => 0x10f,
            Self::HttpVersionFallback => 0x110,
            Self::Qpack(e) => e.code(),
            // These are all internal errors.
            _ => 3,
        }
    }

    #[must_use]
    pub const fn connection_error(&self) -> bool {
        matches!(
            self,
            Self::HttpGeneralProtocol
                | Self::HttpInternal(..)
                | Self::HttpStreamCreation
                | Self::HttpClosedCriticalStream
                | Self::HttpFrameUnexpected
                | Self::HttpFrame
                | Self::HttpExcessiveLoad
                | Self::HttpId
                | Self::HttpSettings
                | Self::HttpMissingSettings
                | Self::Qpack(QpackError::EncoderStream | QpackError::DecoderStream)
        )
    }

    #[must_use]
    pub const fn stream_reset_error(&self) -> bool {
        matches!(self, Self::HttpGeneralProtocolStream | Self::InvalidHeader)
    }

    /// # Errors
    ///
    /// Any error is mapped to the indicated type.
    ///
    /// # Panics
    ///
    /// On internal errors, in debug mode.
    fn map_error<R>(r: Result<R, impl Into<Self>>, err: Self) -> Result<R, Self> {
        r.map_err(|e| {
            debug_assert!(!matches!(e.into(), Self::HttpInternal(..)));
            debug_assert!(!matches!(err, Self::HttpInternal(..)));
            err
        })
    }
}

impl From<QpackError> for Error {
    fn from(err: QpackError) -> Self {
        match err {
            QpackError::ClosedCriticalStream => Self::HttpClosedCriticalStream,
            e => Self::Qpack(e),
        }
    }
}

impl From<TransportError> for Error {
    fn from(err: TransportError) -> Self {
        match err {
            TransportError::InvalidStreamId | TransportError::FinalSize => {
                Self::TransportStreamDoesNotExist
            }
            TransportError::InvalidInput => Self::InvalidInput,
            TransportError::ConnectionState => Self::Unavailable,
            TransportError::StreamLimit => Self::StreamLimit,
            other => Self::Transport(other),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Http3StreamType {
    Control,
    Decoder,
    Encoder,
    NewStream,
    Http,
    ExtendedConnect,
    WebTransport(StreamId),
    Unknown,
}

#[must_use]
#[derive(Default, PartialEq, Eq, Debug)]
enum ReceiveOutput {
    #[default]
    NoOutput,
    ControlFrames(Vec<HFrame>),
    UnblockedStreams(Vec<StreamId>),
    NewStream(NewStreamType),
}

trait Stream: Debug {
    fn stream_type(&self) -> Http3StreamType;

    // Unreachable: callers filter by ExtendedConnect before calling.
    #[cfg_attr(coverage_nightly, coverage(off))]
    fn session_protocol(&self) -> Option<String> {
        None
    }

    #[cfg_attr(coverage_nightly, coverage(off))]
    fn register_send_group(&mut self, _id: SendGroupId) -> Res<()> {
        debug_assert!(
            false,
            "register_send_group called on a stream that does not support send groups"
        );
        Err(Error::InvalidStreamId)
    }

    #[cfg_attr(coverage_nightly, coverage(off))]
    fn validate_send_group(&self, _group_id: SendGroupId) -> bool {
        debug_assert!(
            false,
            "validate_send_group called on a stream that does not support send groups"
        );
        false
    }
}

trait RecvStream: Stream {
    /// The stream reads data from the corresponding quic stream and returns `ReceiveOutput`.
    /// The function also returns true as the second parameter if the stream is done and
    /// could be forgotten, i.e. removed from all records.
    ///
    /// # Errors
    ///
    /// An error may happen while reading a stream, e.g. early close, protocol error, etc.
    fn receive(&mut self, conn: &mut Connection, now: Instant) -> Res<(ReceiveOutput, bool)>;

    /// # Errors
    ///
    /// An error may happen while reading a stream, e.g. early close, etc.
    fn reset(&mut self, close_type: CloseType) -> Res<()>;

    /// The function allows an app to read directly from the quic stream. The function
    /// returns the number of bytes written into `buf` and true/false if the stream is
    /// completely done and can be forgotten, i.e. removed from all records.
    ///
    /// # Errors
    ///
    /// An error may happen while reading a stream, e.g. early close, protocol error, etc.
    fn read_data(
        &mut self,
        _conn: &mut Connection,
        _buf: &mut [u8],
        _now: Instant,
    ) -> Res<(usize, bool)> {
        Err(Error::InvalidStreamId)
    }

    fn http_stream(&mut self) -> Option<&mut dyn HttpRecvStream> {
        None
    }

    fn extended_connect_session(&self) -> Option<Rc<RefCell<extended_connect::session::Session>>> {
        None
    }

    /// This function is only implemented by `WebTransportRecvStream`.
    fn stats(&mut self, _conn: &mut Connection) -> Res<recv_stream::Stats> {
        Err(Error::Unavailable)
    }
}

trait HttpRecvStream: RecvStream {
    /// This function is similar to the receive function and has the same output, i.e.
    /// a `ReceiveOutput` enum and bool. The bool is true if the stream is completely done
    /// and can be forgotten, i.e. removed from all records.
    ///
    /// # Errors
    ///
    /// An error may happen while reading a stream, e.g. early close, protocol error, etc.
    fn header_unblocked(
        &mut self,
        conn: &mut Connection,
        now: Instant,
    ) -> Res<(ReceiveOutput, bool)>;

    fn maybe_update_priority(&mut self, priority: Priority) -> Res<bool>;
    fn priority_update_frame(&mut self) -> Option<HFrame>;
    fn priority_update_sent(&mut self) -> Res<()>;

    fn set_new_listener(&mut self, _conn_events: Box<dyn HttpRecvStreamEvents>) {}
    fn extended_connect_wait_for_response(&self) -> bool {
        false
    }
}

#[derive(Debug, PartialEq, Eq, Copy, Clone)]
pub struct Http3StreamInfo {
    stream_id: StreamId,
    stream_type: Http3StreamType,
}

impl Http3StreamInfo {
    #[must_use]
    pub const fn new(stream_id: StreamId, stream_type: Http3StreamType) -> Self {
        Self {
            stream_id,
            stream_type,
        }
    }

    #[must_use]
    pub const fn stream_id(&self) -> StreamId {
        self.stream_id
    }

    #[must_use]
    pub const fn session_id(&self) -> Option<StreamId> {
        if let Http3StreamType::WebTransport(session) = self.stream_type {
            Some(session)
        } else {
            None
        }
    }

    pub(crate) const fn stream_type(&self) -> Http3StreamType {
        self.stream_type
    }

    #[must_use]
    pub fn is_http(&self) -> bool {
        self.stream_type == Http3StreamType::Http
    }
}

trait RecvStreamEvents: Debug {
    fn data_readable(&self, _stream_info: &Http3StreamInfo) {}
    fn recv_closed(&self, _stream_info: &Http3StreamInfo, _close_type: CloseType) {}
}

trait HttpRecvStreamEvents: RecvStreamEvents {
    fn header_ready(
        &self,
        stream_info: &Http3StreamInfo,
        headers: Vec<Header>,
        interim: bool,
        fin: bool,
    );
    fn extended_connect_new_session(&self, _stream_id: StreamId, _headers: Vec<Header>) {}
}

trait SendStream: Stream {
    /// # Errors
    ///
    /// Error may occur during sending data, e.g. protocol error, etc.
    fn send(&mut self, conn: &mut Connection, now: Instant) -> Res<()>;
    fn has_data_to_send(&self) -> bool;
    fn stream_writable(&self);
    fn done(&self) -> bool;

    /// Commit to reliably delivering the data buffered so far, so that it is still delivered
    /// (via `RESET_STREAM_AT`) even if the stream is later reset. Implementations of this are
    /// expected to send all data they have buffered so the commitment covers everything written so
    /// far. The default implementation fails immediately.
    ///
    /// # Errors
    /// Transport errors (e.g. the peer did not enable reliable reset),
    /// or [`Error::FlowControlLimit`] if the buffered data could not be fully flushed.
    /// The latter should be rare as most cases of buffering should include a couple of bytes.
    fn commit(&mut self, _conn: &mut Connection, _now: Instant) -> Res<()> {
        Err(Error::Unavailable)
    }

    /// # Errors
    ///
    /// Error may occur during sending data, e.g. protocol error, etc.
    fn send_data(&mut self, _conn: &mut Connection, _buf: &[u8], now: Instant) -> Res<usize>;

    /// # Errors
    ///
    /// It may happen that the transport stream is already closed. This is unlikely.
    fn close(&mut self, conn: &mut Connection, now: Instant) -> Res<()>;

    /// # Errors
    ///
    /// It may happen that the transport stream is already closed. This is unlikely.
    fn close_with_message(
        &mut self,
        _conn: &mut Connection,
        _error: u32,
        _message: &str,
        _now: Instant,
    ) -> Res<()> {
        Err(Error::InvalidStreamId)
    }

    /// This function is called when sending side is closed abruptly by the peer or
    /// the application.
    fn handle_stop_sending(&mut self, close_type: CloseType);
    fn http_stream(&mut self) -> Option<&mut dyn HttpSendStream> {
        None
    }

    /// # Errors
    ///
    /// It may happen that the transport stream is already closed. This is unlikely.
    fn send_data_atomic(&mut self, _conn: &mut Connection, _buf: &[u8], _now: Instant) -> Res<()> {
        Err(Error::InvalidStreamId)
    }

    /// This function is only implemented by
    /// [`WebTransportSendStream`](crate::features::extended_connect::webtransport_streams::WebTransportSendStream).
    fn stats(&mut self, _conn: &mut Connection) -> Res<send_stream::Stats> {
        Err(Error::Unavailable)
    }

    /// This function is only implemented by
    /// [`WebTransportSendStream`](crate::features::extended_connect::webtransport_streams::WebTransportSendStream).
    #[cfg_attr(coverage_nightly, coverage(off))]
    fn set_send_group(&mut self, _send_group: SendGroupId) -> Res<()> {
        Err(Error::Unavailable)
    }
    /// This function is only implemented by
    /// [`WebTransportSendStream`](crate::features::extended_connect::webtransport_streams::WebTransportSendStream).
    #[cfg_attr(coverage_nightly, coverage(off))]
    fn clear_send_group(&mut self) -> Res<()> {
        Err(Error::Unavailable)
    }
}

trait HttpSendStream: SendStream {
    /// This function is used to supply headers to a http message. The
    /// function is used for request headers, response headers, 1xx response and
    /// trailers.
    ///
    /// # Errors
    ///
    /// This can also return an error if the underlying stream is closed.
    fn send_headers(&mut self, headers: &[Header], conn: &mut Connection) -> Res<()>;
    fn set_new_listener(&mut self, _conn_events: Box<dyn SendStreamEvents>) {}
}

trait SendStreamEvents: Debug {
    fn send_closed(&self, _stream_info: &Http3StreamInfo, _close_type: CloseType) {}
    fn data_writable(&self, _stream_info: &Http3StreamInfo) {}
}

/// This enum is used to mark a different type of closing a stream:
///   `ResetApp` - the application has closed the stream.
///   `ResetRemote` - the stream was closed by the peer.
///   `LocalError` - There was a stream error on the stream. The stream errors are errors
///                  that do not close the complete connection, e.g. unallowed headers.
///   `Done` - the stream was closed without an error.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum CloseType {
    ResetApp(AppError),
    ResetRemote(AppError),
    LocalError(AppError),
    Done,
}

impl CloseType {
    #[must_use]
    pub const fn error(&self) -> Option<AppError> {
        match self {
            Self::ResetApp(error) | Self::ResetRemote(error) | Self::LocalError(error) => {
                Some(*error)
            }
            Self::Done => None,
        }
    }

    #[must_use]
    pub const fn locally_initiated(&self) -> bool {
        matches!(self, Self::ResetApp(_))
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use neqo_transport::StreamId;

    use super::{Error, Http3StreamInfo, Http3StreamType};

    #[test]
    fn stream_info_is_http() {
        let http = Http3StreamInfo::new(StreamId::new(0), Http3StreamType::Http);
        assert!(http.is_http());

        let control = Http3StreamInfo::new(StreamId::new(2), Http3StreamType::Control);
        assert!(!control.is_http());

        let wt = Http3StreamInfo::new(
            StreamId::new(4),
            Http3StreamType::WebTransport(StreamId::new(0)),
        );
        assert!(!wt.is_http());
    }

    #[test]
    fn error_codes() {
        for (error, expected) in [
            (Error::HttpNone, 0x100),
            (Error::HttpGeneralProtocol, 0x101),
            (Error::HttpGeneralProtocolStream, 0x101),
            (Error::InvalidHeader, 0x10e),
            (Error::HttpInternal(0), 0x102),
            (Error::HttpStreamCreation, 0x103),
            (Error::HttpClosedCriticalStream, 0x104),
            (Error::HttpFrameUnexpected, 0x105),
            (Error::HttpFrame, 0x106),
            (Error::HttpExcessiveLoad, 0x107),
            (Error::HttpId, 0x108),
            (Error::HttpSettings, 0x109),
            (Error::HttpMissingSettings, 0x10a),
            (Error::HttpRequestRejected, 0x10b),
            (Error::HttpRequestCancelled, 0x10c),
            (Error::HttpRequestIncomplete, 0x10d),
            (Error::HttpMessage, 0x10e),
            (Error::HttpConnect, 0x10f),
            (Error::HttpVersionFallback, 0x110),
        ] {
            assert_eq!(error.code(), expected);
        }
    }

    #[test]
    fn error_mapping() {
        use neqo_transport::Error as Te;

        for (input, expected) in [
            (Te::InvalidStreamId, Error::TransportStreamDoesNotExist),
            (Te::FinalSize, Error::TransportStreamDoesNotExist),
            (Te::InvalidInput, Error::InvalidInput),
            (Te::ConnectionState, Error::Unavailable),
            (Te::StreamLimit, Error::StreamLimit),
            (Te::NotConnected, Error::Transport(Te::NotConnected)),
        ] {
            assert_eq!(Error::from(input), expected);
        }
    }
}
