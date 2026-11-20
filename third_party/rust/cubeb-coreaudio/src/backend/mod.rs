// Copyright © 2018 Mozilla Foundation
//
// This program is made available under an ISC-style license.  See the
// accompanying file LICENSE for details.
#![allow(unused_assignments)]
#![allow(unused_must_use)]
extern crate coreaudio_sys_utils;
extern crate libc;
extern crate ringbuf;

mod aggregate_device;
mod auto_release;
mod buffer_manager;
mod device_property;
mod intern;
mod mixer;
mod resampler;
mod utils;

use self::aggregate_device::*;
use self::auto_release::*;
use self::buffer_manager::*;
use self::coreaudio_sys_utils::aggregate_device::*;
#[allow(unused_imports)]
use self::coreaudio_sys_utils::audio_device_extensions::*;
use self::coreaudio_sys_utils::audio_object::*;
use self::coreaudio_sys_utils::audio_unit::*;
use self::coreaudio_sys_utils::cf_mutable_dict::*;
use self::coreaudio_sys_utils::dispatch::*;
use self::coreaudio_sys_utils::string::*;
use self::coreaudio_sys_utils::sys::*;
use self::device_property::*;
use self::mixer::*;
use self::resampler::*;
use self::utils::*;
use backend::ringbuf::traits::{Consumer, Observer, Producer, Split};
use backend::ringbuf::{HeapCons, HeapProd, HeapRb as RingBuffer};
#[cfg(feature = "audio-dump")]
use cubeb_backend::ffi::cubeb_audio_dump_stream_t;
use cubeb_backend::{
    ffi, ChannelLayout, ContextOps, DeviceId, DeviceInfo, DeviceRef, DeviceType, Error,
    InputProcessingParams, Ops, Result, SampleFormat, State, Stream, StreamOps, StreamParams,
    StreamParamsRef, StreamPrefs,
};
use mach2::mach_time::{mach_absolute_time, mach_timebase_info};
use std::cmp;
use std::collections::HashSet;
use std::ffi::{CStr, CString};
use std::fmt;
use std::mem;
use std::os::raw::{c_uint, c_void};
use std::ptr;
use std::slice;
use std::str::FromStr;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, LazyLock, Mutex, MutexGuard, Weak};
use std::time::{Duration, Instant};
const NO_ERR: OSStatus = 0;

const AU_OUT_BUS: AudioUnitElement = 0;
const AU_IN_BUS: AudioUnitElement = 1;

const PRIVATE_AGGREGATE_DEVICE_NAME: &str = "CubebAggregateDevice";
const VOICEPROCESSING_AGGREGATE_DEVICE_NAME: &str = "VPAUAggregateAudioDevice";

const APPLE_STUDIO_DISPLAY_USB_ID: &str = "05AC:1114";

// Testing empirically, some headsets report a minimal latency that is very low,
// but this does not work in practice. Lie and say the minimum is 128 frames.
const SAFE_MIN_LATENCY_FRAMES: u32 = 128;
const SAFE_MAX_LATENCY_FRAMES: u32 = 512;

const VPIO_IDLE_TIMEOUT: Duration = Duration::from_secs(10);

const MACOS_KERNEL_MAJOR_VERSION_MONTEREY: u32 = 21;

// Global registry for tracking valid sync callback pointers.
static SYNC_CALLBACK_REGISTRY: LazyLock<Mutex<HashSet<usize>>> =
    LazyLock::new(|| Mutex::new(HashSet::new()));

fn sync_callback_registry_register(ptr: usize) {
    SYNC_CALLBACK_REGISTRY.lock().unwrap().insert(ptr);
}

fn sync_callback_registry_unregister(ptr: usize) {
    SYNC_CALLBACK_REGISTRY.lock().unwrap().remove(&ptr);
}

fn with_sync_callback_ptr<F>(ptr: *mut c_void, f: F)
where
    F: FnOnce(),
{
    let guard = SYNC_CALLBACK_REGISTRY.lock().unwrap();
    if guard.contains(&(ptr as usize)) {
        f();
    }
}

#[derive(Debug, PartialEq)]
enum ParseMacOSKernelVersionError {
    SysCtl,
    Malformed,
    Parsing,
}

fn macos_kernel_major_version() -> std::result::Result<u32, ParseMacOSKernelVersionError> {
    let ver = whatsys::kernel_version();
    if ver.is_none() {
        return Err(ParseMacOSKernelVersionError::SysCtl);
    }
    let ver = ver.unwrap();
    let major = ver.split('.').next();
    if major.is_none() {
        return Err(ParseMacOSKernelVersionError::Malformed);
    }
    let parsed_major = u32::from_str(major.unwrap());
    if parsed_major.is_err() {
        return Err(ParseMacOSKernelVersionError::Parsing);
    }
    Ok(parsed_major.unwrap())
}

bitflags! {
    #[allow(non_camel_case_types)]
    #[derive(Clone, Debug, PartialEq, Copy)]
    struct device_flags: u32 {
        const DEV_UNKNOWN           = 0b0000_0000; // Unknown
        const DEV_INPUT             = 0b0000_0001; // Record device like mic
        const DEV_OUTPUT            = 0b0000_0010; // Playback device like speakers
        const DEV_SELECTED_DEFAULT  = 0b0000_0100; // User selected to use the system default device
    }
}

#[cfg(feature = "audio-dump")]
fn dump_audio(stream: cubeb_audio_dump_stream_t, audio_samples: *mut c_void, count: u32) {
    unsafe {
        let rv = ffi::cubeb_audio_dump_write(stream, audio_samples, count);
        if rv != 0 {
            cubeb_alog!("Error dumping audio data");
        }
    }
}

fn make_sized_audio_channel_layout(sz: usize) -> AutoRelease<AudioChannelLayout> {
    assert!(sz >= mem::size_of::<AudioChannelLayout>());
    assert_eq!(
        (sz - mem::size_of::<AudioChannelLayout>()) % mem::size_of::<AudioChannelDescription>(),
        0
    );
    let acl = unsafe { libc::calloc(1, sz) } as *mut AudioChannelLayout;

    unsafe extern "C" fn free_acl(acl: *mut AudioChannelLayout) {
        libc::free(acl as *mut libc::c_void);
    }

    AutoRelease::new(acl, free_acl)
}

#[allow(non_camel_case_types)]
#[derive(Clone, Debug)]
struct device_info {
    id: AudioDeviceID,
    flags: device_flags,
}

impl Default for device_info {
    fn default() -> Self {
        Self {
            id: kAudioObjectUnknown,
            flags: device_flags::DEV_UNKNOWN,
        }
    }
}

#[allow(non_camel_case_types)]
#[derive(Debug)]
struct device_property_listener {
    device: AudioDeviceID,
    property: AudioObjectPropertyAddress,
    listener: audio_object_property_listener_proc,
}

impl device_property_listener {
    fn new(
        device: AudioDeviceID,
        property: AudioObjectPropertyAddress,
        listener: audio_object_property_listener_proc,
    ) -> Self {
        Self {
            device,
            property,
            listener,
        }
    }
}

#[derive(Debug, PartialEq)]
struct CAChannelLabel(AudioChannelLabel);

impl From<CAChannelLabel> for mixer::Channel {
    fn from(label: CAChannelLabel) -> mixer::Channel {
        use self::coreaudio_sys_utils::sys;
        match label.0 {
            sys::kAudioChannelLabel_Left => mixer::Channel::FrontLeft,
            sys::kAudioChannelLabel_Right => mixer::Channel::FrontRight,
            sys::kAudioChannelLabel_Center | sys::kAudioChannelLabel_Mono => {
                mixer::Channel::FrontCenter
            }
            sys::kAudioChannelLabel_LFEScreen => mixer::Channel::LowFrequency,
            sys::kAudioChannelLabel_LeftSurround => mixer::Channel::BackLeft,
            sys::kAudioChannelLabel_RightSurround => mixer::Channel::BackRight,
            sys::kAudioChannelLabel_LeftCenter => mixer::Channel::FrontLeftOfCenter,
            sys::kAudioChannelLabel_RightCenter => mixer::Channel::FrontRightOfCenter,
            sys::kAudioChannelLabel_CenterSurround => mixer::Channel::BackCenter,
            sys::kAudioChannelLabel_LeftSurroundDirect => mixer::Channel::SideLeft,
            sys::kAudioChannelLabel_RightSurroundDirect => mixer::Channel::SideRight,
            sys::kAudioChannelLabel_TopCenterSurround => mixer::Channel::TopCenter,
            sys::kAudioChannelLabel_VerticalHeightLeft => mixer::Channel::TopFrontLeft,
            sys::kAudioChannelLabel_VerticalHeightCenter => mixer::Channel::TopFrontCenter,
            sys::kAudioChannelLabel_VerticalHeightRight => mixer::Channel::TopFrontRight,
            sys::kAudioChannelLabel_TopBackLeft => mixer::Channel::TopBackLeft,
            sys::kAudioChannelLabel_TopBackCenter => mixer::Channel::TopBackCenter,
            sys::kAudioChannelLabel_TopBackRight => mixer::Channel::TopBackRight,
            sys::kAudioChannelLabel_Unknown => mixer::Channel::Discrete,
            sys::kAudioChannelLabel_Unused => mixer::Channel::Silence,
            v => {
                eprintln!("Warning: channel label value {v} isn't handled");
                mixer::Channel::Silence
            }
        }
    }
}

fn set_notification_runloop() {
    let address = AudioObjectPropertyAddress {
        mSelector: kAudioHardwarePropertyRunLoop,
        mScope: kAudioObjectPropertyScopeGlobal,
        mElement: kAudioObjectPropertyElementMaster,
    };

    // Ask HAL to manage its own thread for notification by setting the run_loop to NULL.
    // Otherwise HAL may use main thread to fire notifications.
    let run_loop: CFRunLoopRef = ptr::null_mut();
    let size = mem::size_of::<CFRunLoopRef>();
    let status =
        audio_object_set_property_data(kAudioObjectSystemObject, &address, size, &run_loop);
    if status != NO_ERR {
        cubeb_log!("Could not make global CoreAudio notifications use their own thread.");
    }
}

fn create_device_info(devid: AudioDeviceID, devtype: DeviceType) -> Option<device_info> {
    assert_ne!(devid, kAudioObjectSystemObject);
    debug_assert_running_serially();

    let mut flags = match devtype {
        DeviceType::INPUT => device_flags::DEV_INPUT,
        DeviceType::OUTPUT => device_flags::DEV_OUTPUT,
        _ => panic!("Only accept input or output type"),
    };

    if devid == kAudioObjectUnknown {
        cubeb_log!("Using the system default device");
        flags |= device_flags::DEV_SELECTED_DEFAULT;
        get_default_device(devtype).map(|id| device_info { id, flags })
    } else {
        Some(device_info { id: devid, flags })
    }
}

fn create_stream_description(stream_params: &StreamParams) -> Result<AudioStreamBasicDescription> {
    debug_assert!(stream_params.rate() > 0);
    debug_assert!(stream_params.channels() > 0);
    if stream_params.rate() == 0 {
        cubeb_log!(
            "create_stream_description: invalid rate 0 (channels={})",
            stream_params.channels()
        );
        return Err(Error::Error);
    }
    if stream_params.channels() == 0 {
        cubeb_log!(
            "create_stream_description: invalid channel count 0 (rate={})",
            stream_params.rate()
        );
        return Err(Error::Error);
    }

    let mut desc = AudioStreamBasicDescription::default();

    match stream_params.format() {
        SampleFormat::S16LE => {
            desc.mBitsPerChannel = 16;
            desc.mFormatFlags = kAudioFormatFlagIsSignedInteger;
        }
        SampleFormat::S16BE => {
            desc.mBitsPerChannel = 16;
            desc.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsBigEndian;
        }
        SampleFormat::Float32LE => {
            desc.mBitsPerChannel = 32;
            desc.mFormatFlags = kAudioFormatFlagIsFloat;
        }
        SampleFormat::Float32BE => {
            desc.mBitsPerChannel = 32;
            desc.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsBigEndian;
        }
        _ => {
            return Err(Error::InvalidFormat);
        }
    }

    desc.mFormatID = kAudioFormatLinearPCM;
    desc.mFormatFlags |= kLinearPCMFormatFlagIsPacked;
    desc.mSampleRate = f64::from(stream_params.rate());
    desc.mChannelsPerFrame = stream_params.channels();

    desc.mBytesPerFrame = (desc.mBitsPerChannel / 8) * desc.mChannelsPerFrame;
    desc.mFramesPerPacket = 1;
    desc.mBytesPerPacket = desc.mBytesPerFrame * desc.mFramesPerPacket;

    desc.mReserved = 0;

    Ok(desc)
}

fn set_volume(unit: AudioUnit, volume: f32) -> Result<()> {
    debug_assert!(!unit.is_null());
    if unit.is_null() {
        cubeb_log!("set_volume: audio unit is null");
        return Err(Error::Error);
    }
    let r = audio_unit_set_parameter(
        unit,
        kHALOutputParam_Volume,
        kAudioUnitScope_Global,
        0,
        volume,
        0,
    );
    if r == NO_ERR {
        Ok(())
    } else {
        cubeb_log!("AudioUnitSetParameter/kHALOutputParam_Volume rv={}", r);
        Err(Error::Error)
    }
}

fn get_volume(unit: AudioUnit) -> Result<f32> {
    debug_assert!(!unit.is_null());
    if unit.is_null() {
        cubeb_log!("get_volume: audio unit is null");
        return Err(Error::Error);
    }
    let mut volume: f32 = 0.0;
    let r = audio_unit_get_parameter(
        unit,
        kHALOutputParam_Volume,
        kAudioUnitScope_Global,
        0,
        &mut volume,
    );
    if r == NO_ERR {
        Ok(volume)
    } else {
        cubeb_log!("AudioUnitGetParameter/kHALOutputParam_Volume rv={}", r);
        Err(Error::Error)
    }
}

fn set_input_mute(unit: AudioUnit, mute: bool) -> Result<()> {
    debug_assert!(!unit.is_null());
    if unit.is_null() {
        cubeb_log!("set_input_mute: audio unit is null");
        return Err(Error::Error);
    }
    let mute: u32 = mute.into();
    let mut old_mute: u32 = 0;
    let r = audio_unit_get_property(
        unit,
        kAUVoiceIOProperty_MuteOutput,
        kAudioUnitScope_Global,
        AU_IN_BUS,
        &mut old_mute,
        &mut mem::size_of::<u32>(),
    );
    if r != NO_ERR {
        cubeb_log!(
            "AudioUnitGetProperty/kAUVoiceIOProperty_MuteOutput rv={}",
            r
        );
        return Err(Error::Error);
    }
    if old_mute == mute {
        return Ok(());
    }
    let r = audio_unit_set_property(
        unit,
        kAUVoiceIOProperty_MuteOutput,
        kAudioUnitScope_Global,
        AU_IN_BUS,
        &mute,
        mem::size_of::<u32>(),
    );
    if r == NO_ERR {
        Ok(())
    } else {
        cubeb_log!(
            "AudioUnitSetProperty/kAUVoiceIOProperty_MuteOutput rv={}",
            r
        );
        Err(Error::Error)
    }
}

fn set_input_processing_params(unit: AudioUnit, params: InputProcessingParams) -> Result<()> {
    debug_assert!(!unit.is_null());
    if unit.is_null() {
        cubeb_log!("set_input_processing_params: audio unit is null");
        return Err(Error::Error);
    }
    let aec = params.contains(InputProcessingParams::ECHO_CANCELLATION);
    let ns = params.contains(InputProcessingParams::NOISE_SUPPRESSION);
    let agc = params.contains(InputProcessingParams::AUTOMATIC_GAIN_CONTROL);
    assert_eq!(aec, ns);

    let mut old_agc: u32 = 0;
    let r = audio_unit_get_property(
        unit,
        kAUVoiceIOProperty_VoiceProcessingEnableAGC,
        kAudioUnitScope_Global,
        AU_IN_BUS,
        &mut old_agc,
        &mut mem::size_of::<u32>(),
    );
    if r != NO_ERR {
        cubeb_log!(
            "AudioUnitGetProperty/kAUVoiceIOProperty_VoiceProcessingEnableAGC rv={}",
            r
        );
        return Err(Error::Error);
    }

    if (old_agc == 1) != agc {
        let agc = u32::from(agc);
        let r = audio_unit_set_property(
            unit,
            kAUVoiceIOProperty_VoiceProcessingEnableAGC,
            kAudioUnitScope_Global,
            AU_IN_BUS,
            &agc,
            mem::size_of::<u32>(),
        );
        if r != NO_ERR {
            cubeb_log!(
                "AudioUnitSetProperty/kAUVoiceIOProperty_VoiceProcessingEnableAGC rv={}",
                r
            );
            return Err(Error::Error);
        }
        cubeb_log!(
            "set_input_processing_params on unit {:p} - set agc: {}",
            unit,
            agc
        );
    }

    let mut old_bypass: u32 = 0;
    let r = audio_unit_get_property(
        unit,
        kAUVoiceIOProperty_BypassVoiceProcessing,
        kAudioUnitScope_Global,
        AU_IN_BUS,
        &mut old_bypass,
        &mut mem::size_of::<u32>(),
    );
    if r != NO_ERR {
        cubeb_log!(
            "AudioUnitGetProperty/kAUVoiceIOProperty_BypassVoiceProcessing rv={}",
            r
        );
        return Err(Error::Error);
    }

    let bypass = u32::from(!aec);
    if old_bypass != bypass {
        let r = audio_unit_set_property(
            unit,
            kAUVoiceIOProperty_BypassVoiceProcessing,
            kAudioUnitScope_Global,
            AU_IN_BUS,
            &bypass,
            mem::size_of::<u32>(),
        );
        if r != NO_ERR {
            cubeb_log!(
                "AudioUnitSetProperty/kAUVoiceIOProperty_BypassVoiceProcessing rv={}",
                r
            );
            return Err(Error::Error);
        }
        cubeb_log!(
            "set_input_processing_params on unit {:p} - set bypass: {}",
            unit,
            bypass
        );
    }

    Ok(())
}

fn minimum_resampling_input_frames(
    input_rate: f64,
    output_rate: f64,
    output_frames: usize,
) -> usize {
    assert!(!approx_eq!(f64, input_rate, 0_f64));
    assert!(!approx_eq!(f64, output_rate, 0_f64));
    if approx_eq!(f64, input_rate, output_rate) {
        return output_frames;
    }
    (input_rate * output_frames as f64 / output_rate).ceil() as usize
}

fn audiounit_make_silent(io_data: &AudioBuffer) {
    assert!(!io_data.mData.is_null());
    let bytes = unsafe {
        let ptr = io_data.mData as *mut u8;
        let len = io_data.mDataByteSize as usize;
        slice::from_raw_parts_mut(ptr, len)
    };
    for data in bytes.iter_mut() {
        *data = 0;
    }
}

