// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::fmt::{self, Display, Formatter};

use neqo_common::{Encoder, Header};
use neqo_transport::{Connection, StreamId};
use tracing::debug;

use crate::{
    Error, Res, Settings,
    decoder_instructions::DecoderInstruction,
    encoder_instructions::{DecodedEncoderInstruction, EncoderInstructionReader},
    header_block::{HeaderDecoder, HeaderDecoderResult},
    reader::{ReadByte, Reader, ReceiverConnWrapper},
    stats::Stats,
    table::HeaderTable,
};

pub const QPACK_UNI_STREAM_TYPE_DECODER: u64 = 0x3;

#[derive(Debug)]
pub struct Decoder {
    instruction_reader: EncoderInstructionReader,
    table: HeaderTable,
    acked_inserts: u64,
    max_entries: u64,
    send_buf: Encoder,
    local_stream_id: Option<StreamId>,
    max_table_size: u64,
    max_blocked_streams: usize,
    blocked_streams: Vec<(StreamId, u64)>, // stream_id and requested inserts count.
    stats: Stats,
    recv_stream_id: Option<StreamId>,
}

impl Decoder {
    /// # Panics
    ///
    /// If settings include invalid values.
    #[must_use]
    pub fn new(qpack_settings: &Settings) -> Self {
        debug!("Decoder: creating a new qpack decoder");
        let mut send_buf = Encoder::default();
        send_buf.encode_varint(QPACK_UNI_STREAM_TYPE_DECODER);
        let max_blocked_streams = usize::from(qpack_settings.max_blocked_streams);
        Self {
            instruction_reader: EncoderInstructionReader::default(),
            table: HeaderTable::new(false),
            acked_inserts: 0,
            max_entries: qpack_settings.max_table_size_decoder >> 5,
            send_buf,
            local_stream_id: None,
            max_table_size: qpack_settings.max_table_size_decoder,
            max_blocked_streams,
            blocked_streams: Vec::with_capacity(max_blocked_streams),
            stats: Stats::default(),
            recv_stream_id: None,
        }
    }

    #[must_use]
    const fn capacity(&self) -> u64 {
        self.table.capacity()
    }

    /// Returns a list of unblocked streams.
    ///
    /// # Errors
    ///
    /// May return: `ClosedCriticalStream` if stream has been closed or `EncoderStream`
    /// in case of any other transport error.
    pub fn receive(&mut self, conn: &mut Connection) -> Res<Vec<StreamId>> {
        let base_old = self.table.base();
        self.read_instructions(conn).map_err(map_error)?;
        let base_new = self.table.base();
        if base_old == base_new {
            return Ok(Vec::new());
        }

        Ok(self
            .blocked_streams
            .extract_if(.., |(_, req)| *req <= base_new)
            .map(|(id, _)| id)
            .collect())
    }

    fn read_instructions(&mut self, conn: &mut Connection) -> Res<()> {
        let Some(stream_id) = self.recv_stream_id else {
            debug_assert!(false, "receive() before add_recv_stream()");
            return Err(Error::Internal);
        };
        let mut recv = ReceiverConnWrapper::new(conn, stream_id);
        self.process_instructions(&mut recv)
    }

    pub(crate) fn process_instructions<T: ReadByte + Reader>(&mut self, recv: &mut T) -> Res<()> {
        loop {
            match self.instruction_reader.read_instructions(recv) {
                Ok(instruction) => self.execute_instruction(instruction)?,
                Err(Error::NeedMoreData) => break Ok(()),
                Err(e) => break Err(e),
            }
        }
    }

    fn execute_instruction(&mut self, instruction: DecodedEncoderInstruction) -> Res<()> {
        match instruction {
            DecodedEncoderInstruction::Capacity { value } => self.set_capacity(value)?,
            DecodedEncoderInstruction::InsertWithNameRefStatic { index, value } => {
                Error::map_error(
                    self.table.insert_with_name_ref(true, index, &value),
                    Error::EncoderStream,
                )?;
                self.stats.dynamic_table_inserts += 1;
            }
            DecodedEncoderInstruction::InsertWithNameRefDynamic { index, value } => {
                Error::map_error(
                    self.table.insert_with_name_ref(false, index, &value),
                    Error::EncoderStream,
                )?;
                self.stats.dynamic_table_inserts += 1;
            }
            DecodedEncoderInstruction::InsertWithNameLiteral { name, value } => {
                Error::map_error(
                    self.table.insert(&name, &value).map(|_| ()),
                    Error::EncoderStream,
                )?;
                self.stats.dynamic_table_inserts += 1;
            }
            DecodedEncoderInstruction::Duplicate { index } => {
                Error::map_error(self.table.duplicate(index), Error::EncoderStream)?;
                self.stats.dynamic_table_inserts += 1;
            }
            DecodedEncoderInstruction::NoInstruction => {
                unreachable!("This can be call only with an instruction");
            }
        }
        Ok(())
    }

