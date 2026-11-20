// Copyright © 2017-2018 Mozilla Foundation
//
// This program is made available under an ISC-style license.  See the
// accompanying file LICENSE for details.

use crate::backend::cork_state::CorkState;
use crate::backend::*;
use cubeb_backend::{
    ffi, log_enabled, ChannelLayout, DeviceId, DeviceRef, Error, InputProcessingParams, Result,
    SampleFormat, StreamOps, StreamParamsRef, StreamPrefs,
};
use pulse::{self, CVolumeExt, ChannelMapExt, SampleSpecExt, StreamLatency, USecExt};
use pulse_ffi::*;
use ringbuf::RingBuffer;
use std::ffi::{CStr, CString};
use std::os::raw::{c_long, c_void};
use std::slice;
use std::sync::atomic::{AtomicPtr, AtomicUsize, Ordering};
use std::{mem, ptr};

use self::LinearInputBuffer::*;
use self::RingBufferConsumer::*;
use self::RingBufferProducer::*;

const PULSE_NO_GAIN: f32 = -1.0;

/// Iterator interface to `ChannelLayout`.
///
/// Iterates each channel in the set represented by `ChannelLayout`.
struct ChannelLayoutIter {
    /// The layout set being iterated
    layout: ChannelLayout,
    /// The next flag to test
    index: u8,
}

fn channel_layout_iter(layout: ChannelLayout) -> ChannelLayoutIter {
    let index = 0;
    ChannelLayoutIter { layout, index }
}

impl Iterator for ChannelLayoutIter {
    type Item = ChannelLayout;

    fn next(&mut self) -> Option<Self::Item> {
        while !self.layout.is_empty() {
            let test = Self::Item::from_bits_truncate(1 << self.index);
            self.index += 1;
            if self.layout.contains(test) {
                self.layout.remove(test);
                return Some(test);
            }
        }
        None
    }
}

fn cubeb_channel_to_pa_channel(channel: ffi::cubeb_channel) -> pa_channel_position_t {
    match channel {
        ffi::CHANNEL_FRONT_LEFT => PA_CHANNEL_POSITION_FRONT_LEFT,
        ffi::CHANNEL_FRONT_RIGHT => PA_CHANNEL_POSITION_FRONT_RIGHT,
        ffi::CHANNEL_FRONT_CENTER => PA_CHANNEL_POSITION_FRONT_CENTER,
        ffi::CHANNEL_LOW_FREQUENCY => PA_CHANNEL_POSITION_LFE,
        ffi::CHANNEL_BACK_LEFT => PA_CHANNEL_POSITION_REAR_LEFT,
        ffi::CHANNEL_BACK_RIGHT => PA_CHANNEL_POSITION_REAR_RIGHT,
        ffi::CHANNEL_FRONT_LEFT_OF_CENTER => PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
        ffi::CHANNEL_FRONT_RIGHT_OF_CENTER => PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
        ffi::CHANNEL_BACK_CENTER => PA_CHANNEL_POSITION_REAR_CENTER,
        ffi::CHANNEL_SIDE_LEFT => PA_CHANNEL_POSITION_SIDE_LEFT,
        ffi::CHANNEL_SIDE_RIGHT => PA_CHANNEL_POSITION_SIDE_RIGHT,
        ffi::CHANNEL_TOP_CENTER => PA_CHANNEL_POSITION_TOP_CENTER,
        ffi::CHANNEL_TOP_FRONT_LEFT => PA_CHANNEL_POSITION_TOP_FRONT_LEFT,
        ffi::CHANNEL_TOP_FRONT_CENTER => PA_CHANNEL_POSITION_TOP_FRONT_CENTER,
        ffi::CHANNEL_TOP_FRONT_RIGHT => PA_CHANNEL_POSITION_TOP_FRONT_RIGHT,
        ffi::CHANNEL_TOP_BACK_LEFT => PA_CHANNEL_POSITION_TOP_REAR_LEFT,
        ffi::CHANNEL_TOP_BACK_CENTER => PA_CHANNEL_POSITION_TOP_REAR_CENTER,
        ffi::CHANNEL_TOP_BACK_RIGHT => PA_CHANNEL_POSITION_TOP_REAR_RIGHT,
        _ => PA_CHANNEL_POSITION_INVALID,
    }
}

fn layout_to_channel_map(layout: ChannelLayout) -> pulse::ChannelMap {
    assert_ne!(layout, ChannelLayout::UNDEFINED);

    let mut cm = pulse::ChannelMap::init();
    for (i, channel) in channel_layout_iter(layout).enumerate() {
        cm.map[i] = cubeb_channel_to_pa_channel(channel.into());
    }
    cm.channels = layout.num_channels() as _;

    // Special case single channel center mapping as mono.
    if cm.channels == 1 && cm.map[0] == PA_CHANNEL_POSITION_FRONT_CENTER {
        cm.map[0] = PA_CHANNEL_POSITION_MONO;
    }

    cm
}

fn default_layout_for_channels(ch: u32) -> ChannelLayout {
    match ch {
        1 => ChannelLayout::MONO,
        2 => ChannelLayout::STEREO,
        3 => ChannelLayout::_3F,
        4 => ChannelLayout::QUAD,
        5 => ChannelLayout::_3F2,
        6 => ChannelLayout::_3F_LFE | ChannelLayout::SIDE_LEFT | ChannelLayout::SIDE_RIGHT,
        7 => ChannelLayout::_3F3R_LFE,
        8 => ChannelLayout::_3F4_LFE,
        _ => panic!("channel must be between 1 to 8."),
    }
}

pub struct Device(ffi::cubeb_device);

impl Drop for Device {
    fn drop(&mut self) {
        unsafe {
            if !self.0.input_name.is_null() {
                let _ = CString::from_raw(self.0.input_name as *mut _);
            }
            if !self.0.output_name.is_null() {
                let _ = CString::from_raw(self.0.output_name as *mut _);
            }
        }
    }
}

enum RingBufferConsumer {
    IntegerRingBufferConsumer(ringbuf::Consumer<i16>),
    FloatRingBufferConsumer(ringbuf::Consumer<f32>),
}

enum RingBufferProducer {
    IntegerRingBufferProducer(ringbuf::Producer<i16>),
    FloatRingBufferProducer(ringbuf::Producer<f32>),
}

enum LinearInputBuffer {
    IntegerLinearInputBuffer(Vec<i16>),
    FloatLinearInputBuffer(Vec<f32>),
}

struct BufferManager {
    consumer: RingBufferConsumer,
    producer: RingBufferProducer,
    linear_input_buffer: LinearInputBuffer,
}