extern "C" fn audiounit_input_callback(
    user_ptr: *mut c_void,
    flags: *mut AudioUnitRenderActionFlags,
    tstamp: *const AudioTimeStamp,
    bus: u32,
    input_frames: u32,
    _: *mut AudioBufferList,
) -> OSStatus {
    enum ErrorHandle {
        Return(OSStatus),
        Reinit,
    }

    assert_eq!(bus, AU_IN_BUS);

    assert!(!user_ptr.is_null());
    let stm = unsafe { &mut *(user_ptr as *mut AudioUnitStream) };

    if input_frames == 0 {
        cubeb_alog!(
            "({:p}) input callback empty.",
            stm as *const AudioUnitStream
        );
        return NO_ERR;
    }

    if unsafe { *flags | kAudioTimeStampHostTimeValid } != 0 {
        let now = unsafe { mach_absolute_time() };
        let input_latency_frames = compute_input_latency(stm, unsafe { (*tstamp).mHostTime }, now);
        stm.total_input_latency_frames
            .store(input_latency_frames, Ordering::SeqCst);
    }

    if stm.stopped.load(Ordering::SeqCst) {
        cubeb_log!("({:p}) input stopped", stm as *const AudioUnitStream);
        return NO_ERR;
    }

    let handler = |stm: &mut AudioUnitStream,
                   flags: *mut AudioUnitRenderActionFlags,
                   tstamp: *const AudioTimeStamp,
                   bus: u32,
                   input_frames: u32|
     -> ErrorHandle {
        let input_buffer_manager = stm.core_stream_data.input_buffer_manager.as_mut().unwrap();
        assert_eq!(
            stm.core_stream_data.stm_ptr,
            user_ptr as *const AudioUnitStream
        );

        // `flags` and `tstamp` must be non-null so they can be casted into the references.
        assert!(!flags.is_null());
        let flags = unsafe { &mut (*flags) };
        assert!(!tstamp.is_null());
        let tstamp = unsafe { &(*tstamp) };

        // Create the AudioBufferList to store input.
        let mut input_buffer_list = AudioBufferList::default();
        input_buffer_list.mBuffers[0].mDataByteSize =
            stm.core_stream_data.input_dev_desc.mBytesPerFrame * input_frames;
        input_buffer_list.mBuffers[0].mData = ptr::null_mut();
        input_buffer_list.mBuffers[0].mNumberChannels =
            stm.core_stream_data.input_dev_desc.mChannelsPerFrame;
        input_buffer_list.mNumberBuffers = 1;

        debug_assert!(!stm.core_stream_data.input_unit.is_null());
        let status = audio_unit_render(
            stm.core_stream_data.input_unit,
            flags,
            tstamp,
            bus,
            input_frames,
            &mut input_buffer_list,
        );
        if (status != NO_ERR)
            && (status != kAudioUnitErr_CannotDoInCurrentContext
                || stm.core_stream_data.output_unit.is_null())
        {
            return ErrorHandle::Return(status);
        }
        let handle = if status == kAudioUnitErr_CannotDoInCurrentContext {
            assert!(!stm.core_stream_data.output_unit.is_null());
            // kAudioUnitErr_CannotDoInCurrentContext is returned when using a BT
            // headset and the profile is changed from A2DP to HFP/HSP. The previous
            // output device is no longer valid and must be reset.
            // For now state that no error occurred and feed silence, stream will be
            // resumed once reinit has completed.
            ErrorHandle::Reinit
        } else {
            assert_eq!(status, NO_ERR);

            #[cfg(feature = "audio-dump")]
            {
                dump_audio(
                    stm.core_stream_data.audio_dump_input,
                    input_buffer_list.mBuffers[0].mData,
                    input_frames * stm.core_stream_data.input_dev_desc.mChannelsPerFrame,
                );
            }

            input_buffer_manager
                .push_data(input_buffer_list.mBuffers[0].mData, input_frames as usize);
            ErrorHandle::Return(status)
        };

        // Full Duplex. We'll call data_callback in the AudioUnit output callback. Record this
        // callback for logging.
        if !stm.core_stream_data.output_unit.is_null() {
            let input_callback_data = InputCallbackData {
                bytes: input_buffer_list.mBuffers[0].mDataByteSize,
                rendered_frames: input_frames,
                total_available: input_buffer_manager.available_frames_from_producer(),
                channels: input_buffer_list.mBuffers[0].mNumberChannels,
                num_buf: input_buffer_list.mNumberBuffers,
            };
            stm.core_stream_data
                .input_logging
                .as_mut()
                .unwrap()
                .push(input_callback_data);
            return handle;
        }

        cubeb_alogv!(
            "({:p}) input: buffers {}, size {}, channels {}, rendered frames {}, total frames {}.",
            stm.core_stream_data.stm_ptr,
            input_buffer_list.mNumberBuffers,
            input_buffer_list.mBuffers[0].mDataByteSize,
            input_buffer_list.mBuffers[0].mNumberChannels,
            input_frames,
            input_buffer_manager.available_frames_from_consumer()
        );

        // Input only. Call the user callback through resampler.
        // Resampler will deliver input buffer in the correct rate.
        assert!(input_frames as usize <= input_buffer_manager.available_frames_from_consumer());
        stm.frames_read.fetch_add(
            input_buffer_manager.available_frames_from_consumer(),
            Ordering::SeqCst,
        );
        let mut total_input_frames = input_buffer_manager.available_frames_from_consumer() as i64;
        let input_buffer = input_buffer_manager
            .get_linear_data(input_buffer_manager.available_frames_from_consumer());
        let outframes = stm.core_stream_data.resampler.fill(
            input_buffer,
            &mut total_input_frames,
            ptr::null_mut(),
            0,
        );
        if outframes < 0 {
            if !stm.stopped.swap(true, Ordering::SeqCst) {
                stm.notify_state_changed(State::Error);
                // Use a new thread, through the queue, to avoid deadlock when calling
                // AudioOutputUnitStop method from inside render callback
                stm.queue.clone().run_async(move || {
                    stm.core_stream_data.stop_audiounits();
                });
            }
            return ErrorHandle::Return(status);
        }
        if outframes < total_input_frames {
            stm.draining.store(true, Ordering::SeqCst);
        }

        handle
    };

    // If the stream is drained, do nothing.
    let handle = if !stm.draining.load(Ordering::SeqCst) {
        handler(stm, flags, tstamp, bus, input_frames)
    } else {
        ErrorHandle::Return(NO_ERR)
    };

    // If the input (input-only stream) is drained, cancel this callback. Whenever an output
    // is involved, the output callback handles stopping all units and notifying of state.
    if stm.core_stream_data.output_unit.is_null()
        && stm.draining.load(Ordering::SeqCst)
        && !stm.stopped.swap(true, Ordering::SeqCst)
    {
        cubeb_alog!("({:p}) Input-only drained.", stm as *const AudioUnitStream);
        stm.notify_state_changed(State::Drained);
        // Use a new thread, through the queue, to avoid deadlock when calling
        // AudioOutputUnitStop method from inside render callback
        let stm_ptr = user_ptr as usize;
        stm.queue.clone().run_async(move || {
            let stm = unsafe { &mut *(stm_ptr as *mut AudioUnitStream) };
            stm.core_stream_data.stop_audiounits();
        });
    }

    // AudioOutputUnitStop waits for in-flight callbacks, but TSan cannot see
    // CoreAudio's internal synchronization. Release on the AudioUnit handle to
    // pair with the acquire in stop_audiounit, modeling the per-unit HALB_Mutex.
    #[cfg(feature = "tsan-annotations")]
    {
        extern "C" {
            fn __tsan_release(addr: *mut c_void);
        }
        unsafe {
            __tsan_release(stm.core_stream_data.input_unit as *mut c_void);
        }
    }

    match handle {
        ErrorHandle::Reinit => {
            stm.reinit_async();
            NO_ERR
        }
        ErrorHandle::Return(s) => s,
    }
}

fn host_time_to_ns(ctx: &AudioUnitContext, host_time: u64) -> u64 {
    let mut rv: f64 = host_time as f64;
    rv *= ctx.host_time_to_ns_ratio.0 as f64;
    rv /= ctx.host_time_to_ns_ratio.1 as f64;
    rv as u64
}

fn compute_output_latency(stm: &AudioUnitStream, audio_output_time: u64, now: u64) -> u32 {
    const NS2S: u64 = 1_000_000_000;
    let output_hw_rate = stm.core_stream_data.output_dev_desc.mSampleRate as u64;
    let fixed_latency_ns =
        (stm.output_device_latency_frames.load(Ordering::SeqCst) as u64 * NS2S) / output_hw_rate;
    // The total output latency is the timestamp difference + the stream latency + the hardware
    // latency.
    let total_output_latency_ns =
        fixed_latency_ns + host_time_to_ns(stm.context, audio_output_time.saturating_sub(now));

    (total_output_latency_ns * output_hw_rate / NS2S) as u32
}

fn compute_input_latency(stm: &AudioUnitStream, audio_input_time: u64, now: u64) -> u32 {
    const NS2S: u64 = 1_000_000_000;
    let input_hw_rate = stm.core_stream_data.input_dev_desc.mSampleRate as u64;
    let fixed_latency_ns =
        (stm.input_device_latency_frames.load(Ordering::SeqCst) as u64 * NS2S) / input_hw_rate;
    // The total input latency is the timestamp difference + the stream latency +
    // the hardware latency.
    let total_input_latency_ns =
        host_time_to_ns(stm.context, now.saturating_sub(audio_input_time)) + fixed_latency_ns;

    (total_input_latency_ns * input_hw_rate / NS2S) as u32
}

extern "C" fn audiounit_output_callback(
    user_ptr: *mut c_void,
    flags: *mut AudioUnitRenderActionFlags,
    tstamp: *const AudioTimeStamp,
    bus: u32,
    output_frames: u32,
    out_buffer_list: *mut AudioBufferList,
) -> OSStatus {
    assert_eq!(bus, AU_OUT_BUS);
    assert!(!out_buffer_list.is_null());

    assert!(!user_ptr.is_null());
    let stm = unsafe { &mut *(user_ptr as *mut AudioUnitStream) };

    if output_frames == 0 {
        cubeb_alog!(
            "({:p}) output callback empty.",
            stm as *const AudioUnitStream
        );
        return NO_ERR;
    }

    let out_buffer_list_ref = unsafe { &mut (*out_buffer_list) };
    assert_eq!(out_buffer_list_ref.mNumberBuffers, 1);
    let buffers = unsafe {
        let ptr = out_buffer_list_ref.mBuffers.as_mut_ptr();
        let len = out_buffer_list_ref.mNumberBuffers as usize;
        slice::from_raw_parts_mut(ptr, len)
    };

    if stm.stopped.load(Ordering::SeqCst) {
        cubeb_alog!("({:p}) output stopped.", stm as *const AudioUnitStream);
        audiounit_make_silent(&buffers[0]);
        #[cfg(feature = "audio-dump")]
        {
            dump_audio(
                stm.core_stream_data.audio_dump_output,
                buffers[0].mData,
                output_frames * stm.core_stream_data.output_dev_desc.mChannelsPerFrame,
            );
        }
        return NO_ERR;
    }

    if stm.draining.load(Ordering::SeqCst) {
        // Cancel all callbacks. For input-only streams, the input callback handles
        // cancelling itself.
        audiounit_make_silent(&buffers[0]);
        #[cfg(feature = "audio-dump")]
        {
            dump_audio(
                stm.core_stream_data.audio_dump_output,
                buffers[0].mData,
                output_frames * stm.core_stream_data.output_dev_desc.mChannelsPerFrame,
            );
        }
        if !stm.stopped.swap(true, Ordering::SeqCst) {
            cubeb_alog!("({:p}) output drained.", stm as *const AudioUnitStream);
            stm.notify_state_changed(State::Drained);
            // Use a new thread, through the queue, to avoid deadlock when calling
            // AudioOutputUnitStop method from inside render callback
            stm.queue.clone().run_async(move || {
                stm.core_stream_data.stop_audiounits();
            });
        }
        return NO_ERR;
    }

    let now = unsafe { mach_absolute_time() };

    if unsafe { *flags | kAudioTimeStampHostTimeValid } != 0 {
        let output_latency_frames =
            compute_output_latency(stm, unsafe { (*tstamp).mHostTime }, now);
        stm.total_output_latency_frames
            .store(output_latency_frames, Ordering::SeqCst);
    }
    // Get output buffer
    let output_buffer = match stm.core_stream_data.mixer.as_mut() {
        None => buffers[0].mData,
        Some(mixer) => {
            // If remixing needs to occur, we can't directly work in our final
            // destination buffer as data may be overwritten or too small to start with.
            mixer.update_buffer_size(output_frames as usize);
            mixer.get_buffer_mut_ptr() as *mut c_void
        }
    };

    let prev_frames_written = stm.frames_written.load(Ordering::SeqCst);

    stm.frames_written
        .fetch_add(output_frames as usize, Ordering::SeqCst);

    // Also get the input buffer if the stream is duplex
    let (input_buffer, mut input_frames) = if !stm.core_stream_data.input_unit.is_null() {
        let input_logging = &mut stm.core_stream_data.input_logging.as_mut().unwrap();
        if input_logging.is_empty() {
            cubeb_alogv!("no audio input data in output callback");
        } else {
            while let Some(input_callback_data) = input_logging.pop() {
                cubeb_alogv!(
                    "input: buffers {}, size {}, channels {}, rendered frames {}, total frames {}.",
                    input_callback_data.num_buf,
                    input_callback_data.bytes,
                    input_callback_data.channels,
                    input_callback_data.rendered_frames,
                    input_callback_data.total_available
                );
            }
        }
        let input_buffer_manager = stm.core_stream_data.input_buffer_manager.as_mut().unwrap();
        assert_ne!(stm.core_stream_data.input_dev_desc.mChannelsPerFrame, 0);
        // If the output callback came first and this is a duplex stream, we need to
        // fill in some additional silence in the resampler.
        // Otherwise, if we had more than expected callbacks in a row, or we're
        // currently switching, we add some silence as well to compensate for the
        // fact that we're lacking some input data.
        let input_frames_needed = minimum_resampling_input_frames(
            stm.core_stream_data.input_dev_desc.mSampleRate,
            f64::from(stm.core_stream_data.output_stream_params.rate()),
            output_frames as usize,
        );
        let buffered_input_frames = input_buffer_manager.available_frames_from_consumer();
        // Else if the input has buffered a lot already because the output started late, we
        // need to trim the input buffer
        if prev_frames_written == 0 && buffered_input_frames > input_frames_needed {
            input_buffer_manager.trim(input_frames_needed);
            let popped_frames = buffered_input_frames - input_frames_needed;
            cubeb_alog!("Dropping {} frames in input buffer.", popped_frames);
        }

        let input_frames = if input_frames_needed > buffered_input_frames
            && (stm.switching_device.load(Ordering::SeqCst)
                || stm.reinit_pending.load(Ordering::SeqCst)
                || stm.frames_read.load(Ordering::SeqCst) == 0)
        {
            // The silent frames will be inserted in `get_linear_data` below.
            let silent_frames_to_push = input_frames_needed - buffered_input_frames;
            cubeb_alog!(
                "({:p}) Missing Frames: {} will append {} frames of input silence.",
                stm.core_stream_data.stm_ptr,
                if stm.frames_read.load(Ordering::SeqCst) == 0 {
                    "input hasn't started,"
                } else if stm.switching_device.load(Ordering::SeqCst) {
                    "device switching,"
                } else {
                    "reinit pending,"
                },
                silent_frames_to_push
            );
            input_frames_needed
        } else {
            buffered_input_frames
        };

        stm.frames_read.fetch_add(input_frames, Ordering::SeqCst);
        (
            input_buffer_manager.get_linear_data(input_frames),
            input_frames as i64,
        )
    } else {
        (ptr::null_mut::<c_void>(), 0)
    };

    cubeb_alogv!(
        "({:p}) output: buffers {}, size {}, channels {}, frames {}.",
        stm as *const AudioUnitStream,
        buffers.len(),
        buffers[0].mDataByteSize,
        buffers[0].mNumberChannels,
        output_frames
    );

    assert_ne!(output_frames, 0);
    let outframes = stm.core_stream_data.resampler.fill(
        input_buffer,
        if input_buffer.is_null() {
            ptr::null_mut()
        } else {
            &mut input_frames
        },
        output_buffer,
        i64::from(output_frames),
    );

    if outframes < 0 || outframes > i64::from(output_frames) {
        audiounit_make_silent(&buffers[0]);

        #[cfg(feature = "audio-dump")]
        {
            dump_audio(
                stm.core_stream_data.audio_dump_output,
                buffers[0].mData,
                output_frames * stm.core_stream_data.output_dev_desc.mChannelsPerFrame,
            );
        }
        if !stm.stopped.swap(true, Ordering::SeqCst) {
            stm.notify_state_changed(State::Error);
            // Use a new thread, through the queue, to avoid deadlock when calling
            // AudioOutputUnitStop method from inside render callback
            stm.queue.clone().run_async(move || {
                stm.core_stream_data.stop_audiounits();
            });
        }
        return NO_ERR;
    }

    stm.draining
        .store(outframes < i64::from(output_frames), Ordering::SeqCst);
    stm.output_callback_timing_data_write
        .write(OutputCallbackTimingData {
            frames_queued: stm.frames_queued,
            timestamp: now,
            buffer_size: outframes as u64,
        });

    stm.frames_queued += outframes as u64;

    // Post process output samples.
    if stm.draining.load(Ordering::SeqCst) {
        // Clear missing frames (silence)
        let frames_to_bytes = |frames: usize| -> usize {
            let sample_size = cubeb_sample_size(stm.core_stream_data.output_stream_params.format());
            let channel_count = stm.core_stream_data.output_stream_params.channels() as usize;
            frames * sample_size * channel_count
        };
        let out_bytes = unsafe {
            slice::from_raw_parts_mut(
                output_buffer as *mut u8,
                frames_to_bytes(output_frames as usize),
            )
        };
        let start = frames_to_bytes(outframes as usize);
        for byte in out_bytes.iter_mut().skip(start) {
            *byte = 0;
        }
    }

    // Mixing
    if let Some(mixer) = stm.core_stream_data.mixer.as_mut() {
        let needed = stm.core_stream_data.output_dev_desc.mBytesPerFrame * output_frames;
        if buffers[0].mDataByteSize < needed {
            cubeb_log!(
                "({:p}) output buffer too small for mixer: have {} bytes, need {} bytes",
                stm as *const AudioUnitStream,
                buffers[0].mDataByteSize,
                needed
            );
            audiounit_make_silent(&buffers[0]);
            return NO_ERR;
        }
        mixer.mix(
            output_frames as usize,
            buffers[0].mData,
            buffers[0].mDataByteSize as usize,
        );
    }

    #[cfg(feature = "audio-dump")]
    {
        dump_audio(
            stm.core_stream_data.audio_dump_output,
            buffers[0].mData,
            output_frames * stm.core_stream_data.output_dev_desc.mChannelsPerFrame,
        );
    }

    #[cfg(feature = "tsan-annotations")]
    {
        extern "C" {
            fn __tsan_release(addr: *mut c_void);
        }
        unsafe {
            __tsan_release(stm.core_stream_data.output_unit as *mut c_void);
        }
    }

    NO_ERR
}

#[allow(clippy::cognitive_complexity)]
extern "C" fn audiounit_property_listener_callback(
    id: AudioObjectID,
    address_count: u32,
    addresses: *const AudioObjectPropertyAddress,
    user: *mut c_void,
) -> OSStatus {
    assert_ne!(address_count, 0);

    let stm = unsafe { &mut *(user as *mut AudioUnitStream) };
    let addrs = unsafe { slice::from_raw_parts(addresses, address_count as usize) };
    if stm.switching_device.load(Ordering::SeqCst) {
        cubeb_log!(
            "Switching is already taking place. Skipping event for device {}",
            id
        );
        return NO_ERR;
    }
    stm.switching_device.store(true, Ordering::SeqCst);

    let mut explicit_device_dead = false;

    cubeb_log!(
        "({:p}) Handling {} device changed events for device {}",
        stm as *const AudioUnitStream,
        address_count,
        id
    );
    for (i, addr) in addrs.iter().enumerate() {
        let p = PropertySelector::from(addr.mSelector);
        cubeb_log!("Event #{}: {}", i, p);
        assert_ne!(p, PropertySelector::Unknown);
        if p == PropertySelector::DeviceIsAlive {
            explicit_device_dead = true;
        }
    }

    // Handle the events
    if explicit_device_dead {
        if !stm.stopped.swap(true, Ordering::SeqCst) {
            cubeb_log!("The user-selected input or output device is dead, entering error state");

            // Use a different thread, through the queue, to avoid deadlock when calling
            // Get/SetProperties method from inside notify callback
            stm.queue.clone().run_async(move || {
                stm.core_stream_data.stop_audiounits();
                stm.close_on_error();
            });
        }
        return NO_ERR;
    }
    {
        let callback = stm.device_changed_callback.lock().unwrap();
        if let Some(device_changed_callback) = *callback {
            cubeb_log!("Calling device changed callback");
            unsafe {
                device_changed_callback(stm.user_ptr);
            }
        }
    }

    cubeb_log!("Reinitializing stream with new device because of device change, async");
    stm.reinit_async();

    NO_ERR
}

fn get_default_device(devtype: DeviceType) -> Option<AudioObjectID> {
    debug_assert_running_serially();
    match get_default_device_id(devtype) {
        Err(e) => {
            cubeb_log!("Cannot get default {:?} device. Error: {}", devtype, e);
            None
        }
        Ok(id) if id == kAudioObjectUnknown => {
            cubeb_log!("Get an invalid default {:?} device: {}", devtype, id);
            None
        }
        Ok(id) => Some(id),
    }
}

fn get_default_device_id(devtype: DeviceType) -> std::result::Result<AudioObjectID, OSStatus> {
    debug_assert_running_serially();
    let address = get_property_address(
        match devtype {
            DeviceType::INPUT => Property::HardwareDefaultInputDevice,
            DeviceType::OUTPUT => Property::HardwareDefaultOutputDevice,
            _ => panic!("Unsupport type"),
        },
        DeviceType::INPUT | DeviceType::OUTPUT,
    );

    let mut devid: AudioDeviceID = kAudioObjectUnknown;
    let mut size = mem::size_of::<AudioDeviceID>();
    let status =
        audio_object_get_property_data(kAudioObjectSystemObject, &address, &mut size, &mut devid);
    if status == NO_ERR {
        Ok(devid)
    } else {
        Err(status)
    }
}

fn audiounit_convert_channel_layout(layout: &AudioChannelLayout) -> Result<Vec<mixer::Channel>> {
    if layout.mChannelLayoutTag != kAudioChannelLayoutTag_UseChannelDescriptions {
        // kAudioChannelLayoutTag_UseChannelBitmap
        // kAudioChannelLayoutTag_Mono
        // kAudioChannelLayoutTag_Stereo
        // ....
        cubeb_log!("Only handling UseChannelDescriptions for now.\n");
        return Err(Error::Error);
    }

    let channel_descriptions = unsafe {
        slice::from_raw_parts(
            layout.mChannelDescriptions.as_ptr(),
            layout.mNumberChannelDescriptions as usize,
        )
    };

    let mut channels = Vec::with_capacity(layout.mNumberChannelDescriptions as usize);
    for description in channel_descriptions {
        let label = CAChannelLabel(description.mChannelLabel);
        channels.push(label.into());
    }

    Ok(channels)
}

fn audiounit_get_preferred_channel_layout(output_unit: AudioUnit) -> Result<Vec<mixer::Channel>> {
    debug_assert_running_serially();
    let mut rv = NO_ERR;
    let mut size: usize = 0;
    rv = audio_unit_get_property_info(
        output_unit,
        kAudioDevicePropertyPreferredChannelLayout,
        kAudioUnitScope_Output,
        AU_OUT_BUS,
        &mut size,
        None,
    );
    if rv != NO_ERR {
        cubeb_log!(
            "AudioUnitGetPropertyInfo/kAudioDevicePropertyPreferredChannelLayout rv={}",
            rv
        );
        return Err(Error::Error);
    }
    debug_assert!(size > 0);

    let mut layout = make_sized_audio_channel_layout(size);
    rv = audio_unit_get_property(
        output_unit,
        kAudioDevicePropertyPreferredChannelLayout,
        kAudioUnitScope_Output,
        AU_OUT_BUS,
        layout.as_mut(),
        &mut size,
    );
    if rv != NO_ERR {
        cubeb_log!(
            "AudioUnitGetProperty/kAudioDevicePropertyPreferredChannelLayout rv={}",
            rv
        );
        return Err(Error::Error);
    }

    audiounit_convert_channel_layout(layout.as_ref())
}

// This is for output AudioUnit only. Calling this by input-only AudioUnit is prone
// to crash intermittently.
fn audiounit_get_current_channel_layout(output_unit: AudioUnit) -> Result<Vec<mixer::Channel>> {
    debug_assert_running_serially();
    let mut rv = NO_ERR;
    let mut size: usize = 0;
    rv = audio_unit_get_property_info(
        output_unit,
        kAudioUnitProperty_AudioChannelLayout,
        kAudioUnitScope_Output,
        AU_OUT_BUS,
        &mut size,
        None,
    );
    if rv != NO_ERR {
        cubeb_log!(
            "AudioUnitGetPropertyInfo/kAudioUnitProperty_AudioChannelLayout rv={}",
            rv
        );
        return Err(Error::Error);
    }
    debug_assert!(size > 0);

    let mut layout = make_sized_audio_channel_layout(size);
    rv = audio_unit_get_property(
        output_unit,
        kAudioUnitProperty_AudioChannelLayout,
        kAudioUnitScope_Output,
        AU_OUT_BUS,
        layout.as_mut(),
        &mut size,
    );
    if rv != NO_ERR {
        cubeb_log!(
            "AudioUnitGetProperty/kAudioUnitProperty_AudioChannelLayout rv={}",
            rv
        );
        return Err(Error::Error);
    }

    audiounit_convert_channel_layout(layout.as_ref())
}

fn get_channel_layout(output_unit: AudioUnit) -> Result<Vec<mixer::Channel>> {
    debug_assert_running_serially();
    audiounit_get_current_channel_layout(output_unit)
        .or_else(|_| {
            // The kAudioUnitProperty_AudioChannelLayout property isn't known before
            // macOS 10.12, attempt another method.
            cubeb_log!(
                "Cannot get current channel layout for audiounit @ {:p}. Trying preferred channel layout.",
                output_unit
            );
            audiounit_get_preferred_channel_layout(output_unit)
        })
}

