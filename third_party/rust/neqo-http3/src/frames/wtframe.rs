// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use neqo_common::{Decoder, Encoder, to_u64};

use super::hframe::HFrameType;
use crate::{Error, Res, frames::reader::FrameDecoder};

pub type WebTransportFrameType = u64;

#[derive(PartialEq, Eq, Debug)]
pub enum WebTransportFrame {
    CloseSession { error: u32, message: String },
}

const _: () = assert!(WebTransportFrame::CLOSE_MAX_MESSAGE_SIZE <= to_u64(usize::MAX) - 4);

impl WebTransportFrame {
    /// The frame type for WebTransport `CLOSE_SESSION`, as defined in
    /// [WebTransport over HTTP/3 (RFC 9297, Section 4.6)](https://datatracker.ietf.org/doc/html/rfc9297#section-4.6).
    /// The value 0x2843 is assigned for `CLOSE_SESSION`.
    const CLOSE_SESSION: WebTransportFrameType = 0x2843;

    /// The maximum allowed message size for `CLOSE_SESSION` messages, as recommended
    /// in [WebTransport over HTTP/3 (RFC 9297, Section 4.6)](https://datatracker.ietf.org/doc/html/rfc9297#section-4.6).
    /// The value 1024 is used to limit the message size for security and interoperability.
    const CLOSE_MAX_MESSAGE_SIZE: u64 = 1024;

    /// Limit on the declared length of a `CLOSE_SESSION` frame.
    #[expect(clippy::cast_possible_truncation, reason = "value is checked above")]
    pub const MAX_CLOSE_SESSION_BYTES: usize = Self::CLOSE_MAX_MESSAGE_SIZE as usize + 4;

    pub fn encode(&self, enc: &mut Encoder) {
        #[cfg(feature = "build-fuzzing-corpus")]
        let start = enc.len();

        enc.encode_varint(Self::CLOSE_SESSION);
        let Self::CloseSession { error, message } = &self;
        enc.encode_len(4 + message.len());
        enc.encode_uint(4, *error);
        enc.encode(message.as_bytes());

        #[cfg(feature = "build-fuzzing-corpus")]
        neqo_common::write_item_to_fuzzing_corpus("wtframe", &enc.as_ref()[start..]);
    }
}

impl FrameDecoder<Self> for WebTransportFrame {
    #[cfg(feature = "build-fuzzing-corpus")]
    const FUZZING_CORPUS: Option<&'static str> = Some("wtframe");

    fn decode(frame_type: HFrameType, frame_len: u64, data: Option<&[u8]>) -> Res<Option<Self>> {
        if let Some(payload) = data {
            let mut dec = Decoder::from(payload);
            if frame_type == HFrameType(Self::CLOSE_SESSION) {
                if frame_len > Self::CLOSE_MAX_MESSAGE_SIZE + 4 {
                    return Err(Error::HttpMessage);
                }
                let error = dec.decode_uint().ok_or(Error::HttpMessage)?;
                let Ok(message) = String::from_utf8(dec.decode_remainder().to_vec()) else {
                    return Err(Error::HttpMessage);
                };
                Ok(Some(Self::CloseSession { error, message }))
            } else {
                Ok(None)
            }
        } else {
            Ok(None)
        }
    }

    fn is_known_type(frame_type: HFrameType) -> bool {
        frame_type == HFrameType(Self::CLOSE_SESSION)
    }

    fn max_frame_data(frame_type: HFrameType) -> usize {
        if frame_type == HFrameType(Self::CLOSE_SESSION) {
            Self::MAX_CLOSE_SESSION_BYTES
        } else {
            usize::MAX
        }
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use neqo_common::to_u64;

    use super::{HFrameType, WebTransportFrame};
    use crate::frames::reader::FrameDecoder as _;

    #[test]
    fn is_known_type_close_session() {
        assert!(WebTransportFrame::is_known_type(HFrameType(
            WebTransportFrame::CLOSE_SESSION
        )));
    }

    #[test]
    fn max_frame_data_unknown_type_is_unbounded() {
        assert_eq!(
            WebTransportFrame::max_frame_data(HFrameType(0x1230)),
            usize::MAX
        );
    }

    #[test]
    fn is_known_type_unknown() {
        assert!(!WebTransportFrame::is_known_type(HFrameType(0x1234)));
        assert!(!WebTransportFrame::is_known_type(HFrameType(0)));
    }

    #[test]
    fn decode_close_session_too_large() {
        // Message size exceeds CLOSE_MAX_MESSAGE_SIZE (1024) + 4 bytes for error code.
        let large_message = vec![0u8; 1025];
        let mut payload = vec![0, 0, 0, 0]; // 4-byte error code
        payload.extend(&large_message);
        let frame_len = to_u64(payload.len());

        let result = WebTransportFrame::decode(
            HFrameType(WebTransportFrame::CLOSE_SESSION),
            frame_len,
            Some(&payload),
        );
        assert!(result.is_err());
    }

    #[test]
    fn decode_close_session_at_limit() {
        // Message size exactly at CLOSE_MAX_MESSAGE_SIZE (1024).
        let message = vec![b'a'; 1024];
        let mut payload = vec![0, 0, 0, 0]; // 4-byte error code
        payload.extend(&message);
        let frame_len = to_u64(payload.len());

        let result = WebTransportFrame::decode(
            HFrameType(WebTransportFrame::CLOSE_SESSION),
            frame_len,
            Some(&payload),
        );
        assert!(result.is_ok());
    }
}