impl BufferManager {
    // When opening a duplex stream, the sample-spec are guaranteed to match. It's ok to have
    // either the input or output sample-spec here.
    fn new(input_buffer_size: usize, sample_spec: &pulse::SampleSpec) -> BufferManager {
        if sample_spec.format == PA_SAMPLE_S16BE || sample_spec.format == PA_SAMPLE_S16LE {
            let ring = RingBuffer::<i16>::new(input_buffer_size);
            let (prod, cons) = ring.split();
            BufferManager {
                producer: IntegerRingBufferProducer(prod),
                consumer: IntegerRingBufferConsumer(cons),
                linear_input_buffer: IntegerLinearInputBuffer(Vec::<i16>::with_capacity(
                    input_buffer_size,
                )),
            }
        } else {
            let ring = RingBuffer::<f32>::new(input_buffer_size);
            let (prod, cons) = ring.split();
            BufferManager {
                producer: FloatRingBufferProducer(prod),
                consumer: FloatRingBufferConsumer(cons),
                linear_input_buffer: FloatLinearInputBuffer(Vec::<f32>::with_capacity(
                    input_buffer_size,
                )),
            }
        }
    }

    fn push_input_data(&mut self, input_data: *const c_void, read_samples: usize) {
        match &mut self.producer {
            RingBufferProducer::FloatRingBufferProducer(p) => {
                let input_data =
                    unsafe { slice::from_raw_parts::<f32>(input_data as *const f32, read_samples) };
                // we don't do anything in particular if we can't push everything
                p.push_slice(input_data);
            }
            RingBufferProducer::IntegerRingBufferProducer(p) => {
                let input_data =
                    unsafe { slice::from_raw_parts::<i16>(input_data as *const i16, read_samples) };
                p.push_slice(input_data);
            }
        }
    }

    fn pull_input_data(&mut self, input_data: *mut c_void, needed_samples: usize) {
        match &mut self.consumer {
            IntegerRingBufferConsumer(p) => {
                let input: &mut [i16] = unsafe {
                    slice::from_raw_parts_mut::<i16>(input_data as *mut i16, needed_samples)
                };
                let read = p.pop_slice(input);
                if read < needed_samples {
                    for i in 0..(needed_samples - read) {
                        input[read + i] = 0;
                    }
                }
            }
            FloatRingBufferConsumer(p) => {
                let input: &mut [f32] = unsafe {
                    slice::from_raw_parts_mut::<f32>(input_data as *mut f32, needed_samples)
                };
                let read = p.pop_slice(input);
                if read < needed_samples {
                    for i in 0..(needed_samples - read) {
                        input[read + i] = 0.;
                    }
                }
            }
        }
    }

    fn get_linear_input_data(&mut self, nsamples: usize) -> *const c_void {
        let p = match &mut self.linear_input_buffer {
            LinearInputBuffer::IntegerLinearInputBuffer(b) => {
                b.resize(nsamples, 0);
                b.as_mut_ptr() as *mut c_void
            }
            LinearInputBuffer::FloatLinearInputBuffer(b) => {
                b.resize(nsamples, 0.);
                b.as_mut_ptr() as *mut c_void
            }
        };
        self.pull_input_data(p, nsamples);

        p
    }

    pub fn trim(&mut self, final_size: usize) {
        match &self.linear_input_buffer {
            LinearInputBuffer::IntegerLinearInputBuffer(b) => {
                let length = b.len();
                assert!(final_size <= length);
                let nframes_to_pop = length - final_size;
                self.get_linear_input_data(nframes_to_pop);
            }
            LinearInputBuffer::FloatLinearInputBuffer(b) => {
                let length = b.len();
                assert!(final_size <= length);
                let nframes_to_pop = length - final_size;
                self.get_linear_input_data(nframes_to_pop);
            }
        }
    }
    pub fn available_samples(&mut self) -> usize {
        match &self.linear_input_buffer {
            LinearInputBuffer::IntegerLinearInputBuffer(b) => b.len(),
            LinearInputBuffer::FloatLinearInputBuffer(b) => b.len(),
        }
    }
}

impl std::fmt::Debug for BufferManager {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "")
    }
}

#[repr(C)]
#[derive(Debug)]
pub struct PulseStream<'ctx> {
    context: &'ctx PulseContext,
    user_ptr: *mut c_void,
    output_stream: Option<pulse::Stream>,
    input_stream: Option<pulse::Stream>,
    data_callback: ffi::cubeb_data_callback,
    state_callback: ffi::cubeb_state_callback,
    output_preroll_event: AtomicPtr<pa_defer_event>,
    drain_timer: AtomicPtr<pa_time_event>,
    output_sample_spec: pulse::SampleSpec,
    input_sample_spec: pulse::SampleSpec,
    // output frames count excluding pre-buffering
    output_frame_count: AtomicUsize,
    shutdown: bool,
    volume: f32,
    state: ffi::cubeb_state,
    input_buffer_manager: Option<BufferManager>,
}

impl<'ctx> PulseStream<'ctx> {
    fn cancel_output_preroll(&self) -> bool {
        let output_preroll_event = self
            .output_preroll_event
            .swap(ptr::null_mut(), Ordering::AcqRel);
        if output_preroll_event.is_null() {
            return false;
        }

        self.context
            .mainloop
            .get_api()
            .defer_free(output_preroll_event);
        true
    }