fn start_audiounit(unit: AudioUnit) -> Result<()> {
    let status = audio_output_unit_start(unit);
    if status == NO_ERR {
        Ok(())
    } else {
        cubeb_log!("Cannot start audiounit @ {:p}. Error: {}", unit, status);
        Err(Error::Error)
    }
}

fn stop_audiounit(unit: AudioUnit) -> Result<()> {
    let status = audio_output_unit_stop(unit);
    // AudioOutputUnitStop waits for in-flight callbacks, but TSan cannot see
    // CoreAudio's internal HALB_Mutex synchronization. Acquire on the AudioUnit
    // handle to pair with the release at the end of each callback.
    #[cfg(feature = "tsan-annotations")]
    {
        extern "C" {
            fn __tsan_acquire(addr: *mut c_void);
        }
        unsafe {
            __tsan_acquire(unit as *mut c_void);
        }
    }
    if status == NO_ERR {
        Ok(())
    } else {
        cubeb_log!("Cannot stop audiounit @ {:p}. Error: {}", unit, status);
        Err(Error::Error)
    }
}

fn create_audiounit(device: &device_info) -> Result<AudioUnit> {
    assert!(device
        .flags
        .intersects(device_flags::DEV_INPUT | device_flags::DEV_OUTPUT));
    assert!(!device
        .flags
        .contains(device_flags::DEV_INPUT | device_flags::DEV_OUTPUT));
    debug_assert_running_serially();

    let unit = create_blank_audiounit()?;
    let mut bus = AU_OUT_BUS;

    if device.flags.contains(device_flags::DEV_INPUT) {
        // Input only.
        if let Err(e) = enable_audiounit_scope(unit, DeviceType::INPUT, true) {
            cubeb_log!("Failed to enable audiounit input scope. Error: {}", e);
            dispose_audio_unit(unit);
            return Err(Error::Error);
        }
        if let Err(e) = enable_audiounit_scope(unit, DeviceType::OUTPUT, false) {
            cubeb_log!("Failed to disable audiounit output scope. Error: {}", e);
            dispose_audio_unit(unit);
            return Err(Error::Error);
        }
        bus = AU_IN_BUS;
    }

    if device.flags.contains(device_flags::DEV_OUTPUT) {
        // Output only.
        if let Err(e) = enable_audiounit_scope(unit, DeviceType::OUTPUT, true) {
            cubeb_log!("Failed to enable audiounit output scope. Error: {}", e);
            dispose_audio_unit(unit);
            return Err(Error::Error);
        }
        if let Err(e) = enable_audiounit_scope(unit, DeviceType::INPUT, false) {
            cubeb_log!("Failed to disable audiounit input scope. Error: {}", e);
            dispose_audio_unit(unit);
            return Err(Error::Error);
        }
        bus = AU_OUT_BUS;
    }

    if let Err(e) = set_device_to_audiounit(unit, device.id, bus) {
        cubeb_log!(
            "Failed to set device {} to the created audiounit. Error: {}",
            device.id,
            e
        );
        dispose_audio_unit(unit);
        return Err(Error::Error);
    }

    Ok(unit)
}

fn get_voiceprocessing_audiounit(
    shared_voice_processing_unit: &mut SharedVoiceProcessingUnitManager,
    in_device: &device_info,
    out_device: &device_info,
) -> Result<OwningHandle<VoiceProcessingUnit>> {
    debug_assert_running_serially();
    assert!(in_device.flags.contains(device_flags::DEV_INPUT));
    assert!(!in_device.flags.contains(device_flags::DEV_OUTPUT));
    assert!(!out_device.flags.contains(device_flags::DEV_INPUT));

    let unit_handle = shared_voice_processing_unit.take_or_create();
    if let Err(e) = unit_handle {
        cubeb_log!(
            "Failed to create shared voiceprocessing audiounit. Error: {}",
            e
        );
        return Err(Error::Error);
    }
    let mut unit_handle = unit_handle.unwrap();

    if let Err(e) = set_device_to_audiounit(unit_handle.as_mut().unit, in_device.id, AU_IN_BUS) {
        cubeb_log!(
            "Failed to set in device {} to the created audiounit. Error: {}",
            in_device.id,
            e
        );
        return Err(Error::Error);
    }

    let has_output = out_device.id != kAudioObjectUnknown;
    if let Err(e) =
        enable_audiounit_scope(unit_handle.as_mut().unit, DeviceType::OUTPUT, has_output)
    {
        cubeb_log!("Failed to enable audiounit input scope. Error: {}", e);
        return Err(Error::Error);
    }
    if has_output {
        if let Err(e) =
            set_device_to_audiounit(unit_handle.as_mut().unit, out_device.id, AU_OUT_BUS)
        {
            cubeb_log!(
                "Failed to set out device {} to the created audiounit. Error: {}",
                out_device.id,
                e
            );
            return Err(Error::Error);
        }
    }

    Ok(unit_handle)
}

fn enable_audiounit_scope(
    unit: AudioUnit,
    devtype: DeviceType,
    enable_io: bool,
) -> std::result::Result<(), OSStatus> {
    assert!(!unit.is_null());

    let enable = u32::from(enable_io);
    let (scope, element) = match devtype {
        DeviceType::INPUT => (kAudioUnitScope_Input, AU_IN_BUS),
        DeviceType::OUTPUT => (kAudioUnitScope_Output, AU_OUT_BUS),
        _ => panic!(
            "Enable AudioUnit {:?} with unsupported type: {:?}",
            unit, devtype
        ),
    };
    let status = audio_unit_set_property(
        unit,
        kAudioOutputUnitProperty_EnableIO,
        scope,
        element,
        &enable,
        mem::size_of::<u32>(),
    );
    if status == NO_ERR {
        Ok(())
    } else {
        Err(status)
    }
}

fn set_device_to_audiounit(
    unit: AudioUnit,
    device_id: AudioObjectID,
    bus: AudioUnitElement,
) -> std::result::Result<(), OSStatus> {
    assert!(!unit.is_null());

    let status = audio_unit_set_property(
        unit,
        kAudioOutputUnitProperty_CurrentDevice,
        kAudioUnitScope_Global,
        bus,
        &device_id,
        mem::size_of::<AudioDeviceID>(),
    );
    if status == NO_ERR {
        Ok(())
    } else {
        Err(status)
    }
}

fn create_typed_audiounit(sub_type: c_uint) -> Result<AudioUnit> {
    let desc = AudioComponentDescription {
        componentType: kAudioUnitType_Output,
        componentSubType: sub_type,
        componentManufacturer: kAudioUnitManufacturer_Apple,
        componentFlags: 0,
        componentFlagsMask: 0,
    };

    let comp = unsafe { AudioComponentFindNext(ptr::null_mut(), &desc) };
    if comp.is_null() {
        cubeb_log!("Could not find matching audio hardware.");
        return Err(Error::Error);
    }
    let mut unit: AudioUnit = ptr::null_mut();
    let status = unsafe { AudioComponentInstanceNew(comp, &mut unit) };
    if status == NO_ERR {
        assert!(!unit.is_null());
        Ok(unit)
    } else {
        cubeb_log!("Fail to get a new AudioUnit. Error: {}", status);
        Err(Error::Error)
    }
}

fn create_blank_audiounit() -> Result<AudioUnit> {
    #[cfg(not(target_os = "ios"))]
    return create_typed_audiounit(kAudioUnitSubType_HALOutput);
    #[cfg(target_os = "ios")]
    return create_typed_audiounit(kAudioUnitSubType_RemoteIO);
}

fn create_voiceprocessing_audiounit() -> Result<VoiceProcessingUnit> {
    let res = create_typed_audiounit(kAudioUnitSubType_VoiceProcessingIO);
    if res.is_err() {
        return Err(Error::Error);
    }

    #[cfg(not(feature = "no-private-apis"))]
    match get_default_device(DeviceType::OUTPUT) {
        None => {
            cubeb_log!("Could not get default output device in order to undo vpio ducking");
        }
        Some(id) => {
            let r = audio_device_duck(id, 1.0, ptr::null_mut(), 0.5);
            if r != NO_ERR {
                cubeb_log!(
                        "Failed to undo ducking of voiceprocessing on output device {}. Proceeding... Error: {}",
                        id,
                        r
                    );
            }
        }
    };

    res.map(|unit| VoiceProcessingUnit { unit })
}

fn get_buffer_size(unit: AudioUnit, devtype: DeviceType) -> std::result::Result<u32, OSStatus> {
    assert!(!unit.is_null());
    let (scope, element) = match devtype {
        DeviceType::INPUT => (kAudioUnitScope_Output, AU_IN_BUS),
        DeviceType::OUTPUT => (kAudioUnitScope_Input, AU_OUT_BUS),
        _ => panic!(
            "Get buffer size of AudioUnit {:?} with unsupported type: {:?}",
            unit, devtype
        ),
    };
    let mut frames: u32 = 0;
    let mut size = mem::size_of::<u32>();
    let status = audio_unit_get_property(
        unit,
        kAudioDevicePropertyBufferFrameSize,
        scope,
        element,
        &mut frames,
        &mut size,
    );
    if status == NO_ERR {
        Ok(frames)
    } else {
        Err(status)
    }
}

fn set_buffer_size(
    unit: AudioUnit,
    devtype: DeviceType,
    frames: u32,
) -> std::result::Result<(), OSStatus> {
    assert!(!unit.is_null());
    let (scope, element) = match devtype {
        DeviceType::INPUT => (kAudioUnitScope_Output, AU_IN_BUS),
        DeviceType::OUTPUT => (kAudioUnitScope_Input, AU_OUT_BUS),
        _ => panic!(
            "Set buffer size of AudioUnit {:?} with unsupported type: {:?}",
            unit, devtype
        ),
    };
    let status = audio_unit_set_property(
        unit,
        kAudioDevicePropertyBufferFrameSize,
        scope,
        element,
        &frames,
        mem::size_of_val(&frames),
    );
    if status == NO_ERR {
        Ok(())
    } else {
        Err(status)
    }
}

#[allow(clippy::mutex_atomic)] // The mutex needs to be fed into Condvar::wait_timeout.
fn set_buffer_size_sync(unit: AudioUnit, devtype: DeviceType, frames: u32) -> Result<()> {
    let current_frames = get_buffer_size(unit, devtype).map_err(|e| {
        cubeb_log!(
            "Cannot get buffer size of AudioUnit {:?} for {:?}. Error: {}",
            unit,
            devtype,
            e
        );
        Error::Error
    })?;
    if frames == current_frames {
        cubeb_log!(
            "The buffer frame size of AudioUnit {:?} for {:?} is already {}",
            unit,
            devtype,
            frames
        );
        return Ok(());
    }

    let waiting_time = Duration::from_millis(100);
    let pair = Box::new((Mutex::new(false), Condvar::new()));
    let pair_ptr = Box::into_raw(pair);
    sync_callback_registry_register(pair_ptr as usize);

    assert_eq!(
        audio_unit_add_property_listener(
            unit,
            kAudioDevicePropertyBufferFrameSize,
            buffer_size_changed_callback,
            pair_ptr as *mut c_void,
        ),
        NO_ERR
    );

    let _teardown = finally(|| {
        assert_eq!(
            audio_unit_remove_property_listener_with_user_data(
                unit,
                kAudioDevicePropertyBufferFrameSize,
                buffer_size_changed_callback,
                pair_ptr as *mut c_void,
            ),
            NO_ERR
        );
        // Unregister blocks if callback is in-flight (holds registry lock)
        sync_callback_registry_unregister(pair_ptr as usize);
        // Safe to drop: callback has either completed or will exit early
        unsafe { drop(Box::from_raw(pair_ptr)) };
    });

    set_buffer_size(unit, devtype, frames).map_err(|e| {
        cubeb_log!(
            "Failed to set buffer size for AudioUnit {:?} for {:?}. Error: {}",
            unit,
            devtype,
            e
        );
        Error::Error
    })?;

    let (lock, cvar) = unsafe { &*pair_ptr };
    let changed = lock.lock().unwrap();
    if !*changed {
        let (chg, timeout_res) = cvar.wait_timeout(changed, waiting_time).unwrap();
        if timeout_res.timed_out() {
            cubeb_log!(
                "Timed out for waiting the buffer frame size setting of AudioUnit {:?} for {:?}",
                unit,
                devtype
            );
        }
        if !*chg {
            return Err(Error::Error);
        }
    }

    let new_frames = get_buffer_size(unit, devtype).map_err(|e| {
        cubeb_log!(
            "Cannot get new buffer size of AudioUnit {:?} for {:?}. Error: {}",
            unit,
            devtype,
            e
        );
        Error::Error
    })?;
    cubeb_log!(
        "The new buffer frames size of AudioUnit {:?} for {:?} is {}",
        unit,
        devtype,
        new_frames
    );

    extern "C" fn buffer_size_changed_callback(
        in_client_data: *mut c_void,
        _in_unit: AudioUnit,
        in_property_id: AudioUnitPropertyID,
        in_scope: AudioUnitScope,
        in_element: AudioUnitElement,
    ) {
        if in_scope == 0 {
            return;
        }
        with_sync_callback_ptr(in_client_data, || {
            assert!(in_element == AU_IN_BUS || in_element == AU_OUT_BUS);
            assert_eq!(in_property_id, kAudioDevicePropertyBufferFrameSize);
            let pair = unsafe { &*(in_client_data as *const (Mutex<bool>, Condvar)) };
            let (lock, cvar) = pair;
            let mut changed = lock.lock().unwrap();
            *changed = true;
            cvar.notify_one();
        });
    }

    Ok(())
}

fn convert_uint32_into_string(data: u32) -> CString {
    let empty = CString::default();
    if data == 0 {
        return empty;
    }

    // Reverse 0xWXYZ into 0xZYXW.
    let mut buffer = vec![b'\x00'; 4]; // 4 bytes for uint32.
    buffer[0] = (data >> 24) as u8;
    buffer[1] = (data >> 16) as u8;
    buffer[2] = (data >> 8) as u8;
    buffer[3] = (data) as u8;

    // CString::new() will consume the input bytes vec and add a '\0' at the
    // end of the bytes. The input bytes vec must not contain any 0 bytes in
    // it in case causing memory leaks.
    CString::new(buffer).unwrap_or(empty)
}

fn get_channel_count(
    devid: AudioObjectID,
    devtype: DeviceType,
) -> std::result::Result<u32, OSStatus> {
    assert_ne!(devid, kAudioObjectUnknown);
    debug_assert_running_serially();

    let devstreams = get_device_streams(devid, devtype)?;
    let mut count: u32 = 0;
    for ds in devstreams {
        if devtype == DeviceType::INPUT
            && CoreStreamData::should_force_vpio_for_input_device(ds.device)
        {
            count += 1;
        } else {
            count += get_stream_virtual_format(ds.stream)
                .map(|f| f.mChannelsPerFrame)
                .unwrap_or(0);
        }
    }
    Ok(count)
}

fn get_range_of_sample_rates(
    devid: AudioObjectID,
    devtype: DeviceType,
) -> std::result::Result<(f64, f64), String> {
    debug_assert_running_serially();
    let result = get_ranges_of_device_sample_rate(devid, devtype);
    if let Err(e) = result {
        return Err(format!("status {e}"));
    }
    let rates = result.unwrap();
    if rates.is_empty() {
        return Err(String::from("No data"));
    }
    let (mut min, mut max) = (f64::MAX, f64::MIN);
    for rate in rates {
        if rate.mMaximum > max {
            max = rate.mMaximum;
        }
        if rate.mMinimum < min {
            min = rate.mMinimum;
        }
    }
    Ok((min, max))
}

fn get_fixed_latency(devid: AudioObjectID, devtype: DeviceType) -> u32 {
    debug_assert_running_serially();
    let device_latency = match get_device_latency(devid, devtype) {
        Ok(latency) => latency,
        Err(e) => {
            cubeb_log!(
                "Cannot get the device latency for device {} in {:?} scope. Error: {}",
                devid,
                devtype,
                e
            );
            0 // default device latency
        }
    };

    let stream_latency = get_device_streams(devid, devtype).and_then(|devstreams| {
        if devstreams.is_empty() {
            cubeb_log!(
                "No stream on device {} in {:?} scope!",
                devid,
                devtype
            );
            Ok(0) // default stream latency
        } else {
            get_stream_latency(devstreams[0].stream)
        }
    }).inspect_err(|e| {
        cubeb_log!(
            "Cannot get the stream, or the latency of the first stream on device {} in {:?} scope. Error: {}",
            devid,
            devtype,
            e
        );
    }).unwrap_or(0); // default stream latency

    device_latency + stream_latency
}

#[allow(non_upper_case_globals)]
fn get_device_group_id(
    id: AudioDeviceID,
    devtype: DeviceType,
) -> std::result::Result<CString, OSStatus> {
    debug_assert_running_serially();
    match get_device_transport_type(id, devtype) {
        Ok(kAudioDeviceTransportTypeBuiltIn) => {
            cubeb_log!(
                "The transport type is {:?}",
                convert_uint32_into_string(kAudioDeviceTransportTypeBuiltIn)
            );
            match get_custom_group_id(id, devtype) {
                Some(id) => return Ok(id),
                None => {
                    cubeb_log!("Getting model UID instead.");
                }
            };
        }
        Ok(trans_type) => {
            cubeb_log!(
                "The transport type is {:?}. Getting model UID instead.",
                convert_uint32_into_string(trans_type)
            );
        }
        Err(e) => {
            cubeb_log!(
                "Error: {} when getting transport type. Get model uid instead.",
                e
            );
        }
    }

    // Some devices (e.g. AirPods) might only set the model-uid in the global scope.
    // The query might fail if the scope is input-only or output-only.
    get_device_model_uid(id, devtype)
        .or_else(|_| get_device_model_uid(id, DeviceType::INPUT | DeviceType::OUTPUT))
        .map(|uid| uid.into_cstring())
}

fn get_custom_group_id(id: AudioDeviceID, devtype: DeviceType) -> Option<CString> {
    debug_assert_running_serially();

    const IMIC: u32 = 0x696D_6963; // "imic" (internal microphone)
    const ISPK: u32 = 0x6973_706B; // "ispk" (internal speaker)
    const EMIC: u32 = 0x656D_6963; // "emic" (external microphone)
    const HDPN: u32 = 0x6864_706E; // "hdpn" (headphone)

    match get_device_source(id, devtype) {
        s @ Ok(IMIC) | s @ Ok(ISPK) => {
            const GROUP_ID: &str = "builtin-internal-mic|spk";
            cubeb_log!(
                "Using hardcode group id: {} when source is: {:?}.",
                GROUP_ID,
                convert_uint32_into_string(s.unwrap())
            );
            return Some(CString::new(GROUP_ID).unwrap());
        }
        s @ Ok(EMIC) | s @ Ok(HDPN) => {
            const GROUP_ID: &str = "builtin-external-mic|hdpn";
            cubeb_log!(
                "Using hardcode group id: {} when source is: {:?}.",
                GROUP_ID,
                convert_uint32_into_string(s.unwrap())
            );
            return Some(CString::new(GROUP_ID).unwrap());
        }
        Ok(s) => {
            cubeb_log!(
                "No custom group id when source is: {:?}.",
                convert_uint32_into_string(s)
            );
        }
        Err(e) => {
            cubeb_log!("Error: {} when getting device source. ", e);
        }
    }
    None
}

fn get_device_label(
    id: AudioDeviceID,
    devtype: DeviceType,
) -> std::result::Result<StringRef, OSStatus> {
    debug_assert_running_serially();
    get_device_source_name(id, devtype).or_else(|_| get_device_name(id, devtype))
}

fn get_device_global_uid(id: AudioDeviceID) -> std::result::Result<StringRef, OSStatus> {
    debug_assert_running_serially();
    get_device_uid(id, DeviceType::INPUT | DeviceType::OUTPUT)
}

#[allow(clippy::cognitive_complexity)]
fn create_cubeb_device_info(
    intern: &Arc<Mutex<intern::Intern>>,
    devid: AudioObjectID,
    devtype: DeviceType,
) -> Result<ffi::cubeb_device_info> {
    if devtype != DeviceType::INPUT && devtype != DeviceType::OUTPUT {
        return Err(Error::Error);
    }
    let channels = get_channel_count(devid, devtype).map_err(|e| {
        cubeb_log!("Cannot get the channel count. Error: {}", e);
        Error::Error
    })?;
    if channels == 0 {
        // Invalid type for this device.
        return Err(Error::Error);
    }

    let mut dev_info = ffi::cubeb_device_info {
        max_channels: channels,
        ..Default::default()
    };

    match get_device_uid(devid, devtype) {
        Ok(uid) => {
            let c_string = uid.into_cstring();
            // Intern the device UID to provide a stable devid pointer for the lifetime of the context.
            dev_info.devid = intern.lock().unwrap().add(&c_string) as ffi::cubeb_devid;
            dev_info.device_id = c_string.into_raw();
        }
        Err(e) => {
            cubeb_log!(
                "Cannot get the UID for device {} in {:?} scope. Error: {}",
                devid,
                devtype,
                e
            );
        }
    }

    match get_device_group_id(devid, devtype) {
        Ok(group_id) => {
            dev_info.group_id = group_id.into_raw();
        }
        Err(e) => {
            cubeb_log!(
                "Cannot get the model UID for device {} in {:?} scope. Error: {}",
                devid,
                devtype,
                e
            );
        }
    }

    let label = match get_device_label(devid, devtype) {
        Ok(label) => label.into_cstring(),
        Err(e) => {
            cubeb_log!(
                "Cannot get the label for device {} in {:?} scope. Error: {}",
                devid,
                devtype,
                e
            );
            CString::default()
        }
    };
    dev_info.friendly_name = label.into_raw();

    match get_device_manufacturer(devid, devtype) {
        Ok(vendor) => {
            let vendor = vendor.into_cstring();
            dev_info.vendor_name = vendor.into_raw();
        }
        Err(e) => {
            cubeb_log!(
                "Cannot get the manufacturer for device {} in {:?} scope. Error: {}",
                devid,
                devtype,
                e
            );
        }
    }

    dev_info.device_type = match devtype {
        DeviceType::INPUT => ffi::CUBEB_DEVICE_TYPE_INPUT,
        DeviceType::OUTPUT => ffi::CUBEB_DEVICE_TYPE_OUTPUT,
        _ => panic!("invalid type"),
    };

    dev_info.state = ffi::CUBEB_DEVICE_STATE_ENABLED;
    dev_info.preferred = match get_default_device(devtype) {
        Some(id) if id == devid => ffi::CUBEB_DEVICE_PREF_ALL,
        _ => ffi::CUBEB_DEVICE_PREF_NONE,
    };

    dev_info.format = ffi::CUBEB_DEVICE_FMT_ALL;
    dev_info.default_format = ffi::CUBEB_DEVICE_FMT_F32NE;

    match get_device_sample_rate(devid, devtype) {
        Ok(rate) => {
            dev_info.default_rate = rate as u32;
        }
        Err(e) => {
            cubeb_log!(
                "Cannot get the sample rate for device {} in {:?} scope. Error: {}",
                devid,
                devtype,
                e
            );
        }
    }

    match get_range_of_sample_rates(devid, devtype) {
        Ok((min, max)) => {
            dev_info.min_rate = min as u32;
            dev_info.max_rate = max as u32;
        }
        Err(e) => {
            cubeb_log!(
                "Cannot get the range of sample rate for device {} in {:?} scope. Error: {}",
                devid,
                devtype,
                e
            );
        }
    }

    let latency = get_fixed_latency(devid, devtype);

    let (latency_low, latency_high) = match get_device_buffer_frame_size_range(devid, devtype) {
        Ok(range) => (
            latency + range.mMinimum as u32,
            latency + range.mMaximum as u32,
        ),
        Err(e) => {
            cubeb_log!("Cannot get the buffer frame size for device {} in {:?} scope. Using default value instead. Error: {}", devid, devtype, e);
            (
                10 * dev_info.default_rate / 1000,
                100 * dev_info.default_rate / 1000,
            )
        }
    };
    dev_info.latency_lo = latency_low;
    dev_info.latency_hi = latency_high;

    Ok(dev_info)
}