    fn set_capacity(&mut self, cap: u64) -> Res<()> {
        debug!("[{self}] received instruction capacity cap={cap}");
        if cap > self.max_table_size {
            return Err(Error::EncoderStream);
        }
        self.table.set_capacity(cap)
    }

    fn header_ack(&mut self, stream_id: StreamId, required_inserts: u64) {
        DecoderInstruction::HeaderAck { stream_id }.marshal(&mut self.send_buf);
        if required_inserts > self.acked_inserts {
            self.acked_inserts = required_inserts;
        }
    }

    pub fn cancel_stream(&mut self, stream_id: StreamId) {
        self.blocked_streams.retain(|(id, _)| *id != stream_id);
        if self.max_table_size > 0 {
            DecoderInstruction::StreamCancellation { stream_id }.marshal(&mut self.send_buf);
        }
    }

    /// # Errors
    ///
    /// May return [`Error::Internal`] if the decoder stream is not initialized,
    /// or [`Error::DecoderStream`] if sending on the decoder stream fails.
    ///
    /// # Panics
    ///
    /// Never, but rust doesn't know that.
    pub fn send(&mut self, conn: &mut Connection) -> Res<()> {
        // Encode increment instruction if needed.
        let increment = self.table.base() - self.acked_inserts;
        if increment > 0 {
            DecoderInstruction::InsertCountIncrement { increment }.marshal(&mut self.send_buf);
            self.acked_inserts = self.table.base();
        }
        if !self.send_buf.is_empty() && self.local_stream_id.is_some() {
            let r = conn
                .stream_send(
                    self.local_stream_id.ok_or(Error::Internal)?,
                    self.send_buf.as_ref(),
                )
                .map_err(|_| Error::DecoderStream)?;
            debug!("[{self}] {r} bytes sent");
            self.send_buf.skip(r);
            self.send_buf.compact();
        }
        Ok(())
    }

    /// # Errors
    ///
    /// May return `Error::Decompression` if header block is incorrect or incomplete.
    pub fn refers_dynamic_table(&self, buf: &[u8]) -> Res<bool> {
        HeaderDecoder::new(buf).refers_dynamic_table(self.max_entries, self.table.base())
    }

    /// This function returns None if the stream is blocked waiting for table insertions.
    /// 'buf' must contain the complete header block.
    ///
    /// # Errors
    ///
    /// May return `Error::Decompression` if header block is incorrect or incomplete.
    ///
    /// # Panics
    ///
    /// When there is a programming error.
    pub fn decode_header_block(
        &mut self,
        buf: &[u8],
        stream_id: StreamId,
    ) -> Res<Option<Vec<Header>>> {
        #[cfg(feature = "build-fuzzing-corpus")]
        crate::fuzz::write_item_to_fuzzing_corpus(stream_id, buf);

        debug!("[{self}] decode header block");
        let mut decoder = HeaderDecoder::new(buf);

        match decoder.decode_header_block(&self.table, self.max_entries, self.table.base()) {
            Ok(HeaderDecoderResult::Blocked(req_insert_cnt)) => {
                let found = self
                    .blocked_streams
                    .iter()
                    .any(|(id, _req)| *id == stream_id);
                if found {
                    Ok(None)
                } else if self.blocked_streams.len() >= self.max_blocked_streams {
                    Err(Error::Decompression)
                } else {
                    self.blocked_streams.push((stream_id, req_insert_cnt));
                    Ok(None)
                }
            }
            Ok(HeaderDecoderResult::Headers(h)) => {
                if decoder.get_req_insert_cnt() != 0 {
                    self.header_ack(stream_id, decoder.get_req_insert_cnt());
                    self.stats.dynamic_table_references += 1;
                }
                Ok(Some(h))
            }
            Err(_) => Err(Error::Decompression),
        }
    }