    #[allow(clippy::too_many_arguments)]
    pub fn new(
        context: &'ctx PulseContext,
        stream_name: Option<&CStr>,
        input_device: DeviceId,
        input_stream_params: Option<&StreamParamsRef>,
        output_device: DeviceId,
        output_stream_params: Option<&StreamParamsRef>,
        latency_frames: u32,
        data_callback: ffi::cubeb_data_callback,
        state_callback: ffi::cubeb_state_callback,
        user_ptr: *mut c_void,
    ) -> Result<Box<Self>> {
        fn check_error(s: &pulse::Stream, u: *mut c_void) {
            let stm = unsafe { &mut *(u as *mut PulseStream) };
            if !s.get_state().is_good() {
                cubeb_alog!("Calling error callback");
                stm.state_change_callback(ffi::CUBEB_STATE_ERROR);
            }
            stm.context.mainloop.signal();
        }

        fn read_data(s: &pulse::Stream, nbytes: usize, u: *mut c_void) {
            fn read_from_input(
                s: &pulse::Stream,
                buffer: *mut *const c_void,
                size: *mut usize,
            ) -> i32 {
                let readable_size = s.readable_size().map(|s| s as i32).unwrap_or(-1);
                if readable_size > 0 && unsafe { s.peek(buffer, size).is_err() } {
                    cubeb_logv!("Error while peeking the input stream");
                    return -1;
                }
                readable_size
            }

            cubeb_alogv!("Input callback buffer size {}", nbytes);
            let stm = unsafe { &mut *(u as *mut PulseStream) };
            if stm.shutdown {
                return;
            }

            let mut read_data: *const c_void = ptr::null();
            let mut read_size: usize = 0;
            while read_from_input(s, &mut read_data, &mut read_size) > 0 {
                /* read_data can be NULL in case of a hole. */
                if !read_data.is_null() {
                    let in_frame_size = stm.input_sample_spec.frame_size();
                    let read_frames = read_size / in_frame_size;
                    let read_samples = read_size / stm.input_sample_spec.sample_size();

                    if stm.output_stream.is_some() {
                        // duplex stream: push the input data to the ring buffer.
                        stm.input_buffer_manager
                            .as_mut()
                            .unwrap()
                            .push_input_data(read_data, read_samples);
                    } else {
                        // input/capture only operation. Call callback directly
                        let got = unsafe {
                            stm.data_callback.unwrap()(
                                stm as *mut _ as *mut _,
                                stm.user_ptr,
                                read_data,
                                ptr::null_mut(),
                                read_frames as c_long,
                            )
                        };

                        if got < 0 || got as usize != read_frames {
                            let _ = s.cancel_write();
                            stm.shutdown = true;
                            if got < 0 {
                                unsafe {
                                    stm.state_callback.unwrap()(
                                        stm as *mut _ as *mut _,
                                        stm.user_ptr,
                                        ffi::CUBEB_STATE_ERROR,
                                    );
                                }
                            }
                            break;
                        }
                    }
                }

                if read_size > 0 {
                    let _ = s.drop();
                }

                if stm.shutdown {
                    return;
                }
            }
        }

        fn write_data(_: &pulse::Stream, nbytes: usize, u: *mut c_void) {
            cubeb_alogv!("Output callback to be written buffer size {}", nbytes);
            let stm = unsafe { &mut *(u as *mut PulseStream) };
            if stm.shutdown || stm.state != ffi::CUBEB_STATE_STARTED {
                return;
            }

            let nframes = nbytes / stm.output_sample_spec.frame_size();
            let first_callback = stm.output_frame_count.fetch_add(nframes, Ordering::SeqCst) == 0;
            if stm.input_stream.is_some() {
                let nsamples_input = nframes * stm.input_sample_spec.channels as usize;
                let input_buffer_manager = stm.input_buffer_manager.as_mut().unwrap();

                if first_callback {
                    let buffered_input_frames = input_buffer_manager.available_samples()
                        / stm.input_sample_spec.channels as usize;
                    if buffered_input_frames > nframes {
                        // Trim the buffer to ensure minimal roundtrip latency
                        let popped_frames = buffered_input_frames - nframes;
                        input_buffer_manager
                            .trim(nframes * stm.input_sample_spec.channels as usize);
                        cubeb_alog!("Dropping {} frames in input buffer.", popped_frames);
                    }
                }

                let p = input_buffer_manager.get_linear_input_data(nsamples_input);
                stm.trigger_user_callback(p, nbytes);
            } else {
                // Output/playback only operation.
                // Write directly to output
                debug_assert!(stm.output_stream.is_some());
                stm.trigger_user_callback(ptr::null(), nbytes);
            }
        }

        let mut stm = Box::new(PulseStream {
            context,
            output_stream: None,
            input_stream: None,
            data_callback,
            state_callback,
            user_ptr,
            output_preroll_event: AtomicPtr::new(ptr::null_mut()),
            drain_timer: AtomicPtr::new(ptr::null_mut()),
            output_sample_spec: pulse::SampleSpec::default(),
            input_sample_spec: pulse::SampleSpec::default(),
            output_frame_count: AtomicUsize::new(0),
            shutdown: false,
            volume: PULSE_NO_GAIN,
            state: ffi::CUBEB_STATE_ERROR,
            input_buffer_manager: None,
        });

        if let Some(ref context) = stm.context.context {
            stm.context.mainloop.lock();

            // Setup output stream
            if let Some(stream_params) = output_stream_params {
                match PulseStream::stream_init(context, stream_params, stream_name) {
                    Ok(s) => {
                        stm.output_sample_spec = *s.get_sample_spec();

                        s.set_state_callback(check_error, stm.as_mut() as *mut _ as *mut _);
                        s.set_write_callback(write_data, stm.as_mut() as *mut _ as *mut _);

                        let buffer_size_bytes =
                            latency_frames * stm.output_sample_spec.frame_size() as u32;

                        let battr = pa_buffer_attr {
                            maxlength: u32::MAX,
                            prebuf: u32::MAX,
                            fragsize: u32::MAX,
                            tlength: buffer_size_bytes * 2,
                            minreq: buffer_size_bytes / 4,
                        };
                        let device_name = super::try_cstr_from(output_device as *const _);
                        let mut stream_flags = pulse::StreamFlags::AUTO_TIMING_UPDATE
                            | pulse::StreamFlags::INTERPOLATE_TIMING
                            | pulse::StreamFlags::START_CORKED
                            | pulse::StreamFlags::ADJUST_LATENCY;
                        if device_name.is_some()
                            || stream_params
                                .prefs()
                                .contains(StreamPrefs::DISABLE_DEVICE_SWITCHING)
                        {
                            stream_flags |= pulse::StreamFlags::DONT_MOVE;
                        }
                        let connect_result =
                            s.connect_playback(device_name, &battr, stream_flags, None, None);
                        // Set output_stream before checking connect_result so
                        // destroy() can clean up the stream on error.
                        stm.output_stream = Some(s);
                        if let Err(e) = connect_result {
                            cubeb_log!("Output stream connect error: {}", e);
                            stm.context.mainloop.unlock();
                            stm.destroy();
                            return Err(Error::Error);
                        }
                    }
                    Err(e) => {
                        cubeb_log!("Output stream initialization error");
                        stm.context.mainloop.unlock();
                        stm.destroy();
                        return Err(e);
                    }
                }
            }

            // Set up input stream
            if let Some(stream_params) = input_stream_params {
                match PulseStream::stream_init(context, stream_params, stream_name) {
                    Ok(s) => {
                        stm.input_sample_spec = *s.get_sample_spec();

                        s.set_state_callback(check_error, stm.as_mut() as *mut _ as *mut _);
                        s.set_read_callback(read_data, stm.as_mut() as *mut _ as *mut _);

                        let buffer_size_bytes =
                            latency_frames * stm.input_sample_spec.frame_size() as u32;
                        let battr = pa_buffer_attr {
                            maxlength: u32::MAX,
                            prebuf: u32::MAX,
                            fragsize: buffer_size_bytes,
                            tlength: buffer_size_bytes,
                            minreq: buffer_size_bytes,
                        };
                        let device_name = super::try_cstr_from(input_device as *const _);
                        let mut stream_flags = pulse::StreamFlags::AUTO_TIMING_UPDATE
                            | pulse::StreamFlags::INTERPOLATE_TIMING
                            | pulse::StreamFlags::START_CORKED
                            | pulse::StreamFlags::ADJUST_LATENCY;
                        if device_name.is_some()
                            || stream_params
                                .prefs()
                                .contains(StreamPrefs::DISABLE_DEVICE_SWITCHING)
                        {
                            stream_flags |= pulse::StreamFlags::DONT_MOVE;
                        }
                        let connect_result = s.connect_record(device_name, &battr, stream_flags);
                        // Set input_stream before checking connect_result so
                        // destroy() can clean up the stream on error.
                        stm.input_stream = Some(s);
                        if let Err(e) = connect_result {
                            cubeb_log!("Input stream connect error: {}", e);
                            stm.context.mainloop.unlock();
                            stm.destroy();
                            return Err(Error::Error);
                        }
                    }
                    Err(e) => {
                        cubeb_log!("Input stream initialization error");
                        stm.context.mainloop.unlock();
                        stm.destroy();
                        return Err(e);
                    }
                }
            }

            // Duplex, set up the ringbuffer
            if input_stream_params.is_some() && output_stream_params.is_some() {
                // A bit more room in case of output underrun.
                let buffer_size_bytes =
                    2 * latency_frames * stm.input_sample_spec.frame_size() as u32;
                stm.input_buffer_manager = Some(BufferManager::new(
                    buffer_size_bytes as usize,
                    &stm.input_sample_spec,
                ))
            }

            let r = if stm.wait_until_ready() {
                /* force a timing update now, otherwise timing info does not become valid
                until some point after initialization has completed. */
                stm.update_timing_info()
            } else {
                false
            };

            stm.context.mainloop.unlock();

            if !r {
                stm.destroy();
                cubeb_log!("Error while waiting for the stream to be ready");
                return Err(Error::Error);
            }

            // TODO:
            if log_enabled() {
                if let Some(ref output_stream) = stm.output_stream {
                    let output_att = output_stream.get_buffer_attr();
                    cubeb_log!(
                        "Output buffer attributes maxlength {}, tlength {}, \
                         prebuf {}, minreq {}, fragsize {}",
                        output_att.maxlength,
                        output_att.tlength,
                        output_att.prebuf,
                        output_att.minreq,
                        output_att.fragsize
                    );
                }

                if let Some(ref input_stream) = stm.input_stream {
                    let input_att = input_stream.get_buffer_attr();
                    cubeb_log!(
                        "Input buffer attributes maxlength {}, tlength {}, \
                         prebuf {}, minreq {}, fragsize {}",
                        input_att.maxlength,
                        input_att.tlength,
                        input_att.prebuf,
                        input_att.minreq,
                        input_att.fragsize
                    );
                }
            }
        }

        Ok(stm)
    }

