// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{cell::RefCell, rc::Rc, time::Instant};

use neqo_common::{Encoder, to_u64};
use neqo_transport::{
    Connection, Error as TransportError, StreamId, recv_stream, send_stream, streams::SendGroupId,
};
use tracing::debug;

use super::session::Session;
use crate::{
    CloseType, Http3StreamInfo, Http3StreamType, ReceiveOutput, RecvStream, RecvStreamEvents, Res,
    SendStream, SendStreamEvents, Stream,
};

pub const WEBTRANSPORT_UNI_STREAM: u64 = 0x54;
pub const WEBTRANSPORT_STREAM: u64 = 0x41;

/// Size of the WebTransport preface (stream type + session ID varints).
const fn preface_len(stream_info: &Http3StreamInfo) -> u64 {
    const TYPE_LEN_UNI: usize = Encoder::varint_len(WEBTRANSPORT_UNI_STREAM);
    const TYPE_LEN_BIDI: usize = Encoder::varint_len(WEBTRANSPORT_STREAM);

    let Some(session_id) = stream_info.session_id() else {
        return 0;
    };
    let id_len = if stream_info.stream_id().is_uni() {
        TYPE_LEN_UNI
    } else {
        TYPE_LEN_BIDI
    };
    to_u64(id_len + Encoder::varint_len(session_id.as_u64()))
}

#[derive(Debug)]
pub struct WebTransportRecvStream {
    stream_info: Http3StreamInfo,
    events: Box<dyn RecvStreamEvents>,
    session: Rc<RefCell<Session>>,
}

impl WebTransportRecvStream {
    pub fn new(
        stream_id: StreamId,
        events: Box<dyn RecvStreamEvents>,
        session: Rc<RefCell<Session>>,
    ) -> Self {
        let session_id = session.borrow().id();
        Self {
            stream_info: Http3StreamInfo::new(stream_id, Http3StreamType::WebTransport(session_id)),
            events,
            session,
        }
    }
}

impl Stream for WebTransportRecvStream {
    fn stream_type(&self) -> Http3StreamType {
        self.stream_info.stream_type()
    }
}

impl RecvStream for WebTransportRecvStream {
    fn receive(&mut self, _conn: &mut Connection, _now: Instant) -> Res<(ReceiveOutput, bool)> {
        if self.session.as_ref().borrow().is_active() {
            self.events.data_readable(&self.stream_info);
        }
        Ok((ReceiveOutput::NoOutput, false))
    }

    fn reset(&mut self, close_type: CloseType) -> Res<()> {
        if !matches!(close_type, CloseType::ResetApp(_)) {
            self.events.recv_closed(&self.stream_info, close_type);
        }
        self.session
            .borrow_mut()
            .remove_recv_stream(self.stream_info.stream_id());
        Ok(())
    }

    fn read_data(
        &mut self,
        conn: &mut Connection,
        buf: &mut [u8],
        _now: Instant,
    ) -> Res<(usize, bool)> {
        let stream_id = self.stream_info.stream_id();
        let (amount, fin) = conn.stream_recv(stream_id, buf)?;
        if fin {
            self.session.borrow_mut().remove_recv_stream(stream_id);
        }
        Ok((amount, fin))
    }

    fn stats(&mut self, conn: &mut Connection) -> Res<recv_stream::Stats> {
        let stream_id = self.stream_info.stream_id();
        let stream_header_size = if stream_id.is_server_initiated() {
            preface_len(&self.stream_info)
        } else {
            0
        };

        let stats = conn.recv_stream_stats(stream_id)?;
        if stream_header_size == 0 {
            return Ok(stats);
        }

        let subtract_non_app_bytes =
            |count: u64| -> u64 { count.saturating_sub(stream_header_size) };

        let bytes_received = subtract_non_app_bytes(stats.bytes_received());
        let bytes_read = subtract_non_app_bytes(stats.bytes_read());

        Ok(recv_stream::Stats::new(bytes_received, bytes_read))
    }
}

#[derive(Debug, PartialEq)]
enum WebTransportSenderStreamState {
    SendingInit { buf: Vec<u8>, fin: bool },
    SendingData,
    Done,
}

#[derive(Debug)]
pub struct WebTransportSendStream {
    stream_info: Http3StreamInfo,
    state: WebTransportSenderStreamState,
    events: Box<dyn SendStreamEvents>,
    session: Rc<RefCell<Session>>,
    send_group: Option<SendGroupId>,
}

impl WebTransportSendStream {
    pub fn new(
        stream_id: StreamId,
        events: Box<dyn SendStreamEvents>,
        session: Rc<RefCell<Session>>,
        local: bool,
        send_group: Option<SendGroupId>,
    ) -> Self {
        let session_id = session.borrow().id();
        Self {
            stream_info: Http3StreamInfo::new(stream_id, Http3StreamType::WebTransport(session_id)),
            state: if local {
                let mut d = Encoder::default();
                if stream_id.is_uni() {
                    d.encode_varint(WEBTRANSPORT_UNI_STREAM);
                } else {
                    d.encode_varint(WEBTRANSPORT_STREAM);
                }
                d.encode_varint(session_id.as_u64());
                WebTransportSenderStreamState::SendingInit {
                    buf: d.into(),
                    fin: false,
                }
            } else {
                WebTransportSenderStreamState::SendingData
            },
            events,
            session,
            send_group,
        }
    }

