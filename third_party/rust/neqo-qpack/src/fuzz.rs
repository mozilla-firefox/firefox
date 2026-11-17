// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

//! Fuzzing support for QPACK.

#[cfg(feature = "build-fuzzing-corpus")]
pub use write_corpus::write_item_to_fuzzing_corpus;

#[cfg(feature = "build-fuzzing-corpus")]
/// Helpers to write data to the fuzzing corpus.
mod write_corpus {
    use neqo_transport::StreamId;

    /// Write QPACK data to the fuzzing corpus.
    pub fn write_item_to_fuzzing_corpus(stream_id: StreamId, buf: &[u8]) {
        let mut data = Vec::with_capacity(size_of::<u64>() + buf.len());
        data.extend_from_slice(&stream_id.as_u64().to_le_bytes());
        data.extend_from_slice(buf);
        neqo_common::write_item_to_fuzzing_corpus("qpack", &data);
    }
}

#[cfg(fuzzing)]
/// Helpers to support fuzzing.
mod fuzzing {
    use crate::{
        Decoder, Error, Res,
        decoder::map_error,
        reader::{ReadByte, Reader},
    };

    /// Buffer wrapper that implements `ReadByte` and `Reader` for a byte slice.
    /// Returns `NeedMoreData` when the buffer is exhausted.
    struct BufferReader<'a> {
        buf: &'a [u8],
        offset: usize,
    }

    impl ReadByte for BufferReader<'_> {
        fn read_byte(&mut self) -> Res<u8> {
            if self.offset < self.buf.len() {
                let b = self.buf[self.offset];
                self.offset += 1;
                Ok(b)
            } else {
                Err(Error::NeedMoreData)
            }
        }
    }

    impl Reader for BufferReader<'_> {
        fn read(&mut self, buf: &mut [u8]) -> Res<usize> {
            let available = self.buf.len() - self.offset;
            let to_read = buf.len().min(available);
            buf[..to_read].copy_from_slice(&self.buf[self.offset..self.offset + to_read]);
            self.offset += to_read;
            Ok(to_read)
        }
    }

    impl<'a> BufferReader<'a> {
        const fn new(buf: &'a [u8]) -> Self {
            Self { buf, offset: 0 }
        }
    }

    #[expect(clippy::multiple_inherent_impl, reason = "Fuzzing-only API.")]
    impl Decoder {
        /// Processes encoder stream data from a byte buffer.
        /// This populates the dynamic table with header entries from encoder instructions.
        ///
        /// # Errors
        ///
        /// May return `Error::EncoderStream` if the encoder instructions are malformed.
        pub fn receive_encoder_stream(&mut self, buf: &[u8]) -> Res<()> {
            let mut recv = BufferReader::new(buf);
            self.process_instructions(&mut recv).map_err(map_error)
        }
    }
}