    fn destroy(&mut self) {
        self.context.mainloop.lock();
        self.shutdown = true;
        self.cancel_output_preroll();
        self.context.mainloop.unlock();

        self.cork(CorkState::cork());

        self.context.mainloop.lock();
        {
            if let Some(stm) = self.output_stream.take() {
                let drain_timer = self.drain_timer.load(Ordering::Acquire);
                if !drain_timer.is_null() {
                    /* there's no pa_rttime_free, so use this instead. */
                    self.context.mainloop.get_api().time_free(drain_timer);
                }
                stm.clear_state_callback();
                stm.clear_write_callback();
                let _ = stm.disconnect();
                stm.unref();
            }

            if let Some(stm) = self.input_stream.take() {
                stm.clear_state_callback();
                stm.clear_read_callback();
                let _ = stm.disconnect();
                stm.unref();
            }
        }
        self.context.mainloop.unlock();
    }
}

impl Drop for PulseStream<'_> {
    fn drop(&mut self) {
        self.destroy();
    }
}

impl StreamOps for PulseStream<'_> {
    fn start(&mut self) -> Result<()> {
        fn output_preroll(a: &pulse::MainloopApi, e: *mut pa_defer_event, u: *mut c_void) {
            let stm = unsafe { &mut *(u as *mut PulseStream) };
            let output_preroll_event = stm
                .output_preroll_event
                .swap(ptr::null_mut(), Ordering::AcqRel);
            debug_assert_eq!(output_preroll_event, e);

            if !stm.shutdown {
                let size = stm
                    .output_stream
                    .as_ref()
                    .map_or(0, |s| s.writable_size().unwrap_or(0));
                stm.trigger_user_callback(std::ptr::null(), size);
            }

            a.defer_free(e);
        }

        // Restarting a stream that is still draining is a cubeb API contract
        // violation: the pending drain_timer holds a pointer to this stream,
        // and re-prerolling would arm a second timer and leak the first.
        if !self.drain_timer.load(Ordering::Acquire).is_null() {
            cubeb_log!("Rejecting start() on a draining stream");
            return Err(Error::Error);
        }
        self.shutdown = false;
        self.cork(CorkState::uncork() | CorkState::notify());

        if self.output_stream.is_some() {
            /* When doing output-only or duplex, we need to manually call user cb once in order to
             * make things roll. This is done via a defer event in order to execute it from PA
             * server thread. */
            self.context.mainloop.lock();
            if self.output_preroll_event.load(Ordering::Acquire).is_null() {
                let output_preroll_event = self
                    .context
                    .mainloop
                    .get_api()
                    .defer_new(output_preroll, self as *const _ as *mut _);
                assert!(!output_preroll_event.is_null());
                self.output_preroll_event
                    .store(output_preroll_event, Ordering::Release);
            }
            self.context.mainloop.unlock();
        }

        Ok(())
    }

    fn stop(&mut self) -> Result<()> {
        {
            self.context.mainloop.lock();
            self.shutdown = true;
            if self.cancel_output_preroll() {
                cubeb_log!("Stream stop: cancelling output preroll");
            }
            // Cancel any pending drain timer rather than blocking until it
            // fires, matching destroy() and other backends' behaviour.
            let drain_timer = self.drain_timer.load(Ordering::Acquire);
            if !drain_timer.is_null() {
                cubeb_log!("Stream stop: cancelling drain timer");
                self.context.mainloop.get_api().time_free(drain_timer);
                self.drain_timer.store(ptr::null_mut(), Ordering::Release);
            }
            self.context.mainloop.unlock();
        }
        self.cork(CorkState::cork() | CorkState::notify());

        Ok(())
    }

    fn position(&mut self) -> Result<u64> {
        let in_thread = self.context.mainloop.in_thread();

        if !in_thread {
            self.context.mainloop.lock();
        }

        if self.output_stream.is_none() {
            cubeb_log!("Calling position() on an input-only stream");
            return Err(Error::Error);
        }

        let stm = self.output_stream.as_ref().unwrap();
        let r = match stm.get_time() {
            Ok(r_usec) => {
                let bytes = USecExt::to_bytes(r_usec, &self.output_sample_spec);
                Ok((bytes / self.output_sample_spec.frame_size()) as u64)
            }
            Err(_) => {
                cubeb_log!("Error: stm.get_time failed");
                Err(Error::Error)
            }
        };

        if !in_thread {
            self.context.mainloop.unlock();
        }

        r
    }

    fn latency(&mut self) -> Result<u32> {
        let in_thread = self.context.mainloop.in_thread();

        if !in_thread {
            self.context.mainloop.lock();
        }

        let r = match self.output_stream {
            None => {
                cubeb_log!("Error: calling latency() on an input-only stream");
                Err(Error::Error)
            }
            Some(ref stm) => match stm.get_latency() {
                Ok(StreamLatency::Positive(r_usec)) => {
                    let latency = (r_usec * pa_usec_t::from(self.output_sample_spec.rate)
                        / PA_USEC_PER_SEC) as u32;
                    Ok(latency)
                }
                Ok(_) => {
                    panic!("Can not handle negative latency values.");
                }
                Err(_) => {
                    cubeb_log!("Error: get_latency() failed for an output stream");
                    Err(Error::Error)
                }
            },
        };

        if !in_thread {
            self.context.mainloop.unlock();
        }

        r
    }

    fn input_latency(&mut self) -> Result<u32> {
        let in_thread = self.context.mainloop.in_thread();

        if !in_thread {
            self.context.mainloop.lock();
        }

        let r = match self.input_stream {
            None => {
                cubeb_log!("Error: calling input_latency() on an output-only stream");
                Err(Error::Error)
            }
            Some(ref stm) => match stm.get_latency() {
                Ok(StreamLatency::Positive(w_usec)) => {
                    let latency = (w_usec * pa_usec_t::from(self.input_sample_spec.rate)
                        / PA_USEC_PER_SEC) as u32;
                    Ok(latency)
                }
                // Input stream can be negative only if it is attached to a
                // monitor source device
                Ok(StreamLatency::Negative(_)) => Ok(0),
                Err(_) => {
                    cubeb_log!("Error: stm.get_latency() failed for an input stream");
                    Err(Error::Error)
                }
            },
        };

        if !in_thread {
            self.context.mainloop.unlock();
        }

        r
    }

    fn set_volume(&mut self, volume: f32) -> Result<()> {
        match self.output_stream {
            None => {
                cubeb_log!("Error: can't set volume on an input-only stream");
                Err(Error::Error)
            }
            Some(ref stm) => {
                if let Some(ref context) = self.context.context {
                    self.context.mainloop.lock();

                    let mut cvol: pa_cvolume = Default::default();

                    /* if the pulse daemon is configured to use flat
                     * volumes, apply our own gain instead of changing
                     * the input volume on the sink. */
                    let flags = {
                        match self.context.default_sink_info {
                            Some(ref info) => info.flags,
                            _ => pulse::SinkFlags::empty(),
                        }
                    };

                    if flags.contains(pulse::SinkFlags::FLAT_VOLUME) {
                        self.volume = volume;
                    } else {
                        let channels = stm.get_sample_spec().channels;
                        let vol = pulse::sw_volume_from_linear(f64::from(volume));
                        cvol.set(u32::from(channels), vol);

                        let index = stm.get_index();

                        let context_ptr = self.context as *const _ as *mut _;
                        if let Ok(o) = context.set_sink_input_volume(
                            index,
                            &cvol,
                            context_success,
                            context_ptr,
                        ) {
                            self.context.operation_wait(stm, &o);
                        }
                    }

                    self.context.mainloop.unlock();
                    Ok(())
                } else {
                    cubeb_log!("Error: set_volume: no context?");
                    Err(Error::Error)
                }
            }
        }
    }

    fn set_name(&mut self, name: &CStr) -> Result<()> {
        match self.output_stream {
            None => {
                cubeb_log!("Error: can't set the name on a input-only stream.");
                Err(Error::Error)
            }
            Some(ref stm) => {
                self.context.mainloop.lock();
                if let Ok(o) = stm.set_name(name, stream_success, self as *const _ as *mut _) {
                    self.context.operation_wait(stm, &o);
                }
                self.context.mainloop.unlock();
                Ok(())
            }
        }
    }

    fn current_device(&mut self) -> Result<&DeviceRef> {
        if self.context.version_0_9_8 {
            let in_thread = self.context.mainloop.in_thread();

            if !in_thread {
                self.context.mainloop.lock();
            }

            let mut dev: Box<ffi::cubeb_device> = Box::new(unsafe { mem::zeroed() });

            if let Some(ref stm) = self.input_stream {
                dev.input_name = match stm.get_device_name() {
                    Ok(name) => name.to_owned().into_raw(),
                    Err(_) => {
                        if !in_thread {
                            self.context.mainloop.unlock();
                        }
                        cubeb_log!("Error: couldn't get the input stream's device name");
                        return Err(Error::Error);
                    }
                }
            }

            if let Some(ref stm) = self.output_stream {
                dev.output_name = match stm.get_device_name() {
                    Ok(name) => name.to_owned().into_raw(),
                    Err(_) => {
                        if !in_thread {
                            self.context.mainloop.unlock();
                        }
                        cubeb_log!("Error: couldn't get the output stream's device name");
                        return Err(Error::Error);
                    }
                }
            }

            if !in_thread {
                self.context.mainloop.unlock();
            }

            Ok(unsafe { DeviceRef::from_ptr(Box::into_raw(dev) as *mut _) })
        } else {
            cubeb_log!("Error: PulseAudio context too old");
            Err(Error::NotSupported)
        }
    }

    fn set_input_mute(&mut self, _mute: bool) -> Result<()> {
        Err(Error::NotSupported)
    }

    fn set_input_processing_params(&mut self, _params: InputProcessingParams) -> Result<()> {
        Err(Error::NotSupported)
    }

    fn device_destroy(&mut self, device: &DeviceRef) -> Result<()> {
        if device.as_ptr().is_null() {
            cubeb_log!("Error: can't destroy null device");
            Err(Error::Error)
        } else {
            unsafe {
                let _: Box<Device> = Box::from_raw(device.as_ptr() as *mut _);
            }
            Ok(())
        }
    }

    fn register_device_changed_callback(
        &mut self,
        _: ffi::cubeb_device_changed_callback,
    ) -> Result<()> {
        cubeb_log!("Error: register_device_change_callback unimplemented");
        Err(Error::Error)
    }
}