    #[expect(dead_code, reason = "pending send group further integration")]
    pub(crate) const fn send_group(&self) -> Option<SendGroupId> {
        self.send_group
    }

    pub(crate) fn update_send_group(&mut self, send_group: Option<SendGroupId>) -> Res<()> {
        if let Some(group_id) = send_group
            && !self.session.borrow().validate_send_group(group_id)
        {
            return Err(crate::Error::InvalidState);
        }
        self.send_group = send_group;
        Ok(())
    }

    fn set_done(&mut self, close_type: CloseType) {
        self.state = WebTransportSenderStreamState::Done;
        self.events.send_closed(&self.stream_info, close_type);
        self.session
            .borrow_mut()
            .remove_send_stream(self.stream_info.stream_id());
    }
}

impl Stream for WebTransportSendStream {
    fn stream_type(&self) -> Http3StreamType {
        self.stream_info.stream_type()
    }
}

impl SendStream for WebTransportSendStream {
    fn send(&mut self, conn: &mut Connection, _now: Instant) -> Res<()> {
        let WebTransportSenderStreamState::SendingInit { ref mut buf, fin } = self.state else {
            return Ok(());
        };

        let stream_id = self.stream_info.stream_id();
        let sent = conn.stream_send(stream_id, &buf[..])?;
        if sent == buf.len() {
            // Note that it is safe to commit here because WebTransport requires reliable reset.
            // However, there are other reasons that a commit or send might fail
            // and we don't want to get stuck in the `SendingInit` state.
            // So defer reporting of errors to guarantee that the state transition completes.
            let mut res = conn.stream_commit(stream_id);
            if res == Err(TransportError::NotAvailable) {
                debug!("[{conn}]: Peer supports webtransport, but not reliable reset: ignoring");
                // Old versions might need to work when reliable resets are not available.
                // TODO: Remove this override when we remove support for old WebTransport versions.
                res = Ok(());
            }
            if fin {
                let close = conn.stream_close_send(stream_id);
                res = res.and(close);
                self.set_done(CloseType::Done);
            } else {
                self.state = WebTransportSenderStreamState::SendingData;
            }
            res?;
        } else {
            let b = buf.split_off(sent);
            *buf = b;
        }
        Ok(())
    }

    fn commit(&mut self, conn: &mut Connection, now: Instant) -> Res<()> {
        self.send(conn, now)?;
        if self.state == WebTransportSenderStreamState::SendingData {
            conn.stream_commit(self.stream_info.stream_id())?;
            Ok(())
        } else {
            // Avoid committing unless the preface is successfully sent.
            Err(crate::Error::FlowControlLimit)
        }
    }

    fn has_data_to_send(&self) -> bool {
        matches!(
            self.state,
            WebTransportSenderStreamState::SendingInit { .. }
        )
    }

    fn stream_writable(&self) {
        self.events.data_writable(&self.stream_info);
    }

    fn done(&self) -> bool {
        self.state == WebTransportSenderStreamState::Done
    }

    fn send_data(&mut self, conn: &mut Connection, buf: &[u8], now: Instant) -> Res<usize> {
        self.send(conn, now)?;
        if self.state == WebTransportSenderStreamState::SendingData {
            let sent = conn.stream_send(self.stream_info.stream_id(), buf)?;
            Ok(sent)
        } else {
            Ok(0)
        }
    }

    fn handle_stop_sending(&mut self, close_type: CloseType) {
        self.set_done(close_type);
    }

    fn close(&mut self, conn: &mut Connection, _now: Instant) -> Res<()> {
        if let WebTransportSenderStreamState::SendingInit { ref mut fin, .. } = self.state {
            *fin = true;
        } else {
            self.state = WebTransportSenderStreamState::Done;
            conn.stream_close_send(self.stream_info.stream_id())?;
            self.set_done(CloseType::Done);
        }
        Ok(())
    }

    fn stats(&mut self, conn: &mut Connection) -> Res<send_stream::Stats> {
        let stream_id = self.stream_info.stream_id();
        let stream_header_size = if stream_id.is_client_initiated() {
            preface_len(&self.stream_info)
        } else {
            0
        };

        let stats = conn.send_stream_stats(stream_id)?;
        if stream_header_size == 0 {
            return Ok(stats);
        }

        let subtract_non_app_bytes =
            |count: u64| -> u64 { count.saturating_sub(stream_header_size) };

        let bytes_written = subtract_non_app_bytes(stats.bytes_written());
        let bytes_sent = subtract_non_app_bytes(stats.bytes_sent());
        let bytes_acked = subtract_non_app_bytes(stats.bytes_acked());
        Ok(send_stream::Stats::new(
            bytes_written,
            bytes_sent,
            bytes_acked,
        ))
    }

    fn set_send_group(&mut self, send_group: SendGroupId) -> Res<()> {
        self.update_send_group(Some(send_group))
    }
    fn clear_send_group(&mut self) -> Res<()> {
        self.update_send_group(None)
    }
}
