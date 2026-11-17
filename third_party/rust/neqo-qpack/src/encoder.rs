// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cmp::{max, min},
    collections::VecDeque,
    fmt::{self, Display, Formatter},
    time::Instant,
};

use neqo_common::{Header, to_u64};
use neqo_transport::{Connection, Error as TransportError, StreamId};
use rustc_hash::{FxHashMap as HashMap, FxHashSet as HashSet};
use tracing::{debug, error, trace};

use crate::{
    Error, Res, Settings,
    decoder_instructions::{DecoderInstruction, DecoderInstructionReader},
    encoder_instructions::EncoderInstruction,
    header_block::HeaderEncoder,
    reader::ReceiverConnWrapper,
    stats::Stats,
    table::{ADDITIONAL_TABLE_ENTRY_SIZE, HeaderTable, LookupResult},
};

pub const QPACK_UNI_STREAM_TYPE_ENCODER: u64 = 0x2;

#[derive(Debug, PartialEq)]
enum LocalStreamState {
    NoStream,
    Uninitialized(StreamId),
    Initialized(StreamId),
}

impl LocalStreamState {
    pub const fn stream_id(&self) -> Option<StreamId> {
        match self {
            Self::NoStream => None,
            Self::Uninitialized(stream_id) | Self::Initialized(stream_id) => Some(*stream_id),
        }
    }
}

#[derive(Debug)]
pub struct Encoder {
    table: HeaderTable,
    max_table_size: u64,
    /// Maximum entry count derived from the decoder's advertised table capacity.
    max_entries: u64,
    instruction_reader: DecoderInstructionReader,
    local_stream: LocalStreamState,
    max_blocked_streams: u16,
    /// Upper bound on the number of entries in `unacked_header_blocks`; see
    /// [`Settings::max_tracked_streams`](crate::Settings::max_tracked_streams).
    max_tracked_streams: usize,
    // Remember header blocks that are referring to dynamic table.
    // There can be multiple header blocks in one stream, headers, trailer, push stream request,
    // etc. This HashMap maps a stream ID to a list of header blocks. Each header block is its
    // Required Insert Count and a list of referenced dynamic table entries.
    unacked_header_blocks: HashMap<StreamId, VecDeque<(u64, HashSet<u64>)>>,
    blocked_stream_cnt: u16,
    use_huffman: bool,
    next_capacity: Option<u64>,
    stats: Stats,
    recv_stream_id: Option<StreamId>,
}

impl Encoder {
    #[must_use]
    pub fn new(qpack_settings: &Settings, use_huffman: bool) -> Self {
        Self {
            table: HeaderTable::new(true),
            max_table_size: qpack_settings.max_table_size_encoder,
            max_entries: 0,
            instruction_reader: DecoderInstructionReader::default(),
            local_stream: LocalStreamState::NoStream,
            max_blocked_streams: 0,
            max_tracked_streams: qpack_settings.max_tracked_streams,
            unacked_header_blocks: HashMap::default(),
            blocked_stream_cnt: 0,
            use_huffman,
            next_capacity: None,
            stats: Stats::default(),
            recv_stream_id: None,
        }
    }

    /// Decoder stream has been created. Add the stream id.
    ///
    /// # Errors
    ///
    /// If a stream has already been added.
    pub const fn add_recv_stream(&mut self, stream_id: StreamId) -> Res<()> {
        if self.recv_stream_id.is_some() {
            return Err(Error::Internal);
        }
        self.recv_stream_id = Some(stream_id);
        Ok(())
    }

    /// This function is use for setting encoders table max capacity. The value is received as
    /// a `SETTINGS_QPACK_MAX_TABLE_CAPACITY` setting parameter.
    ///
    /// # Errors
    ///
    /// `EncoderStream` if value is too big.
    /// `ChangeCapacity` if table capacity cannot be reduced.
    pub fn set_max_capacity(&mut self, cap: u64) -> Res<()> {
        if cap > (1 << 30) - 1 {
            return Err(Error::EncoderStream);
        }

        // The decoder's maximum capacity, not the capacity we select, determines
        // when table indices roll over. See RFC 9204, Section 4.5.1.1.
        // Track this separately for when the decoder accepts a table
        // larger than we are willing to use.
        self.max_entries = cap / to_u64(ADDITIONAL_TABLE_ENTRY_SIZE);

        if cap == self.table.capacity() {
            return Ok(());
        }

        debug!(
            "[{self}] Set max capacity to new capacity:{cap} old:{} max_table_size={}",
            self.table.capacity(),
            self.max_table_size,
        );

        let new_cap = min(self.max_table_size, cap);
        // we also set our table to the max allowed.
        self.change_capacity(new_cap);
        Ok(())
    }

    /// This function is use for setting encoders max blocked streams. The value is received as
    /// a `SETTINGS_QPACK_BLOCKED_STREAMS` setting parameter.
    ///
    /// # Errors
    ///
    /// `EncoderStream` if value is too big.
    pub fn set_max_blocked_streams(&mut self, blocked_streams: u64) -> Res<()> {
        self.max_blocked_streams = u16::try_from(blocked_streams).or(Err(Error::EncoderStream))?;
        Ok(())
    }

    /// Reads decoder instructions.
    ///
    /// # Errors
    ///
    /// May return: `ClosedCriticalStream` if stream has been closed or `DecoderStream`
    /// in case of any other transport error.
    pub fn receive(&mut self, conn: &mut Connection, _now: Instant) -> Res<()> {
        self.read_instructions(conn).map_err(map_error)
    }

    fn read_instructions(&mut self, conn: &mut Connection) -> Res<()> {
        debug!("[{self}] read a new instruction");
        let Some(stream_id) = self.recv_stream_id else {
            debug_assert!(false, "receive() before add_recv_stream()");
            return Err(Error::Internal);
        };
        loop {
            let mut recv = ReceiverConnWrapper::new(conn, stream_id);
            match self.instruction_reader.read_instructions(&mut recv) {
                Ok(instruction) => self.call_instruction(instruction)?,
                Err(Error::NeedMoreData) => break Ok(()),
                Err(e) => break Err(e),
            }
        }
    }