fn destroy_cubeb_device_info(device: &mut ffi::cubeb_device_info) {
    // This should be mapped to the memory allocation in `create_cubeb_device_info`.
    // The `device_id`, `group_id`, `vendor_name` can be null pointer if the queries
    // failed, while `friendly_name` will be assigned to a default empty "" string.
    // Set the pointers to null in case it points to some released memory.
    unsafe {
        if !device.device_id.is_null() {
            let _ = CString::from_raw(device.device_id as *mut _);
            device.device_id = ptr::null();
        }

        if !device.group_id.is_null() {
            let _ = CString::from_raw(device.group_id as *mut _);
            device.group_id = ptr::null();
        }

        if !device.friendly_name.is_null() {
            let _ = CString::from_raw(device.friendly_name as *mut _);
            device.friendly_name = ptr::null();
        }

        if !device.vendor_name.is_null() {
            let _ = CString::from_raw(device.vendor_name as *mut _);
            device.vendor_name = ptr::null();
        }
    }
}

fn audiounit_get_devices_of_type(devtype: DeviceType) -> Vec<AudioObjectID> {
    assert!(devtype.intersects(DeviceType::INPUT | DeviceType::OUTPUT));
    debug_assert_running_serially();

    let mut devices = get_devices();

    // Remove the aggregate device from the list of devices (if any).
    devices.retain(|&device| {
        // TODO: (bug 1628411) Figure out when `device` is `kAudioObjectUnknown`.
        if device == kAudioObjectUnknown {
            false
        } else if let Ok(uid) = get_device_global_uid(device) {
            let uid = uid.into_string();
            !uid.contains(PRIVATE_AGGREGATE_DEVICE_NAME)
                && !uid.contains(VOICEPROCESSING_AGGREGATE_DEVICE_NAME)
        } else {
            // Fail to get device uid.
            true
        }
    });

    // Expected sorted but did not find anything in the docs.
    devices.sort_unstable();
    if devtype.contains(DeviceType::INPUT | DeviceType::OUTPUT) {
        return devices;
    }

    let mut devices_in_scope = Vec::new();
    for device in devices {
        let label = match get_device_label(device, DeviceType::OUTPUT | DeviceType::INPUT) {
            Ok(label) => label.into_string(),
            Err(e) => format!("Unknown(error: {e})"),
        };
        let info = format!("{device} ({label})");

        if let Ok(channels) = get_channel_count(device, devtype) {
            cubeb_log!("Device {info} has {channels} {devtype:?}-channels");
            if channels > 0 {
                devices_in_scope.push(device);
            }
        } else {
            cubeb_log!("Cannot get the channel count for device {}. Ignored.", info);
        }
    }

    devices_in_scope
}

extern "C" fn audiounit_collection_changed_callback(
    _in_object_id: AudioObjectID,
    _in_number_addresses: u32,
    _in_addresses: *const AudioObjectPropertyAddress,
    in_client_data: *mut c_void,
) -> OSStatus {
    let context = unsafe { &mut *(in_client_data as *mut AudioUnitContext) };

    let queue = context.serial_queue.clone();

    // This can be called from inside an AudioUnit function, dispatch to another queue.
    queue.run_async(move || {
        let ctx_ptr = context as *const AudioUnitContext;

        let mut devices = context.devices.lock().unwrap();

        if devices.input.changed_callback.is_none() && devices.output.changed_callback.is_none() {
            return;
        }
        if devices.input.changed_callback.is_some() {
            let input_devices = audiounit_get_devices_of_type(DeviceType::INPUT);
            if devices.input.update_devices(input_devices) {
                unsafe {
                    devices.input.changed_callback.unwrap()(
                        ctx_ptr as *mut ffi::cubeb,
                        devices.input.callback_user_ptr,
                    );
                }
            }
        }
        if devices.output.changed_callback.is_some() {
            let output_devices = audiounit_get_devices_of_type(DeviceType::OUTPUT);
            if devices.output.update_devices(output_devices) {
                unsafe {
                    devices.output.changed_callback.unwrap()(
                        ctx_ptr as *mut ffi::cubeb,
                        devices.output.callback_user_ptr,
                    );
                }
            }
        }
    });

    NO_ERR
}

extern "C" fn audiounit_default_device_changed_callback(
    _in_object_id: AudioObjectID,
    in_number_addresses: u32,
    in_addresses: *const AudioObjectPropertyAddress,
    in_client_data: *mut c_void,
) -> OSStatus {
    let context = unsafe { &mut *(in_client_data as *mut AudioUnitContext) };

    let queue = context.serial_queue.clone();

    let addresses = unsafe { *in_addresses };

    // This can be called from inside an AudioUnit function, dispatch to another queue.
    queue.run_async(move || {
        let ctx_ptr = context as *const AudioUnitContext;

        let devices = context.devices.lock().unwrap();

        if devices.input.changed_callback.is_none() && devices.output.changed_callback.is_none() {
            return;
        }

        // Default device changes count as device collection changes
        let addresses = unsafe { slice::from_raw_parts(&addresses, in_number_addresses as usize) };
        for addr in addresses {
            let property = PropertySelector::from(addr.mSelector);
            match property {
                PropertySelector::DefaultInputDevice => {
                    if let Some(cb) = devices.input.changed_callback {
                        unsafe {
                            cb(ctx_ptr as *mut ffi::cubeb, devices.input.callback_user_ptr);
                        }
                    }
                }
                PropertySelector::DefaultOutputDevice => {
                    if let Some(cb) = devices.output.changed_callback {
                        unsafe {
                            cb(ctx_ptr as *mut ffi::cubeb, devices.output.callback_user_ptr);
                        }
                    }
                }
                _ => {}
            }
        }
    });

    NO_ERR
}

#[derive(Debug)]
struct DevicesData {
    changed_callback: ffi::cubeb_device_collection_changed_callback,
    callback_user_ptr: *mut c_void,
    devices: Vec<AudioObjectID>,
}

impl DevicesData {
    fn set(
        &mut self,
        changed_callback: ffi::cubeb_device_collection_changed_callback,
        callback_user_ptr: *mut c_void,
        devices: Vec<AudioObjectID>,
    ) {
        self.changed_callback = changed_callback;
        self.callback_user_ptr = callback_user_ptr;
        self.devices = devices;
    }

    fn update_devices(&mut self, devices: Vec<AudioObjectID>) -> bool {
        // Elements in the vector expected sorted.
        if self.devices == devices {
            return false;
        }
        self.devices = devices;
        true
    }

    fn clear(&mut self) {
        self.changed_callback = None;
        self.callback_user_ptr = ptr::null_mut();
        self.devices.clear();
    }

    fn is_empty(&self) -> bool {
        self.changed_callback.is_none()
            && self.callback_user_ptr.is_null()
            && self.devices.is_empty()
    }
}

impl Default for DevicesData {
    fn default() -> Self {
        Self {
            changed_callback: None,
            callback_user_ptr: ptr::null_mut(),
            devices: Vec::new(),
        }
    }
}

#[derive(Debug, Default)]
struct SharedDevices {
    input: DevicesData,
    output: DevicesData,
}

#[derive(Debug, Default)]
struct LatencyController {
    streams: u32,
    latency: Option<u32>,
}

impl LatencyController {
    fn add_stream(&mut self, latency: u32) -> u32 {
        self.streams += 1;
        // For the 1st stream set anything within safe min-max
        if self.streams == 1 {
            assert!(self.latency.is_none());
            // Silently clamp the latency down to the platform default, because we
            // synthetize the clock from the callbacks, and we want the clock to update often.
            self.latency = Some(latency.clamp(SAFE_MIN_LATENCY_FRAMES, SAFE_MAX_LATENCY_FRAMES));
        }
        self.latency.unwrap_or(latency)
    }

    fn subtract_stream(&mut self) {
        self.streams -= 1;
        if self.streams == 0 {
            assert!(self.latency.is_some());
            self.latency = None;
        }
    }
}

// SharedStorage<T> below looks generic but has evolved to be pretty tailored
// the observed behavior of VoiceProcessingIO audio units on macOS 14.
// Some key points are:
// - Creating the first VoiceProcessingIO unit in a process takes a long time, often > 3s.
// - Creating a second VoiceProcessingIO unit in a process is significantly faster, < 1s.
// - Disposing of a VoiceProcessingIO unit when all other VoiceProcessingIO units are
//   uninitialized will take significantly longer than disposing the remaining
//   VoiceProcessingIO units, and will have other side effects: starting another
//   VoiceProcessingIO unit after this is on par with creating the first one in the
//   process, bluetooth devices will move away from the handsfree profile, etc.
// The takeaway is that there is something internal to the VoiceProcessingIO audio unit
// that is costly to create and dispose of and its creation is triggered by creation of
// the first VoiceProcessingIO unit, and its disposal is triggered by the disposal of
// the first VoiceProcessingIO unit when no other VoiceProcessingIO units are initialized.
//
// The intended behavior of SharedStorage<T> and SharedVoiceProcessingUnitManager is therefore:
// - Retain ideally just one VoiceProcessingIO unit after stream destruction, so device
//   switching is fast. The benefit of retaining more than one is unclear.
// - Dispose of either all VoiceProcessingIO units, or none at all, such that the retained
//   VoiceProcessingIO unit really helps speed up creating and starting the next. In practice
//   this means we retain all VoiceProcessingIO units until they can all be disposed of.

#[derive(Debug)]
struct SharedStorageInternal<T> {
    // Storage for shared elements.
    elements: Vec<T>,
    // Number of elements in use, i.e. all elements created/taken and not recycled.
    outstanding_element_count: usize,
    // Used for invalidation of in-flight tasks to clear elements.
    // Incremented when something takes a shared element.
    generation: usize,
}

#[derive(Debug)]
struct SharedStorage<T> {
    queue: Queue,
    idle_timeout: Duration,
    storage: Mutex<SharedStorageInternal<T>>,
}

impl<T: Send> SharedStorage<T> {
    fn with_idle_timeout(queue: Queue, idle_timeout: Duration) -> Self {
        Self {
            queue,
            idle_timeout,
            storage: Mutex::new(SharedStorageInternal::<T> {
                elements: Vec::default(),
                outstanding_element_count: 0,
                generation: 0,
            }),
        }
    }

    fn take_locked(guard: &mut MutexGuard<'_, SharedStorageInternal<T>>) -> Result<T> {
        if let Some(e) = guard.elements.pop() {
            cubeb_log!("Taking shared element #{}.", guard.elements.len());
            guard.outstanding_element_count += 1;
            guard.generation += 1;
            return Ok(e);
        }

        Err(Error::NotSupported)
    }

    fn create_with_locked<F>(
        guard: &mut MutexGuard<'_, SharedStorageInternal<T>>,
        f: F,
    ) -> Result<T>
    where
        F: FnOnce() -> Result<T>,
    {
        let start = Instant::now();
        match f() {
            Ok(obj) => {
                cubeb_log!(
                    "Just created shared element #{}. Took {}s.",
                    guard.outstanding_element_count,
                    (Instant::now() - start).as_secs_f32()
                );
                guard.outstanding_element_count += 1;
                guard.generation += 1;
                Ok(obj)
            }
            Err(_) => {
                cubeb_log!("Creating shared element failed");
                Err(Error::Error)
            }
        }
    }

    #[cfg(test)]
    fn take(&self) -> Result<T> {
        let mut guard = self.storage.lock().unwrap();
        SharedStorage::take_locked(&mut guard)
    }

    fn take_or_create_with<F>(&self, f: F) -> Result<T>
    where
        F: FnOnce() -> Result<T>,
    {
        let mut guard = self.storage.lock().unwrap();
        SharedStorage::take_locked(&mut guard)
            .or_else(|_| SharedStorage::create_with_locked(&mut guard, f))
    }

    fn recycle(&self, obj: T) {
        let mut guard = self.storage.lock().unwrap();
        guard.outstanding_element_count -= 1;
        cubeb_log!(
            "Recycling shared element #{}. Nr of live elements now {}.",
            guard.elements.len(),
            guard.outstanding_element_count
        );
        guard.elements.push(obj);
    }

    fn clear_locked(guard: &mut MutexGuard<'_, SharedStorageInternal<T>>) {
        let count = guard.elements.len();
        let start = Instant::now();
        guard.elements.clear();
        cubeb_log!(
            "Cleared {} shared element{}. Took {}s.",
            count,
            if count == 1 { "" } else { "s" },
            (Instant::now() - start).as_secs_f32()
        );
    }

    fn clear(&self) {
        debug_assert_running_serially();
        let mut guard = self.storage.lock().unwrap();
        SharedStorage::clear_locked(&mut guard);
    }

    fn clear_if_all_idle_async(storage: &Arc<SharedStorage<T>>) {
        let (queue, outstanding_element_count, generation) = {
            let guard = storage.storage.lock().unwrap();
            (
                storage.queue.clone(),
                guard.outstanding_element_count,
                guard.generation,
            )
        };
        if outstanding_element_count > 0 {
            cubeb_log!(
                "Not clearing shared voiceprocessing unit storage because {} elements are in use. Generation={}.",
                outstanding_element_count,
                generation
            );
            return;
        }
        cubeb_log!(
            "Clearing shared voiceprocessing unit storage in {}s if still at generation {}.",
            storage.idle_timeout.as_secs_f32(),
            generation
        );
        let storage = storage.clone();
        queue.run_after(Instant::now() + storage.idle_timeout, move || {
            let mut guard = storage.storage.lock().unwrap();
            if generation != guard.generation {
                cubeb_log!(
                    "Not clearing shared voiceprocessing unit storage for generation {} as we're now at {}.",
                    generation,
                    guard.generation
                );
                return;
            }
            SharedStorage::clear_locked(&mut guard);
        });
    }
}

#[derive(Debug)]
struct OwningHandle<T>
where
    T: Send,
{
    storage: Weak<SharedStorage<T>>,
    obj: Option<T>,
}

impl<T: Send> OwningHandle<T> {
    fn new(storage: Weak<SharedStorage<T>>, obj: T) -> Self {
        Self {
            storage,
            obj: Some(obj),
        }
    }
}

impl<T: Send> AsRef<T> for OwningHandle<T> {
    fn as_ref(&self) -> &T {
        self.obj.as_ref().unwrap()
    }
}

impl<T: Send> AsMut<T> for OwningHandle<T> {
    fn as_mut(&mut self) -> &mut T {
        self.obj.as_mut().unwrap()
    }
}

impl<T: Send> Drop for OwningHandle<T> {
    fn drop(&mut self) {
        let storage = self.storage.upgrade();
        assert!(
            storage.is_some(),
            "Storage must outlive the handle, but didn't"
        );
        let storage = storage.unwrap();
        if self.obj.is_none() {
            return;
        }
        let obj = self.obj.take().unwrap();
        storage.recycle(obj);
        SharedStorage::clear_if_all_idle_async(&storage);
    }
}

#[derive(Debug)]
struct VoiceProcessingUnit {
    unit: AudioUnit,
}

impl Drop for VoiceProcessingUnit {
    fn drop(&mut self) {
        assert!(!self.unit.is_null());
        dispose_audio_unit(self.unit);
    }
}

unsafe impl Send for VoiceProcessingUnit {}

#[derive(Debug)]
struct SharedVoiceProcessingUnitManager {
    sync_storage: Mutex<Option<Arc<SharedStorage<VoiceProcessingUnit>>>>,
    queue: Queue,
    idle_timeout: Duration,
}

impl SharedVoiceProcessingUnitManager {
    fn with_idle_timeout(queue: Queue, idle_timeout: Duration) -> Self {
        Self {
            sync_storage: Mutex::new(None),
            queue,
            idle_timeout,
        }
    }

    fn new(queue: Queue) -> Self {
        SharedVoiceProcessingUnitManager::with_idle_timeout(queue, VPIO_IDLE_TIMEOUT)
    }

    fn ensure_storage_locked(
        &self,
        guard: &mut MutexGuard<Option<Arc<SharedStorage<VoiceProcessingUnit>>>>,
    ) {
        if guard.is_some() {
            return;
        }
        cubeb_log!("Creating shared voiceprocessing storage.");
        let storage = SharedStorage::<VoiceProcessingUnit>::with_idle_timeout(
            self.queue.clone(),
            self.idle_timeout,
        );
        let old_storage = guard.replace(Arc::from(storage));
        assert!(old_storage.is_none());
    }

    // Take an already existing, shared, vpio unit, if one is available.
    #[cfg(test)]
    fn take(&mut self) -> Result<OwningHandle<VoiceProcessingUnit>> {
        debug_assert_running_serially();
        let mut guard = self.sync_storage.lock().unwrap();
        self.ensure_storage_locked(&mut guard);
        let storage = guard.as_mut().unwrap();
        let res = storage.take();
        res.map(|u| OwningHandle::new(Arc::downgrade(storage), u))
    }

    // Take an already existing, shared, vpio unit, or create one if none are available.
    fn take_or_create(&mut self) -> Result<OwningHandle<VoiceProcessingUnit>> {
        debug_assert_running_serially();
        let mut guard = self.sync_storage.lock().unwrap();
        self.ensure_storage_locked(&mut guard);
        let storage = guard.as_mut().unwrap();
        let res = storage.take_or_create_with(create_voiceprocessing_audiounit);
        res.map(|u| OwningHandle::new(Arc::downgrade(storage), u))
    }
}

unsafe impl Send for SharedVoiceProcessingUnitManager {}
unsafe impl Sync for SharedVoiceProcessingUnitManager {}

impl Drop for SharedVoiceProcessingUnitManager {
    fn drop(&mut self) {
        debug_assert_not_running_serially();
        self.queue.run_final(|| {
            let mut guard = self.sync_storage.lock().unwrap();
            if guard.is_none() {
                return;
            }
            guard.as_mut().unwrap().clear();
        });
    }
}

pub const OPS: Ops = capi_new!(AudioUnitContext, AudioUnitStream);

// The fisrt member of the Cubeb context must be a pointer to a Ops struct. The Ops struct is an
// interface to link to all the Cubeb APIs, and the Cubeb interface use this assumption to operate
// the Cubeb APIs on different implementation.
// #[repr(C)] is used to prevent any padding from being added in the beginning of the AudioUnitContext.
#[repr(C)]
#[derive(Debug)]
pub struct AudioUnitContext {
    _ops: *const Ops,
    serial_queue: Queue,
    latency_controller: Mutex<LatencyController>,
    devices: Mutex<SharedDevices>,
    host_time_to_ns_ratio: (u32, u32),
    // Storage for a context-global vpio unit. Duplex streams that need one will take this
    // and return it when done.
    shared_voice_processing_unit: SharedVoiceProcessingUnitManager,
    devids: Arc<Mutex<intern::Intern>>,
}

impl AudioUnitContext {
    fn new() -> Self {
        let queue_label = format!("{DISPATCH_QUEUE_LABEL}.context");
        let serial_queue =
            Queue::new_with_target(queue_label.as_str(), get_serial_queue_singleton());
        let shared_vp_queue = Queue::new_with_target(
            format!("{DISPATCH_QUEUE_LABEL}.context.shared_vpio").as_str(),
            &serial_queue,
        );
        let host_time_to_ns_ratio = {
            let mut timebase_info = mach_timebase_info { numer: 0, denom: 0 };
            unsafe {
                mach_timebase_info(&mut timebase_info);
            }
            (timebase_info.numer, timebase_info.denom)
        };
        Self {
            _ops: &OPS as *const _,
            serial_queue,
            latency_controller: Mutex::new(LatencyController::default()),
            devices: Mutex::new(SharedDevices::default()),
            host_time_to_ns_ratio,
            shared_voice_processing_unit: SharedVoiceProcessingUnitManager::new(shared_vp_queue),
            devids: Arc::new(Mutex::new(intern::Intern::new())),
        }
    }

    fn active_streams(&self) -> u32 {
        let controller = self.latency_controller.lock().unwrap();
        controller.streams
    }

    fn update_latency_by_adding_stream(&self, latency_frames: u32) -> u32 {
        let mut controller = self.latency_controller.lock().unwrap();
        controller.add_stream(latency_frames)
    }

    fn update_latency_by_removing_stream(&self) {
        let mut controller = self.latency_controller.lock().unwrap();
        controller.subtract_stream();
    }

