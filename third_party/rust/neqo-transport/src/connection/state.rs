// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{cmp::min, rc::Rc, time::Instant};

use neqo_common::{Buffer, Encoder, to_u64};

use crate::{
    CloseReason, Error,
    frame::{FrameEncoder as _, FrameType},
    packet,
    path::PathRef,
    recovery,
};

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
/// The state of the Connection.
pub enum State {
    /// A newly created connection.
    Init,
    /// Waiting for the first Initial packet.
    WaitInitial,
    /// Waiting to confirm which version was selected.
    /// For a client, this is confirmed when a CRYPTO frame is received;
    /// the version of the packet determines the version.
    /// For a server, this is confirmed when transport parameters are
    /// received and processed.
    WaitVersion,
    /// Exchanging Handshake packets.
    Handshaking,
    Connected,
    Confirmed,
    Closing {
        error: CloseReason,
        timeout: Instant,
    },
    Draining {
        error: CloseReason,
        timeout: Instant,
    },
    Closed(CloseReason),
}

impl State {
    #[must_use]
    pub const fn connected(&self) -> bool {
        matches!(self, Self::Connected | Self::Confirmed)
    }

    #[must_use]
    pub const fn closed(&self) -> bool {
        matches!(
            self,
            Self::Closing { .. } | Self::Draining { .. } | Self::Closed(_)
        )
    }

    #[must_use]
    pub const fn error(&self) -> Option<&CloseReason> {
        if let Self::Closing { error, .. } | Self::Draining { error, .. } | Self::Closed(error) =
            self
        {
            Some(error)
        } else {
            None
        }
    }

    #[must_use]
    pub const fn closing(&self) -> bool {
        matches!(self, Self::Closing { .. } | Self::Draining { .. })
    }
}

#[derive(Debug, Clone)]
pub struct ClosingFrame {
    path: PathRef,
    error: CloseReason,
    frame_type: FrameType,
    reason_phrase: Vec<u8>,
    /// Packets received in the closing state.
    received: usize,
}

impl ClosingFrame {
    fn new(
        path: PathRef,
        error: CloseReason,
        frame_type: FrameType,
        message: impl AsRef<str>,
    ) -> Self {
        let reason_phrase = message.as_ref().as_bytes().to_vec();
        Self {
            path,
            error,
            frame_type,
            reason_phrase,
            received: 0,
        }
    }

    pub const fn path(&self) -> &PathRef {
        &self.path
    }

    pub fn sanitize(&self) -> Option<Self> {
        if let CloseReason::Application(_) = self.error {
            // The default CONNECTION_CLOSE frame that is sent when an application
            // error code needs to be sent in an Initial or Handshake packet.
            Some(Self {
                path: Rc::clone(&self.path),
                error: CloseReason::Transport(Error::Application),
                frame_type: FrameType::Padding,
                reason_phrase: Vec::new(),
                received: self.received,
            })
        } else {
            None
        }
    }

    /// Length of a closing frame with a truncated `reason_length`. Allow 8 bytes for the reason
    /// phrase to ensure that if it needs to be truncated there is still at least a few bytes of
    /// the value.
    pub const MIN_LENGTH: usize = 1 + 8 + 8 + 2 + 8;

    pub fn write_frame<B: Buffer>(&self, builder: &mut packet::Builder<B>) {
        if builder.remaining() < Self::MIN_LENGTH {
            return;
        }
        let (frame, code, trigger) = match &self.error {
            CloseReason::Transport(e) => (
                FrameType::ConnectionCloseTransport,
                e.code(),
                Some(self.frame_type),
            ),
            CloseReason::Application(code) => (FrameType::ConnectionCloseApplication, *code, None),
        };
        // Truncate the reason phrase if it doesn't fit.  As we send this frame in
        // multiple packet number spaces, limit the overall size to 256.
        let available = min(256, builder.remaining())
            - Encoder::varint_len(frame.into())
            - Encoder::varint_len(code)
            - trigger.map_or(0, |t| Encoder::varint_len(t.into()));
        let reason = if available < Encoder::vvec_len(self.reason_phrase.len()) {
            &self.reason_phrase[..available - Encoder::varint_len(to_u64(available))]
        } else {
            &self.reason_phrase
        };
        builder.encode_frame(frame, |b| {
            b.encode_varint(code);
            if let Some(trigger) = trigger {
                b.encode_varint(trigger);
            }
            b.encode_vvec(reason);
        });
    }
}

/// `StateSignaling` manages whether we need to send `HANDSHAKE_DONE` and `CONNECTION_CLOSE`.
/// Valid state transitions are:
/// * Idle -> `HandshakeDone`: at the server when the handshake completes
/// * `HandshakeDone` -> Idle: when a `HANDSHAKE_DONE` frame is sent
/// * Idle/HandshakeDone -> Closing/Draining: when closing or draining
/// * Closing/Draining -> `CloseSent`: after sending `CONNECTION_CLOSE`
/// * `CloseSent` -> Closing: any time a new `CONNECTION_CLOSE` is needed
/// * -> Reset: from any state in case of a stateless reset
#[derive(Debug, Clone)]
pub enum StateSignaling {
    Idle,
    HandshakeDone,
    /// These states save the frame that needs to be sent.
    Closing(ClosingFrame),
    Draining(ClosingFrame),
    /// This state saves the frame that might need to be sent again.
    /// If it is `None`, then we are draining and don't send.
    CloseSent(Option<ClosingFrame>),
    Reset,
}

