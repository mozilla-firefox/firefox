// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::fmt::{self, Debug, Write as _};

use neqo_common::{Buffer, Decoder, Encoder, hex::HexWithLen};
use neqo_transport::StreamId;
use nss::random;

use crate::{Error, Priority, Res, frames::reader::FrameDecoder, settings::HSettings};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HFrameType(pub u64);

/// Limit on the declared length of a HEADERS frame we'll buffer
/// before decoding.
///
/// A conservative reuse of the QPACK decoded-size limit: encoded size is usually smaller than
/// decoded. Worst-case memory is bounded by the transport's stream concurrency limit, roughly
/// `max_streams_bidi * MAX_HEADER_BYTES` of buffered frames.
pub const MAX_HEADER_BYTES: usize = neqo_qpack::reader::LiteralReader::MAX_LEN;

/// Limit for frame types that carry at most a single varint.
pub const MAX_SINGLE_VARINT_FRAME_BYTES: usize = 8;

/// Limit for other buffered frame types (`SETTINGS`, `PRIORITY_UPDATE_REQUEST`).
pub const MAX_BUFFERED_FRAME_BYTES: usize = 4 * 1024;

impl HFrameType {
    pub const DATA: Self = Self(0x0);
    pub const HEADERS: Self = Self(0x1);
    pub const SETTINGS: Self = Self(0x4);
    pub const GOAWAY: Self = Self(0x7);
    pub const PRIORITY_UPDATE_REQUEST: Self = Self(0xf0700);

    /// Server push frame types. Server push is not supported, so these are not handled; they are
    /// recognized only to reject them with a connection error. See [`HFrame::decode`].
    pub const CANCEL_PUSH: Self = Self(0x3);
    pub const PUSH_PROMISE: Self = Self(0x5);
    pub const MAX_PUSH_ID: Self = Self(0xd);
    pub const PRIORITY_UPDATE_PUSH: Self = Self(0xf0701);

    /// See <https://www.rfc-editor.org/rfc/rfc9114.html#section-11.2.1> for these reserved types.
    pub const RESERVED: &[Self] = &[Self(0x2), Self(0x6), Self(0x8), Self(0x9)];
}

impl From<HFrameType> for u64 {
    fn from(t: HFrameType) -> Self {
        t.0
    }
}

// data for DATA frame is not read into HFrame::Data.
#[derive(PartialEq, Eq)]
pub enum HFrame {
    Data {
        len: u64, // length of the data
    },
    Headers {
        header_block: Vec<u8>,
    },
    Settings {
        settings: HSettings,
    },
    Goaway {
        stream_id: StreamId,
    },
    Grease,
    PriorityUpdateRequest {
        element_id: u64,
        priority: Priority,
    },
    // Server push is not supported. These frames are recognized (their contents are ignored) only
    // so that the role-aware stream handlers can reject them with the correct connection error;
    // neqo never sends them. See `HFrame::decode`.
    CancelPush,
    PushPromise,
    MaxPushId,
    PriorityUpdatePush,
}

impl HFrame {
    fn get_type(&self) -> HFrameType {
        match self {
            Self::Data { .. } => HFrameType::DATA,
            Self::Headers { .. } => HFrameType::HEADERS,
            Self::Settings { .. } => HFrameType::SETTINGS,
            Self::Goaway { .. } => HFrameType::GOAWAY,
            Self::PriorityUpdateRequest { .. } => HFrameType::PRIORITY_UPDATE_REQUEST,
            Self::CancelPush => HFrameType::CANCEL_PUSH,
            Self::PushPromise => HFrameType::PUSH_PROMISE,
            Self::MaxPushId => HFrameType::MAX_PUSH_ID,
            Self::PriorityUpdatePush => HFrameType::PRIORITY_UPDATE_PUSH,
            Self::Grease => {
                let r = u64::from_ne_bytes(random::<8>());
                // Zero out the top 7 bits: 2 for being a varint; 5 to account for the *0x1f.
                HFrameType((r >> 7) * 0x1f + 0x21)
            }
        }
    }

    pub fn encode<B: Buffer>(&self, enc: &mut Encoder<B>) {
        enc.encode_varint(self.get_type());

        match self {
            Self::Data { len } => {
                // DATA frame only encode the length here.
                enc.encode_varint(*len);
            }
            Self::Headers { header_block } => {
                enc.encode_vvec(header_block);
            }
            Self::Settings { settings } => {
                settings.encode_frame_contents(enc);
            }
            Self::Goaway { stream_id } => {
                enc.encode_vvec_with(|enc_inner| {
                    enc_inner.encode_varint(stream_id.as_u64());
                });
            }
            Self::Grease => {
                // Encode some number of random bytes.
                let r = random::<8>();
                enc.encode_vvec(&r[1..usize::from(1 + (r[0] & 0x7))]);
            }
            Self::PriorityUpdateRequest {
                element_id,
                priority,
            } => {
                enc.encode_vvec_with(|enc_inner| {
                    enc_inner.encode_varint(*element_id);
                    write!(enc_inner, "{priority}").expect("write OK");
                });
            }
            Self::CancelPush | Self::PushPromise | Self::MaxPushId | Self::PriorityUpdatePush => {
                unreachable!("server push is not supported, so these frames are never sent")
            }
        }
    }
}

