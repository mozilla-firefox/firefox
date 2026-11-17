// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    collections::VecDeque,
    fmt::{self, Display, Formatter},
    time::Instant,
};

use neqo_transport::{Connection, StreamId, StreamType};
use rustc_hash::FxHashMap as HashMap;
use tracing::trace;

use crate::{BufferedStream, Error, Http3StreamType, RecvStream, Res, frames::HFrame};

pub const HTTP3_UNI_STREAM_TYPE_CONTROL: u64 = 0x0;

/// The local control stream, responsible for encoding frames and sending them
#[derive(Debug, Default)]
pub struct ControlStreamLocal {
    stream: BufferedStream,
    /// `stream_id`s of outstanding request streams
    outstanding_priority_update: VecDeque<StreamId>,
}

impl Display for ControlStreamLocal {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "Local control stream {:?}", self.stream)
    }
}

impl ControlStreamLocal {
    /// Add a new frame that needs to be send.
    pub fn queue_frame(&mut self, f: &HFrame) {
        self.stream.encode_with(|e| f.encode(e));
    }

    pub fn queue_update_priority(&mut self, stream_id: StreamId) {
        self.outstanding_priority_update.push_back(stream_id);
    }

    /// Send control data if available.
    pub fn send(
        &mut self,
        conn: &mut Connection,
        recv_conn: &mut HashMap<StreamId, Box<dyn RecvStream>>,
        now: Instant,
    ) -> Res<()> {
        self.stream.send_buffer(conn, now)?;
        self.send_priority_update(conn, recv_conn, now)
    }

    fn send_priority_update(
        &mut self,
        conn: &mut Connection,
        recv_conn: &mut HashMap<StreamId, Box<dyn RecvStream>>,
        now: Instant,
    ) -> Res<()> {
        // send all necessary priority updates
        while let Some(update_id) = self.outstanding_priority_update.pop_front() {
            let Some(update_stream) = recv_conn.get_mut(&update_id) else {
                continue;
            };

            // can assert and unwrap here, because priority updates can only be added to
            // HttpStreams in [Http3Connection::queue_update_priority}
            debug_assert!(matches!(update_stream.stream_type(), Http3StreamType::Http));
            let stream = update_stream.http_stream().ok_or(Error::Internal)?;

            // in case multiple priority_updates were issued, ignore now irrelevant
            if let Some(hframe) = stream.priority_update_frame() {
                if self
                    .stream
                    .send_atomic_with(conn, |e| hframe.encode(e), now)?
                {
                    stream.priority_update_sent()?;
                } else {
                    self.outstanding_priority_update.push_front(update_id);
                    break;
                }
            }
        }
        Ok(())
    }

    /// Create a control stream.
    pub fn create(&mut self, conn: &mut Connection) -> Res<()> {
        trace!("[{self}] Create a control stream");
        self.stream.init(conn.stream_create(StreamType::UniDi)?);
        self.stream
            .buffer(&[u8::try_from(HTTP3_UNI_STREAM_TYPE_CONTROL).map_err(|_| Error::Internal)?]);
        Ok(())
    }

    #[must_use]
    pub fn stream_id(&self) -> Option<StreamId> {
        (&self.stream).into()
    }
}

#[test]
fn control_stream_local_display() {
    let stream = ControlStreamLocal::default();
    assert!(stream.to_string().starts_with("Local control stream"));
}