    fn add_devices_changed_listener(
        &mut self,
        devtype: DeviceType,
        collection_changed_callback: ffi::cubeb_device_collection_changed_callback,
        user_ptr: *mut c_void,
    ) -> Result<()> {
        assert!(devtype.intersects(DeviceType::INPUT | DeviceType::OUTPUT));
        assert!(collection_changed_callback.is_some());

        let context_ptr = self as *mut AudioUnitContext;
        let mut devices = self.devices.lock().unwrap();

        // Note: second register without unregister first causes 'nope' error.
        // Current implementation requires unregister before register a new cb.
        if devtype.contains(DeviceType::INPUT) && devices.input.changed_callback.is_some()
            || devtype.contains(DeviceType::OUTPUT) && devices.output.changed_callback.is_some()
        {
            return Err(Error::InvalidParameter);
        }

        if devices.input.changed_callback.is_none() && devices.output.changed_callback.is_none() {
            let hardware_address = get_property_address(
                Property::HardwareDevices,
                DeviceType::INPUT | DeviceType::OUTPUT,
            );
            let default_input_address = get_property_address(
                Property::HardwareDefaultInputDevice,
                DeviceType::INPUT | DeviceType::OUTPUT,
            );
            let default_output_address = get_property_address(
                Property::HardwareDefaultOutputDevice,
                DeviceType::INPUT | DeviceType::OUTPUT,
            );

            use bitflags::bitflags;

            bitflags! {
                #[derive(Clone, Copy, Debug)]
                struct CleanupFlags: u8 {
                    const HARDWARE = 0b001;
                    const INPUT = 0b010;
                }
            }

            // Cleanup function that removes any previously registered listeners based on flags
            let cleanup = |flags: CleanupFlags| {
                if flags.contains(CleanupFlags::HARDWARE) {
                    audio_object_remove_property_listener(
                        kAudioObjectSystemObject,
                        &hardware_address,
                        audiounit_collection_changed_callback,
                        context_ptr,
                    );
                }
                if flags.contains(CleanupFlags::INPUT) {
                    audio_object_remove_property_listener(
                        kAudioObjectSystemObject,
                        &default_input_address,
                        audiounit_default_device_changed_callback,
                        context_ptr,
                    );
                }
            };

            // Add hardware devices listener
            let ret = audio_object_add_property_listener(
                kAudioObjectSystemObject,
                &hardware_address,
                audiounit_collection_changed_callback,
                context_ptr,
            );
            if ret != NO_ERR {
                cubeb_log!(
                    "Cannot add devices-changed listener for {:?}, Error: {}",
                    devtype,
                    ret
                );
                return Err(Error::Error);
            }

            // Add default input device listener
            let ret = audio_object_add_property_listener(
                kAudioObjectSystemObject,
                &default_input_address,
                audiounit_default_device_changed_callback,
                context_ptr,
            );
            if ret != NO_ERR {
                cleanup(CleanupFlags::HARDWARE);
                cubeb_log!(
                    "Cannot add default input device listener for {:?}, Error: {}",
                    devtype,
                    ret
                );
                return Err(Error::Error);
            }

            // Add default output device listener
            let ret = audio_object_add_property_listener(
                kAudioObjectSystemObject,
                &default_output_address,
                audiounit_default_device_changed_callback,
                context_ptr,
            );
            if ret != NO_ERR {
                cleanup(CleanupFlags::HARDWARE | CleanupFlags::INPUT);
                cubeb_log!(
                    "Cannot add default output device listener for {:?}, Error: {}",
                    devtype,
                    ret
                );
                return Err(Error::Error);
            }
        }

        if devtype.contains(DeviceType::INPUT) {
            // Expected empty after unregister.
            assert!(devices.input.is_empty());
            devices.input.set(
                collection_changed_callback,
                user_ptr,
                audiounit_get_devices_of_type(DeviceType::INPUT),
            );
        }

        if devtype.contains(DeviceType::OUTPUT) {
            // Expected empty after unregister.
            assert!(devices.output.is_empty());
            devices.output.set(
                collection_changed_callback,
                user_ptr,
                audiounit_get_devices_of_type(DeviceType::OUTPUT),
            );
        }

        Ok(())
    }

    fn remove_devices_changed_listener(&mut self, devtype: DeviceType) -> Result<()> {
        if !devtype.intersects(DeviceType::INPUT | DeviceType::OUTPUT) {
            return Err(Error::InvalidParameter);
        }

        let context_ptr = self as *mut AudioUnitContext;
        let mut devices = self.devices.lock().unwrap();

        if devtype.contains(DeviceType::INPUT) {
            devices.input.clear();
        }

        if devtype.contains(DeviceType::OUTPUT) {
            devices.output.clear();
        }

        if devices.input.changed_callback.is_some() || devices.output.changed_callback.is_some() {
            return Ok(());
        }

        let address = get_property_address(
            Property::HardwareDevices,
            DeviceType::INPUT | DeviceType::OUTPUT,
        );
        let input_address = get_property_address(
            Property::HardwareDefaultInputDevice,
            DeviceType::INPUT | DeviceType::OUTPUT,
        );
        let output_address = get_property_address(
            Property::HardwareDefaultOutputDevice,
            DeviceType::INPUT | DeviceType::OUTPUT,
        );

        let mut errors = Vec::new();

        // Note: unregister a non registered cb is not a problem, but we should still check for other errors
        let ret = audio_object_remove_property_listener(
            kAudioObjectSystemObject,
            &address,
            audiounit_collection_changed_callback,
            context_ptr,
        );
        if ret != NO_ERR {
            cubeb_log!(
                "Cannot remove devices-changed listener for {:?}, Error: {}",
                devtype,
                ret
            );
            errors.push(ret);
        }

        let ret = audio_object_remove_property_listener(
            kAudioObjectSystemObject,
            &input_address,
            audiounit_default_device_changed_callback,
            context_ptr,
        );
        if ret != NO_ERR {
            cubeb_log!(
                "Cannot remove default input device listener for {:?}, Error: {}",
                devtype,
                ret
            );
            errors.push(ret);
        }

        let ret = audio_object_remove_property_listener(
            kAudioObjectSystemObject,
            &output_address,
            audiounit_default_device_changed_callback,
            context_ptr,
        );
        if ret != NO_ERR {
            cubeb_log!(
                "Cannot remove default output device listener for {:?}, Error: {}",
                devtype,
                ret
            );
            errors.push(ret);
        }

        if errors.is_empty() {
            Ok(())
        } else {
            Err(Error::Error)
        }
    }
}

impl ContextOps for AudioUnitContext {
    fn init(_context_name: Option<&CStr>) -> Result<Box<Self>> {
        run_serially(set_notification_runloop);
        let mut ctx = Box::new(AudioUnitContext::new());
        let queue_label = format!("{}.context.{:p}", DISPATCH_QUEUE_LABEL, ctx.as_ref());
        ctx.serial_queue =
            Queue::new_with_target(queue_label.as_str(), get_serial_queue_singleton());
        let shared_vp_queue = Queue::new_with_target(
            format!("{queue_label}.shared_vpio").as_str(),
            &ctx.serial_queue,
        );
        ctx.shared_voice_processing_unit = SharedVoiceProcessingUnitManager::new(shared_vp_queue);
        Ok(ctx)
    }

    fn backend_id(&mut self) -> &'static CStr {
        // https://github.com/rust-lang/rust-clippy/issues/13531
        #[allow(clippy::manual_c_str_literals)]
        unsafe {
            CStr::from_ptr(b"audiounit-rust\0".as_ptr() as *const _)
        }
    }
    #[cfg(target_os = "ios")]
    fn max_channel_count(&mut self) -> Result<u32> {
        Ok(2u32)
    }
    #[cfg(not(target_os = "ios"))]
    fn max_channel_count(&mut self) -> Result<u32> {
        self.serial_queue
            .run_sync(|| {
                let device = match get_default_device(DeviceType::OUTPUT) {
                    None => {
                        cubeb_log!("Could not get default output device");
                        return Err(Error::Error);
                    }
                    Some(id) => id,
                };
                get_channel_count(device, DeviceType::OUTPUT).map_err(|e| {
                    cubeb_log!("Cannot get the channel count. Error: {}", e);
                    Error::Error
                })
            })
            .unwrap()
    }
    #[cfg(target_os = "ios")]
    fn min_latency(&mut self, _params: StreamParams) -> Result<u32> {
        Err(not_supported());
    }
    #[cfg(not(target_os = "ios"))]
    fn min_latency(&mut self, _params: StreamParams) -> Result<u32> {
        self.serial_queue
            .run_sync(|| {
                let device = match get_default_device(DeviceType::OUTPUT) {
                    None => {
                        cubeb_log!("Could not get default output device");
                        return Err(Error::Error);
                    }
                    Some(id) => id,
                };

                let range = get_device_buffer_frame_size_range(device, DeviceType::OUTPUT)
                    .map_err(|e| {
                        cubeb_log!("Could not get acceptable latency range. Error: {}", e);
                        Error::Error
                    })?;

                Ok(cmp::max(range.mMinimum as u32, SAFE_MIN_LATENCY_FRAMES))
            })
            .unwrap()
    }
    #[cfg(target_os = "ios")]
    fn preferred_sample_rate(&mut self) -> Result<u32> {
        Err(not_supported());
    }
    #[cfg(not(target_os = "ios"))]
    fn preferred_sample_rate(&mut self) -> Result<u32> {
        self.serial_queue
            .run_sync(|| {
                let device = match get_default_device(DeviceType::OUTPUT) {
                    None => {
                        cubeb_log!("Could not get default output device");
                        return Err(Error::Error);
                    }
                    Some(id) => id,
                };
                let rate = get_device_sample_rate(device, DeviceType::OUTPUT).map_err(|e| {
                    cubeb_log!(
                        "Cannot get the sample rate of the default output device. Error: {}",
                        e
                    );
                    Error::Error
                })?;
                Ok(rate as u32)
            })
            .unwrap()
    }
    fn supported_input_processing_params(&mut self) -> Result<InputProcessingParams> {
        Ok(InputProcessingParams::ECHO_CANCELLATION
            | InputProcessingParams::NOISE_SUPPRESSION
            | InputProcessingParams::AUTOMATIC_GAIN_CONTROL)
    }
    fn enumerate_devices(&mut self, devtype: DeviceType) -> Result<Box<[DeviceInfo]>> {
        let intern = self.devids.clone();
        let device_infos = self
            .serial_queue
            .run_sync(|| {
                let mut dev_types = vec![DeviceType::INPUT, DeviceType::OUTPUT];
                dev_types.retain(|&dt| devtype.contains(dt));
                let device_ids: Vec<(DeviceType, Vec<AudioObjectID>)> = dev_types
                    .iter()
                    .map(|&dt| (dt, audiounit_get_devices_of_type(dt)))
                    .collect();
                let count = device_ids.iter().map(|(_dt, ids)| ids.len()).sum();
                let mut device_infos = Vec::with_capacity(count);
                for (dt, dev_ids) in device_ids {
                    for dev_id in dev_ids {
                        if let Ok(info) = create_cubeb_device_info(&intern, dev_id, dt) {
                            device_infos.push(info);
                        }
                    }
                }
                device_infos
            })
            .unwrap();
        Ok(device_infos
            .into_iter()
            .map(DeviceInfo::from)
            .collect::<Vec<_>>()
            .into_boxed_slice())
    }
    fn device_collection_destroy(&mut self, collection: Box<[DeviceInfo]>) -> Result<()> {
        for device in collection {
            let mut device = ffi::cubeb_device_info::from(device);
            destroy_cubeb_device_info(&mut device);
        }
        Ok(())
    }
    fn stream_init(
        &mut self,
        _stream_name: Option<&CStr>,
        input_device: DeviceId,
        input_stream_params: Option<&StreamParamsRef>,
        output_device: DeviceId,
        output_stream_params: Option<&StreamParamsRef>,
        latency_frames: u32,
        data_callback: ffi::cubeb_data_callback,
        state_callback: ffi::cubeb_state_callback,
        user_ptr: *mut c_void,
    ) -> Result<Stream> {
        if !input_device.is_null() && input_stream_params.is_none() {
            cubeb_log!("Cannot init an input device without input stream params");
            return Err(Error::InvalidParameter);
        }

        if !output_device.is_null() && output_stream_params.is_none() {
            cubeb_log!("Cannot init an output device without output stream params");
            return Err(Error::InvalidParameter);
        }

        if input_stream_params.is_none() && output_stream_params.is_none() {
            cubeb_log!("Cannot init a stream without any stream params");
            return Err(Error::InvalidParameter);
        }

        if data_callback.is_none() {
            cubeb_log!("Cannot init a stream without a data callback");
            return Err(Error::InvalidParameter);
        }

        let in_stm_settings = if let Some(params) = input_stream_params {
            let in_device = match self
                .serial_queue
                .run_sync(|| {
                    create_device_info(
                        get_device_from_devid(input_device).unwrap(),
                        DeviceType::INPUT,
                    )
                })
                .unwrap()
            {
                None => {
                    cubeb_log!("Fail to create device info for input");
                    return Err(Error::Error);
                }
                Some(d) => d,
            };
            let stm_params = StreamParams::from(unsafe { *params.as_ptr() });
            Some((stm_params, in_device))
        } else {
            None
        };

        let out_stm_settings = if let Some(params) = output_stream_params {
            let out_device = match self
                .serial_queue
                .run_sync(|| {
                    create_device_info(
                        get_device_from_devid(output_device).unwrap(),
                        DeviceType::OUTPUT,
                    )
                })
                .unwrap()
            {
                None => {
                    cubeb_log!("Fail to create device info for output");
                    return Err(Error::Error);
                }
                Some(d) => d,
            };
            let stm_params = StreamParams::from(unsafe { *params.as_ptr() });
            Some((stm_params, out_device))
        } else {
            None
        };

        // Latency cannot change if another stream is operating in parallel. In this case
        // latency is set to the other stream value.
        let global_latency_frames = self.update_latency_by_adding_stream(latency_frames);
        if global_latency_frames != latency_frames {
            cubeb_log!(
                "Use global latency {} instead of the requested latency {}.",
                global_latency_frames,
                latency_frames
            );
        }

        let mut boxed_stream = Box::new(AudioUnitStream::new(
            self,
            user_ptr,
            data_callback,
            state_callback,
            global_latency_frames,
        ));

        // Rename the task queue to be an unique label.
        let queue_label = format!(
            "{}.stream.{:p}",
            DISPATCH_QUEUE_LABEL,
            boxed_stream.as_ref()
        );
        boxed_stream.queue = Queue::new_with_target(queue_label.as_str(), &boxed_stream.queue);

        boxed_stream.core_stream_data =
            CoreStreamData::new(boxed_stream.as_ref(), in_stm_settings, out_stm_settings);

        let result = boxed_stream
            .queue
            .clone()
            .run_sync(|| {
                boxed_stream
                    .core_stream_data
                    .setup(&mut boxed_stream.context.shared_voice_processing_unit)
            })
            .unwrap();
        if let Err(r) = result {
            cubeb_log!(
                "({:p}) Could not setup the audiounit stream.",
                boxed_stream.as_ref()
            );
            return Err(r);
        }

        let cubeb_stream = unsafe { Stream::from_ptr(Box::into_raw(boxed_stream) as *mut _) };
        cubeb_log!(
            "({:p}) Cubeb stream init successful.",
            cubeb_stream.as_ref()
        );
        Ok(cubeb_stream)
    }
    fn register_device_collection_changed(
        &mut self,
        devtype: DeviceType,
        collection_changed_callback: ffi::cubeb_device_collection_changed_callback,
        user_ptr: *mut c_void,
    ) -> Result<()> {
        if devtype == DeviceType::UNKNOWN {
            return Err(Error::InvalidParameter);
        }
        self.serial_queue
            .clone()
            .run_sync(|| {
                if collection_changed_callback.is_some() {
                    self.add_devices_changed_listener(
                        devtype,
                        collection_changed_callback,
                        user_ptr,
                    )
                } else {
                    self.remove_devices_changed_listener(devtype)
                }
            })
            .unwrap()
    }
}

impl Drop for AudioUnitContext {
    fn drop(&mut self) {
        assert!({
            let devices = self.devices.lock().unwrap();
            devices.input.changed_callback.is_none() && devices.output.changed_callback.is_none()
        });

        self.shared_voice_processing_unit =
            SharedVoiceProcessingUnitManager::new(self.serial_queue.clone());

        // Make sure all the pending (device-collection-changed-callback) tasks
        // in queue are done, and cancel all the tasks appended after `drop` is executed.
        let queue = self.serial_queue.clone();
        queue.run_final(|| {});

        {
            let controller = self.latency_controller.lock().unwrap();
            // Disabling this assert in release for bug 1083664 -- we seem to leak a stream
            // assert(controller.streams == 0);
            debug_assert!(controller.streams == 0);
            if controller.streams > 0 {
                cubeb_log!(
                    "({:p}) API misuse, {} streams active when context destroyed!",
                    self as *const AudioUnitContext,
                    controller.streams
                );
            }
        }
    }
}

#[allow(clippy::non_send_fields_in_send_ty)]
unsafe impl Send for AudioUnitContext {}
unsafe impl Sync for AudioUnitContext {}

// Holds the information for an audio input callback call, for debugging purposes.
struct InputCallbackData {
    bytes: u32,
    rendered_frames: u32,
    total_available: usize,
    channels: u32,
    num_buf: u32,
}
struct InputCallbackLogger {
    prod: HeapProd<InputCallbackData>,
    cons: HeapCons<InputCallbackData>,
}

impl InputCallbackLogger {
    fn new() -> Self {
        let ring = RingBuffer::<InputCallbackData>::new(16);
        let (prod, cons) = ring.split();
        Self { prod, cons }
    }

    fn push(&mut self, data: InputCallbackData) {
        // Drop log entries if input callbacks outpace the output callback draining this queue.
        let _ = self.prod.try_push(data);
    }

    fn pop(&mut self) -> Option<InputCallbackData> {
        self.cons.try_pop()
    }

    fn is_empty(&self) -> bool {
        self.cons.is_empty()
    }
}

impl fmt::Debug for InputCallbackLogger {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "InputCallbackLogger  {{ prod: {}, cons: {} }}",
            self.prod.occupied_len(),
            self.cons.occupied_len()
        )
    }
}

#[derive(Debug)]
struct CoreStreamData<'ctx> {
    stm_ptr: *const AudioUnitStream<'ctx>,
    aggregate_device: Option<AggregateDevice>,
    mixer: Option<Mixer>,
    resampler: Resampler,
    // Stream creation parameters.
    input_stream_params: StreamParams,
    output_stream_params: StreamParams,
    // Device settings for AudioUnits.
    input_dev_desc: AudioStreamBasicDescription,
    output_dev_desc: AudioStreamBasicDescription,
    // I/O AudioUnits.
    input_unit: AudioUnit,
    output_unit: AudioUnit,
    // Handle to shared voiceprocessing AudioUnit, if in use.
    voiceprocessing_unit_handle: Option<OwningHandle<VoiceProcessingUnit>>,
    // Info of the I/O devices.
    input_device: device_info,
    output_device: device_info,
    input_processing_params: InputProcessingParams,
    input_mute: bool,
    input_buffer_manager: Option<BufferManager>,
    units_running: bool,
    // Listeners indicating what system events are monitored.
    default_input_listener: Option<device_property_listener>,
    default_output_listener: Option<device_property_listener>,
    input_alive_listener: Option<device_property_listener>,
    input_source_listener: Option<device_property_listener>,
    output_alive_listener: Option<device_property_listener>,
    output_source_listener: Option<device_property_listener>,
    input_logging: Option<InputCallbackLogger>,
    #[cfg(feature = "audio-dump")]
    audio_dump_session: ffi::cubeb_audio_dump_session_t,
    #[cfg(feature = "audio-dump")]
    audio_dump_session_running: bool,
    #[cfg(feature = "audio-dump")]
    audio_dump_input: ffi::cubeb_audio_dump_stream_t,
    #[cfg(feature = "audio-dump")]
    audio_dump_output: ffi::cubeb_audio_dump_stream_t,
}

impl Default for CoreStreamData<'_> {
    fn default() -> Self {
        Self {
            stm_ptr: ptr::null(),
            aggregate_device: None,
            mixer: None,
            resampler: Resampler::default(),
            input_stream_params: StreamParams::from(ffi::cubeb_stream_params {
                format: ffi::CUBEB_SAMPLE_FLOAT32NE,
                rate: 0,
                channels: 0,
                layout: ffi::CUBEB_LAYOUT_UNDEFINED,
                prefs: ffi::CUBEB_STREAM_PREF_NONE,
                input_params: ffi::CUBEB_INPUT_PROCESSING_PARAM_NONE,
            }),
            output_stream_params: StreamParams::from(ffi::cubeb_stream_params {
                format: ffi::CUBEB_SAMPLE_FLOAT32NE,
                rate: 0,
                channels: 0,
                layout: ffi::CUBEB_LAYOUT_UNDEFINED,
                prefs: ffi::CUBEB_STREAM_PREF_NONE,
                input_params: ffi::CUBEB_INPUT_PROCESSING_PARAM_NONE,
            }),
            input_dev_desc: AudioStreamBasicDescription::default(),
            output_dev_desc: AudioStreamBasicDescription::default(),
            input_unit: ptr::null_mut(),
            output_unit: ptr::null_mut(),
            voiceprocessing_unit_handle: None,
            input_device: device_info::default(),
            output_device: device_info::default(),
            input_processing_params: InputProcessingParams::NONE,
            input_mute: false,
            input_buffer_manager: None,
            units_running: false,
            default_input_listener: None,
            default_output_listener: None,
            input_alive_listener: None,
            input_source_listener: None,
            output_alive_listener: None,
            output_source_listener: None,
            input_logging: None,
            #[cfg(feature = "audio-dump")]
            audio_dump_session: ptr::null_mut(),
            #[cfg(feature = "audio-dump")]
            audio_dump_session_running: false,
            #[cfg(feature = "audio-dump")]
            audio_dump_input: ptr::null_mut(),
            #[cfg(feature = "audio-dump")]
            audio_dump_output: ptr::null_mut(),
        }
    }
}

impl<'ctx> CoreStreamData<'ctx> {
    fn new(
        stm: &AudioUnitStream<'ctx>,
        input_stream_settings: Option<(StreamParams, device_info)>,
        output_stream_settings: Option<(StreamParams, device_info)>,
    ) -> Self {
        fn get_default_sttream_params() -> StreamParams {
            StreamParams::from(ffi::cubeb_stream_params {
                format: ffi::CUBEB_SAMPLE_FLOAT32NE,
                rate: 0,
                channels: 0,
                layout: ffi::CUBEB_LAYOUT_UNDEFINED,
                prefs: ffi::CUBEB_STREAM_PREF_NONE,
                input_params: ffi::CUBEB_INPUT_PROCESSING_PARAM_NONE,
            })
        }
        let (in_stm_params, in_dev) =
            input_stream_settings.unwrap_or((get_default_sttream_params(), device_info::default()));
        let (out_stm_params, out_dev) = output_stream_settings
            .unwrap_or((get_default_sttream_params(), device_info::default()));
        Self {
            stm_ptr: stm,
            aggregate_device: None,
            mixer: None,
            resampler: Resampler::default(),
            input_stream_params: in_stm_params,
            output_stream_params: out_stm_params,
            input_dev_desc: AudioStreamBasicDescription::default(),
            output_dev_desc: AudioStreamBasicDescription::default(),
            input_unit: ptr::null_mut(),
            output_unit: ptr::null_mut(),
            voiceprocessing_unit_handle: None,
            input_device: in_dev,
            output_device: out_dev,
            input_processing_params: InputProcessingParams::NONE,
            input_mute: false,
            input_buffer_manager: None,
            units_running: false,
            default_input_listener: None,
            default_output_listener: None,
            input_alive_listener: None,
            input_source_listener: None,
            output_alive_listener: None,
            output_source_listener: None,
            input_logging: None,
            #[cfg(feature = "audio-dump")]
            audio_dump_session: ptr::null_mut(),
            #[cfg(feature = "audio-dump")]
            audio_dump_session_running: false,
            #[cfg(feature = "audio-dump")]
            audio_dump_input: ptr::null_mut(),
            #[cfg(feature = "audio-dump")]
            audio_dump_output: ptr::null_mut(),
        }
    }