    fn recalculate_blocked_streams(&mut self) {
        let acked_inserts_cnt = self.table.get_acked_inserts_cnt();
        self.blocked_stream_cnt = 0;
        #[expect(
            clippy::iter_over_hash_type,
            reason = "OK to loop over unACKed blocks in an undefined order."
        )]
        for hb_list in self.unacked_header_blocks.values_mut() {
            debug_assert!(!hb_list.is_empty());
            if hb_list
                .iter()
                .any(|(req_insert_cnt, _)| *req_insert_cnt > acked_inserts_cnt)
            {
                self.blocked_stream_cnt += 1;
            }
        }
    }

    fn insert_count_instruction(&mut self, increment: u64) -> Res<()> {
        self.table
            .increment_acked(increment)
            .map_err(|_| Error::DecoderStream)?;
        self.recalculate_blocked_streams();
        Ok(())
    }

    fn header_ack(&mut self, stream_id: StreamId) -> Res<()> {
        self.stats.header_acks_recv += 1;
        // RFC 9204, Section 4.4.1: a Header Acknowledgment referring to a stream on which
        // every header block with a non-zero Required Insert Count has already been
        // acknowledged is a connection error of type QPACK_DECODER_STREAM_ERROR. Such a
        // stream has no entry in `unacked_header_blocks` (entries are removed once empty).
        // We only remove entries in response to messages from the peer, so this will only fail
        // if the peer references a stream that never generated unacknowledged blocks.
        let hb_list = self
            .unacked_header_blocks
            .get_mut(&stream_id)
            .ok_or(Error::DecoderStream)?;
        let mut new_acked = self.table.get_acked_inserts_cnt();
        if let Some((req_insert_cnt, ref_list)) = hb_list.pop_back() {
            #[expect(
                clippy::iter_over_hash_type,
                reason = "OK to loop over unACKed blocks in an undefined order."
            )]
            for iter in ref_list {
                self.table.remove_ref(iter);
            }
            new_acked = max(new_acked, req_insert_cnt);
        } else {
            debug_assert!(false, "We should have at least one header block");
        }
        if hb_list.is_empty() {
            self.unacked_header_blocks.remove(&stream_id);
        }
        if new_acked > self.table.get_acked_inserts_cnt() {
            self.insert_count_instruction(new_acked - self.table.get_acked_inserts_cnt())?;
        }
        Ok(())
    }

    fn stream_cancellation(&mut self, stream_id: StreamId) {
        self.stats.stream_cancelled_recv += 1;
        let mut was_blocker = false;
        if let Some(mut hb_list) = self.unacked_header_blocks.remove(&stream_id) {
            debug_assert!(!hb_list.is_empty());
            while let Some((req_insert_cnt, ref_list)) = hb_list.pop_front() {
                #[expect(
                    clippy::iter_over_hash_type,
                    reason = "OK to loop over unACKed blocks in an undefined order."
                )]
                for iter in ref_list {
                    self.table.remove_ref(iter);
                }
                was_blocker = was_blocker || req_insert_cnt > self.table.get_acked_inserts_cnt();
            }
        }
        if was_blocker {
            debug_assert!(self.blocked_stream_cnt > 0);
            self.blocked_stream_cnt -= 1;
        }
    }

    fn call_instruction(&mut self, instruction: DecoderInstruction) -> Res<()> {
        debug!("[{self}] call instruction {instruction:?}");
        match instruction {
            DecoderInstruction::InsertCountIncrement { increment } => {
                // RFC 9204, Section 4.4.3: an Increment of zero is a connection error
                // of type QPACK_DECODER_STREAM_ERROR, the same as one that raises the
                // count past the inserts sent (rejected in `increment_acked`).
                if increment == 0 {
                    return Err(Error::DecoderStream);
                }

                self.insert_count_instruction(increment)
            }
            DecoderInstruction::HeaderAck { stream_id } => self.header_ack(stream_id),
            DecoderInstruction::StreamCancellation { stream_id } => {
                self.stream_cancellation(stream_id);
                Ok(())
            }
            DecoderInstruction::NoInstruction => Ok(()),
        }
    }

    /// Inserts a new entry into a table and sends the corresponding instruction to a peer. An entry
    /// is added only if it is possible to send the corresponding instruction immediately, i.e.
    /// the encoder stream is not blocked by the flow control (or stream internal buffer(this is
    /// very unlikely)).
    ///
    /// # Errors
    ///
    /// `EncoderStreamBlocked` if the encoder stream is blocked by the flow control.
    /// `DynamicTableFull` if the dynamic table does not have enough space for the entry.
    /// The function can return transport errors: `InvalidStreamId`, `InvalidInput` and
    /// `FinalSizeError`.
    ///
    /// # Panics
    ///
    /// When the insertion fails (it should not).
    pub fn send_and_insert(
        &mut self,
        conn: &mut Connection,
        name: &[u8],
        value: &[u8],
    ) -> Res<u64> {
        debug!("[{self}] insert {name:?} {value:?}");

        let entry_size = name.len() + value.len() + ADDITIONAL_TABLE_ENTRY_SIZE;

        if !self.table.insert_possible(entry_size) {
            return Err(Error::DynamicTableFull);
        }

        let mut buf = neqo_common::Encoder::default();
        EncoderInstruction::InsertWithNameLiteral { name, value }
            .marshal(&mut buf, self.use_huffman);

        let stream_id = self.local_stream.stream_id().ok_or(Error::Internal)?;

        let sent = conn
            .stream_send_atomic(stream_id, buf.as_ref())
            .map_err(|e| map_stream_send_atomic_error(&e))?;
        if !sent {
            return Err(Error::EncoderStreamBlocked);
        }

        self.stats.dynamic_table_inserts += 1;

        match self.table.insert(name, value) {
            Ok(inx) => Ok(inx),
            Err(e) => {
                debug_assert!(false);
                Err(e)
            }
        }
    }

    fn change_capacity(&mut self, value: u64) {
        debug!("[{self}] change capacity: {value}");
        self.next_capacity = Some(value);
    }

    fn maybe_send_change_capacity(
        &mut self,
        conn: &mut Connection,
        stream_id: StreamId,
    ) -> Res<()> {
        if let Some(cap) = self.next_capacity {
            // Check if it is possible to reduce the capacity, e.g. if enough space can be made free
            // for the reduction.
            if cap < self.table.capacity() && !self.table.can_evict_to(cap) {
                return Err(Error::DynamicTableFull);
            }
            let mut buf = neqo_common::Encoder::default();
            EncoderInstruction::Capacity { value: cap }.marshal(&mut buf, self.use_huffman);
            if !conn.stream_send_atomic(stream_id, buf.as_ref())? {
                return Err(Error::EncoderStreamBlocked);
            }
            if self.table.set_capacity(cap).is_err() {
                debug_assert!(
                    false,
                    "can_evict_to should have checked and make sure this operation is possible"
                );
                return Err(Error::Internal);
            }
            self.next_capacity = None;
        }
        Ok(())
    }

    /// Sends any qpack encoder instructions.
    ///
    /// # Errors
    ///
    ///   returns `EncoderStream` in case of an error.
    pub fn send_encoder_updates(&mut self, conn: &mut Connection) -> Res<()> {
        match self.local_stream {
            LocalStreamState::NoStream => {
                error!("Send call but there is no stream yet");
                Ok(())
            }
            LocalStreamState::Uninitialized(stream_id) => {
                let mut buf = neqo_common::Encoder::default();
                buf.encode_varint(QPACK_UNI_STREAM_TYPE_ENCODER);
                if !conn.stream_send_atomic(stream_id, buf.as_ref())? {
                    return Err(Error::EncoderStreamBlocked);
                }
                self.local_stream = LocalStreamState::Initialized(stream_id);
                self.maybe_send_change_capacity(conn, stream_id)
            }
            LocalStreamState::Initialized(stream_id) => {
                self.maybe_send_change_capacity(conn, stream_id)
            }
        }
    }

    fn is_stream_blocker(&self, stream_id: StreamId) -> bool {
        self.unacked_header_blocks
            .get(&stream_id)
            .is_some_and(|hb_list| {
                debug_assert!(!hb_list.is_empty());
                hb_list
                    .iter()
                    .any(|(req_insert_cnt, _)| *req_insert_cnt > self.table.get_acked_inserts_cnt())
            })
    }

    /// Encodes headers
    ///
    /// # Errors
    ///
    /// `ClosedCriticalStream` if the encoder stream is closed.
    /// `InternalError` if an unexpected error occurred.
    ///
    /// # Panics
    ///
    /// If there is a programming error.
    pub fn encode_header_block(
        &mut self,
        conn: &mut Connection,
        h: &[Header],
        stream_id: StreamId,
    ) -> HeaderEncoder {
        debug!("[{self}] encoding headers");

        // Try to send capacity instructions if present.
        // This code doesn't try to deal with errors, it just tries
        // to write to the encoder stream AND if it can't uses
        // literal instructions.
        // The errors can be:
        //   1) `EncoderStreamBlocked` - this is an error that can occur.
        //   2) `InternalError` - this is unexpected error.
        //   3) `ClosedCriticalStream` - this is error that should close the HTTP/3 session.
        // The last 2 errors are ignored here and will be picked up
        // by the main loop.
        let mut encoder_blocked = self.send_encoder_updates(conn).is_err();

        let mut encoded_h =
            HeaderEncoder::new(self.table.base(), self.use_huffman, self.max_entries);

        // Avoid the dynamic table unless we have space to track.
        let stream_was_blocking = self.is_stream_blocker(stream_id);
        let can_track = stream_was_blocking
            || self.unacked_header_blocks.contains_key(&stream_id)
            || self.unacked_header_blocks.len() < self.max_tracked_streams;

        // Avoid blocking unless the stream is already blocked or
        // we can use the dynamic table and there is space for blocked streams.
        let can_block = stream_was_blocking
            || (can_track && self.blocked_stream_cnt < self.max_blocked_streams);

        let mut ref_entries = HashSet::default();

        for iter in h {
            let name = iter.name().as_bytes().to_vec();
            let value = iter.value();
            trace!("encoding {name:x?} {value:x?}");

            let found = if can_track {
                self.table.lookup(&name, value, can_block)
            } else {
                HeaderTable::static_lookup(&name, value)
            };
            if let Some(LookupResult {
                index,
                static_table,
                value_matches,
            }) = found
            {
                trace!(
                    "[{self}] found a {} entry, value-match={value_matches}",
                    if static_table { "static" } else { "dynamic" }
                );
                if value_matches {
                    if static_table {
                        encoded_h.encode_indexed_static(index);
                    } else {
                        encoded_h.encode_indexed_dynamic(index);
                    }
                } else {
                    encoded_h.encode_literal_with_name_ref(static_table, index, value);
                }
                if !static_table && ref_entries.insert(index) {
                    self.table.add_ref(index);
                }
            } else if can_block && !encoder_blocked {
                // Insert using an InsertWithNameLiteral instruction. This entry name does not match
                // any name in the tables therefore we cannot use any other
                // instruction.
                if let Ok(index) = self.send_and_insert(conn, &name, value) {
                    encoded_h.encode_indexed_dynamic(index);
                    ref_entries.insert(index);
                    self.table.add_ref(index);
                } else {
                    // This code doesn't try to deal with errors, it just tries
                    // to write to the encoder stream AND if it can't uses
                    // literal instructions.
                    // The errors can be:
                    //   1) `EncoderStreamBlocked` - this is an error that can occur.
                    //   2) `DynamicTableFull` - this is an error that can occur.
                    //   3) `InternalError` - this is unexpected error.
                    //   4) `ClosedCriticalStream` - this is error that should close the HTTP/3
                    //      session.
                    // The last 2 errors are ignored here and will be picked up
                    // by the main loop.
                    // As soon as one of the instructions cannot be written or the table is full, do
                    // not try again.
                    encoder_blocked = true;
                    encoded_h.encode_literal_with_name_literal(&name, value);
                }
            } else {
                encoded_h.encode_literal_with_name_literal(&name, value);
            }
        }

        encoded_h.encode_header_block_prefix();

        if let Some(max_ref) = ref_entries.iter().max() {
            let req_insert_cnt = max_ref + 1;
            // The streams was not a blocker, check if the stream is a blocker now.
            if !stream_was_blocking && req_insert_cnt > self.table.get_acked_inserts_cnt() {
                debug_assert!(self.blocked_stream_cnt <= self.max_blocked_streams);
                self.blocked_stream_cnt += 1;
            }

            self.unacked_header_blocks
                .entry(stream_id)
                .or_default()
                .push_front((req_insert_cnt, ref_entries));
            self.stats.dynamic_table_references += 1;
        }
        #[cfg(feature = "build-fuzzing-corpus")]
        crate::fuzz::write_item_to_fuzzing_corpus(stream_id, &encoded_h);
        encoded_h
    }

    /// Encoder stream has been created. Add the stream id.
    ///
    /// # Errors
    ///
    /// If a stream has already been added.
    pub const fn add_send_stream(&mut self, stream_id: StreamId) -> Res<()> {
        if !matches!(self.local_stream, LocalStreamState::NoStream) {
            return Err(Error::Internal);
        }
        self.local_stream = LocalStreamState::Uninitialized(stream_id);
        Ok(())
    }

    #[must_use]
    pub fn stats(&self) -> Stats {
        self.stats.clone()
    }

    #[must_use]
    pub const fn local_stream_id(&self) -> Option<StreamId> {
        self.local_stream.stream_id()
    }

    #[cfg(test)]
    const fn blocked_stream_cnt(&self) -> u16 {
        self.blocked_stream_cnt
    }
}