impl PulseStream<'_> {
    fn stream_init(
        context: &pulse::Context,
        stream_params: &StreamParamsRef,
        stream_name: Option<&CStr>,
    ) -> Result<pulse::Stream> {
        if stream_params.prefs() == StreamPrefs::LOOPBACK {
            cubeb_log!("Error: StreamPref::LOOPBACK unimplemented");
            return Err(Error::NotSupported);
        }

        fn to_pulse_format(format: SampleFormat) -> pulse::SampleFormat {
            match format {
                SampleFormat::S16LE => pulse::SampleFormat::Signed16LE,
                SampleFormat::S16BE => pulse::SampleFormat::Signed16BE,
                SampleFormat::Float32LE => pulse::SampleFormat::Float32LE,
                SampleFormat::Float32BE => pulse::SampleFormat::Float32BE,
                _ => pulse::SampleFormat::Invalid,
            }
        }

        let fmt = to_pulse_format(stream_params.format());
        if fmt == pulse::SampleFormat::Invalid {
            cubeb_log!("Error: invalid sample format");
            return Err(Error::InvalidFormat);
        }

        let ss = pulse::SampleSpec {
            channels: stream_params.channels() as u8,
            format: fmt.into(),
            rate: stream_params.rate(),
        };

        let cm: Option<pa_channel_map> = match stream_params.layout() {
            ChannelLayout::UNDEFINED => {
                if stream_params.channels() <= 8
                    && pulse::ChannelMap::init_auto(
                        stream_params.channels(),
                        PA_CHANNEL_MAP_DEFAULT,
                    )
                    .is_none()
                {
                    cubeb_log!("Layout undefined and PulseAudio's default layout has not been configured, guess one.");
                    Some(layout_to_channel_map(default_layout_for_channels(
                        stream_params.channels(),
                    )))
                } else {
                    cubeb_log!("Layout undefined, PulseAudio will use its default.");
                    None
                }
            }
            _ => Some(layout_to_channel_map(stream_params.layout())),
        };

        let stream = pulse::Stream::new(context, stream_name.unwrap(), &ss, cm.as_ref());

        match stream {
            None => {
                cubeb_log!("Error: pulse::Stream::new failure");
                Err(Error::Error)
            }
            Some(stm) => Ok(stm),
        }
    }

    pub fn cork_stream(&self, stream: Option<&pulse::Stream>, state: CorkState) {
        if let Some(stm) = stream {
            if let Ok(o) = stm.cork(
                state.is_cork() as i32,
                stream_success,
                self as *const _ as *mut _,
            ) {
                self.context.operation_wait(stream, &o);
            }
        }
    }

    fn cork(&mut self, state: CorkState) {
        {
            self.context.mainloop.lock();
            self.cork_stream(self.output_stream.as_ref(), state);
            self.cork_stream(self.input_stream.as_ref(), state);
            self.context.mainloop.unlock()
        }

        if state.is_notify() {
            self.state_change_callback(if state.is_cork() {
                ffi::CUBEB_STATE_STOPPED
            } else {
                ffi::CUBEB_STATE_STARTED
            });
        }
    }

    fn update_timing_info(&self) -> bool {
        let mut r = false;

        if let Some(ref stm) = self.output_stream {
            if let Ok(o) = stm.update_timing_info(stream_success, self as *const _ as *mut _) {
                r = self.context.operation_wait(stm, &o);
            }

            if !r {
                return r;
            }
        }

        if let Some(ref stm) = self.input_stream {
            if let Ok(o) = stm.update_timing_info(stream_success, self as *const _ as *mut _) {
                r = self.context.operation_wait(stm, &o);
            }
        }

        r
    }

    pub fn state_change_callback(&mut self, s: ffi::cubeb_state) {
        self.state = s;
        unsafe {
            (self.state_callback.unwrap())(
                self as *mut PulseStream as *mut ffi::cubeb_stream,
                self.user_ptr,
                s,
            )
        };
    }

    fn wait_until_ready(&self) -> bool {
        fn wait_until_io_stream_ready(stm: &pulse::Stream, ctx: &PulseContext) -> bool {
            if ctx.mainloop.is_null() {
                return false;
            }

            let context = match ctx.context {
                Some(ref c) => c,
                None => return false,
            };

            loop {
                let state = stm.get_state();
                if !state.is_good() {
                    return false;
                }
                if state == pulse::StreamState::Ready {
                    break;
                }
                if !context.get_state().is_good() {
                    return false;
                }
                ctx.mainloop.wait();
            }

            true
        }

        if let Some(ref stm) = self.output_stream {
            if !wait_until_io_stream_ready(stm, self.context) {
                return false;
            }
        }

        if let Some(ref stm) = self.input_stream {
            if !wait_until_io_stream_ready(stm, self.context) {
                return false;
            }
        }

        true
    }

    #[allow(clippy::cognitive_complexity)]
    fn trigger_user_callback(&mut self, input_data: *const c_void, nbytes: usize) {
        fn drained_cb(
            a: &pulse::MainloopApi,
            e: *mut pa_time_event,
            _tv: &pulse::TimeVal,
            u: *mut c_void,
        ) {
            cubeb_logv!("Drain finished callback.");
            let stm = unsafe { &mut *(u as *mut PulseStream) };
            let drain_timer = stm.drain_timer.load(Ordering::Acquire);
            debug_assert_eq!(drain_timer, e);
            stm.state_change_callback(ffi::CUBEB_STATE_DRAINED);
            /* there's no pa_rttime_free, so use this instead. */
            a.time_free(drain_timer);
            stm.drain_timer.store(ptr::null_mut(), Ordering::Release);
            stm.context.mainloop.signal();
        }

        if let Some(ref stm) = self.output_stream {
            let frame_size = self.output_sample_spec.frame_size();
            debug_assert_eq!(nbytes % frame_size, 0);

            let mut towrite = nbytes;
            let mut read_offset = 0usize;
            while towrite > 0 {
                match stm.begin_write(towrite) {
                    Err(e) => {
                        cubeb_logv!("Error: failure to write data");
                        panic!("Failed to write data: {e}");
                    }
                    Ok((buffer, size)) => {
                        debug_assert!(size > 0);
                        debug_assert_eq!(size % frame_size, 0);

                        cubeb_logv!(
                            "Trigger user callback with output buffer size={}, read_offset={}",
                            size,
                            read_offset
                        );
                        let read_ptr = unsafe { (input_data as *const u8).add(read_offset) };
                        #[allow(clippy::unnecessary_cast)]
                        let mut got = unsafe {
                            self.data_callback.unwrap()(
                                self as *const _ as *mut _,
                                self.user_ptr,
                                read_ptr as *const _ as *mut _,
                                buffer,
                                (size / frame_size) as c_long,
                            ) as i64
                        };
                        if got < 0 {
                            let _ = stm.cancel_write();
                            self.shutdown = true;
                            unsafe {
                                self.state_callback.unwrap()(
                                    self as *const _ as *mut _,
                                    self.user_ptr,
                                    ffi::CUBEB_STATE_ERROR,
                                );
                            }
                            return;
                        }

                        // If more iterations move offset of read buffer
                        if !input_data.is_null() {
                            let in_frame_size = self.input_sample_spec.frame_size();
                            read_offset += (size / frame_size) * in_frame_size;
                        }

                        if self.volume != PULSE_NO_GAIN {
                            let samples = (self.output_sample_spec.channels as usize * size
                                / frame_size) as isize;

                            if self.output_sample_spec.format == PA_SAMPLE_S16BE
                                || self.output_sample_spec.format == PA_SAMPLE_S16LE
                            {
                                let b = buffer as *mut i16;
                                for i in 0..samples {
                                    unsafe { *b.offset(i) *= self.volume as i16 };
                                }
                            } else {
                                let b = buffer as *mut f32;
                                for i in 0..samples {
                                    unsafe { *b.offset(i) *= self.volume };
                                }
                            }
                        }

                        let should_drain = (got as usize) < size / frame_size;

                        if should_drain && self.output_frame_count.load(Ordering::SeqCst) == 0 {
                            // Draining during preroll, ensure `prebuf` frames are written so
                            // the stream starts. If not, pad with a bit of silence.
                            let prebuf_size_bytes = stm.get_buffer_attr().prebuf as usize;
                            let got_bytes = got as usize * frame_size;
                            if prebuf_size_bytes > got_bytes {
                                let padding_bytes = prebuf_size_bytes - got_bytes;
                                if padding_bytes + got_bytes <= size {
                                    // A slice that starts after the data provided by the callback,
                                    // with just enough room to provide a final buffer big enough.
                                    let padding_buf: &mut [u8] = unsafe {
                                        slice::from_raw_parts_mut::<u8>(
                                            buffer.add(got_bytes) as *mut u8,
                                            padding_bytes,
                                        )
                                    };
                                    padding_buf.fill(0);
                                    got += (padding_bytes / frame_size) as i64;
                                }
                            } else {
                                cubeb_logv!(
                                    "Not enough room to pad up to prebuf when prebuffering."
                                )
                            }
                        }

                        let r = stm.write(
                            buffer,
                            got as usize * frame_size,
                            0,
                            pulse::SeekMode::Relative,
                        );

                        if should_drain {
                            cubeb_logv!("Draining {} < {}", got, size / frame_size);
                            let latency = match stm.get_latency() {
                                Ok(StreamLatency::Positive(l)) => l,
                                Ok(_) => {
                                    panic!("Can not handle negative latency values.");
                                }
                                Err(e) => {
                                    debug_assert_eq!(
                                        e,
                                        pulse::ErrorCode::from_error_code(PA_ERR_NODATA)
                                    );
                                    /* this needs a better guess. */
                                    100 * PA_USEC_PER_MSEC
                                }
                            };

                            /* pa_stream_drain is useless, see PA bug# 866. this is a workaround. */
                            /* arbitrary safety margin: double the current latency. */
                            assert!(
                                self.drain_timer.load(Ordering::Acquire).is_null(),
                                "drain timer already armed; stream restarted while draining"
                            );
                            let stream_ptr = self as *const _ as *mut _;
                            if let Some(ref context) = self.context.context {
                                self.drain_timer.store(
                                    context.rttime_new(
                                        pulse::rtclock_now() + 2 * latency,
                                        drained_cb,
                                        stream_ptr,
                                    ),
                                    Ordering::Release,
                                );
                            }
                            self.shutdown = true;
                            return;
                        }

                        debug_assert!(r.is_ok());

                        towrite -= size;
                    }
                }
            }
            debug_assert_eq!(towrite, 0);
        }
    }
}