    fn debug_assert_is_on_stream_queue(&self) {
        if self.stm_ptr.is_null() {
            return;
        }
        let stm = unsafe { &*self.stm_ptr };
        stm.queue.debug_assert_is_current();
    }

    fn start_audiounits(&mut self) -> Result<()> {
        self.debug_assert_is_on_stream_queue();
        // Only allowed to be called after the stream is initialized
        // and before the stream is destroyed.
        debug_assert!(!self.input_unit.is_null() || !self.output_unit.is_null());

        // Match other cubeb backends: reset the async logger's recorded
        // producer thread id before the CoreAudio I/O proc begins logging.
        unsafe {
            ffi::cubeb_async_log_reset_threads();
        }

        if !self.input_unit.is_null() {
            start_audiounit(self.input_unit)?;
        }
        if self.using_voice_processing_unit() {
            // Handle the VoiceProcessIO case where there is a single unit.

            // Always try to remember the applied input processing params. If they cannot
            // be applied in the new device pair, we notify the client of an error and it
            // will have to open a new stream.
            if let Err(r) =
                set_input_processing_params(self.input_unit, self.input_processing_params)
            {
                cubeb_log!(
                    "({:p}) Failed to set params of voiceprocessing. Error: {}",
                    self.stm_ptr,
                    r
                );
                return Err(r);
            }
            return Ok(());
        }
        if !self.output_unit.is_null() {
            start_audiounit(self.output_unit)?;
        }
        self.units_running = true;
        Ok(())
    }

    fn stop_audiounits(&mut self) {
        self.debug_assert_is_on_stream_queue();
        self.units_running = false;
        if !self.input_unit.is_null() {
            let r = stop_audiounit(self.input_unit);
            assert!(r.is_ok());
        }
        if self.using_voice_processing_unit() {
            // Handle the VoiceProcessIO case where there is a single unit.

            // Always reset input processing params to VPIO defaults in case VPIO is reused later.
            let vpio_defaults = InputProcessingParams::ECHO_CANCELLATION
                | InputProcessingParams::AUTOMATIC_GAIN_CONTROL
                | InputProcessingParams::NOISE_SUPPRESSION;
            if let Err(r) = set_input_processing_params(self.input_unit, vpio_defaults) {
                cubeb_log!(
                    "({:p}) Failed to reset params of voiceprocessing. Error: {}",
                    self.stm_ptr,
                    r
                );
            }
            return;
        }
        if !self.output_unit.is_null() {
            let r = stop_audiounit(self.output_unit);
            assert!(r.is_ok());
        }
    }

    fn has_input(&self) -> bool {
        self.input_stream_params.rate() > 0
    }

    fn has_output(&self) -> bool {
        self.output_stream_params.rate() > 0
    }

    fn using_voice_processing_unit(&self) -> bool {
        self.voiceprocessing_unit_handle.is_some()
    }

    fn same_clock_domain(&self) -> bool {
        self.debug_assert_is_on_stream_queue();
        // If not setting up a duplex stream, there is only one device,
        // no reclocking necessary.
        if !(self.has_input() && self.has_output()) {
            return true;
        }
        let input_domain = match get_clock_domain(self.input_device.id, DeviceType::INPUT) {
            Ok(clock_domain) => clock_domain,
            Err(_) => {
                cubeb_log!("Coudn't determine clock domains for input.");
                return false;
            }
        };

        let output_domain = match get_clock_domain(self.output_device.id, DeviceType::OUTPUT) {
            Ok(clock_domain) => clock_domain,
            Err(_) => {
                cubeb_log!("Coudn't determine clock domains for input.");
                return false;
            }
        };
        input_domain == output_domain
    }

    #[allow(non_upper_case_globals)]
    #[cfg(feature = "vpio-forcelist")]
    fn should_force_vpio_for_input_device(id: AudioDeviceID) -> bool {
        assert!(id != kAudioObjectUnknown);
        debug_assert_running_serially();
        match get_device_transport_type(id, DeviceType::INPUT) {
            Ok(kAudioDeviceTransportTypeBuiltIn) => {
                cubeb_log!(
                    "Input device {} is on the VPIO force list because it is built in, \
                     and its volume is known to be very low without VPIO whenever VPIO \
                     is hooked up to it elsewhere.",
                    id
                );
                true
            }
            _ => false,
        }
    }

    #[cfg(not(feature = "vpio-forcelist"))]
    fn should_force_vpio_for_input_device(_id: AudioDeviceID) -> bool {
        false
    }

    fn should_block_vpio_for_device_pair(
        &self,
        in_device: &device_info,
        out_device: &device_info,
    ) -> bool {
        self.debug_assert_is_on_stream_queue();
        cubeb_log!("Evaluating device pair against VPIO block list");
        let log_device_and_get_model_uid = |id, devtype| -> String {
            let device_model_uid = get_device_model_uid(id, devtype)
                .map(|s| s.into_string())
                .unwrap_or_default();
            cubeb_log!("{} uid=\"{}\", model_uid=\"{}\", transport_type={:?}, source={:?}, source_name=\"{}\", name=\"{}\", manufacturer=\"{}\"",
                if devtype == DeviceType::INPUT {
                    "Input"
                } else {
                    debug_assert_eq!(devtype, DeviceType::OUTPUT);
                    "Output"
                },
                get_device_uid(id, devtype).map(|s| s.into_string()).unwrap_or_default(),
                device_model_uid,
                convert_uint32_into_string(get_device_transport_type(id, devtype).unwrap_or(0)),
                convert_uint32_into_string(get_device_source(id, devtype).unwrap_or(0)),
                get_device_source_name(id, devtype).map(|s| s.into_string()).unwrap_or_default(),
                get_device_name(id, devtype).map(|s| s.into_string()).unwrap_or_default(),
                get_device_manufacturer(id, devtype).map(|s| s.into_string()).unwrap_or_default());
            device_model_uid
        };

        #[allow(non_upper_case_globals)]
        let in_id = match in_device.id {
            kAudioObjectUnknown => None,
            id => Some(id),
        };
        #[allow(non_upper_case_globals)]
        let out_id = match out_device.id {
            kAudioObjectUnknown => None,
            id => Some(id),
        };

        let (in_model_uid, out_model_uid) = (
            in_id
                .map(|id| log_device_and_get_model_uid(id, DeviceType::INPUT))
                .unwrap_or_default(),
            out_id
                .or_else(|| get_default_device(DeviceType::OUTPUT))
                .map(|id| log_device_and_get_model_uid(id, DeviceType::OUTPUT))
                .unwrap_or_default(),
        );

        if in_model_uid.contains(APPLE_STUDIO_DISPLAY_USB_ID)
            && out_model_uid.contains(APPLE_STUDIO_DISPLAY_USB_ID)
        {
            cubeb_log!("Both input and output device is an Apple Studio Display. BLOCKED");
            return true;
        }

        cubeb_log!("Device pair is not blocked");
        false
    }

    fn create_audiounits(
        &mut self,
        shared_voice_processing_unit: &mut SharedVoiceProcessingUnitManager,
    ) -> Result<(device_info, device_info)> {
        self.debug_assert_is_on_stream_queue();
        let should_use_voice_processing_unit = self.has_input()
            && (self
                .input_stream_params
                .prefs()
                .contains(StreamPrefs::VOICE)
                || CoreStreamData::should_force_vpio_for_input_device(self.input_device.id))
            && !self.should_block_vpio_for_device_pair(&self.input_device, &self.output_device)
            && macos_kernel_major_version() != Ok(MACOS_KERNEL_MAJOR_VERSION_MONTEREY);

        let should_use_aggregate_device = {
            // It's impossible to create an aggregate device from an aggregate device, and it's
            // unnecessary to create an aggregate device when opening the same device input/output. In
            // all other cases, use an aggregate device.
            let mut either_already_aggregate = false;
            if self.has_input() {
                let input_is_aggregate =
                    get_device_transport_type(self.input_device.id, DeviceType::INPUT).unwrap_or(0)
                        == kAudioDeviceTransportTypeAggregate;
                if input_is_aggregate {
                    either_already_aggregate = true;
                }
                cubeb_log!(
                    "Input device ID: {} (aggregate: {:?})",
                    self.input_device.id,
                    input_is_aggregate
                );
            }
            if self.has_output() {
                let output_is_aggregate =
                    get_device_transport_type(self.output_device.id, DeviceType::OUTPUT)
                        .unwrap_or(0)
                        == kAudioDeviceTransportTypeAggregate;
                if output_is_aggregate {
                    either_already_aggregate = true;
                }
                cubeb_log!(
                    "Output device ID: {} (aggregate: {:?})",
                    self.output_device.id,
                    output_is_aggregate
                );
            }
            // Only use an aggregate device when the device are different.
            self.has_input()
                && self.has_output()
                && self.input_device.id != self.output_device.id
                && !either_already_aggregate
        };

        // Create an AudioUnit:
        // - If we're eligible to use voice processing, try creating a VoiceProcessingIO AudioUnit.
        // - If we should use an aggregate device, try creating one and input and output AudioUnits next.
        // - As last resort, create regular AudioUnits. This is also the normal non-duplex path.

        if should_use_voice_processing_unit {
            if let Ok(mut au_handle) = get_voiceprocessing_audiounit(
                shared_voice_processing_unit,
                &self.input_device,
                &self.output_device,
            ) {
                self.input_unit = au_handle.as_mut().unit;
                if self.has_output() {
                    self.output_unit = au_handle.as_mut().unit;
                }
                self.voiceprocessing_unit_handle = Some(au_handle);
                return Ok((self.input_device.clone(), self.output_device.clone()));
            }
            cubeb_log!(
                "({:p}) Failed to get VoiceProcessingIO AudioUnit. Trying a regular one.",
                self.stm_ptr
            );
        }

        if should_use_aggregate_device {
            if let Ok(device) = AggregateDevice::new(self.input_device.id, self.output_device.id) {
                let in_dev_info = {
                    device_info {
                        id: device.get_device_id(),
                        ..self.input_device
                    }
                };
                let out_dev_info = {
                    device_info {
                        id: device.get_device_id(),
                        ..self.output_device
                    }
                };

                match (
                    create_audiounit(&in_dev_info),
                    create_audiounit(&out_dev_info),
                ) {
                    (Ok(in_au), Ok(out_au)) => {
                        cubeb_log!(
                            "({:p}) Using an aggregate device {} for input and output.",
                            self.stm_ptr,
                            device.get_device_id()
                        );
                        self.aggregate_device = Some(device);
                        self.input_unit = in_au;
                        self.output_unit = out_au;
                        return Ok((in_dev_info, out_dev_info));
                    }
                    (Err(e), Ok(au)) => {
                        cubeb_log!(
                            "({:p}) Failed to create input AudioUnit for aggregate device. Error: {}.",
                            self.stm_ptr,
                            e
                        );
                        dispose_audio_unit(au);
                    }
                    (Ok(au), Err(e)) => {
                        cubeb_log!(
                            "({:p}) Failed to create output AudioUnit for aggregate device. Error: {}.",
                            self.stm_ptr,
                            e
                        );
                        dispose_audio_unit(au);
                    }
                    (Err(e), _) => {
                        cubeb_log!(
                            "({:p}) Failed to create AudioUnits for aggregate device. Error: {}.",
                            self.stm_ptr,
                            e
                        );
                    }
                }
            }
            cubeb_log!(
                "({:p}) Failed to set up aggregate device. Using regular AudioUnits.",
                self.stm_ptr
            );
        }

        if self.has_input() {
            match create_audiounit(&self.input_device) {
                Ok(in_au) => self.input_unit = in_au,
                Err(e) => {
                    cubeb_log!(
                        "({:p}) Failed to create regular AudioUnit for input. Error: {}",
                        self.stm_ptr,
                        e
                    );
                    return Err(e);
                }
            }
        }

        if self.has_output() {
            match create_audiounit(&self.output_device) {
                Ok(out_au) => self.output_unit = out_au,
                Err(e) => {
                    cubeb_log!(
                        "({:p}) Failed to create regular AudioUnit for output. Error: {}",
                        self.stm_ptr,
                        e
                    );
                    if !self.input_unit.is_null() {
                        dispose_audio_unit(self.input_unit);
                        self.input_unit = ptr::null_mut();
                    }
                    return Err(e);
                }
            }
        }

        Ok((self.input_device.clone(), self.output_device.clone()))
    }

