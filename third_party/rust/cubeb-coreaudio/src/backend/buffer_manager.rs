use std::cmp::Ordering;
use std::fmt;
use std::os::raw::c_void;
use std::slice;

use cubeb_backend::SampleFormat;
use num_traits::AsPrimitive;

use super::ringbuf::traits::{Consumer, Observer, Producer, Split};
use super::ringbuf::HeapRb as RingBuffer;

use self::LinearBuffer::*;
use self::RingBufferConsumer::*;
use self::RingBufferProducer::*;

// Shuffles the data so that the first n channels of the interleaved buffer are overwritten by
// the remaining channels.
fn drop_first_n_channels_in_place<T: Copy>(
    n: usize,
    data: &mut [T],
    frame_count: usize,
    channel_count: usize,
) {
    // This function works if the numbers are equal but it's not particularly useful, so we hope to
    // catch issues by checking using > and not >= here.
    assert!(channel_count > n);
    let mut read_idx: usize = 0;
    let mut write_idx: usize = 0;

    let channel_to_keep = channel_count - n;
    for _ in 0..frame_count {
        read_idx += n;
        for _ in 0..channel_to_keep {
            data[write_idx] = data[read_idx];
            read_idx += 1;
            write_idx += 1;
        }
    }
}

trait DataType: AsPrimitive<f32> {
    fn from_f32(v: f32) -> Self;
}

impl<T: AsPrimitive<f32>> DataType for T
where
    f32: AsPrimitive<T>,
{
    fn from_f32(v: f32) -> T {
        v.as_()
    }
}

// It can be that the a stereo microphone is in use, but the user asked for mono input. In this
// particular case, downmix the stereo pair into a mono channel. In all other cases, simply drop
// the remaining channels before appending to the ringbuffer, becauses there is no right or wrong
// way to do this, unlike with the output side, where proper channel matrixing can be done.
// Return the number of valid samples in the buffer.
fn remix_or_drop_channels<T: DataType>(
    input_channels: usize,
    output_channels: usize,
    data: &mut [T],
    frame_count: usize,
) -> usize {
    assert!(input_channels >= output_channels);
    // Nothing to do, just return
    if input_channels == output_channels {
        return output_channels * frame_count;
    }
    // Simple stereo downmix
    if input_channels == 2 && output_channels == 1 {
        let mut read_idx = 0;
        for (write_idx, _) in (0..frame_count).enumerate() {
            let avg = (data[read_idx].as_() + data[read_idx + 1].as_()) / 2.0;
            data[write_idx] = DataType::from_f32(avg);
            read_idx += 2;
        }
        return output_channels * frame_count;
    }
    // Drop excess channels
    let mut read_idx = 0;
    let mut write_idx = 0;
    let channel_dropped_count = input_channels - output_channels;
    for _ in 0..frame_count {
        for _ in 0..output_channels {
            data[write_idx] = data[read_idx];
            write_idx += 1;
            read_idx += 1;
        }
        read_idx += channel_dropped_count;
    }
    output_channels * frame_count
}

fn process_data<T: DataType>(
    data: *mut c_void,
    frame_count: usize,
    input_channel_count: usize,
    input_channels_to_ignore: usize,
    output_channel_count: usize,
) -> &'static [T] {
    assert!(
        input_channels_to_ignore == 0
            || input_channel_count >= input_channels_to_ignore + output_channel_count
    );
    let input_slice = unsafe {
        slice::from_raw_parts_mut::<T>(data as *mut T, frame_count * input_channel_count)
    };
    match input_channel_count.cmp(&output_channel_count) {
        Ordering::Equal => {
            assert_eq!(input_channels_to_ignore, 0);
            input_slice
        }
        Ordering::Greater => {
            if input_channels_to_ignore > 0 {
                drop_first_n_channels_in_place(
                    input_channels_to_ignore,
                    input_slice,
                    frame_count,
                    input_channel_count,
                );
            }
            let new_count_remixed = remix_or_drop_channels(
                input_channel_count - input_channels_to_ignore,
                output_channel_count,
                input_slice,
                frame_count,
            );
            unsafe { slice::from_raw_parts_mut::<T>(data as *mut T, new_count_remixed) }
        }
        Ordering::Less => {
            assert!(input_channel_count < output_channel_count);
            // Upmix happens on pull.
            input_slice
        }
    }
}

pub enum RingBufferConsumer {
    IntegerRingBufferConsumer(ringbuf::HeapCons<i16>),
    FloatRingBufferConsumer(ringbuf::HeapCons<f32>),
}

pub enum RingBufferProducer {
    IntegerRingBufferProducer(ringbuf::HeapProd<i16>),
    FloatRingBufferProducer(ringbuf::HeapProd<f32>),
}

pub enum LinearBuffer {
    IntegerLinearBuffer(Vec<i16>),
    FloatLinearBuffer(Vec<f32>),
}