impl Display for Encoder {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "QPack")
    }
}

fn map_error(err: Error) -> Error {
    match err {
        Error::ClosedCriticalStream | Error::Internal => err,
        _ => Error::DecoderStream,
    }
}

fn map_stream_send_atomic_error(err: &TransportError) -> Error {
    match err {
        TransportError::InvalidStreamId | TransportError::FinalSize => Error::ClosedCriticalStream,
        _ => {
            debug_assert!(false, "Unexpected error");
            Error::Internal
        }
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::time::Instant;

    use neqo_common::expect_usize;
    use neqo_transport::{ConnectionParameters, StreamDataLimit, StreamId, StreamType};
    use test_fixture::{
        CountingConnectionIdGenerator, DEFAULT_ALPN, default_client, default_server, handshake,
        new_server, now,
    };

    use super::{Connection, Encoder, Error, Header, Res, map_error};
    use crate::{Decoder, Settings};

    struct TestEncoder {
        encoder: Encoder,
        send_stream_id: StreamId,
        recv_stream_id: StreamId,
        conn: Connection,
        peer_conn: Connection,
    }

    impl TestEncoder {
        pub fn change_capacity(&mut self, capacity: u64) -> Res<()> {
            self.encoder.set_max_capacity(capacity)?;
            // We will try to really change the table only when we send the change capacity
            // instruction.
            self.encoder.send_encoder_updates(&mut self.conn)
        }

        pub fn insert(&mut self, header: &[u8], value: &[u8], inst: &[u8]) {
            let res = self.encoder.send_and_insert(&mut self.conn, header, value);
            assert!(res.is_ok());
            self.send_instructions(inst);
        }

        pub fn encode_header_block(
            &mut self,
            stream_id: StreamId,
            headers: &[Header],
            expected_encoding: &[u8],
            inst: &[u8],
        ) {
            let buf = self
                .encoder
                .encode_header_block(&mut self.conn, headers, stream_id);
            assert_eq!(buf.as_ref(), expected_encoding);
            self.send_instructions(inst);
        }

        pub fn send_instructions(&mut self, encoder_instruction: &[u8]) {
            self.encoder.send_encoder_updates(&mut self.conn).unwrap();
            let out = self.conn.process_output(now());
            let out2 = self.peer_conn.process(out.dgram(), now());
            drop(self.conn.process(out2.dgram(), now()));
            let mut buf = [0_u8; 100];
            let (amount, fin) = self
                .peer_conn
                .stream_recv(self.send_stream_id, &mut buf)
                .unwrap();
            assert!(!fin);
            assert_eq!(buf[..amount], encoder_instruction[..]);
        }
    }

    fn connect_generic(huffman: bool, max_data: Option<u64>, max_table_size: u64) -> TestEncoder {
        let mut conn = default_client();
        let mut peer_conn = max_data.map_or_else(default_server, |max| {
            new_server::<CountingConnectionIdGenerator, &str>(
                DEFAULT_ALPN,
                ConnectionParameters::default()
                    .max_stream_data(StreamDataLimit::UniDi, max)
                    .max_stream_data(StreamDataLimit::BiDiRemote, max)
                    .max_stream_data(StreamDataLimit::BiDiLocal, max),
            )
        });
        handshake(&mut conn, &mut peer_conn);

        // create a stream
        let recv_stream_id = peer_conn.stream_create(StreamType::UniDi).unwrap();
        let send_stream_id = conn.stream_create(StreamType::UniDi).unwrap();

        // create an encoder
        let mut encoder = Encoder::new(
            &Settings {
                max_table_size_encoder: max_table_size,
                max_table_size_decoder: 0,
                max_blocked_streams: 0,
                max_tracked_streams: 4096,
            },
            huffman,
        );
        encoder.add_send_stream(send_stream_id).unwrap();
        encoder.add_recv_stream(recv_stream_id).unwrap();

        TestEncoder {
            encoder,
            send_stream_id,
            recv_stream_id,
            conn,
            peer_conn,
        }
    }

    fn connect(huffman: bool) -> TestEncoder {
        connect_generic(huffman, None, 1500)
    }

    fn connect_flow_control(max_data: u64) -> TestEncoder {
        connect_generic(true, Some(max_data), 1500)
    }

    fn recv_instruction(encoder: &mut TestEncoder, decoder_instruction: &[u8], now: Instant) {
        encoder
            .peer_conn
            .stream_send(encoder.recv_stream_id, decoder_instruction)
            .unwrap();
        let out = encoder.peer_conn.process_output(now);
        drop(encoder.conn.process(out.dgram(), now));
        assert!(encoder.encoder.read_instructions(&mut encoder.conn).is_ok());
    }

    const CAP_INSTRUCTION_200: &[u8] = &[0x02, 0x3f, 0xa9, 0x01];
    const CAP_INSTRUCTION_60: &[u8] = &[0x02, 0x3f, 0x1d];
    const CAP_INSTRUCTION_1000: &[u8] = &[0x02, 0x3f, 0xc9, 0x07];
    const CAP_INSTRUCTION_1500: &[u8] = &[0x02, 0x3f, 0xbd, 0x0b];

    const HEADER_CONTENT_LENGTH: &[u8] = b"content-length";
    const VALUE_1: &[u8] = b"1234";
    const VALUE_2: &[u8] = b"12345";

    // HEADER_CONTENT_LENGTH and VALUE_1 encoded by instruction insert_with_name_literal.
    const HEADER_CONTENT_LENGTH_VALUE_1_NAME_LITERAL: &[u8] = &[
        0x4e, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2d, 0x6c, 0x65, 0x6e, 0x67, 0x74, 0x68,
        0x04, 0x31, 0x32, 0x33, 0x34,
    ];

    // HEADER_CONTENT_LENGTH and VALUE_2 encoded by instruction insert_with_name_literal.
    const HEADER_CONTENT_LENGTH_VALUE_2_NAME_LITERAL: &[u8] = &[
        0x4e, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2d, 0x6c, 0x65, 0x6e, 0x67, 0x74, 0x68,
        0x05, 0x31, 0x32, 0x33, 0x34, 0x35,
    ];

    // Indexed Header Field that refers to the first entry in the dynamic table.
    const ENCODE_INDEXED_REF_DYNAMIC: &[u8] = &[0x02, 0x00, 0x80];

    const STREAM_1: StreamId = StreamId::new(1);
    const STREAM_2: StreamId = StreamId::new(2);
    const HEADER_ACK_STREAM_ID_1: &[u8] = &[0x81];
    const HEADER_ACK_STREAM_ID_2: &[u8] = &[0x82];
    const STREAM_CANCELED_ID_1: &[u8] = &[0x41];

    // test insert_with_name_literal which fails because there is not enough space in the table
    #[test]
    fn insert_with_name_literal_1() {
        let mut encoder = connect(false);

        // insert "content-length: 1234
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_1);
        assert_eq!(Error::DynamicTableFull, res.unwrap_err());
        encoder.send_instructions(&[0x02]);
    }

    // test insert_with_name_literal - succeeds
    #[test]
    fn insert_with_name_literal_2() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(200).is_ok());
        // test the change capacity instruction.
        encoder.send_instructions(CAP_INSTRUCTION_200);

        // insert "content-length: 1234
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_1);
        assert!(res.is_ok());
        encoder.send_instructions(HEADER_CONTENT_LENGTH_VALUE_1_NAME_LITERAL);
    }

    #[test]
    fn change_capacity() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(200).is_ok());
        encoder.send_instructions(CAP_INSTRUCTION_200);
    }

    struct TestElement {
        pub headers: Vec<Header>,
        pub header_block: &'static [u8],
        pub encoder_inst: &'static [u8],
    }

    #[test]
    fn header_block_encoder_non() {
        let test_cases: [TestElement; 6] = [
            // test a header with ref to static - encode_indexed
            TestElement {
                headers: vec![Header::new(":method", "GET")],
                header_block: &[0x00, 0x00, 0xd1],
                encoder_inst: &[],
            },
            // test encode_literal_with_name_ref
            TestElement {
                headers: vec![Header::new(":path", "/somewhere")],
                header_block: &[
                    0x00, 0x00, 0x51, 0x0a, 0x2f, 0x73, 0x6f, 0x6d, 0x65, 0x77, 0x68, 0x65, 0x72,
                    0x65,
                ],
                encoder_inst: &[],
            },
            // test adding a new header and encode_post_base_index, also test
            // fix_header_block_prefix
            TestElement {
                headers: vec![Header::new("my-header", "my-value")],
                header_block: &[0x02, 0x80, 0x10],
                encoder_inst: &[
                    0x49, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72, 0x08, 0x6d, 0x79,
                    0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65,
                ],
            },
            // test encode_indexed with a ref to dynamic table.
            TestElement {
                headers: vec![Header::new("my-header", "my-value")],
                header_block: ENCODE_INDEXED_REF_DYNAMIC,
                encoder_inst: &[],
            },
            // test encode_literal_with_name_ref.
            TestElement {
                headers: vec![Header::new("my-header", "my-value2")],
                header_block: &[
                    0x02, 0x00, 0x40, 0x09, 0x6d, 0x79, 0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x32,
                ],
                encoder_inst: &[],
            },
            // test multiple headers
            TestElement {
                headers: vec![
                    Header::new(":method", "GET"),
                    Header::new(":path", "/somewhere"),
                    Header::new(":authority", "example.com"),
                    Header::new(":scheme", "https"),
                ],
                header_block: &[
                    0x00, 0x01, 0xd1, 0x51, 0x0a, 0x2f, 0x73, 0x6f, 0x6d, 0x65, 0x77, 0x68, 0x65,
                    0x72, 0x65, 0x50, 0x0b, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63,
                    0x6f, 0x6d, 0xd7,
                ],
                encoder_inst: &[],
            },
        ];

        let mut encoder = connect(false);

        encoder.encoder.set_max_blocked_streams(100).unwrap();
        encoder.encoder.set_max_capacity(200).unwrap();

        // test the change capacity instruction.
        encoder.send_instructions(CAP_INSTRUCTION_200);

        for t in &test_cases {
            let buf = encoder
                .encoder
                .encode_header_block(&mut encoder.conn, &t.headers, STREAM_1);
            assert_eq!(buf.as_ref(), t.header_block);
            encoder.send_instructions(t.encoder_inst);
        }
    }

    #[test]
    fn header_block_encoder_huffman() {
        let test_cases: [TestElement; 6] = [
            // test a header with ref to static - encode_indexed
            TestElement {
                headers: vec![Header::new(":method", "GET")],
                header_block: &[0x00, 0x00, 0xd1],
                encoder_inst: &[],
            },
            // test encode_literal_with_name_ref
            TestElement {
                headers: vec![Header::new(":path", "/somewhere")],
                header_block: &[
                    0x00, 0x00, 0x51, 0x87, 0x61, 0x07, 0xa4, 0xbe, 0x27, 0x2d, 0x85,
                ],
                encoder_inst: &[],
            },
            // test adding a new header and encode_post_base_index, also test
            // fix_header_block_prefix
            TestElement {
                headers: vec![Header::new("my-header", "my-value")],
                header_block: &[0x02, 0x80, 0x10],
                encoder_inst: &[
                    0x67, 0xa7, 0xd2, 0xd3, 0x94, 0x72, 0x16, 0xcf, 0x86, 0xa7, 0xd2, 0xdd, 0xc7,
                    0x45, 0xa5,
                ],
            },
            // test encode_indexed with a ref to dynamic table.
            TestElement {
                headers: vec![Header::new("my-header", "my-value")],
                header_block: ENCODE_INDEXED_REF_DYNAMIC,
                encoder_inst: &[],
            },
            // test encode_literal_with_name_ref.
            TestElement {
                headers: vec![Header::new("my-header", "my-value2")],
                header_block: &[
                    0x02, 0x00, 0x40, 0x87, 0xa7, 0xd2, 0xdd, 0xc7, 0x45, 0xa5, 0x17,
                ],
                encoder_inst: &[],
            },
            // test multiple headers
            TestElement {
                headers: vec![
                    Header::new(":method", "GET"),
                    Header::new(":path", "/somewhere"),
                    Header::new(":authority", "example.com"),
                    Header::new(":scheme", "https"),
                ],
                header_block: &[
                    0x00, 0x01, 0xd1, 0x51, 0x87, 0x61, 0x07, 0xa4, 0xbe, 0x27, 0x2d, 0x85, 0x50,
                    0x88, 0x2f, 0x91, 0xd3, 0x5d, 0x05, 0x5c, 0x87, 0xa7, 0xd7,
                ],
                encoder_inst: &[],
            },
        ];

        let mut encoder = connect(true);

        encoder.encoder.set_max_blocked_streams(100).unwrap();
        encoder.encoder.set_max_capacity(200).unwrap();

        // test the change capacity instruction.
        encoder.send_instructions(CAP_INSTRUCTION_200);

        for t in &test_cases {
            let buf = encoder
                .encoder
                .encode_header_block(&mut encoder.conn, &t.headers, STREAM_1);
            assert_eq!(buf.as_ref(), t.header_block);
            encoder.send_instructions(t.encoder_inst);
        }
    }

    // Test inserts block on waiting for an insert count increment.
    #[test]
    fn insertion_blocked_on_insert_count_feedback() {
        let mut encoder = connect(false);

        encoder.encoder.set_max_capacity(60).unwrap();

        // test the change capacity instruction.
        encoder.send_instructions(CAP_INSTRUCTION_60);

        // insert "content-length: 1234
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_1);
        assert!(res.is_ok());
        encoder.send_instructions(HEADER_CONTENT_LENGTH_VALUE_1_NAME_LITERAL);

        // insert "content-length: 12345 which will fail because the entry in the table cannot be
        // evicted.
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_2);
        assert!(res.is_err());
        encoder.send_instructions(&[]);

        // receive an insert count increment.
        recv_instruction(&mut encoder, &[0x01], now());

        // insert "content-length: 12345 again it will succeed.
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_2);
        assert!(res.is_ok());
        encoder.send_instructions(HEADER_CONTENT_LENGTH_VALUE_2_NAME_LITERAL);
    }

    // Test inserts block on waiting for ACKs
    // test the table insertion is blocked:
    // 0 - waiting for a header ack
    // 2 - waiting for a stream cancel.
    fn test_insertion_blocked_on_waiting_for_header_ack_or_stream_cancel(wait: u8) {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(60).is_ok());
        // test the change capacity instruction.
        encoder.send_instructions(CAP_INSTRUCTION_60);

        // insert "content-length: 1234
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_1);
        assert!(res.is_ok());
        encoder.send_instructions(HEADER_CONTENT_LENGTH_VALUE_1_NAME_LITERAL);

        // receive an insert count increment.
        recv_instruction(&mut encoder, &[0x01], now());

        // send a header block
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("content-length", "1234")],
            STREAM_1,
        );
        assert_eq!(buf.as_ref(), ENCODE_INDEXED_REF_DYNAMIC);
        encoder.send_instructions(&[]);

        // insert "content-length: 12345 which will fail because the entry in the table cannot be
        // evicted
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_2);
        assert!(res.is_err());
        encoder.send_instructions(&[]);

        if wait == 0 {
            // receive a header_ack.
            recv_instruction(&mut encoder, HEADER_ACK_STREAM_ID_1, now());
        } else {
            // receive a stream canceled
            recv_instruction(&mut encoder, STREAM_CANCELED_ID_1, now());
        }

        // insert "content-length: 12345 again it will succeed.
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_2);
        assert!(res.is_ok());
        encoder.send_instructions(HEADER_CONTENT_LENGTH_VALUE_2_NAME_LITERAL);
    }

    #[test]
    fn header_ack() {
        test_insertion_blocked_on_waiting_for_header_ack_or_stream_cancel(0);
    }

    #[test]
    fn stream_canceled() {
        test_insertion_blocked_on_waiting_for_header_ack_or_stream_cancel(1);
    }

    /// RFC 9204, Section 4.4.3: an Insert Count Increment instruction with an
    /// Increment of zero is a connection error of type `QPACK_DECODER_STREAM_ERROR`,
    /// the same as one that raises the count past the inserts the encoder has sent
    /// (already rejected in `increment_acked`).
    #[test]
    fn insert_count_increment_zero_is_rejected() {
        let mut encoder = connect(false);

        // 0x00 is an Insert Count Increment instruction carrying an Increment of 0.
        encoder
            .peer_conn
            .stream_send(encoder.recv_stream_id, &[0x00])
            .unwrap();
        let out = encoder.peer_conn.process_output(now());
        encoder.conn.process_input(out.dgram().unwrap(), now());
        assert_eq!(
            encoder.encoder.read_instructions(&mut encoder.conn),
            Err(Error::DecoderStream)
        );
    }

    /// RFC 9204, Section 4.4.1: a Header Acknowledgment for a stream that has no
    /// outstanding acknowledgment is a connection error of type `QPACK_DECODER_STREAM_ERROR`.
    #[test]
    fn header_ack_without_outstanding_block_is_rejected() {
        let mut encoder = connect(false);

        // 0x81 is a Header Acknowledgment for stream 1, which has never sent a header
        // block referencing the dynamic table.
        encoder
            .peer_conn
            .stream_send(encoder.recv_stream_id, HEADER_ACK_STREAM_ID_1)
            .unwrap();
        let out = encoder.peer_conn.process_output(now());
        encoder.conn.process_input(out.dgram().unwrap(), now());
        assert_eq!(
            encoder.encoder.read_instructions(&mut encoder.conn),
            Err(Error::DecoderStream)
        );
    }

    fn assert_is_index_to_dynamic(buf: &[u8]) {
        assert_eq!(buf[2] & 0xc0, 0x80);
    }

    fn assert_is_index_to_dynamic_post(buf: &[u8]) {
        assert_eq!(buf[2] & 0xf0, 0x10);
    }

    fn assert_is_index_to_static_name_only(buf: &[u8]) {
        assert_eq!(buf[2] & 0xf0, 0x50);
    }

    fn assert_is_literal_value_literal_name(buf: &[u8]) {
        assert_eq!(buf[2] & 0xf0, 0x20);
    }

    #[test]
    fn max_block_streams1() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(60).is_ok());

        // change capacity to 60.
        encoder.send_instructions(CAP_INSTRUCTION_60);

        // insert "content-length: 1234
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_1);

        assert!(res.is_ok());
        encoder.send_instructions(HEADER_CONTENT_LENGTH_VALUE_1_NAME_LITERAL);

        encoder.encoder.set_max_blocked_streams(1).unwrap();

        // send a header block, it refers to unacked entry.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("content-length", "1234")],
            STREAM_1,
        );
        assert_is_index_to_dynamic(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);

        encoder.send_instructions(&[]);

        // The next one will not use the dynamic entry because it is exceeding the
        // max_blocked_streams limit.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("content-length", "1234")],
            StreamId::new(2),
        );
        assert_is_index_to_static_name_only(&buf);

        encoder.send_instructions(&[]);
        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);

        // another header block to already blocked stream can still use the entry.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("content-length", "1234")],
            STREAM_1,
        );
        assert_is_index_to_dynamic(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);
    }

    #[test]
    fn max_block_streams2() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(200).is_ok());

        // change capacity to 200.
        encoder.send_instructions(CAP_INSTRUCTION_200);

        // insert "content-length: 1234
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_1);

        assert!(res.is_ok());
        encoder.send_instructions(HEADER_CONTENT_LENGTH_VALUE_1_NAME_LITERAL);

        // insert "content-length: 12345
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_2);

        assert!(res.is_ok());
        encoder.send_instructions(HEADER_CONTENT_LENGTH_VALUE_2_NAME_LITERAL);

        encoder.encoder.set_max_blocked_streams(1).unwrap();

        // send a header block, it refers to unacked entry.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("content-length", "1234")],
            STREAM_1,
        );
        assert_is_index_to_dynamic(&buf);

        // encode another header block for the same stream that will refer to the second entry
        // in the dynamic table.
        // This should work because the stream is already a blocked stream
        // send a header block, it refers to unacked entry.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("content-length", "12345")],
            STREAM_1,
        );
        assert_is_index_to_dynamic(&buf);
    }

    #[test]
    fn max_block_streams3() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(200).is_ok());

        // change capacity to 200.
        encoder.send_instructions(CAP_INSTRUCTION_200);

        encoder.encoder.set_max_blocked_streams(1).unwrap();

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 0);

        // send a header block, that creates an new entry and refers to it.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name1", "value1")],
            STREAM_1,
        );
        assert_is_index_to_dynamic_post(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);

        // The next one will not create a new entry because the encoder is on max_blocked_streams
        // limit.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name2", "value2")],
            STREAM_2,
        );
        assert_is_literal_value_literal_name(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);

        // another header block to already blocked stream can still create a new entry.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name2", "value2")],
            STREAM_1,
        );
        assert_is_index_to_dynamic_post(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);
    }

    #[test]
    fn max_block_streams4() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(200).is_ok());

        // change capacity to 200.
        encoder.send_instructions(CAP_INSTRUCTION_200);

        encoder.encoder.set_max_blocked_streams(1).unwrap();

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 0);

        // send a header block, that creates an new entry and refers to it.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name1", "value1")],
            STREAM_1,
        );
        assert_is_index_to_dynamic_post(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);

        // another header block to already blocked stream can still create a new entry.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name2", "value2")],
            STREAM_1,
        );
        assert_is_index_to_dynamic_post(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);

        // receive a header_ack for the first header block.
        recv_instruction(&mut encoder, HEADER_ACK_STREAM_ID_1, now());

        // The stream is still blocking because the second header block is not acked.
        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);
    }

    #[test]
    fn max_block_streams5() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(200).is_ok());

        // change capacity to 200.
        encoder.send_instructions(CAP_INSTRUCTION_200);

        encoder.encoder.set_max_blocked_streams(1).unwrap();

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 0);

        // send a header block, that creates an new entry and refers to it.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name1", "value1")],
            STREAM_1,
        );
        assert_is_index_to_dynamic_post(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);

        // another header block to already blocked stream can still create a new entry.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name1", "value1")],
            STREAM_1,
        );
        assert_is_index_to_dynamic(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);

        // receive a header_ack for the first header block.
        recv_instruction(&mut encoder, HEADER_ACK_STREAM_ID_1, now());

        // The stream is not blocking anymore because header ack also ACKs the instruction.
        assert_eq!(encoder.encoder.blocked_stream_cnt(), 0);
    }

    #[test]
    fn max_block_streams6() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(200).is_ok());

        // change capacity to 200.
        encoder.send_instructions(CAP_INSTRUCTION_200);

        encoder.encoder.set_max_blocked_streams(2).unwrap();

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 0);

        // send a header block, that creates an new entry and refers to it.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name1", "value1")],
            STREAM_1,
        );
        assert_is_index_to_dynamic_post(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);

        // header block for the next stream will create an new entry as well.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name2", "value2")],
            STREAM_2,
        );
        assert_is_index_to_dynamic_post(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 2);

        // receive a header_ack for the second header block. This will ack the first as well
        recv_instruction(&mut encoder, HEADER_ACK_STREAM_ID_2, now());

        // The stream is not blocking anymore because header ack also ACKs the instruction.
        assert_eq!(encoder.encoder.blocked_stream_cnt(), 0);
    }

    #[test]
    fn max_block_streams7() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(200).is_ok());

        // change capacity to 200.
        encoder.send_instructions(CAP_INSTRUCTION_200);

        encoder.encoder.set_max_blocked_streams(2).unwrap();

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 0);

        // send a header block, that creates an new entry and refers to it.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name1", "value1")],
            STREAM_1,
        );
        assert_is_index_to_dynamic_post(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);

        // header block for the next stream will create an new entry as well.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name1", "value1")],
            STREAM_2,
        );
        assert_is_index_to_dynamic(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 2);

        // receive a stream cancel for the first stream.
        // This will remove the first stream as blocking but it will not mark the instruction as
        // acked. and the second steam will still be blocking.
        recv_instruction(&mut encoder, STREAM_CANCELED_ID_1, now());

        // The stream is not blocking anymore because header ack also ACKs the instruction.
        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);
    }

    #[test]
    fn max_block_stream8() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(200).is_ok());

        // change capacity to 200.
        encoder.send_instructions(CAP_INSTRUCTION_200);

        encoder.encoder.set_max_blocked_streams(2).unwrap();

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 0);

        // send a header block, that creates an new entry and refers to it.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name1", "value1")],
            STREAM_1,
        );
        assert_is_index_to_dynamic_post(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);

        // header block for the next stream will refer to the same entry.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name1", "value1")],
            STREAM_2,
        );
        assert_is_index_to_dynamic(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 2);

        // send another header block on stream 1.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name2", "value2")],
            STREAM_1,
        );
        assert_is_index_to_dynamic_post(&buf);

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 2);

        // stream 1 is block on entries 1 and 2; stream 2 is block only on 1.
        // receive an Insert Count Increment for the first entry.
        // After that only stream 1 will be blocking.
        recv_instruction(&mut encoder, &[0x01], now());

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);
    }

    // Acknowledged inserts unblock a stream before its Section Acknowledgment arrives.
    #[test]
    fn max_block_streams_acked_inserts_not_blocking() {
        let mut encoder = connect(false);
        assert!(encoder.encoder.set_max_capacity(200).is_ok());
        encoder.send_instructions(CAP_INSTRUCTION_200);
        encoder.encoder.set_max_blocked_streams(1).unwrap();

        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name1", "value1")],
            STREAM_1,
        );
        assert_is_index_to_dynamic_post(&buf);
        recv_instruction(&mut encoder, &[0x01], now());
        assert_eq!(encoder.encoder.blocked_stream_cnt(), 0);

        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name2", "value2")],
            STREAM_2,
        );
        assert_is_index_to_dynamic_post(&buf);
        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);

        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name3", "value3")],
            STREAM_1,
        );
        assert_is_literal_value_literal_name(&buf);
        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);
    }

    #[test]
    fn dynamic_table_can_evict1() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(60).is_ok());

        // change capacity to 60.
        encoder.send_instructions(CAP_INSTRUCTION_60);

        encoder.encoder.set_max_blocked_streams(2).unwrap();

        // insert "content-length: 1234
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_1);

        assert!(res.is_ok());
        encoder.send_instructions(HEADER_CONTENT_LENGTH_VALUE_1_NAME_LITERAL);

        // send a header block, it refers to unacked entry.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("content-length", "1234")],
            STREAM_1,
        );
        assert_is_index_to_dynamic(&buf);

        // trying to evict the entry will failed.
        assert!(encoder.change_capacity(10).is_err());

        // receive an Insert Count Increment for the entry.
        recv_instruction(&mut encoder, &[0x01], now());

        // trying to evict the entry will failed. The stream is still referring to it.
        assert!(encoder.change_capacity(10).is_err());

        // receive a header_ack for the header block.
        recv_instruction(&mut encoder, HEADER_ACK_STREAM_ID_1, now());

        // now entry can be evicted.
        assert!(encoder.change_capacity(10).is_ok());
    }

    #[test]
    fn dynamic_table_can_evict2() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(60).is_ok());

        // change capacity to 60.
        encoder.send_instructions(CAP_INSTRUCTION_60);

        encoder.encoder.set_max_blocked_streams(2).unwrap();

        // insert "content-length: 1234
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_1);

        assert!(res.is_ok());
        encoder.send_instructions(HEADER_CONTENT_LENGTH_VALUE_1_NAME_LITERAL);

        // send a header block, it refers to unacked entry.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("content-length", "1234")],
            STREAM_1,
        );
        assert_is_index_to_dynamic(&buf);

        // trying to evict the entry will failed.
        assert!(encoder.change_capacity(10).is_err());

        // receive an Insert Count Increment for the entry.
        recv_instruction(&mut encoder, &[0x01], now());

        // trying to evict the entry will failed. The stream is still referring to it.
        assert!(encoder.change_capacity(10).is_err());

        // receive a stream cancelled.
        recv_instruction(&mut encoder, STREAM_CANCELED_ID_1, now());

        // now entry can be evicted.
        assert!(encoder.change_capacity(10).is_ok());
    }

    #[test]
    fn dynamic_table_can_evict3() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(60).is_ok());

        // change capacity to 60.
        encoder.send_instructions(CAP_INSTRUCTION_60);

        encoder.encoder.set_max_blocked_streams(2).unwrap();

        // insert "content-length: 1234
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_1);

        assert!(res.is_ok());
        encoder.send_instructions(HEADER_CONTENT_LENGTH_VALUE_1_NAME_LITERAL);

        // trying to evict the entry will failed, because the entry is not acked.
        assert!(encoder.change_capacity(10).is_err());

        // receive an Insert Count Increment for the entry.
        recv_instruction(&mut encoder, &[0x01], now());

        // now entry can be evicted.
        assert!(encoder.change_capacity(10).is_ok());
    }

    #[test]
    fn dynamic_table_can_evict4() {
        let mut encoder = connect(false);

        assert!(encoder.encoder.set_max_capacity(60).is_ok());

        // change capacity to 60.
        encoder.send_instructions(CAP_INSTRUCTION_60);

        encoder.encoder.set_max_blocked_streams(2).unwrap();

        // insert "content-length: 1234
        let res =
            encoder
                .encoder
                .send_and_insert(&mut encoder.conn, HEADER_CONTENT_LENGTH, VALUE_1);

        assert!(res.is_ok());
        encoder.send_instructions(HEADER_CONTENT_LENGTH_VALUE_1_NAME_LITERAL);

        // send a header block, it refers to unacked entry.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("content-length", "1234")],
            STREAM_1,
        );
        assert_is_index_to_dynamic(&buf);

        // trying to evict the entry will failed. The stream is still referring to it and
        // entry is not acked.
        assert!(encoder.change_capacity(10).is_err());

        // receive a header_ack for the header block. This will also ack the instruction.
        recv_instruction(&mut encoder, HEADER_ACK_STREAM_ID_1, now());

        // now entry can be evicted.
        assert!(encoder.change_capacity(10).is_ok());
    }

    #[test]
    fn encoder_flow_controlled_blocked() {
        const SMALL_MAX_DATA: u64 = 20;
        const ONE_INSTRUCTION_1: &[u8] = &[
            0x67, 0x41, 0xe9, 0x2a, 0x67, 0x35, 0x53, 0x7f, 0x83, 0x8, 0x99, 0x6b,
        ];
        const ONE_INSTRUCTION_2: &[u8] = &[
            0x67, 0x41, 0xe9, 0x2a, 0x67, 0x35, 0x53, 0x37, 0x83, 0x8, 0x99, 0x6b,
        ];

        let mut encoder = connect_flow_control(SMALL_MAX_DATA);

        // change capacity to 1000 and max_block streams to 20.
        encoder.encoder.set_max_blocked_streams(20).unwrap();
        assert!(encoder.encoder.set_max_capacity(1000).is_ok());
        encoder.send_instructions(CAP_INSTRUCTION_1000);

        // Encode a header block with 2 headers. The first header will be added to the dynamic
        // table. The second will not be added to the dynamic table, because the
        // corresponding instruction cannot be written immediately due to the flow control
        // limit.
        let buf1 = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[
                Header::new("something", "1234"),
                Header::new("something2", "12345678910"),
            ],
            STREAM_1,
        );

        // Assert that the first header is encoded as an index to the dynamic table (a post form).
        assert!(buf1.len() > 3);
        assert_eq!(buf1[2], 0x10);
        // Assert that the second header is encoded as a literal with a name literal
        assert_eq!(buf1[3] & 0xf0, 0x20);

        // Try to encode another header block. Here both headers will be encoded as a literal with a
        // name literal
        let buf2 = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[
                Header::new("something3", "1234"),
                Header::new("something4", "12345678910"),
            ],
            STREAM_2,
        );
        assert_eq!(buf2[2] & 0xf0, 0x20);

        // Ensure that we have sent only one instruction for (String::from("something", "1234"))
        encoder.send_instructions(ONE_INSTRUCTION_1);

        // exchange a flow control update.
        let out = encoder.peer_conn.process_output(now());
        drop(encoder.conn.process(out.dgram(), now()));

        // Try writing a new header block. Now, headers will be added to the dynamic table again,
        // because instructions can be sent.
        let buf3 = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[
                Header::new("something5", "1234"),
                Header::new("something6", "12345678910"),
            ],
            StreamId::new(3),
        );
        // Assert that the first header is encoded as an index to the dynamic table (a post form).
        assert!(buf3.len() > 3);
        assert_eq!(buf3[2], 0x10);
        // Assert that the second header is encoded as a literal with a name literal
        assert_eq!(buf3[3] & 0xf0, 0x20);

        // Asset that one instruction has been sent
        encoder.send_instructions(ONE_INSTRUCTION_2);
    }

    #[test]
    fn encoder_max_capacity_limit() {
        let mut encoder = connect(false);

        // change capacity to 2000.
        assert!(encoder.encoder.set_max_capacity(2000).is_ok());
        encoder.send_instructions(CAP_INSTRUCTION_1500);
        assert_eq!(encoder.encoder.table.capacity(), 1500);

        // MaxEntries comes from the decoder's advertised capacity (2000), not the
        // locally clamped table capacity (1500). Advance past the smaller modulus
        // to make the encoded Required Insert Count distinguish the two values.
        for i in 0..100 {
            let value = i.to_string();
            encoder
                .encoder
                .table
                .insert(b"x", value.as_bytes())
                .unwrap();
            encoder.encoder.table.increment_acked(1).unwrap();
        }
        let block = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("x", "99")],
            STREAM_1,
        );
        assert_eq!(block.as_ref(), &[0x65, 0x00, 0x80]);
    }

    #[test]
    fn required_insert_count_uses_decoder_max_capacity() {
        // Cover a smaller local limit, equal limits, a smaller peer limit, and a capacity
        // reduction that leaves the peer's advertised maximum unchanged.
        for (local_limit, reduce_capacity) in [(60, false), (128, false), (256, false), (128, true)]
        {
            let mut encoder = connect_generic(false, None, local_limit);
            encoder.encoder.set_max_capacity(128).unwrap();
            encoder.encoder.set_max_blocked_streams(1).unwrap();
            encoder
                .encoder
                .send_encoder_updates(&mut encoder.conn)
                .unwrap();

            let mut decoder = Decoder::new(&Settings::default().max_table_size_decoder(128));
            decoder.add_recv_stream(encoder.send_stream_id).unwrap();
            decoder.add_send_stream(encoder.recv_stream_id).unwrap();
            decoder.send(&mut encoder.peer_conn).unwrap();

            let out = encoder.conn.process_output(now());
            let out2 = encoder.peer_conn.process(out.dgram(), now());
            drop(encoder.conn.process(out2.dgram(), now()));

            let mut stream_type = [0];
            assert_eq!(
                encoder
                    .peer_conn
                    .stream_recv(encoder.send_stream_id, &mut stream_type),
                Ok((1, false))
            );
            assert_eq!(stream_type, [0x02]);
            assert_eq!(
                encoder
                    .conn
                    .stream_recv(encoder.recv_stream_id, &mut stream_type),
                Ok((1, false))
            );
            assert_eq!(stream_type, [0x03]);
            assert!(decoder.receive(&mut encoder.peer_conn).unwrap().is_empty());

            // The peer's maximum gives MaxEntries = 4 and FullRange = 8. Exercise two wraps.
            for insert_count in 1_u8..=17 {
                if reduce_capacity && insert_count == 9 {
                    encoder.encoder.change_capacity(60);
                    encoder
                        .encoder
                        .send_encoder_updates(&mut encoder.conn)
                        .unwrap();
                }
                let headers = vec![Header::new("x", insert_count.to_string())];
                encoder
                    .encoder
                    .send_and_insert(&mut encoder.conn, b"x", headers[0].value())
                    .unwrap();
                let buf =
                    encoder
                        .encoder
                        .encode_header_block(&mut encoder.conn, &headers, STREAM_1);
                let out = encoder.conn.process_output(now());
                let out2 = encoder.peer_conn.process(out.dgram(), now());
                drop(encoder.conn.process(out2.dgram(), now()));
                assert!(decoder.receive(&mut encoder.peer_conn).unwrap().is_empty());
                assert_eq!(
                    decoder.decode_header_block(&buf, STREAM_1),
                    Ok(Some(headers)),
                    "local_limit={local_limit}, reduce_capacity={reduce_capacity}, insert_count={insert_count}"
                );
                assert_eq!(buf.as_ref(), &[insert_count % 8 + 1, 0x00, 0x80]);

                // Absorb the QPACK bookkeeping: acknowledgments and so forth.
                decoder.send(&mut encoder.peer_conn).unwrap();
                let out = encoder.peer_conn.process_output(now());
                let out2 = encoder.conn.process(out.dgram(), now());
                drop(encoder.peer_conn.process(out2.dgram(), now()));
                encoder.encoder.receive(&mut encoder.conn, now()).unwrap();
            }
        }
    }

    #[test]
    fn same_table_capacity_updates_required_insert_count_range() {
        let mut encoder = connect_generic(false, None, 60);
        encoder.encoder.set_max_capacity(128).unwrap();
        encoder.encoder.set_max_blocked_streams(1).unwrap();
        encoder.send_instructions(CAP_INSTRUCTION_60);

        // The new maximum equals the current capacity, but changes the wrapping range.
        encoder.encoder.set_max_capacity(60).unwrap();
        for expected_count in [0x02, 0x01] {
            encoder.insert(
                HEADER_CONTENT_LENGTH,
                VALUE_1,
                HEADER_CONTENT_LENGTH_VALUE_1_NAME_LITERAL,
            );
            encoder.encode_header_block(
                STREAM_1,
                &[Header::new("content-length", "1234")],
                &[expected_count, 0x00, 0x80],
                &[],
            );
            recv_instruction(&mut encoder, HEADER_ACK_STREAM_ID_1, now());
        }
    }

    #[test]
    fn do_not_evict_entry_that_are_referred_only_by_the_same_header_blocked_encoding() {
        let mut encoder = connect(false);

        encoder.encoder.set_max_blocked_streams(20).unwrap();
        assert!(encoder.change_capacity(50).is_ok());

        encoder
            .encoder
            .send_and_insert(&mut encoder.conn, b"something5", b"1234")
            .unwrap();

        encoder
            .encoder
            .send_encoder_updates(&mut encoder.conn)
            .unwrap();
        let out = encoder.conn.process_output(now());
        drop(encoder.peer_conn.process(out.dgram(), now()));
        // receive an insert count increment.
        recv_instruction(&mut encoder, &[0x01], now());

        // The first header will use the table entry and the second will use the literal
        // encoding because the first entry is referred to and cannot be evicted.
        assert_eq!(
            encoder
                .encoder
                .encode_header_block(
                    &mut encoder.conn,
                    &[
                        Header::new("something5", "1234"),
                        Header::new("something6", "1234"),
                    ],
                    StreamId::new(3),
                )
                .to_vec(),
            &[
                0x02, 0x00, 0x80, 0x27, 0x03, 0x73, 0x6f, 0x6d, 0x65, 0x74, 0x68, 0x69, 0x6e, 0x67,
                0x36, 0x04, 0x31, 0x32, 0x33, 0x34
            ]
        );
        // Also check that there is no new instruction send by the encoder.
        assert!(encoder.conn.process_output(now()).dgram().is_none());
    }

    #[test]
    fn streams_cancel_cleans_up_unacked_header_blocks() {
        let mut encoder = connect(false);

        encoder.encoder.set_max_blocked_streams(10).unwrap();
        assert!(encoder.change_capacity(60).is_ok());
        encoder.send_instructions(CAP_INSTRUCTION_60);

        // insert "content-length: 1234
        encoder.insert(
            HEADER_CONTENT_LENGTH,
            VALUE_1,
            HEADER_CONTENT_LENGTH_VALUE_1_NAME_LITERAL,
        );

        // send a header block
        encoder.encode_header_block(
            StreamId::new(1),
            &[Header::new("content-length", "1234")],
            ENCODE_INDEXED_REF_DYNAMIC,
            &[],
        );

        // receive a stream canceled instruction.
        recv_instruction(&mut encoder, STREAM_CANCELED_ID_1, now());

        recv_instruction(&mut encoder, &[0x01], now());
    }

    // A peer that acknowledges an insertion (so a dynamic entry becomes referenceable
    // without blocking) but then resets its streams via STOP_SENDING *without* ever
    // sending a Header Acknowledgement or Stream Cancellation instruction would, absent
    // a cap, make `unacked_header_blocks` grow one entry per stream for the lifetime of
    // the connection: every such stream references the acknowledged entry, which adds an
    // entry to the map and bumps the table entry's reference count, while
    // `blocked_stream_cnt` stays at 0 (so `max_blocked_streams` does not contain it).
    //
    // `Settings::max_tracked_streams` bounds this: once the map is full, new streams are
    // encoded from the static table and literals only, so they create no reference and
    // add nothing to the map.
    #[test]
    fn unacked_header_blocks_are_capped() {
        const CAP: u64 = 5;

        let mut encoder = connect(false);

        // The encoder is never allowed to block a stream, and tracks at most CAP streams.
        encoder.encoder.set_max_blocked_streams(0).unwrap();
        encoder.encoder.max_tracked_streams = expect_usize(CAP);
        assert!(encoder.encoder.set_max_capacity(200).is_ok());
        encoder.send_instructions(CAP_INSTRUCTION_200);

        encoder.insert(
            HEADER_CONTENT_LENGTH,
            VALUE_1,
            HEADER_CONTENT_LENGTH_VALUE_1_NAME_LITERAL,
        );
        recv_instruction(&mut encoder, &[0x01], now());

        assert_eq!(encoder.encoder.blocked_stream_cnt(), 0);
        assert_eq!(encoder.encoder.unacked_header_blocks.len(), 0);

        // Simulate more streams than the cap: each sends a header block referencing the
        // acked entry and is then reset via STOP_SENDING, but the peer never sends a
        // Header Ack or Stream Cancellation instruction for it.
        for i in 0..CAP + 10 {
            let buf = encoder.encoder.encode_header_block(
                &mut encoder.conn,
                &[Header::new(
                    String::from_utf8_lossy(HEADER_CONTENT_LENGTH),
                    VALUE_1,
                )],
                StreamId::new(i * 4),
            );
            // Dynamic entries are used up to the cap.
            if i < CAP {
                assert_is_index_to_dynamic(&buf);
            } else {
                assert_is_index_to_static_name_only(&buf);
            }
        }

        // Nothing ever blocks, but the tracked state is bounded by the cap.
        assert_eq!(encoder.encoder.blocked_stream_cnt(), 0);
        assert_eq!(
            encoder.encoder.unacked_header_blocks.len(),
            expect_usize(CAP)
        );
    }

    /// A stream that is already tracked is not subject to the tracking cap.
    #[test]
    fn tracked_stream_bypasses_tracked_cap() {
        const CAP: usize = 1;

        let mut encoder = connect(false);

        encoder.encoder.set_max_blocked_streams(10).unwrap();
        encoder.encoder.max_tracked_streams = CAP;
        assert!(encoder.encoder.set_max_capacity(200).is_ok());
        encoder.send_instructions(CAP_INSTRUCTION_200);

        // STREAM_1 inserts and references a new, unacknowledged entry: it becomes a
        // blocking stream and fills the single tracked-stream slot.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name1", "value1")],
            STREAM_1,
        );
        assert_is_index_to_dynamic_post(&buf);
        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);
        assert_eq!(encoder.encoder.unacked_header_blocks.len(), CAP);

        // A brand-new stream is now over the cap and must fall back to a literal.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name2", "value2")],
            STREAM_2,
        );
        assert_is_literal_value_literal_name(&buf);
        assert_eq!(encoder.encoder.unacked_header_blocks.len(), CAP);

        // An already-tracked STREAM_1 is not affected by the cap.
        let buf = encoder.encoder.encode_header_block(
            &mut encoder.conn,
            &[Header::new("name3", "value3")],
            STREAM_1,
        );
        assert_is_index_to_dynamic_post(&buf);
        assert_eq!(encoder.encoder.unacked_header_blocks.len(), CAP);
        assert_eq!(encoder.encoder.blocked_stream_cnt(), 1);
    }

    #[test]
    fn duplicate_recv_stream() {
        let mut encoder = Encoder::new(&Settings::default(), true);
        encoder.add_recv_stream(StreamId::new(3)).unwrap();
        assert_eq!(
            encoder.add_recv_stream(StreamId::new(7)),
            Err(Error::Internal)
        );
    }

    /// A missing registration must not be blamed on the peer as a stream error.
    #[test]
    fn map_error_covers_all_arms() {
        assert_eq!(map_error(Error::Internal), Error::Internal);
        assert_eq!(
            map_error(Error::ClosedCriticalStream),
            Error::ClosedCriticalStream
        );
        assert_eq!(map_error(Error::ChangeCapacity), Error::DecoderStream);
    }
}