    #[allow(clippy::cognitive_complexity)] // TODO: Refactoring.
    fn setup(
        &mut self,
        shared_voice_processing_unit: &mut SharedVoiceProcessingUnitManager,
    ) -> Result<()> {
        self.debug_assert_is_on_stream_queue();
        if self
            .input_stream_params
            .prefs()
            .contains(StreamPrefs::LOOPBACK)
            || self
                .output_stream_params
                .prefs()
                .contains(StreamPrefs::LOOPBACK)
        {
            cubeb_log!("({:p}) Loopback not supported for audiounit.", self.stm_ptr);
            return Err(Error::NotSupported);
        }

        let same_clock_domain = self.same_clock_domain();
        let (in_dev_info, out_dev_info) = self.create_audiounits(shared_voice_processing_unit)?;
        let using_voice_processing_unit = self.using_voice_processing_unit();

        assert!(!self.stm_ptr.is_null());
        let stream = unsafe { &(*self.stm_ptr) };

        #[cfg(feature = "audio-dump")]
        unsafe {
            ffi::cubeb_audio_dump_init(&mut self.audio_dump_session);
        }

        // Configure I/O stream
        if self.has_input() {
            assert!(!self.input_unit.is_null());

            cubeb_log!(
                "({:p}) Initializing input by device info: {:?}",
                self.stm_ptr,
                in_dev_info
            );

            let device_channel_count =
                get_channel_count(self.input_device.id, DeviceType::INPUT).unwrap_or(0);
            if device_channel_count < self.input_stream_params.channels() {
                cubeb_log!(
                    "({:p}) Invalid input channel count; device={}, params={}",
                    self.stm_ptr,
                    device_channel_count,
                    self.input_stream_params.channels()
                );
                return Err(Error::InvalidParameter);
            }

            cubeb_log!(
                "({:p}) Opening input side: rate {}, channels {}, format {:?}, layout {:?}, prefs {:?}, latency in frames {}, voice processing {}.",
                self.stm_ptr,
                self.input_stream_params.rate(),
                self.input_stream_params.channels(),
                self.input_stream_params.format(),
                self.input_stream_params.layout(),
                self.input_stream_params.prefs(),
                stream.latency_frames,
                using_voice_processing_unit
            );

            // Get input device hardware information.
            let mut input_hw_desc = AudioStreamBasicDescription::default();
            let mut size = mem::size_of::<AudioStreamBasicDescription>();
            let r = audio_unit_get_property(
                self.input_unit,
                kAudioUnitProperty_StreamFormat,
                if using_voice_processing_unit {
                    // With a VPIO unit the input scope includes AEC reference channels.
                    // We need to use the output scope of the input bus.
                    kAudioUnitScope_Output
                } else {
                    // With a HAL unit the output scope for the input bus returns the number of
                    // output channels of the output device, i.e. it seems the bus is ignored.
                    kAudioUnitScope_Input
                },
                AU_IN_BUS,
                &mut input_hw_desc,
                &mut size,
            );
            if r != NO_ERR {
                cubeb_log!(
                    "AudioUnitGetProperty/input/kAudioUnitProperty_StreamFormat rv={}",
                    r
                );
                return Err(Error::Error);
            }
            cubeb_log!(
                "({:p}) Input hardware description: {:?}",
                self.stm_ptr,
                input_hw_desc
            );
            // These have been observed in the wild.
            if input_hw_desc.mSampleRate <= 0.0 || input_hw_desc.mChannelsPerFrame == 0 {
                cubeb_log!(
                    "({:p}) Invalid input hardware description: rate={}, channels={}",
                    self.stm_ptr,
                    input_hw_desc.mSampleRate,
                    input_hw_desc.mChannelsPerFrame
                );
                return Err(Error::Error);
            }
            // Notice: when we are using an aggregate device, input_hw_desc.mChannelsPerFrame is the
            // sum of all input channels of all devices added to the aggregate device.
            // Because we set the input device first on the aggregate device, the input device's
            // input channels will also be first among all the aggregate device's channels, when
            // accessed in the input callback. By requesting only the input device's channels here,
            // any other input channels, i.e. from the output device, will be truncated away.
            // Conversely, a VPIO unit downmixes the device's input channels to MONO, making
            // device_channel_count too large for stereo input devices.
            // We solve this with min().
            let params = unsafe {
                let mut p = *self.input_stream_params.as_ptr();
                p.channels = cmp::min(input_hw_desc.mChannelsPerFrame, device_channel_count);
                // Input AudioUnit must be configured with device's sample rate.
                // we will resample inside input callback.
                p.rate = input_hw_desc.mSampleRate as _;
                StreamParams::from(p)
            };

            self.input_dev_desc = create_stream_description(&params).inspect_err(|_| {
                cubeb_log!(
                    "({:p}) Setting format description for input failed.",
                    self.stm_ptr
                );
            })?;

            #[cfg(feature = "audio-dump")]
            {
                let name = format!("input-{:p}.wav", self.stm_ptr);
                let cname = CString::new(name).expect("OK");
                let rv = unsafe {
                    ffi::cubeb_audio_dump_stream_init(
                        self.audio_dump_session,
                        &mut self.audio_dump_input,
                        *params.as_ptr(),
                        cname.as_ptr(),
                    )
                };
                if rv == 0 {
                    assert_ne!(self.audio_dump_input, ptr::null_mut(),);
                    cubeb_log!("Successfully inited audio dump for input");
                } else {
                    cubeb_log!("Failed to init audio dump for input");
                }
            }

            assert_eq!(self.input_dev_desc.mSampleRate, input_hw_desc.mSampleRate);

            // Use latency to set buffer size
            assert_ne!(stream.latency_frames, 0);
            if let Err(r) =
                set_buffer_size_sync(self.input_unit, DeviceType::INPUT, stream.latency_frames)
            {
                cubeb_log!("({:p}) Error in change input buffer size.", self.stm_ptr);
                return Err(r);
            }

            let r = audio_unit_set_property(
                self.input_unit,
                kAudioUnitProperty_StreamFormat,
                kAudioUnitScope_Output,
                AU_IN_BUS,
                &self.input_dev_desc,
                mem::size_of::<AudioStreamBasicDescription>(),
            );
            if r != NO_ERR {
                cubeb_log!(
                    "AudioUnitSetProperty/input/kAudioUnitProperty_StreamFormat rv={}",
                    r
                );
                return Err(Error::Error);
            }

            // Frames per buffer in the input callback.
            let r = audio_unit_set_property(
                self.input_unit,
                kAudioUnitProperty_MaximumFramesPerSlice,
                kAudioUnitScope_Global,
                AU_IN_BUS,
                &stream.latency_frames,
                mem::size_of::<u32>(),
            );
            if r != NO_ERR {
                cubeb_log!(
                    "AudioUnitSetProperty/input/kAudioUnitProperty_MaximumFramesPerSlice rv={}",
                    r
                );
                return Err(Error::Error);
            }

            // When we use the aggregate device, the self.input_dev_desc.mChannelsPerFrame is the
            // total input channel count of all the device added in the aggregate device. However,
            // we only need the audio data captured by the requested input device, so we need to
            // ignore some data captured by the audio input of the requested output device (e.g.,
            // the requested output device is a USB headset with built-in mic), in the beginning of
            // the raw data taken from input callback.
            self.input_buffer_manager = Some(BufferManager::new(
                self.input_stream_params.format(),
                SAFE_MAX_LATENCY_FRAMES as usize,
                self.input_dev_desc.mChannelsPerFrame as usize,
                self.input_dev_desc
                    .mChannelsPerFrame
                    .saturating_sub(device_channel_count) as usize,
                self.input_stream_params.channels() as usize,
            ));

            let aurcbs_in = AURenderCallbackStruct {
                inputProc: Some(audiounit_input_callback),
                inputProcRefCon: self.stm_ptr as *mut c_void,
            };

            let r = audio_unit_set_property(
                self.input_unit,
                kAudioOutputUnitProperty_SetInputCallback,
                kAudioUnitScope_Global,
                AU_OUT_BUS,
                &aurcbs_in,
                mem::size_of_val(&aurcbs_in),
            );
            if r != NO_ERR {
                cubeb_log!(
                    "AudioUnitSetProperty/input/kAudioOutputUnitProperty_SetInputCallback rv={}",
                    r
                );
                return Err(Error::Error);
            }

            stream.frames_read.store(0, Ordering::SeqCst);

            cubeb_log!(
                "({:p}) Input audiounit init with device {} successfully.",
                self.stm_ptr,
                in_dev_info.id
            );
        }

        if self.has_input() && !self.has_output() && using_voice_processing_unit {
            // We must configure the output side of VPIO to match the input side, even if we don't use it.
            let r = audio_unit_set_property(
                self.input_unit,
                kAudioUnitProperty_StreamFormat,
                kAudioUnitScope_Input,
                AU_OUT_BUS,
                &self.input_dev_desc,
                mem::size_of::<AudioStreamBasicDescription>(),
            );
            if r != NO_ERR {
                cubeb_log!(
                    "AudioUnitSetProperty/output/kAudioUnitProperty_StreamFormat rv={}",
                    r
                );
                return Err(Error::Error);
            }
        }

        if self.has_output() {
            assert!(!self.output_unit.is_null());

            cubeb_log!(
                "({:p}) Initialize output by device info: {:?}",
                self.stm_ptr,
                out_dev_info
            );

            cubeb_log!(
                "({:p}) Opening output side: rate {}, channels {}, format {:?}, layout {:?}, prefs {:?}, latency in frames {}, voice processing {}.",
                self.stm_ptr,
                self.output_stream_params.rate(),
                self.output_stream_params.channels(),
                self.output_stream_params.format(),
                self.output_stream_params.layout(),
                self.output_stream_params.prefs(),
                stream.latency_frames,
                using_voice_processing_unit
            );

            // Get output device hardware information.
            let mut output_hw_desc = AudioStreamBasicDescription::default();
            let mut size = mem::size_of::<AudioStreamBasicDescription>();
            let r = audio_unit_get_property(
                self.output_unit,
                kAudioUnitProperty_StreamFormat,
                if using_voice_processing_unit {
                    // With a VPIO unit the output scope includes all channels in the hw.
                    // The VPIO unit however is only MONO which the input scope reflects.
                    kAudioUnitScope_Input
                } else {
                    // With a HAL unit the output scope for the output bus returns the number of
                    // output channels of the hw, as we want. The input scope seems limited to
                    // two channels.
                    kAudioUnitScope_Output
                },
                AU_OUT_BUS,
                &mut output_hw_desc,
                &mut size,
            );
            if r != NO_ERR {
                cubeb_log!(
                    "AudioUnitGetProperty/output/kAudioUnitProperty_StreamFormat rv={}",
                    r
                );
                return Err(Error::Error);
            }
            cubeb_log!(
                "({:p}) Output hardware description: {:?}",
                self.stm_ptr,
                output_hw_desc
            );

            // These have been observed in the wild.
            if output_hw_desc.mSampleRate <= 0.0 || output_hw_desc.mChannelsPerFrame == 0 {
                cubeb_log!(
                    "({:p}) Invalid output hardware description: rate={}, channels={}",
                    self.stm_ptr,
                    output_hw_desc.mSampleRate,
                    output_hw_desc.mChannelsPerFrame
                );
                return Err(Error::Error);
            }

            // Simple case of stereo output, map to the stereo pair (that might not be the first
            // two channels). Fall back to regular mixing if this fails.
            let mut maybe_need_mixer = true;
            if self.output_stream_params.channels() == 2
                && self.output_stream_params.layout() == ChannelLayout::STEREO
            {
                let layout = AudioChannelLayout {
                    mChannelLayoutTag: kAudioChannelLayoutTag_Stereo,
                    ..Default::default()
                };
                let r = audio_unit_set_property(
                    self.output_unit,
                    kAudioUnitProperty_AudioChannelLayout,
                    kAudioUnitScope_Input,
                    AU_OUT_BUS,
                    &layout,
                    mem::size_of::<AudioChannelLayout>(),
                );
                if r != NO_ERR {
                    cubeb_log!(
                        "AudioUnitSetProperty/output/kAudioUnitProperty_AudioChannelLayout rv={}",
                        r
                    );
                }
                maybe_need_mixer = r != NO_ERR;
            }

            // Notice: when we are using aggregate device, the output_hw_desc.mChannelsPerFrame is
            // the total of all the output channel count of the devices added in the aggregate device.
            // Due to our aggregate device settings, the data recorded by the input device's output
            // channels will be appended at the end of the raw data given by the output callback.
            let params = unsafe {
                let mut p = *self.output_stream_params.as_ptr();
                p.channels = if maybe_need_mixer {
                    output_hw_desc.mChannelsPerFrame
                } else {
                    self.output_stream_params.channels()
                };
                if using_voice_processing_unit {
                    // VPIO will always use the sample rate of the input hw for both input and output,
                    // as reported to us. (We can override it but we cannot improve quality this way).
                    p.rate = self.input_dev_desc.mSampleRate as _;
                }
                StreamParams::from(p)
            };

            self.output_dev_desc = create_stream_description(&params).inspect_err(|_| {
                cubeb_log!(
                    "({:p}) Could not initialize the audio stream description.",
                    self.stm_ptr
                );
            })?;

            #[cfg(feature = "audio-dump")]
            {
                let name = format!("output-{:p}.wav", self.stm_ptr);
                let cname = CString::new(name).expect("OK");
                let rv = unsafe {
                    ffi::cubeb_audio_dump_stream_init(
                        self.audio_dump_session,
                        &mut self.audio_dump_output,
                        *params.as_ptr(),
                        cname.as_ptr(),
                    )
                };
                if rv == 0 {
                    assert_ne!(self.audio_dump_output, ptr::null_mut(),);
                    cubeb_log!("Successfully inited audio dump for output");
                } else {
                    cubeb_log!("Failed to init audio dump for output");
                }
            }

            let device_layout = self
                .get_output_channel_layout()
                .inspect_err(|_| {
                    cubeb_log!(
                        "({:p}) Could not get any channel layout. Defaulting to no channels.",
                        self.stm_ptr
                    );
                })
                .unwrap_or_default();

            cubeb_log!(
                "({:p} Using output device channel layout {:?}",
                self.stm_ptr,
                device_layout
            );

            if maybe_need_mixer {
                // The mixer will be set up when
                // 0. not playing simply stereo, or failing to set the channel layout to the stereo
                //    pair
                // 1. using aggregate device whose input device has output channels
                // 2. output device has more channels than we need, and stream isn't simply stereo
                // 3. output device has different layout than the one we have
                self.mixer = if self.output_dev_desc.mChannelsPerFrame
                    != self.output_stream_params.channels()
                    || device_layout != mixer::get_channel_order(self.output_stream_params.layout())
                {
                    cubeb_log!("Incompatible channel layouts detected, setting up remixer");
                    // We will be remixing the data before it reaches the output device.
                    Some(Mixer::new(
                        self.output_stream_params.format(),
                        self.output_stream_params.channels() as usize,
                        self.output_stream_params.layout(),
                        self.output_dev_desc.mChannelsPerFrame as usize,
                        device_layout,
                    ))
                } else {
                    None
                };
            }

            let r = audio_unit_set_property(
                self.output_unit,
                kAudioUnitProperty_StreamFormat,
                kAudioUnitScope_Input,
                AU_OUT_BUS,
                &self.output_dev_desc,
                mem::size_of::<AudioStreamBasicDescription>(),
            );
            if r != NO_ERR {
                cubeb_log!(
                    "AudioUnitSetProperty/output/kAudioUnitProperty_StreamFormat rv={}",
                    r
                );
                return Err(Error::Error);
            }

            // Use latency to set buffer size
            assert_ne!(stream.latency_frames, 0);
            if let Err(r) =
                set_buffer_size_sync(self.output_unit, DeviceType::OUTPUT, stream.latency_frames)
            {
                cubeb_log!("({:p}) Error in change output buffer size.", self.stm_ptr);
                return Err(r);
            }

            // Frames per buffer in the input callback.
            let r = audio_unit_set_property(
                self.output_unit,
                kAudioUnitProperty_MaximumFramesPerSlice,
                kAudioUnitScope_Global,
                AU_OUT_BUS,
                &stream.latency_frames,
                mem::size_of::<u32>(),
            );
            if r != NO_ERR {
                cubeb_log!(
                    "AudioUnitSetProperty/output/kAudioUnitProperty_MaximumFramesPerSlice rv={}",
                    r
                );
                return Err(Error::Error);
            }

            let aurcbs_out = AURenderCallbackStruct {
                inputProc: Some(audiounit_output_callback),
                inputProcRefCon: self.stm_ptr as *mut c_void,
            };
            let r = audio_unit_set_property(
                self.output_unit,
                kAudioUnitProperty_SetRenderCallback,
                kAudioUnitScope_Global,
                AU_OUT_BUS,
                &aurcbs_out,
                mem::size_of_val(&aurcbs_out),
            );
            if r != NO_ERR {
                cubeb_log!(
                    "AudioUnitSetProperty/output/kAudioUnitProperty_SetRenderCallback rv={}",
                    r
                );
                return Err(Error::Error);
            }

            stream.frames_written.store(0, Ordering::SeqCst);

            cubeb_log!(
                "({:p}) Output audiounit init with device {} successfully.",
                self.stm_ptr,
                out_dev_info.id
            );
        }

        // We use a resampler because input AudioUnit operates
        // reliable only in the capture device sample rate.
        // Resampler will convert it to the user sample rate
        // and deliver it to the callback.
        let target_sample_rate = if self.has_input() {
            self.input_stream_params.rate()
        } else {
            assert!(self.has_output());
            self.output_stream_params.rate()
        };

        let resampler_input_params = if self.has_input() {
            let mut p = unsafe { *(self.input_stream_params.as_ptr()) };
            p.rate = self.input_dev_desc.mSampleRate as u32;
            Some(p)
        } else {
            None
        };
        let resampler_output_params = if self.has_output() {
            let mut p = unsafe { *(self.output_stream_params.as_ptr()) };
            p.rate = self.output_dev_desc.mSampleRate as u32;
            Some(p)
        } else {
            None
        };

        // Only reclock if there is an input and we couldn't use an aggregate device, and the
        // devices are not part of the same clock domain.
        let reclock_policy = if self.aggregate_device.is_none()
            && !using_voice_processing_unit
            && !same_clock_domain
        {
            cubeb_log!(
                "Reclocking duplex steam using_aggregate_device={} same_clock_domain={}",
                self.aggregate_device.is_some(),
                same_clock_domain
            );
            ffi::CUBEB_RESAMPLER_RECLOCK_INPUT
        } else {
            ffi::CUBEB_RESAMPLER_RECLOCK_NONE
        };

        self.resampler = Resampler::new(
            self.stm_ptr as *mut ffi::cubeb_stream,
            resampler_input_params,
            resampler_output_params,
            target_sample_rate,
            stream.data_callback,
            stream.user_ptr,
            ffi::CUBEB_RESAMPLER_QUALITY_DESKTOP,
            reclock_policy,
        );

        // In duplex, the input thread might be different from the output thread, and we're logging
        // everything from the output thread: relay the audio input callback information using a
        // ring buffer to diagnose issues.
        if self.has_input() && self.has_output() {
            self.input_logging = Some(InputCallbackLogger::new());
        }

        #[cfg(feature = "audio-dump")]
        {
            unsafe { ffi::cubeb_audio_dump_start(self.audio_dump_session) };
            self.audio_dump_session_running = true;
        }

        if !self.input_unit.is_null() {
            let r = audio_unit_initialize(self.input_unit);
            if r != NO_ERR {
                cubeb_log!("AudioUnitInitialize/input rv={}", r);
                return Err(Error::Error);
            }

            stream.input_device_latency_frames.store(
                get_fixed_latency(self.input_device.id, DeviceType::INPUT),
                Ordering::SeqCst,
            );
        }

        if !self.output_unit.is_null() {
            if !std::ptr::eq(self.input_unit, self.output_unit) {
                let r = audio_unit_initialize(self.output_unit);
                if r != NO_ERR {
                    cubeb_log!("AudioUnitInitialize/output rv={}", r);
                    return Err(Error::Error);
                }
            }

            stream.output_device_latency_frames.store(
                get_fixed_latency(self.output_device.id, DeviceType::OUTPUT),
                Ordering::SeqCst,
            );

            let mut unit_s: f64 = 0.0;
            let mut size = mem::size_of_val(&unit_s);
            if audio_unit_get_property(
                self.output_unit,
                kAudioUnitProperty_Latency,
                kAudioUnitScope_Global,
                0,
                &mut unit_s,
                &mut size,
            ) == NO_ERR
            {
                stream.output_device_latency_frames.fetch_add(
                    (unit_s * self.output_dev_desc.mSampleRate) as u32,
                    Ordering::SeqCst,
                );
            }
        }

        #[cfg(not(feature = "no-private-apis"))]
        if using_voice_processing_unit {
            // The VPIO AudioUnit automatically ducks other audio streams on the VPIO
            // output device. Its ramp duration is 0.5s when ducking, so unduck similarly
            // now.
            // NOTE: On MacOS 14 the ducking happens on creation of the VPIO AudioUnit.
            //       On MacOS 10.15 it happens on both creation and initialization, which
            //       is why we defer the unducking until now.
            #[allow(non_upper_case_globals)]
            let mut device = match self.output_device.id {
                kAudioObjectUnknown => None,
                id => Some(id),
            };
            device = device.or_else(|| get_default_device(DeviceType::OUTPUT));
            match device {
                None => {
                    cubeb_log!(
                        "({:p}) No output device to undo vpio ducking on",
                        self.stm_ptr
                    );
                }
                Some(id) => {
                    let r = audio_device_duck(id, 1.0, ptr::null_mut(), 0.5);
                    if r != NO_ERR {
                        cubeb_log!(
                            "({:p}) Failed to undo ducking of voiceprocessing on output device {}. Proceeding... Error: {}",
                            self.stm_ptr,
                            id,
                            r
                        );
                    }
                }
            };

            // Always try to remember the applied input mute state. If it cannot be applied
            // to the new device pair, we notify the client of an error and it will have to
            // open a new stream.
            if let Err(r) = set_input_mute(self.input_unit, self.input_mute) {
                cubeb_log!(
                    "({:p}) Failed to set mute state of voiceprocessing. Error: {}",
                    self.stm_ptr,
                    r
                );
                return Err(r);
            }
        }

        if let Err(r) = self.install_system_changed_callback() {
            cubeb_log!(
                "({:p}) Could not install the device change callback.",
                self.stm_ptr
            );
            return Err(r);
        }

        if let Err(r) = self.install_device_changed_callback() {
            cubeb_log!(
                "({:p}) Could not install all device change callback.",
                self.stm_ptr
            );
            return Err(r);
        }

        // We have either default_input_listener or input_alive_listener.
        // We cannot have both of them at the same time.
        assert!(
            !self.has_input()
                || ((self.default_input_listener.is_some() != self.input_alive_listener.is_some())
                    && (self.default_input_listener.is_some()
                        || self.input_alive_listener.is_some()))
        );

        // We have either default_output_listener or output_alive_listener.
        // We cannot have both of them at the same time.
        assert!(
            !self.has_output()
                || ((self.default_output_listener.is_some()
                    != self.output_alive_listener.is_some())
                    && (self.default_output_listener.is_some()
                        || self.output_alive_listener.is_some()))
        );

        Ok(())
    }

    fn close(&mut self) {
        self.debug_assert_is_on_stream_queue();
        if !self.input_unit.is_null() {
            audio_unit_uninitialize(self.input_unit);
            if self.using_voice_processing_unit() {
                // Handle the VoiceProcessIO case where there is a single unit.
                self.output_unit = ptr::null_mut();
            }

            // Cannot unset self.input_unit yet, since the output callback might be live
            // and reading it.
        }

        if !self.output_unit.is_null() {
            audio_unit_uninitialize(self.output_unit);
            dispose_audio_unit(self.output_unit);
            self.output_unit = ptr::null_mut();
        }

        if !self.input_unit.is_null() {
            if !self.using_voice_processing_unit() {
                // The VPIO unit is shared and must not be disposed.
                dispose_audio_unit(self.input_unit);
            }
            self.input_unit = ptr::null_mut();
        }

        // Return the VPIO unit if present.
        self.voiceprocessing_unit_handle = None;

        #[cfg(feature = "audio-dump")]
        {
            if !self.audio_dump_session.is_null() {
                unsafe {
                    ffi::cubeb_audio_dump_stop(self.audio_dump_session);
                    if !self.audio_dump_input.is_null() {
                        let rv = ffi::cubeb_audio_dump_stream_shutdown(
                            self.audio_dump_session,
                            self.audio_dump_input,
                        );
                        if rv != 0 {
                            cubeb_log!("Failed to shutdown audio dump for input");
                        }
                    }
                    if !self.audio_dump_output.is_null() {
                        let rv = ffi::cubeb_audio_dump_stream_shutdown(
                            self.audio_dump_session,
                            self.audio_dump_output,
                        );
                        if rv != 0 {
                            cubeb_log!("Failed to shutdown audio dump for output");
                        }
                    }
                    ffi::cubeb_audio_dump_shutdown(self.audio_dump_session);
                    self.audio_dump_session = ptr::null_mut();
                    self.audio_dump_session_running = false;
                }
            }
        }

        self.resampler.destroy();
        self.mixer = None;
        self.aggregate_device = None;

        if self.uninstall_system_changed_callback().is_err() {
            cubeb_log!(
                "({:p}) Could not uninstall the system changed callback",
                self.stm_ptr
            );
        }

        if self.uninstall_device_changed_callback().is_err() {
            cubeb_log!(
                "({:p}) Could not uninstall all device change listeners",
                self.stm_ptr
            );
        }
    }

    fn install_device_changed_callback(&mut self) -> Result<()> {
        self.debug_assert_is_on_stream_queue();
        assert!(!self.stm_ptr.is_null());
        let stm = unsafe { &(*self.stm_ptr) };

        if !self.output_unit.is_null() {
            assert_ne!(self.output_device.id, kAudioObjectUnknown);
            assert_ne!(self.output_device.id, kAudioObjectSystemObject);
            assert!(
                self.output_source_listener.is_none(),
                "register output_source_listener without unregistering the one in use"
            );
            assert!(
                self.output_alive_listener.is_none(),
                "register output_alive_listener without unregistering the one in use"
            );

            // Get the notification when the data source on the same device changes,
            // e.g., when the user plugs in a TRRS headset into the headphone jack.
            self.output_source_listener = Some(device_property_listener::new(
                self.output_device.id,
                get_property_address(Property::DeviceSource, DeviceType::OUTPUT),
                audiounit_property_listener_callback,
            ));
            let rv = stm.add_device_listener(self.output_source_listener.as_ref().unwrap());
            if rv != NO_ERR {
                self.output_source_listener = None;
                cubeb_log!("AudioObjectAddPropertyListener/output/kAudioDevicePropertyDataSource rv={}, device id={}", rv, self.output_device.id);
                return Err(Error::Error);
            }

            // Get the notification when the output device is going away
            // if the output doesn't follow the system default.
            if !self
                .output_device
                .flags
                .contains(device_flags::DEV_SELECTED_DEFAULT)
            {
                self.output_alive_listener = Some(device_property_listener::new(
                    self.output_device.id,
                    get_property_address(
                        Property::DeviceIsAlive,
                        DeviceType::INPUT | DeviceType::OUTPUT,
                    ),
                    audiounit_property_listener_callback,
                ));
                let rv = stm.add_device_listener(self.output_alive_listener.as_ref().unwrap());
                if rv != NO_ERR {
                    self.output_alive_listener = None;
                    cubeb_log!("AudioObjectAddPropertyListener/output/kAudioDevicePropertyDeviceIsAlive rv={}, device id ={}", rv, self.output_device.id);
                    return Err(Error::Error);
                }
            }
        }

        if !self.input_unit.is_null() {
            assert_ne!(self.input_device.id, kAudioObjectUnknown);
            assert_ne!(self.input_device.id, kAudioObjectSystemObject);
            assert!(
                self.input_source_listener.is_none(),
                "register input_source_listener without unregistering the one in use"
            );
            assert!(
                self.input_alive_listener.is_none(),
                "register input_alive_listener without unregistering the one in use"
            );

            // Get the notification when the data source on the same device changes,
            // e.g., when the user plugs in a TRRS mic into the headphone jack.
            self.input_source_listener = Some(device_property_listener::new(
                self.input_device.id,
                get_property_address(Property::DeviceSource, DeviceType::INPUT),
                audiounit_property_listener_callback,
            ));
            let rv = stm.add_device_listener(self.input_source_listener.as_ref().unwrap());
            if rv != NO_ERR {
                self.input_source_listener = None;
                cubeb_log!("AudioObjectAddPropertyListener/input/kAudioDevicePropertyDataSource rv={}, device id={}", rv, self.input_device.id);
                return Err(Error::Error);
            }

            // Get the notification when the input device is going away
            // if the input doesn't follow the system default.
            if !self
                .input_device
                .flags
                .contains(device_flags::DEV_SELECTED_DEFAULT)
            {
                self.input_alive_listener = Some(device_property_listener::new(
                    self.input_device.id,
                    get_property_address(
                        Property::DeviceIsAlive,
                        DeviceType::INPUT | DeviceType::OUTPUT,
                    ),
                    audiounit_property_listener_callback,
                ));
                let rv = stm.add_device_listener(self.input_alive_listener.as_ref().unwrap());
                if rv != NO_ERR {
                    self.input_alive_listener = None;
                    cubeb_log!("AudioObjectAddPropertyListener/input/kAudioDevicePropertyDeviceIsAlive rv={}, device id ={}", rv, self.input_device.id);
                    return Err(Error::Error);
                }
            }
        }

        Ok(())
    }

    fn install_system_changed_callback(&mut self) -> Result<()> {
        self.debug_assert_is_on_stream_queue();
        assert!(!self.stm_ptr.is_null());
        let stm = unsafe { &(*self.stm_ptr) };

        if !self.output_unit.is_null()
            && self
                .output_device
                .flags
                .contains(device_flags::DEV_SELECTED_DEFAULT)
        {
            assert!(
                self.default_output_listener.is_none(),
                "register default_output_listener without unregistering the one in use"
            );

            // Get the notification when the default output audio changes, e.g.,
            // when the user plugs in a USB headset and the system chooses it automatically as the default,
            // or when another device is chosen in the dropdown list.
            self.default_output_listener = Some(device_property_listener::new(
                kAudioObjectSystemObject,
                get_property_address(
                    Property::HardwareDefaultOutputDevice,
                    DeviceType::INPUT | DeviceType::OUTPUT,
                ),
                audiounit_property_listener_callback,
            ));
            let r = stm.add_device_listener(self.default_output_listener.as_ref().unwrap());
            if r != NO_ERR {
                self.default_output_listener = None;
                cubeb_log!("AudioObjectAddPropertyListener/output/kAudioHardwarePropertyDefaultOutputDevice rv={}", r);
                return Err(Error::Error);
            }
        }

        if !self.input_unit.is_null()
            && self
                .input_device
                .flags
                .contains(device_flags::DEV_SELECTED_DEFAULT)
        {
            assert!(
                self.default_input_listener.is_none(),
                "register default_input_listener without unregistering the one in use"
            );

            // Get the notification when the default intput audio changes, e.g.,
            // when the user plugs in a USB mic and the system chooses it automatically as the default,
            // or when another device is chosen in the system preference.
            self.default_input_listener = Some(device_property_listener::new(
                kAudioObjectSystemObject,
                get_property_address(
                    Property::HardwareDefaultInputDevice,
                    DeviceType::INPUT | DeviceType::OUTPUT,
                ),
                audiounit_property_listener_callback,
            ));
            let r = stm.add_device_listener(self.default_input_listener.as_ref().unwrap());
            if r != NO_ERR {
                self.default_input_listener = None;
                cubeb_log!("AudioObjectAddPropertyListener/input/kAudioHardwarePropertyDefaultInputDevice rv={}", r);
                return Err(Error::Error);
            }
        }

        Ok(())
    }