impl FrameDecoder<Self> for HFrame {
    #[cfg(feature = "build-fuzzing-corpus")]
    const FUZZING_CORPUS: Option<&'static str> = Some("hframe");

    fn frame_type_allowed(frame_type: HFrameType) -> Res<()> {
        if HFrameType::RESERVED.contains(&frame_type) {
            return Err(Error::HttpFrameUnexpected);
        }
        Ok(())
    }

    fn max_frame_data(frame_type: HFrameType) -> usize {
        match frame_type {
            HFrameType::HEADERS | HFrameType::PUSH_PROMISE => MAX_HEADER_BYTES,
            HFrameType::GOAWAY | HFrameType::CANCEL_PUSH | HFrameType::MAX_PUSH_ID => {
                MAX_SINGLE_VARINT_FRAME_BYTES
            }
            HFrameType::SETTINGS
            | HFrameType::PRIORITY_UPDATE_REQUEST
            | HFrameType::PRIORITY_UPDATE_PUSH => MAX_BUFFERED_FRAME_BYTES,
            _ => usize::MAX,
        }
    }

    fn decode(frame_type: HFrameType, frame_len: u64, data: Option<&[u8]>) -> Res<Option<Self>> {
        if frame_type == HFrameType::DATA {
            Ok(Some(Self::Data { len: frame_len }))
        } else if let Some(payload) = data {
            let mut dec = Decoder::from(payload);
            let f = match frame_type {
                HFrameType::DATA => unreachable!("DATA frame has been handled already"),
                HFrameType::HEADERS => Self::Headers {
                    header_block: dec.decode_remainder().to_vec(),
                },
                HFrameType::SETTINGS => {
                    let mut settings = HSettings::default();
                    settings.decode_frame_contents(&mut dec).map_err(|e| {
                        if e == Error::HttpSettings {
                            e
                        } else {
                            Error::HttpFrame
                        }
                    })?;
                    Self::Settings { settings }
                }
                HFrameType::GOAWAY => Self::Goaway {
                    stream_id: StreamId::new(dec.decode_varint().ok_or(Error::HttpFrame)?),
                },
                HFrameType::PRIORITY_UPDATE_REQUEST => {
                    let element_id = dec.decode_varint().ok_or(Error::HttpFrame)?;
                    let priority = dec.decode_remainder();
                    let priority = Priority::from_bytes(priority)?;
                    Self::PriorityUpdateRequest {
                        element_id,
                        priority,
                    }
                }
                // Server push is not supported. Recognize these frames (ignoring their contents) so
                // that the role-aware stream handlers can reject them with the correct connection
                // error. See PUSH_PROMISE
                // <https://www.rfc-editor.org/rfc/rfc9114.html#section-7.2.5>, CANCEL_PUSH
                // <https://www.rfc-editor.org/rfc/rfc9114.html#section-7.2.3>, MAX_PUSH_ID
                // <https://www.rfc-editor.org/rfc/rfc9114.html#section-7.2.7>, and PRIORITY_UPDATE
                // <https://www.rfc-editor.org/rfc/rfc9218.html#section-7.2>.
                HFrameType::CANCEL_PUSH => Self::CancelPush,
                HFrameType::PUSH_PROMISE => Self::PushPromise,
                HFrameType::MAX_PUSH_ID => Self::MaxPushId,
                HFrameType::PRIORITY_UPDATE_PUSH => Self::PriorityUpdatePush,
                _ => return Ok(None),
            };
            // Push frames are recognized only to be rejected, so ignore any remaining payload.
            if matches!(
                f,
                Self::CancelPush | Self::PushPromise | Self::MaxPushId | Self::PriorityUpdatePush
            ) {
                return Ok(Some(f));
            }
            if dec.remaining() > 0 {
                Err(Error::HttpFrame)
            } else {
                Ok(Some(f))
            }
        } else {
            Ok(None)
        }
    }

    fn is_known_type(frame_type: HFrameType) -> bool {
        matches!(
            frame_type,
            HFrameType::DATA
                | HFrameType::HEADERS
                | HFrameType::SETTINGS
                | HFrameType::GOAWAY
                | HFrameType::PRIORITY_UPDATE_REQUEST
                | HFrameType::CANCEL_PUSH
                | HFrameType::PUSH_PROMISE
                | HFrameType::MAX_PUSH_ID
                | HFrameType::PRIORITY_UPDATE_PUSH
        )
    }
}

impl Debug for HFrame {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Data { len } => {
                write!(f, "DATA[{len}]")
            }
            Self::Headers { header_block } => {
                write!(f, "HEADERS {}", HexWithLen::new(header_block))
            }
            Self::Settings { settings } => {
                write!(f, "SETTINGS {settings:?}")
            }
            Self::Goaway { stream_id } => {
                write!(f, "GOAWAY {stream_id}")
            }
            Self::Grease => f.write_str("GREASE"),
            Self::PriorityUpdateRequest {
                element_id,
                priority,
            } => write!(f, "PRIORITY_UPDATE request {element_id} {priority}"),
            Self::CancelPush => f.write_str("CANCEL_PUSH"),
            Self::PushPromise => f.write_str("PUSH_PROMISE"),
            Self::MaxPushId => f.write_str("MAX_PUSH_ID"),
            Self::PriorityUpdatePush => f.write_str("PRIORITY_UPDATE_PUSH"),
        }
    }
}