pub struct BufferManager {
    consumer: RingBufferConsumer,
    producer: RingBufferProducer,
    linear_buffer: LinearBuffer,
    // The number of channels in the interleaved data given to push_data
    input_channel_count: usize,
    // The number of channels that needs to be skipped in the beginning of input_channel_count
    input_channels_to_ignore: usize,
    // The number of channels we actually needs, which is also the channel count of the
    // processed data stored in the internal ring buffer.
    output_channel_count: usize,
}

impl BufferManager {
    pub fn new(
        format: SampleFormat,
        buffer_size_frames: usize,
        input_channel_count: usize,
        input_channels_to_ignore: usize,
        output_channel_count: usize,
    ) -> Self {
        assert!(
            (input_channels_to_ignore == 0 && input_channel_count == 1)
                || input_channel_count >= input_channels_to_ignore + output_channel_count
        );
        // 8 times the expected callback size, to handle the input callback being caled multiple
        //   times in a row correctly.
        let buffer_element_count = output_channel_count * buffer_size_frames * 8;
        match format {
            SampleFormat::S16LE | SampleFormat::S16BE | SampleFormat::S16NE => {
                let ring = RingBuffer::<i16>::new(buffer_element_count);
                let (prod, cons) = ring.split();
                Self {
                    producer: IntegerRingBufferProducer(prod),
                    consumer: IntegerRingBufferConsumer(cons),
                    linear_buffer: IntegerLinearBuffer(Vec::<i16>::with_capacity(
                        buffer_element_count,
                    )),
                    input_channel_count,
                    input_channels_to_ignore,
                    output_channel_count,
                }
            }
            SampleFormat::Float32LE | SampleFormat::Float32BE | SampleFormat::Float32NE => {
                let ring = RingBuffer::<f32>::new(buffer_element_count);
                let (prod, cons) = ring.split();
                Self {
                    producer: FloatRingBufferProducer(prod),
                    consumer: FloatRingBufferConsumer(cons),
                    linear_buffer: FloatLinearBuffer(Vec::<f32>::with_capacity(
                        buffer_element_count,
                    )),
                    input_channel_count,
                    input_channels_to_ignore,
                    output_channel_count,
                }
            }
        }
    }
    fn stored_channel_count(&self) -> usize {
        if self.output_channel_count > self.input_channel_count {
            // This case allows upmix from mono on pull.
            self.input_channel_count
        } else {
            // Other cases only downmix on push.
            self.output_channel_count
        }
    }
    fn input_channel_count(&self) -> usize {
        self.input_channel_count
    }
    fn input_channels_to_ignore(&self) -> usize {
        self.input_channels_to_ignore
    }
    fn output_channel_count(&self) -> usize {
        self.output_channel_count
    }
    pub fn push_data(&mut self, data: *mut c_void, frame_count: usize) {
        let to_push = frame_count * self.stored_channel_count();
        let input_channel_count = self.input_channel_count();
        let input_channels_to_ignore = self.input_channels_to_ignore();
        let output_channel_count = self.output_channel_count();
        let pushed = match &mut self.producer {
            RingBufferProducer::FloatRingBufferProducer(p) => {
                let processed_input = process_data(
                    data,
                    frame_count,
                    input_channel_count,
                    input_channels_to_ignore,
                    output_channel_count,
                );
                p.push_slice(processed_input)
            }
            RingBufferProducer::IntegerRingBufferProducer(p) => {
                let processed_input = process_data(
                    data,
                    frame_count,
                    input_channel_count,
                    input_channels_to_ignore,
                    output_channel_count,
                );
                p.push_slice(processed_input)
            }
        };
        assert!(pushed <= to_push);
        if pushed != to_push {
            cubeb_alog!(
                "Input ringbuffer full, could only push {} instead of {}",
                pushed,
                to_push
            );
        }
    }
    fn pull_data(&mut self, data: *mut c_void, needed_samples: usize) {
        assert_eq!(needed_samples % self.output_channel_count(), 0);
        let needed_frames = needed_samples / self.output_channel_count();
        let to_pull = needed_frames * self.stored_channel_count();
        match &mut self.consumer {
            IntegerRingBufferConsumer(p) => {
                let input: &mut [i16] =
                    unsafe { slice::from_raw_parts_mut::<i16>(data as *mut i16, needed_samples) };
                let pulled = p.pop_slice(input);
                if pulled < to_pull {
                    cubeb_alog!(
                        "Underrun during input data pull: (needed: {}, available: {})",
                        to_pull,
                        pulled
                    );
                    for i in 0..(to_pull - pulled) {
                        input[pulled + i] = 0;
                    }
                }
                if needed_samples > to_pull {
                    // Mono upmix. This can happen with voice processing.
                    let mut write_idx = needed_samples;
                    for read_idx in (0..to_pull).rev() {
                        write_idx -= self.output_channel_count();
                        for offset in 0..self.output_channel_count() {
                            input[write_idx + offset] = input[read_idx];
                        }
                    }
                }
            }
            FloatRingBufferConsumer(p) => {
                let input: &mut [f32] =
                    unsafe { slice::from_raw_parts_mut::<f32>(data as *mut f32, needed_samples) };
                let pulled = p.pop_slice(input);
                if pulled < to_pull {
                    cubeb_alog!(
                        "Underrun during input data pull: (needed: {}, available: {})",
                        to_pull,
                        pulled
                    );
                    for i in 0..(to_pull - pulled) {
                        input[pulled + i] = 0.0;
                    }
                }
                if needed_samples > to_pull {
                    // Mono upmix. This can happen with voice processing.
                    let mut write_idx = needed_samples;
                    for read_idx in (0..to_pull).rev() {
                        write_idx -= self.output_channel_count();
                        for offset in 0..self.output_channel_count() {
                            input[write_idx + offset] = input[read_idx];
                        }
                    }
                }
            }
        }
    }
    pub fn get_linear_data(&mut self, frame_count: usize) -> *mut c_void {
        let output_sample_count = frame_count * self.output_channel_count();
        let p = match &mut self.linear_buffer {
            LinearBuffer::IntegerLinearBuffer(b) => {
                b.resize(output_sample_count, 0);
                b.as_mut_ptr() as *mut c_void
            }
            LinearBuffer::FloatLinearBuffer(b) => {
                b.resize(output_sample_count, 0.);
                b.as_mut_ptr() as *mut c_void
            }
        };
        self.pull_data(p, output_sample_count);

        p
    }
    // The cached ringbuf endpoints are not Sync: even occupied_len() updates
    // their local indices. Query the producer from the input callback so a
    // duplex stream never touches the consumer from both audio callbacks.
    pub fn available_frames_from_producer(&self) -> usize {
        assert_ne!(self.stored_channel_count(), 0);
        let stored_samples = match &self.producer {
            IntegerRingBufferProducer(p) => p.occupied_len(),
            FloatRingBufferProducer(p) => p.occupied_len(),
        };
        stored_samples / self.stored_channel_count()
    }
    // Only call this from the callback that consumes the input data (the
    // output callback for duplex streams, or the input callback otherwise).
    pub fn available_frames_from_consumer(&self) -> usize {
        assert_ne!(self.stored_channel_count(), 0);
        let stored_samples = match &self.consumer {
            IntegerRingBufferConsumer(p) => p.occupied_len(),
            FloatRingBufferConsumer(p) => p.occupied_len(),
        };
        stored_samples / self.stored_channel_count()
    }
    pub fn trim(&mut self, final_frame_count: usize) {
        let final_sample_count = final_frame_count * self.stored_channel_count();
        match &mut self.consumer {
            IntegerRingBufferConsumer(c) => {
                let available = c.occupied_len();
                assert!(available >= final_sample_count);
                let to_pop = available - final_sample_count;
                c.skip(to_pop);
            }
            FloatRingBufferConsumer(c) => {
                let available = c.occupied_len();
                assert!(available >= final_sample_count);
                let to_pop = available - final_sample_count;
                c.skip(to_pop);
            }
        }
    }
}