    fn uninstall_device_changed_callback(&mut self) -> Result<()> {
        self.debug_assert_is_on_stream_queue();
        if self.stm_ptr.is_null() {
            assert!(
                self.output_source_listener.is_none()
                    && self.output_alive_listener.is_none()
                    && self.input_source_listener.is_none()
                    && self.input_alive_listener.is_none()
            );
            return Ok(());
        }

        let stm = unsafe { &(*self.stm_ptr) };

        // Failing to uninstall listeners is not a fatal error.
        let mut r = Ok(());

        if let Some(listener) = self.output_source_listener.take() {
            let rv = stm.remove_device_listener(&listener);
            if rv != NO_ERR {
                cubeb_log!("AudioObjectRemovePropertyListener/output/kAudioDevicePropertyDataSource rv={}, device id={}", rv, self.output_device.id);
                r = Err(Error::Error);
            }
        }

        if let Some(listener) = self.output_alive_listener.take() {
            let rv = stm.remove_device_listener(&listener);
            if rv != NO_ERR {
                cubeb_log!("AudioObjectRemovePropertyListener/output/kAudioDevicePropertyDeviceIsAlive rv={}, device id={}", rv, self.output_device.id);
                r = Err(Error::Error);
            }
        }

        if let Some(listener) = self.input_source_listener.take() {
            let rv = stm.remove_device_listener(&listener);
            if rv != NO_ERR {
                cubeb_log!("AudioObjectRemovePropertyListener/input/kAudioDevicePropertyDataSource rv={}, device id={}", rv, self.input_device.id);
                r = Err(Error::Error);
            }
        }

        if let Some(listener) = self.input_alive_listener.take() {
            let rv = stm.remove_device_listener(&listener);
            if rv != NO_ERR {
                cubeb_log!("AudioObjectRemovePropertyListener/input/kAudioDevicePropertyDeviceIsAlive rv={}, device id={}", rv, self.input_device.id);
                r = Err(Error::Error);
            }
        }

        r
    }

    fn uninstall_system_changed_callback(&mut self) -> Result<()> {
        self.debug_assert_is_on_stream_queue();
        if self.stm_ptr.is_null() {
            assert!(
                self.default_output_listener.is_none() && self.default_input_listener.is_none()
            );
            return Ok(());
        }

        let stm = unsafe { &(*self.stm_ptr) };

        if let Some(listener) = self.default_output_listener.take() {
            let r = stm.remove_device_listener(&listener);
            if r != NO_ERR {
                return Err(Error::Error);
            }
        }

        if let Some(listener) = self.default_input_listener.take() {
            let r = stm.remove_device_listener(&listener);
            if r != NO_ERR {
                return Err(Error::Error);
            }
        }

        Ok(())
    }

    fn get_output_channel_layout(&self) -> Result<Vec<mixer::Channel>> {
        self.debug_assert_is_on_stream_queue();
        assert!(!self.output_unit.is_null());
        if self.using_voice_processing_unit() {
            return Ok(get_channel_order(ChannelLayout::MONO));
        }
        get_channel_layout(self.output_unit)
    }
}

impl Drop for CoreStreamData<'_> {
    fn drop(&mut self) {
        self.debug_assert_is_on_stream_queue();
        self.stop_audiounits();
        self.close();
    }
}

#[derive(Debug, Clone)]
struct OutputCallbackTimingData {
    frames_queued: u64,
    timestamp: u64,
    buffer_size: u64,
}

// The fisrt two members of the Cubeb stream must be a pointer to its Cubeb context and a void user
// defined pointer. The Cubeb interface use this assumption to operate the Cubeb APIs.
// #[repr(C)] is used to prevent any padding from being added in the beginning of the AudioUnitStream.
#[repr(C)]
#[derive(Debug)]
struct AudioUnitStream<'ctx> {
    context: &'ctx mut AudioUnitContext,
    user_ptr: *mut c_void,
    // Task queue for the stream.
    queue: Queue,

    data_callback: ffi::cubeb_data_callback,
    state_callback: ffi::cubeb_state_callback,
    device_changed_callback: Mutex<ffi::cubeb_device_changed_callback>,
    // Frame counters
    frames_queued: u64,
    // How many frames got read from the input since the stream started (includes
    // padded silence)
    frames_read: AtomicUsize,
    // How many frames got written to the output device since the stream started
    frames_written: AtomicUsize,
    stopped: AtomicBool,
    draining: AtomicBool,
    reinit_pending: AtomicBool,
    delayed_reinit: bool,
    destroy_pending: AtomicBool,
    // Latency requested by the user.
    latency_frames: u32,
    // Fixed latency, characteristic of the device.
    output_device_latency_frames: AtomicU32,
    input_device_latency_frames: AtomicU32,
    // Total latency: the latency of the device + the OS latency
    total_output_latency_frames: AtomicU32,
    total_input_latency_frames: AtomicU32,
    output_callback_timing_data_read: triple_buffer::Output<OutputCallbackTimingData>,
    output_callback_timing_data_write: triple_buffer::Input<OutputCallbackTimingData>,
    prev_position: u64,
    // This is true if a device change callback is currently running.
    switching_device: AtomicBool,
    core_stream_data: CoreStreamData<'ctx>,
}

impl<'ctx> AudioUnitStream<'ctx> {
    fn new(
        context: &'ctx mut AudioUnitContext,
        user_ptr: *mut c_void,
        data_callback: ffi::cubeb_data_callback,
        state_callback: ffi::cubeb_state_callback,
        latency_frames: u32,
    ) -> Self {
        let output_callback_timing_data =
            triple_buffer::TripleBuffer::new(OutputCallbackTimingData {
                frames_queued: 0,
                timestamp: 0,
                buffer_size: 0,
            });
        let (output_callback_timing_data_write, output_callback_timing_data_read) =
            output_callback_timing_data.split();
        let queue = context.serial_queue.clone();
        AudioUnitStream {
            context,
            user_ptr,
            queue,
            data_callback,
            state_callback,
            device_changed_callback: Mutex::new(None),
            frames_queued: 0,
            frames_read: AtomicUsize::new(0),
            frames_written: AtomicUsize::new(0),
            stopped: AtomicBool::new(true),
            draining: AtomicBool::new(false),
            reinit_pending: AtomicBool::new(false),
            delayed_reinit: false,
            destroy_pending: AtomicBool::new(false),
            latency_frames,
            output_device_latency_frames: AtomicU32::new(0),
            input_device_latency_frames: AtomicU32::new(0),
            total_output_latency_frames: AtomicU32::new(0),
            total_input_latency_frames: AtomicU32::new(0),
            output_callback_timing_data_write,
            output_callback_timing_data_read,
            prev_position: 0,
            switching_device: AtomicBool::new(false),
            core_stream_data: CoreStreamData::default(),
        }
    }

    fn add_device_listener(&self, listener: &device_property_listener) -> OSStatus {
        self.queue.debug_assert_is_current();
        audio_object_add_property_listener(
            listener.device,
            &listener.property,
            listener.listener,
            self as *const Self as *mut c_void,
        )
    }

    fn remove_device_listener(&self, listener: &device_property_listener) -> OSStatus {
        self.queue.debug_assert_is_current();
        audio_object_remove_property_listener(
            listener.device,
            &listener.property,
            listener.listener,
            self as *const Self as *mut c_void,
        )
    }

    fn notify_state_changed(&self, state: State) {
        if self.state_callback.is_none() {
            return;
        }
        let callback = self.state_callback.unwrap();
        unsafe {
            callback(
                self as *const AudioUnitStream as *mut ffi::cubeb_stream,
                self.user_ptr,
                state.into(),
            );
        }
    }

    fn reinit(&mut self) -> Result<()> {
        self.queue.debug_assert_is_current();
        // Call stop_audiounits to avoid potential data race. If there is a running data callback,
        // which locks a mutex inside CoreAudio framework, then this call will block the current
        // thread until the callback is finished since this call asks to lock a mutex inside
        // CoreAudio framework that is used by the data callback.
        if !self.stopped.load(Ordering::SeqCst) {
            self.core_stream_data.stop_audiounits();
        }

        if self.stopped.load(Ordering::SeqCst) {
            // Something stopped the stream, reinit on next start
            self.delayed_reinit = true;
            return Ok(());
        }

        debug_assert!(
            !self.core_stream_data.input_unit.is_null()
                || !self.core_stream_data.output_unit.is_null()
        );
        let vol_rv = if self.core_stream_data.output_unit.is_null() {
            Err(Error::Error)
        } else {
            get_volume(self.core_stream_data.output_unit)
        };

        self.core_stream_data.close();

        // Use the new default device if this stream was set to follow the output device.
        if self.core_stream_data.has_output()
            && self
                .core_stream_data
                .output_device
                .flags
                .contains(device_flags::DEV_SELECTED_DEFAULT)
        {
            cubeb_log!("Using new default output device");
            self.core_stream_data.output_device =
                match create_device_info(kAudioObjectUnknown, DeviceType::OUTPUT) {
                    None => {
                        cubeb_log!("Fail to create device info for output");
                        return Err(Error::Error);
                    }
                    Some(d) => d,
                };
        }

        // Likewise, for the input side
        if self.core_stream_data.has_input()
            && self
                .core_stream_data
                .input_device
                .flags
                .contains(device_flags::DEV_SELECTED_DEFAULT)
        {
            cubeb_log!("Using new default input device");
            self.core_stream_data.input_device =
                match create_device_info(kAudioObjectUnknown, DeviceType::INPUT) {
                    None => {
                        cubeb_log!("Fail to create device info for input");
                        return Err(Error::Error);
                    }
                    Some(d) => d,
                }
        }

        cubeb_log!("Reinit: setup");
        self.core_stream_data
            .setup(&mut self.context.shared_voice_processing_unit)
            .inspect_err(|_| {
                cubeb_log!("({:p}) Setup failed.", self.core_stream_data.stm_ptr);
            })?;

        if let Ok(volume) = vol_rv {
            set_volume(self.core_stream_data.output_unit, volume);
        }

        // If the stream was running, start it again.
        if !self.stopped.load(Ordering::SeqCst) {
            self.core_stream_data.start_audiounits().inspect_err(|_| {
                cubeb_log!(
                    "({:p}) Start audiounit failed.",
                    self.core_stream_data.stm_ptr
                );
            })?;
        }

        Ok(())
    }

    fn reinit_async(&mut self) {
        if self.reinit_pending.swap(true, Ordering::SeqCst) {
            // A reinit task is already pending, nothing more to do.
            cubeb_log!(
                "({:p}) re-init stream task already pending, cancelling request",
                self as *const AudioUnitStream
            );
            return;
        }

        let queue = self.queue.clone();
        // Use a new thread, through the queue, to avoid deadlock when calling
        // Get/SetProperties method from inside notify callback
        queue.run_async(move || {
            cubeb_log!("Reinitialization of stream");
            let stm_ptr = self as *const AudioUnitStream;
            if self.destroy_pending.load(Ordering::SeqCst) {
                cubeb_log!(
                    "({:p}) stream pending destroy, cancelling reinit task",
                    stm_ptr
                );
                return;
            }

            if self.reinit().is_err() {
                self.core_stream_data.close();
                self.stopped.store(true, Ordering::SeqCst);
                self.notify_state_changed(State::Error);
                cubeb_log!(
                    "({:p}) Could not reopen the stream after switching.",
                    stm_ptr
                );
            }
            self.switching_device.store(false, Ordering::SeqCst);
            self.reinit_pending.store(false, Ordering::SeqCst);
        });
    }

    fn close_on_error(&mut self) {
        self.queue.debug_assert_is_current();
        let stm_ptr = self as *const AudioUnitStream;

        self.core_stream_data.close();
        self.notify_state_changed(State::Error);
        cubeb_log!("({:p}) Close the stream due to an error.", stm_ptr);

        self.switching_device.store(false, Ordering::SeqCst);
    }

    fn destroy_internal(&mut self) {
        self.queue.debug_assert_is_current();
        self.core_stream_data.close();
        assert!(self.context.active_streams() >= 1);
        self.context.update_latency_by_removing_stream();
    }

    fn destroy(&mut self) {
        self.queue.debug_assert_is_current();
        if self
            .core_stream_data
            .uninstall_system_changed_callback()
            .is_err()
        {
            cubeb_log!(
                "({:p}) Could not uninstall the system changed callback",
                self as *const AudioUnitStream
            );
        }

        if self
            .core_stream_data
            .uninstall_device_changed_callback()
            .is_err()
        {
            cubeb_log!(
                "({:p}) Could not uninstall all device change listeners",
                self as *const AudioUnitStream
            );
        }

        // Execute the stream destroy work.
        self.destroy_pending.store(true, Ordering::SeqCst);

        // Call stop_audiounits to avoid potential data race. If there is a running data callback,
        // which locks a mutex inside CoreAudio framework, then this call will block the current
        // thread until the callback is finished since this call asks to lock a mutex inside
        // CoreAudio framework that is used by the data callback.
        if !self.stopped.swap(true, Ordering::SeqCst) {
            self.core_stream_data.stop_audiounits();
        }

        self.destroy_internal();

        cubeb_log!(
            "Cubeb stream ({:p}) destroyed successful.",
            self as *const AudioUnitStream
        );
    }
}

impl Drop for AudioUnitStream<'_> {
    fn drop(&mut self) {
        // Execute destroy in serial queue to avoid collision with reinit when un/plug devices
        self.queue.clone().run_final(|| {
            self.destroy();
            self.core_stream_data = CoreStreamData::default();
        });
    }
}

impl StreamOps for AudioUnitStream<'_> {
    fn start(&mut self) -> Result<()> {
        let was_stopped = self.stopped.load(Ordering::SeqCst);
        let was_draining = self.draining.load(Ordering::SeqCst);
        self.stopped.store(false, Ordering::SeqCst);
        self.draining.store(false, Ordering::SeqCst);

        self.queue
            .clone()
            .run_sync(|| -> Result<()> {
                // Need reinitialization: device was changed when paused. It will be started after
                // reinit because self.stopped is false.
                if self.delayed_reinit {
                    let rv = self.reinit().inspect_err(|_| {
                        cubeb_log!(
                            "({:p}) delayed reinit during start failed.",
                            self.core_stream_data.stm_ptr
                        );
                    });
                    // In case of failure, restore the state
                    if rv.is_err() {
                        self.stopped.store(was_stopped, Ordering::SeqCst);
                        self.draining.store(was_draining, Ordering::SeqCst);
                        return rv;
                    }
                    self.delayed_reinit = false;
                    Ok(())
                } else {
                    // Execute start in serial queue to avoid racing with destroy or reinit.
                    let rv = self.core_stream_data.start_audiounits();
                    if rv.is_err() {
                        cubeb_log!("({:p}) start failed.", self.core_stream_data.stm_ptr);
                        self.stopped.store(was_stopped, Ordering::SeqCst);
                        self.draining.store(was_draining, Ordering::SeqCst);
                        return rv;
                    }
                    Ok(())
                }
            })
            .unwrap()?;

        self.notify_state_changed(State::Started);

        cubeb_log!(
            "Cubeb stream ({:p}) started successfully.",
            self as *const AudioUnitStream
        );
        Ok(())
    }
    fn stop(&mut self) -> Result<()> {
        if !self.stopped.swap(true, Ordering::SeqCst) {
            // Execute stop in serial queue to avoid racing with destroy or reinit.
            self.queue
                .clone()
                .run_sync(|| self.core_stream_data.stop_audiounits());

            self.notify_state_changed(State::Stopped);

            cubeb_log!(
                "Cubeb stream ({:p}) stopped successfully.",
                self as *const AudioUnitStream
            );
        }
        Ok(())
    }
    fn position(&mut self) -> Result<u64> {
        let OutputCallbackTimingData {
            frames_queued,
            timestamp,
            buffer_size,
        } = self.output_callback_timing_data_read.read().clone();
        let total_output_latency_frames =
            u64::from(self.total_output_latency_frames.load(Ordering::SeqCst));
        // If output latency is available, take it into account. Otherwise, use the number of
        // frames played.
        let position = if total_output_latency_frames != 0 {
            if total_output_latency_frames > frames_queued {
                0
            } else {
                // Interpolate here to match other cubeb backends. Only return an interpolated time
                // if we've played enough frames. If the stream is paused, clamp the interpolated
                // number of frames to the buffer size.
                const NS2S: u64 = 1_000_000_000;
                let now = unsafe { mach_absolute_time() };
                let diff = now - timestamp;
                let interpolated_frames = cmp::min(
                    host_time_to_ns(self.context, diff)
                        * self.core_stream_data.output_stream_params.rate() as u64
                        / NS2S,
                    buffer_size,
                );
                (frames_queued - total_output_latency_frames) + interpolated_frames
            }
        } else {
            frames_queued
        };

        // Ensure mononicity of the clock even when changing output device.
        if position > self.prev_position {
            self.prev_position = position;
        }
        Ok(self.prev_position)
    }
    #[cfg(target_os = "ios")]
    fn latency(&mut self) -> Result<u32> {
        Err(not_supported())
    }
    #[cfg(not(target_os = "ios"))]
    fn latency(&mut self) -> Result<u32> {
        Ok(self.total_output_latency_frames.load(Ordering::SeqCst))
    }
    #[cfg(target_os = "ios")]
    fn input_latency(&mut self) -> Result<u32> {
        Err(not_supported())
    }
    #[cfg(not(target_os = "ios"))]
    fn input_latency(&mut self) -> Result<u32> {
        let user_rate = self.core_stream_data.input_stream_params.rate();
        let hw_rate = self.core_stream_data.input_dev_desc.mSampleRate as u32;
        let frames = self.total_input_latency_frames.load(Ordering::SeqCst);
        if frames != 0 {
            if hw_rate == user_rate {
                Ok(frames)
            } else {
                Ok((frames * user_rate) / hw_rate)
            }
        } else {
            Err(Error::Error)
        }
    }
    fn set_volume(&mut self, volume: f32) -> Result<()> {
        // Execute set_volume in serial queue to avoid racing with destroy or reinit.
        let result = self
            .queue
            .run_sync(|| set_volume(self.core_stream_data.output_unit, volume))
            .unwrap();

        result?;

        cubeb_log!(
            "Cubeb stream ({:p}) set volume to {}.",
            self as *const AudioUnitStream,
            volume
        );
        Ok(())
    }
    fn set_name(&mut self, _: &CStr) -> Result<()> {
        Err(Error::NotSupported)
    }
    fn current_device(&mut self) -> Result<&DeviceRef> {
        Err(Error::NotSupported)
    }
    fn set_input_mute(&mut self, mute: bool) -> Result<()> {
        if self.core_stream_data.input_unit.is_null() {
            return Err(Error::InvalidParameter);
        }

        if !self.core_stream_data.using_voice_processing_unit() {
            return Err(Error::Error);
        }

        // Execute set_input_mute in serial queue to avoid racing with destroy or reinit.
        let mut result = Err(Error::Error);
        let set = &mut result;
        let stream = &self;
        self.queue.run_sync(move || {
            *set = set_input_mute(stream.core_stream_data.input_unit, mute);
        });

        result?;

        cubeb_log!(
            "Cubeb stream ({:p}) set input mute to {}.",
            self as *const AudioUnitStream,
            mute
        );
        self.core_stream_data.input_mute = mute;
        Ok(())
    }
    fn set_input_processing_params(&mut self, params: InputProcessingParams) -> Result<()> {
        // CUBEB_ERROR_INVALID_PARAMETER if a given param is not supported by
        // this backend, or if this stream does not have an input device
        if self.core_stream_data.input_unit.is_null() {
            return Err(Error::InvalidParameter);
        }

        if self
            .context
            .supported_input_processing_params()
            .unwrap()
            .intersection(params)
            != params
        {
            return Err(Error::InvalidParameter);
        }

        // AEC and NS are active as soon as VPIO is not bypassed, therefore the only combinations
        // of those we can explicitly support are {} and {aec, ns}.
        let aec = params.contains(InputProcessingParams::ECHO_CANCELLATION);
        let ns = params.contains(InputProcessingParams::NOISE_SUPPRESSION);
        if aec != ns {
            // No control to turn on AEC without NS or vice versa.
            cubeb_log!(
                "Cubeb stream ({:p}) couldn't set input processing params {:?}. AEC != NS.",
                self as *const AudioUnitStream,
                params
            );
            return Err(Error::Error);
        }

        // CUBEB_ERROR if params could not be applied
        //   note: only works with VoiceProcessingIO
        if !self.core_stream_data.using_voice_processing_unit() {
            return Err(Error::Error);
        }

        // Execute set_input_processing_params in serial queue to avoid racing with destroy or reinit.
        let mut result = Err(Error::Error);
        let result_ = &mut result;
        let mut deferred = false;
        let deferred_ = &mut deferred;
        let stream = &self;
        self.queue.run_sync(move || {
            if stream.core_stream_data.units_running {
                *deferred_ = true;
                *result_ = Ok(());
            } else {
                *deferred_ = false;
                *result_ = set_input_processing_params(stream.core_stream_data.input_unit, params);
            }
        });

        result?;

        cubeb_log!(
            "Cubeb stream ({:p}) {} input processing params {:?}.",
            self as *const AudioUnitStream,
            if deferred { "deferred" } else { "set" },
            params
        );
        self.core_stream_data.input_processing_params = params;
        Ok(())
    }
    #[cfg(target_os = "ios")]
    fn device_destroy(&mut self, device: &DeviceRef) -> Result<()> {
        Err(not_supported())
    }
    #[cfg(not(target_os = "ios"))]
    fn device_destroy(&mut self, device: &DeviceRef) -> Result<()> {
        if device.as_ptr().is_null() {
            Err(Error::Error)
        } else {
            unsafe {
                let mut dev: Box<ffi::cubeb_device> = Box::from_raw(device.as_ptr() as *mut _);
                if !dev.output_name.is_null() {
                    let _ = CString::from_raw(dev.output_name as *mut _);
                    dev.output_name = ptr::null_mut();
                }
                if !dev.input_name.is_null() {
                    let _ = CString::from_raw(dev.input_name as *mut _);
                    dev.input_name = ptr::null_mut();
                }
                drop(dev);
            }
            Ok(())
        }
    }
    fn register_device_changed_callback(
        &mut self,
        device_changed_callback: ffi::cubeb_device_changed_callback,
    ) -> Result<()> {
        let mut callback = self.device_changed_callback.lock().unwrap();
        // Note: second register without unregister first causes 'nope' error.
        // Current implementation requires unregister before register a new cb.
        if device_changed_callback.is_some() && callback.is_some() {
            Err(Error::InvalidParameter)
        } else {
            *callback = device_changed_callback;
            Ok(())
        }
    }
}

#[allow(clippy::non_send_fields_in_send_ty)]
unsafe impl Send for AudioUnitStream<'_> {}
unsafe impl Sync for AudioUnitStream<'_> {}

#[cfg(test)]
mod tests;