    /// # Errors
    ///
    /// When a stream has already been added.
    pub const fn add_send_stream(&mut self, stream_id: StreamId) -> Res<()> {
        if self.local_stream_id.is_some() {
            return Err(Error::Internal);
        }
        self.local_stream_id = Some(stream_id);
        Ok(())
    }

    #[must_use]
    pub const fn local_stream_id(&self) -> Option<StreamId> {
        self.local_stream_id
    }

    /// Encoder stream has been created. Add the stream id.
    ///
    /// # Errors
    ///
    /// When a stream has already been added.
    pub const fn add_recv_stream(&mut self, stream_id: StreamId) -> Res<()> {
        if self.recv_stream_id.is_some() {
            return Err(Error::Internal);
        }
        self.recv_stream_id = Some(stream_id);
        Ok(())
    }

    #[must_use]
    pub fn stats(&self) -> Stats {
        self.stats.clone()
    }
}

impl Display for Decoder {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "QPack {}", self.capacity())
    }
}

pub(crate) fn map_error(err: Error) -> Error {
    match err {
        Error::ClosedCriticalStream | Error::Internal => err,
        _ => Error::EncoderStream,
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use neqo_common::{Header, to_u64};
    use neqo_transport::{ConnectionParameters, StreamDataLimit, StreamId, StreamType};
    use test_fixture::{
        CountingConnectionIdGenerator, DEFAULT_ALPN, default_client, handshake, new_server, now,
    };

    use super::{Connection, Decoder, Error, Res, map_error};
    use crate::Settings;

    const STREAM_0: StreamId = StreamId::new(0);
    const BLOCKING_HEADER_BLOCK: &[u8] = &[0x02, 0x80, 0x10];

    struct TestDecoder {
        decoder: Decoder,
        send_stream_id: StreamId,
        recv_stream_id: StreamId,
        conn: Connection,
        peer_conn: Connection,
    }

    fn connect() -> TestDecoder {
        connect_param(300)
    }

    fn connect_param(max_table_size_decoder: u64) -> TestDecoder {
        connect_generic(max_table_size_decoder, None)
    }

    fn connect_generic(max_table_size_decoder: u64, max_data: Option<u64>) -> TestDecoder {
        let (mut conn, mut peer_conn) = max_data.map_or_else(test_fixture::connect, |max| {
            let mut conn = default_client();
            let mut peer_conn = new_server::<CountingConnectionIdGenerator, &str>(
                DEFAULT_ALPN,
                ConnectionParameters::default().max_stream_data(StreamDataLimit::UniDi, max),
            );
            handshake(&mut conn, &mut peer_conn);
            (conn, peer_conn)
        });

        // create a stream
        let recv_stream_id = peer_conn.stream_create(StreamType::UniDi).unwrap();
        let send_stream_id = conn.stream_create(StreamType::UniDi).unwrap();

        // create a decoder
        let mut decoder = Decoder::new(&Settings {
            max_table_size_encoder: 0,
            max_table_size_decoder,
            max_blocked_streams: 100,
            max_tracked_streams: 4096,
        });
        decoder.add_send_stream(send_stream_id).unwrap();
        decoder.add_recv_stream(recv_stream_id).unwrap();

        TestDecoder {
            decoder,
            send_stream_id,
            recv_stream_id,
            conn,
            peer_conn,
        }
    }

    fn recv_instruction(decoder: &mut TestDecoder, encoder_instruction: &[u8], res: &Res<()>) {
        _ = decoder
            .peer_conn
            .stream_send(decoder.recv_stream_id, encoder_instruction)
            .unwrap();
        let out = decoder.peer_conn.process_output(now());
        drop(decoder.conn.process(out.dgram(), now()));
        assert_eq!(decoder.decoder.read_instructions(&mut decoder.conn), *res);
    }

    fn send_instructions_and_check(decoder: &mut TestDecoder, decoder_instruction: &[u8]) {
        decoder.decoder.send(&mut decoder.conn).unwrap();
        let out = decoder.conn.process_output(now());
        drop(decoder.peer_conn.process(out.dgram(), now()));
        let mut buf = [0_u8; 100];
        let (amount, fin) = decoder
            .peer_conn
            .stream_recv(decoder.send_stream_id, &mut buf)
            .unwrap();
        assert!(!fin);
        assert_eq!(&buf[..amount], decoder_instruction);
    }

    fn decode_headers(
        decoder: &mut TestDecoder,
        header_block: &[u8],
        headers: &[Header],
        stream_id: StreamId,
    ) {
        let decoded_headers = decoder
            .decoder
            .decode_header_block(header_block, stream_id)
            .unwrap();
        let h = decoded_headers.unwrap();
        assert_eq!(h, headers);
    }

    fn test_instruction(
        capacity: u64,
        instruction: &[u8],
        res: &Res<()>,
        decoder_instruction: &[u8],
        check_capacity: u64,
    ) {
        let mut decoder = connect();

        if capacity > 0 {
            assert!(decoder.decoder.set_capacity(capacity).is_ok());
        }

        // recv an instruction
        recv_instruction(&mut decoder, instruction, res);

        // send decoder instruction and check that is what we expect.
        send_instructions_and_check(&mut decoder, decoder_instruction);

        if check_capacity > 0 {
            assert_eq!(decoder.decoder.capacity(), check_capacity);
        }
    }

    // test insert_with_name_ref which fails because there is not enough space in the table
    #[test]
    fn recv_insert_with_name_ref_1() {
        test_instruction(
            0,
            &[0xc4, 0x04, 0x31, 0x32, 0x33, 0x34],
            &Err(Error::EncoderStream),
            &[0x03],
            0,
        );
    }

    // test insert_name_ref that succeeds
    #[test]
    fn recv_insert_with_name_ref_2() {
        test_instruction(
            100,
            &[0xc4, 0x04, 0x31, 0x32, 0x33, 0x34],
            &Ok(()),
            &[0x03, 0x01],
            0,
        );
    }

    /// `dynamic_table_inserts` counts each insert instruction.
    #[test]
    fn dynamic_table_inserts_stat_increments() {
        let mut decoder = connect();
        assert!(decoder.decoder.set_capacity(200).is_ok());
        assert_eq!(decoder.decoder.stats().dynamic_table_inserts, 0);

        recv_instruction(&mut decoder, &[0xc4, 0x04, 0x31, 0x32, 0x33, 0x34], &Ok(()));
        assert_eq!(decoder.decoder.stats().dynamic_table_inserts, 1);

        recv_instruction(&mut decoder, &[0xc4, 0x04, 0x35, 0x36, 0x37, 0x38], &Ok(()));
        assert_eq!(decoder.decoder.stats().dynamic_table_inserts, 2);
    }

    // test insert with name literal - succeeds
    #[test]
    fn recv_insert_with_name_litarel_2() {
        test_instruction(
            200,
            &[
                0x4e, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2d, 0x6c, 0x65, 0x6e, 0x67, 0x74,
                0x68, 0x04, 0x31, 0x32, 0x33, 0x34,
            ],
            &Ok(()),
            &[0x03, 0x01],
            0,
        );
    }

    #[test]
    fn recv_change_capacity() {
        test_instruction(0, &[0x3f, 0xa9, 0x01], &Ok(()), &[0x03], 200);
    }

    #[test]
    fn recv_change_capacity_too_big() {
        test_instruction(
            0,
            &[0x3f, 0xf1, 0x02],
            &Err(Error::EncoderStream),
            &[0x03],
            0,
        );
    }

    // this test tests header decoding, the header acks command and the insert count increment
    // command.
    #[test]
    fn duplicate() {
        let mut decoder = connect();

        assert!(decoder.decoder.set_capacity(100).is_ok());

        // receive an instruction
        recv_instruction(
            &mut decoder,
            &[
                0x4e, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2d, 0x6c, 0x65, 0x6e, 0x67, 0x74,
                0x68, 0x04, 0x31, 0x32, 0x33, 0x34,
            ],
            &Ok(()),
        );

        // receive the second instruction, a duplicate instruction.
        recv_instruction(&mut decoder, &[0x00], &Ok(()));

        send_instructions_and_check(&mut decoder, &[0x03, 0x02]);
    }

    struct TestElement {
        pub headers: Vec<Header>,
        pub header_block: &'static [u8],
        pub encoder_inst: &'static [u8],
    }

    #[test]
    fn encode_incr_encode_header_ack_some() {
        // 1. Decoder receives an instruction (header and value both as literal)
        // 2. Decoder process the instruction and sends an increment instruction.
        // 3. Decoder receives another two instruction (header and value both as literal) and a
        //    header block.
        // 4. Now it sends only a header ack and an increment instruction with increment==1.
        let headers = vec![
            Header::new("my-headera", "my-valuea"),
            Header::new("my-headerb", "my-valueb"),
        ];
        let header_block = &[0x03, 0x81, 0x10, 0x11];
        let first_encoder_inst = &[
            0x4a, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72, 0x61, 0x09, 0x6d, 0x79,
            0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x61,
        ];
        let second_encoder_inst = &[
            0x4a, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72, 0x62, 0x09, 0x6d, 0x79,
            0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x62, 0x4a, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61,
            0x64, 0x65, 0x72, 0x63, 0x09, 0x6d, 0x79, 0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x63,
        ];

        let mut decoder = connect();

        assert!(decoder.decoder.set_capacity(200).is_ok());

        recv_instruction(&mut decoder, first_encoder_inst, &Ok(()));

        send_instructions_and_check(&mut decoder, &[0x03, 0x1]);

        recv_instruction(&mut decoder, second_encoder_inst, &Ok(()));

        decode_headers(&mut decoder, header_block, &headers, STREAM_0);

        send_instructions_and_check(&mut decoder, &[0x80, 0x1]);
    }

    #[test]
    fn encode_incr_encode_header_ack_all() {
        // 1. Decoder receives an instruction (header and value both as literal)
        // 2. Decoder process the instruction and sends an increment instruction.
        // 3. Decoder receives another instruction (header and value both as literal) and a header
        //    block.
        // 4. Now it sends only a header ack.
        let headers = vec![
            Header::new("my-headera", "my-valuea"),
            Header::new("my-headerb", "my-valueb"),
        ];
        let header_block = &[0x03, 0x81, 0x10, 0x11];
        let first_encoder_inst = &[
            0x4a, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72, 0x61, 0x09, 0x6d, 0x79,
            0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x61,
        ];
        let second_encoder_inst = &[
            0x4a, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72, 0x62, 0x09, 0x6d, 0x79,
            0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x62,
        ];

        let mut decoder = connect();

        assert!(decoder.decoder.set_capacity(200).is_ok());

        recv_instruction(&mut decoder, first_encoder_inst, &Ok(()));

        send_instructions_and_check(&mut decoder, &[0x03, 0x1]);

        recv_instruction(&mut decoder, second_encoder_inst, &Ok(()));

        decode_headers(&mut decoder, header_block, &headers, STREAM_0);

        send_instructions_and_check(&mut decoder, &[0x80]);
    }

    #[test]
    fn header_ack_all() {
        // Send two instructions to insert values into the dynamic table and then send a header
        // that references them both. The result should be only a header acknowledgement.
        let headers = vec![
            Header::new("my-headera", "my-valuea"),
            Header::new("my-headerb", "my-valueb"),
        ];
        let header_block = &[0x03, 0x81, 0x10, 0x11];
        let encoder_inst = &[
            0x4a, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72, 0x61, 0x09, 0x6d, 0x79,
            0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x61, 0x4a, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61,
            0x64, 0x65, 0x72, 0x62, 0x09, 0x6d, 0x79, 0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x62,
        ];

        let mut decoder = connect();

        assert!(decoder.decoder.set_capacity(200).is_ok());

        recv_instruction(&mut decoder, encoder_inst, &Ok(()));

        decode_headers(&mut decoder, header_block, &headers, STREAM_0);

        send_instructions_and_check(&mut decoder, &[0x03, 0x80]);
    }

    #[test]
    fn header_ack_and_incr_instruction() {
        // Send two instructions to insert values into the dynamic table and then send a header
        // that references only the first. The result should be a header acknowledgement and a
        // increment instruction.
        let headers = vec![Header::new("my-headera", "my-valuea")];
        let header_block = &[0x02, 0x80, 0x10];
        let encoder_inst = &[
            0x4a, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72, 0x61, 0x09, 0x6d, 0x79,
            0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x61, 0x4a, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61,
            0x64, 0x65, 0x72, 0x62, 0x09, 0x6d, 0x79, 0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x62,
        ];

        let mut decoder = connect();

        assert!(decoder.decoder.set_capacity(200).is_ok());

        recv_instruction(&mut decoder, encoder_inst, &Ok(()));

        decode_headers(&mut decoder, header_block, &headers, STREAM_0);

        send_instructions_and_check(&mut decoder, &[0x03, 0x80, 0x01]);
    }

    #[test]
    fn header_block_decoder() {
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
                header_block: &[0x02, 0x00, 0x80],
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

        let mut decoder = connect();

        assert!(decoder.decoder.set_capacity(200).is_ok());

        for (i, t) in test_cases.iter().enumerate() {
            // receive an instruction
            if !t.encoder_inst.is_empty() {
                recv_instruction(&mut decoder, t.encoder_inst, &Ok(()));
            }

            decode_headers(
                &mut decoder,
                t.header_block,
                &t.headers,
                StreamId::from(to_u64(i)),
            );
        }

        // test header acks and the insert count increment command
        send_instructions_and_check(&mut decoder, &[0x03, 0x82, 0x83, 0x84]);
    }

    #[test]
    fn header_block_decoder_huffman() {
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
                header_block: &[0x02, 0x00, 0x80],
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

        let mut decoder = connect();

        assert!(decoder.decoder.set_capacity(200).is_ok());

        for (i, t) in test_cases.iter().enumerate() {
            // receive an instruction.
            if !t.encoder_inst.is_empty() {
                recv_instruction(&mut decoder, t.encoder_inst, &Ok(()));
            }

            decode_headers(
                &mut decoder,
                t.header_block,
                &t.headers,
                StreamId::from(to_u64(i)),
            );
        }

        // test header acks and the insert count increment command
        send_instructions_and_check(&mut decoder, &[0x03, 0x82, 0x83, 0x84]);
    }

    #[test]
    fn subtract_overflow_in_header_ack() {
        const HEADER_BLOCK_1: &[u8] = &[0x03, 0x81, 0x10, 0x11];
        const ENCODER_INST: &[u8] = &[
            0x4a, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72, 0x61, 0x09, 0x6d, 0x79,
            0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x61, 0x4a, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61,
            0x64, 0x65, 0x72, 0x62, 0x09, 0x6d, 0x79, 0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x62,
        ];
        const HEADER_BLOCK_2: &[u8] = &[0x02, 0x80, 0x10];
        // Test for issue https://github.com/mozilla/neqo/issues/475
        // Send two instructions to insert values into the dynamic table and send a header
        // that references them both. This will increase number of acked inserts in the table
        // to 2. Then send a header that references only one of them which shouldn't increase
        // number of acked inserts.
        let headers = vec![
            Header::new("my-headera", "my-valuea"),
            Header::new("my-headerb", "my-valueb"),
        ];

        let mut decoder = connect();

        assert!(decoder.decoder.set_capacity(200).is_ok());

        recv_instruction(&mut decoder, ENCODER_INST, &Ok(()));

        decode_headers(&mut decoder, HEADER_BLOCK_1, &headers, STREAM_0);

        let headers = vec![Header::new("my-headera", "my-valuea")];

        decode_headers(&mut decoder, HEADER_BLOCK_2, &headers, STREAM_0);
    }

    #[test]
    fn base_larger_than_entry_count() {
        // Test for issue https://github.com/mozilla/neqo/issues/533
        // Send instruction that inserts 2 fields into the dynamic table and send a header that
        // uses base larger than 2.
        const ENCODER_INST: &[u8] = &[
            0x4a, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72, 0x61, 0x09, 0x6d, 0x79,
            0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x61, 0x4a, 0x6d, 0x79, 0x2d, 0x68, 0x65, 0x61,
            0x64, 0x65, 0x72, 0x62, 0x09, 0x6d, 0x79, 0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x62,
        ];

        const HEADER_BLOCK: &[u8] = &[0x03, 0x03, 0x83, 0x84];

        let headers = vec![
            Header::new("my-headerb", "my-valueb"),
            Header::new("my-headera", "my-valuea"),
        ];

        let mut decoder = connect();

        assert!(decoder.decoder.set_capacity(200).is_ok());

        recv_instruction(&mut decoder, ENCODER_INST, &Ok(()));

        decode_headers(&mut decoder, HEADER_BLOCK, &headers, STREAM_0);
    }

    #[test]
    fn blocked_stream_limit_enforced() {
        // A header block whose Required Insert Count refers to a dynamic table
        // entry that has not been inserted yet leaves the stream blocked. RFC 9204
        // Section 2.1.2 allows no more blocked streams than the advertised
        // SETTINGS_QPACK_BLOCKED_STREAMS value; exceeding it is a connection error
        // of type QPACK_DECOMPRESSION_FAILED (Error::Decompression here).
        let mut decoder = Decoder::new(&Settings {
            max_table_size_encoder: 0,
            max_table_size_decoder: 300,
            max_blocked_streams: 1,
            max_tracked_streams: 4096,
        });
        // One blocked stream is within the advertised limit.
        assert!(
            decoder
                .decode_header_block(BLOCKING_HEADER_BLOCK, StreamId::new(0))
                .unwrap()
                .is_none()
        );
        // A second, distinct stream is one more than advertised.
        assert!(
            decoder
                .decode_header_block(BLOCKING_HEADER_BLOCK, StreamId::new(4))
                .is_err()
        );

        // A limit of zero leaves no room for any blocked stream.
        let mut decoder = Decoder::new(&Settings {
            max_table_size_encoder: 0,
            max_table_size_decoder: 300,
            max_blocked_streams: 0,
            max_tracked_streams: 4096,
        });
        assert!(
            decoder
                .decode_header_block(BLOCKING_HEADER_BLOCK, StreamId::new(0))
                .is_err()
        );
    }

    #[test]
    fn cancel_blocked_stream_before_capacity() {
        let mut decoder = connect();
        assert!(
            decoder
                .decoder
                .decode_header_block(BLOCKING_HEADER_BLOCK, STREAM_0)
                .unwrap()
                .is_none()
        );
        decoder.decoder.cancel_stream(STREAM_0);
        assert!(decoder.decoder.blocked_streams.is_empty());
        send_instructions_and_check(&mut decoder, &[0x03, 0x40]);
    }

    #[test]
    fn cancel_stream_max_table_size_zero() {
        let mut decoder = connect_param(0);
        decoder.decoder.cancel_stream(STREAM_0);
        send_instructions_and_check(&mut decoder, &[0x03]);
    }

    /// A peer that keeps the decoder stream flow-control limited must not make the send buffer
    /// grow with the total number of bytes sent.
    #[test]
    fn send_buf_bounded_when_flow_control_limited() {
        const WINDOW: usize = 100;
        let mut decoder = connect_generic(300, Some(to_u64(WINDOW)));

        let mut total = 0;
        let mut queue = 2 * WINDOW; // Keep more pending than the peer allows.
        for _ in 0..100 {
            for _ in 0..queue {
                decoder.decoder.cancel_stream(STREAM_0);
            }
            let before = decoder.decoder.send_buf.len();
            decoder.decoder.send(&mut decoder.conn).unwrap();
            assert!(!decoder.decoder.send_buf.is_empty());
            queue = before - decoder.decoder.send_buf.len();
            total += queue;

            let out = decoder.conn.process_output(now());
            drop(decoder.peer_conn.process(out.dgram(), now()));
            let mut buf = [0; 2 * WINDOW];
            _ = decoder
                .peer_conn
                .stream_recv(decoder.send_stream_id, &mut buf)
                .unwrap();
            let out = decoder.peer_conn.process_output(now());
            drop(decoder.conn.process(out.dgram(), now()));
        }
        assert!(total > 50 * WINDOW);
        assert!(decoder.decoder.send_buf.capacity() < 8 * WINDOW);
    }

    #[test]
    fn duplicate_recv_stream() {
        let mut decoder = Decoder::new(&Settings::default());
        decoder.add_recv_stream(StreamId::new(3)).unwrap();
        assert_eq!(
            decoder.add_recv_stream(StreamId::new(7)),
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
        assert_eq!(map_error(Error::ChangeCapacity), Error::EncoderStream);
    }
}