impl StateSignaling {
    pub fn handshake_done(&mut self) {
        if !matches!(self, Self::Idle) {
            return;
        }
        *self = Self::HandshakeDone;
    }

    pub fn write_done<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
    ) -> Option<recovery::Token> {
        (matches!(self, Self::HandshakeDone) && builder.remaining() >= 1).then(|| {
            *self = Self::Idle;
            builder.encode_frame(FrameType::HandshakeDone, |_| {});
            recovery::Token::HandshakeDone
        })
    }

    pub fn close<A: AsRef<str>>(
        &mut self,
        path: PathRef,
        error: CloseReason,
        frame_type: FrameType,
        message: A,
    ) {
        if !matches!(self, Self::Reset) {
            *self = Self::Closing(ClosingFrame::new(path, error, frame_type, message));
        }
    }

    pub fn drain<A: AsRef<str>>(
        &mut self,
        path: PathRef,
        error: CloseReason,
        frame_type: FrameType,
        message: A,
    ) {
        if !matches!(self, Self::Reset) {
            *self = Self::Draining(ClosingFrame::new(path, error, frame_type, message));
        }
    }

    /// If a close is pending, take a frame.
    pub fn close_frame(&mut self) -> Option<ClosingFrame> {
        match self {
            Self::Closing(frame) => {
                // When we are closing, we might need to send the close frame again.
                let res = Some(frame.clone());
                *self = Self::CloseSent(Some(frame.clone()));
                res
            }
            Self::Draining(frame) => {
                // When we are draining, just send once.
                let res = Some(frame.clone());
                *self = Self::CloseSent(None);
                res
            }
            _ => None,
        }
    }

    /// If a close can be sent again, prepare to send it again.
    pub fn send_close(&mut self) {
        if let Self::CloseSent(Some(frame)) = self {
            frame.received += 1; // Rate limit (RFC 9000, Section 10.2.1).
            if frame.received.is_power_of_two() {
                *self = Self::Closing(frame.clone());
            }
        }
    }

    /// We just got a stateless reset.  Terminate.
    pub fn reset(&mut self) {
        *self = Self::Reset;
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::{cell::RefCell, rc::Rc};

    use neqo_common::{Encoder, qlog::Qlog};
    use test_fixture::{DEFAULT_ADDR, now};

    use super::{ClosingFrame, State, StateSignaling};
    use crate::{CloseReason, ConnectionParameters, Error, frame::FrameType, packet, path::Path};

    #[test]
    fn closing_frame_min_length() {
        // 1 (type) + 8 (error code) + 8 (frame type) + 2 (reason prefix) + 8 (reason capacity).
        assert_eq!(ClosingFrame::MIN_LENGTH, 1 + 8 + 8 + 2 + 8);
    }

    /// A truncated reason phrase leaves room for the rest of the frame.
    #[test]
    fn closing_frame_fits() {
        let path = Rc::new(RefCell::new(Path::temporary(
            DEFAULT_ADDR,
            DEFAULT_ADDR,
            &ConnectionParameters::default(),
            Qlog::default(),
            now(),
        )));
        let errors = [
            (
                CloseReason::Transport(Error::ProtocolViolation),
                FrameType::AckFrequency,
            ),
            (CloseReason::Application(42), FrameType::Padding),
            (CloseReason::Application((1 << 62) - 1), FrameType::Padding),
        ];
        for (error, frame_type) in errors {
            for reason_len in [0, 37, 100, 254, 300] {
                let reason = "x".repeat(reason_len);
                let frame = ClosingFrame::new(Rc::clone(&path), error.clone(), frame_type, &reason);
                for remaining in ClosingFrame::MIN_LENGTH..300 {
                    let mut builder =
                        packet::Builder::short(Encoder::default(), false, None::<&[u8]>, 1500);
                    let start = builder.len();
                    builder.set_limit(start + remaining);
                    frame.write_frame(&mut builder);
                    assert!(
                        start < builder.len() && builder.len() <= builder.limit(),
                        "{error:?} reason {reason_len} remaining {remaining}"
                    );
                }
            }
        }
    }

    fn assert_resets_to_reset(mut ss: StateSignaling) {
        ss.reset();
        assert!(matches!(ss, StateSignaling::Reset));
    }

    #[test]
    fn state_signaling_reset_transitions() {
        assert_resets_to_reset(StateSignaling::Idle);
        assert_resets_to_reset(StateSignaling::HandshakeDone);
    }

    #[test]
    fn state_predicates() {
        let now = now();
        let err = CloseReason::Transport(Error::None);
        let closing = State::Closing {
            error: err.clone(),
            timeout: now,
        };
        let draining = State::Draining {
            error: err.clone(),
            timeout: now,
        };
        let closed = State::Closed(err);

        assert!(!State::Init.connected() && !State::Init.closed() && !State::Init.closing());
        assert!(!State::WaitInitial.connected() && !State::Handshaking.connected());
        assert!(State::Connected.connected() && State::Confirmed.connected());
        assert!(closing.closing() && closing.closed() && closing.error().is_some());
        assert!(draining.closing() && draining.closed());
        assert!(!closed.closing() && closed.closed() && closed.error().is_some());
    }
}