fn stream_success(_: &pulse::Stream, success: i32, u: *mut c_void) {
    let stm = unsafe { &*(u as *mut PulseStream) };
    if success != 1 {
        cubeb_log!("stream_success ignored failure: {}", success);
    }
    stm.context.mainloop.signal();
}

fn context_success(_: &pulse::Context, success: i32, u: *mut c_void) {
    let ctx = unsafe { &*(u as *mut PulseContext) };
    if success != 1 {
        cubeb_log!("context_success ignored failure: {}", success);
    }
    ctx.mainloop.signal();
}

#[cfg(all(test, not(feature = "pulse-dlopen")))]
mod test {
    use super::layout_to_channel_map;
    use cubeb_backend::ChannelLayout;
    use pulse_ffi::*;

    macro_rules! channel_tests {
        {$($name: ident, $layout: ident => [ $($channels: ident),* ]),+} => {
            $(
            #[test]
            fn $name() {
                let layout = ChannelLayout::$layout;
                let mut iter = super::channel_layout_iter(layout);
                $(
                assert_eq!(Some(ChannelLayout::$channels), iter.next());
                )*
                assert_eq!(None, iter.next());
            }

            )*
        }
    }

    channel_tests! {
        channels_unknown, UNDEFINED => [ ],
        channels_mono, MONO => [
            FRONT_CENTER
        ],
        channels_mono_lfe, MONO_LFE => [
            FRONT_CENTER,
            LOW_FREQUENCY
        ],
        channels_stereo, STEREO => [
            FRONT_LEFT,
            FRONT_RIGHT
        ],
        channels_stereo_lfe, STEREO_LFE => [
            FRONT_LEFT,
            FRONT_RIGHT,
            LOW_FREQUENCY
        ],
        channels_3f, _3F => [
            FRONT_LEFT,
            FRONT_RIGHT,
            FRONT_CENTER
        ],
        channels_3f_lfe, _3F_LFE => [
            FRONT_LEFT,
            FRONT_RIGHT,
            FRONT_CENTER,
            LOW_FREQUENCY
        ],
        channels_2f1, _2F1 => [
            FRONT_LEFT,
            FRONT_RIGHT,
            BACK_CENTER
        ],
        channels_2f1_lfe, _2F1_LFE => [
            FRONT_LEFT,
            FRONT_RIGHT,
            LOW_FREQUENCY,
            BACK_CENTER
        ],
        channels_3f1, _3F1 => [
            FRONT_LEFT,
            FRONT_RIGHT,
            FRONT_CENTER,
            BACK_CENTER
        ],
        channels_3f1_lfe, _3F1_LFE => [
            FRONT_LEFT,
            FRONT_RIGHT,
            FRONT_CENTER,
            LOW_FREQUENCY,
            BACK_CENTER
        ],
        channels_2f2, _2F2 => [
            FRONT_LEFT,
            FRONT_RIGHT,
            SIDE_LEFT,
            SIDE_RIGHT
        ],
        channels_2f2_lfe, _2F2_LFE => [
            FRONT_LEFT,
            FRONT_RIGHT,
            LOW_FREQUENCY,
            SIDE_LEFT,
            SIDE_RIGHT
        ],
        channels_quad, QUAD => [
            FRONT_LEFT,
            FRONT_RIGHT,
            BACK_LEFT,
            BACK_RIGHT
        ],
        channels_quad_lfe, QUAD_LFE => [
            FRONT_LEFT,
            FRONT_RIGHT,
            LOW_FREQUENCY,
            BACK_LEFT,
            BACK_RIGHT
        ],
        channels_3f2, _3F2 => [
            FRONT_LEFT,
            FRONT_RIGHT,
            FRONT_CENTER,
            SIDE_LEFT,
            SIDE_RIGHT
        ],
        channels_3f2_lfe, _3F2_LFE => [
            FRONT_LEFT,
            FRONT_RIGHT,
            FRONT_CENTER,
            LOW_FREQUENCY,
            SIDE_LEFT,
            SIDE_RIGHT
        ],
        channels_3f2_back, _3F2_BACK => [
            FRONT_LEFT,
            FRONT_RIGHT,
            FRONT_CENTER,
            BACK_LEFT,
            BACK_RIGHT
        ],
        channels_3f2_lfe_back, _3F2_LFE_BACK => [
            FRONT_LEFT,
            FRONT_RIGHT,
            FRONT_CENTER,
            LOW_FREQUENCY,
            BACK_LEFT,
            BACK_RIGHT
        ],
        channels_3f3r_lfe, _3F3R_LFE => [
            FRONT_LEFT,
            FRONT_RIGHT,
            FRONT_CENTER,
            LOW_FREQUENCY,
            BACK_CENTER,
            SIDE_LEFT,
            SIDE_RIGHT
        ],
        channels_3f4_lfe, _3F4_LFE => [
            FRONT_LEFT,
            FRONT_RIGHT,
            FRONT_CENTER,
            LOW_FREQUENCY,
            BACK_LEFT,
            BACK_RIGHT,
            SIDE_LEFT,
            SIDE_RIGHT
        ]
    }