impl fmt::Debug for BufferManager {
    fn fmt(&self, _: &mut fmt::Formatter<'_>) -> fmt::Result {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn producer_available_frames_tracks_consumption() {
        let mut integer_data = [0i16; 8];
        let mut float_data = [0f32; 8];
        for (format, data) in [
            (
                SampleFormat::S16NE,
                integer_data.as_mut_ptr() as *mut c_void,
            ),
            (
                SampleFormat::Float32NE,
                float_data.as_mut_ptr() as *mut c_void,
            ),
        ] {
            // Include remixing so both views count stored frames, not samples
            // or frames in the output format.
            for (input_channels, output_channels) in [(1, 1), (2, 2), (2, 1)] {
                let mut buffer = BufferManager::new(format, 4, input_channels, 0, output_channels);
                assert_eq!(buffer.available_frames_from_producer(), 0);

                buffer.push_data(data, 4);
                assert_eq!(buffer.available_frames_from_producer(), 4);
                assert_eq!(buffer.available_frames_from_consumer(), 4);

                buffer.get_linear_data(2);
                assert_eq!(buffer.available_frames_from_producer(), 2);

                buffer.trim(1);
                assert_eq!(buffer.available_frames_from_producer(), 1);

                buffer.get_linear_data(1);
                assert_eq!(buffer.available_frames_from_producer(), 0);
            }
        }
    }

    #[test]
    fn remix_stereo_ints() {
        let mut data = [i16::MAX / 2 + 1, i16::MAX / 2 + 1];
        assert_eq!(remix_or_drop_channels(2, 1, &mut data, 1), 1);
    }
}