    #[test]
    fn mono_channels_enumerate() {
        let layout = ChannelLayout::MONO;
        let mut iter = super::channel_layout_iter(layout).enumerate();
        assert_eq!(Some((0, ChannelLayout::FRONT_CENTER)), iter.next());
        assert_eq!(None, iter.next());
    }

    #[test]
    fn stereo_channels_enumerate() {
        let layout = ChannelLayout::STEREO;
        let mut iter = super::channel_layout_iter(layout).enumerate();
        assert_eq!(Some((0, ChannelLayout::FRONT_LEFT)), iter.next());
        assert_eq!(Some((1, ChannelLayout::FRONT_RIGHT)), iter.next());
        assert_eq!(None, iter.next());
    }

    #[test]
    fn quad_channels_enumerate() {
        let layout = ChannelLayout::QUAD;
        let mut iter = super::channel_layout_iter(layout).enumerate();
        assert_eq!(Some((0, ChannelLayout::FRONT_LEFT)), iter.next());
        assert_eq!(Some((1, ChannelLayout::FRONT_RIGHT)), iter.next());
        assert_eq!(Some((2, ChannelLayout::BACK_LEFT)), iter.next());
        assert_eq!(Some((3, ChannelLayout::BACK_RIGHT)), iter.next());
        assert_eq!(None, iter.next());
    }

    macro_rules! map_channel_tests {
        {$($name: ident, $layout: ident => [ $($channels: ident),* ]),+} => {
            $(
            #[test]
            fn $name() {
                let map = layout_to_channel_map(ChannelLayout::$layout);
                assert_eq!(map.channels, map_channel_tests!(__COUNT__ $($channels)*));
                map_channel_tests!(__EACH__ map, 0, $($channels)*);
            }

            )*
        };
        (__COUNT__) => (0u8);
        (__COUNT__ $x:ident $($xs: ident)*) => (1u8 + map_channel_tests!(__COUNT__ $($xs)*));
        (__EACH__ $map:expr, $i:expr, ) => {};
        (__EACH__ $map:expr, $i:expr, $x:ident $($xs: ident)*) => {
            assert_eq!($map.map[$i], $x);
            map_channel_tests!(__EACH__ $map, $i+1, $($xs)* );
        };
    }

    map_channel_tests! {
        map_channel_mono, MONO => [
            PA_CHANNEL_POSITION_MONO
        ],
        map_channel_mono_lfe, MONO_LFE => [
            PA_CHANNEL_POSITION_FRONT_CENTER,
            PA_CHANNEL_POSITION_LFE
        ],
        map_channel_stereo, STEREO => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT
        ],
        map_channel_stereo_lfe, STEREO_LFE => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_LFE
        ],
        map_channel_3f, _3F => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER
        ],
        map_channel_3f_lfe, _3F_LFE => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER,
            PA_CHANNEL_POSITION_LFE
        ],
        map_channel_2f1, _2F1 => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_REAR_CENTER
        ],
        map_channel_2f1_lfe, _2F1_LFE => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_LFE,
            PA_CHANNEL_POSITION_REAR_CENTER
        ],
        map_channel_3f1, _3F1 => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER,
            PA_CHANNEL_POSITION_REAR_CENTER
        ],
        map_channel_3f1_lfe, _3F1_LFE =>[
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER,
            PA_CHANNEL_POSITION_LFE,
            PA_CHANNEL_POSITION_REAR_CENTER
        ],
        map_channel_2f2, _2F2 => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_SIDE_LEFT,
            PA_CHANNEL_POSITION_SIDE_RIGHT
        ],
        map_channel_2f2_lfe, _2F2_LFE => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_LFE,
            PA_CHANNEL_POSITION_SIDE_LEFT,
            PA_CHANNEL_POSITION_SIDE_RIGHT
        ],
        map_channel_quad, QUAD => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_REAR_LEFT,
            PA_CHANNEL_POSITION_REAR_RIGHT
        ],
        map_channel_quad_lfe, QUAD_LFE => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_LFE,
            PA_CHANNEL_POSITION_REAR_LEFT,
            PA_CHANNEL_POSITION_REAR_RIGHT
        ],
        map_channel_3f2, _3F2 => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER,
            PA_CHANNEL_POSITION_SIDE_LEFT,
            PA_CHANNEL_POSITION_SIDE_RIGHT
        ],
        map_channel_3f2_lfe, _3F2_LFE => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER,
            PA_CHANNEL_POSITION_LFE,
            PA_CHANNEL_POSITION_SIDE_LEFT,
            PA_CHANNEL_POSITION_SIDE_RIGHT
        ],
        map_channel_3f2_back, _3F2_BACK => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER,
            PA_CHANNEL_POSITION_REAR_LEFT,
            PA_CHANNEL_POSITION_REAR_RIGHT
        ],
        map_channel_3f2_lfe_back, _3F2_LFE_BACK => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER,
            PA_CHANNEL_POSITION_LFE,
            PA_CHANNEL_POSITION_REAR_LEFT,
            PA_CHANNEL_POSITION_REAR_RIGHT
        ],
        map_channel_3f3r_lfe, _3F3R_LFE => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER,
            PA_CHANNEL_POSITION_LFE,
            PA_CHANNEL_POSITION_REAR_CENTER,
            PA_CHANNEL_POSITION_SIDE_LEFT,
            PA_CHANNEL_POSITION_SIDE_RIGHT
        ],
        map_channel_3f4_lfe, _3F4_LFE => [
            PA_CHANNEL_POSITION_FRONT_LEFT,
            PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER,
            PA_CHANNEL_POSITION_LFE,
            PA_CHANNEL_POSITION_REAR_LEFT,
            PA_CHANNEL_POSITION_REAR_RIGHT,
            PA_CHANNEL_POSITION_SIDE_LEFT,
            PA_CHANNEL_POSITION_SIDE_RIGHT
        ]
    }
}
