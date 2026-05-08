// Copyright © 2017 Mozilla Foundation
//
// This program is made available under an ISC-style license.  See the
// accompanying file LICENSE for details

use crate::PlatformHandle;
use crate::PlatformHandleType;
use crate::INVALID_HANDLE_VALUE;
#[cfg(target_os = "linux")]
use audio_thread_priority::RtPriorityThreadInfo;
use cubeb::ffi;
use serde_derive::Deserialize;
use serde_derive::Serialize;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_uint};
use std::ptr;

#[derive(Debug, Serialize, Deserialize)]
pub struct Device {
    #[serde(with = "serde_bytes")]
    pub output_name: Option<Vec<u8>>,
    #[serde(with = "serde_bytes")]
    pub input_name: Option<Vec<u8>>,
}

impl<'a> From<&'a cubeb::DeviceRef> for Device {
    fn from(info: &'a cubeb::DeviceRef) -> Self {
        Self {
            output_name: info.output_name_bytes().map(|s| s.to_vec()),
            input_name: info.input_name_bytes().map(|s| s.to_vec()),
        }
    }
}

impl From<ffi::cubeb_device> for Device {
    fn from(info: ffi::cubeb_device) -> Self {
        Self {
            output_name: dup_str(info.output_name),
            input_name: dup_str(info.input_name),
        }
    }
}

impl From<Device> for ffi::cubeb_device {
    fn from(info: Device) -> Self {
        Self {
            output_name: opt_str(info.output_name),
            input_name: opt_str(info.input_name),
        }
    }
}

#[derive(Debug, Serialize, Deserialize)]
pub struct DeviceInfo {
    pub devid: usize,
    #[serde(with = "serde_bytes")]
    pub device_id: Option<Vec<u8>>,
    #[serde(with = "serde_bytes")]
    pub friendly_name: Option<Vec<u8>>,
    #[serde(with = "serde_bytes")]
    pub group_id: Option<Vec<u8>>,
    #[serde(with = "serde_bytes")]
    pub vendor_name: Option<Vec<u8>>,

    pub device_type: ffi::cubeb_device_type,
    pub state: ffi::cubeb_device_state,
    pub preferred: ffi::cubeb_device_pref,

    pub format: ffi::cubeb_device_fmt,
    pub default_format: ffi::cubeb_device_fmt,
    pub max_channels: u32,
    pub default_rate: u32,
    pub max_rate: u32,
    pub min_rate: u32,

    pub latency_lo: u32,
    pub latency_hi: u32,
}

impl<'a> From<&'a cubeb::DeviceInfoRef> for DeviceInfo {
    fn from(info: &'a cubeb::DeviceInfoRef) -> Self {
        let info = unsafe { &*info.as_ptr() };
        DeviceInfo {
            devid: info.devid as _,
            device_id: dup_str(info.device_id),
            friendly_name: dup_str(info.friendly_name),
            group_id: dup_str(info.group_id),
            vendor_name: dup_str(info.vendor_name),

            device_type: info.device_type,
            state: info.state,
            preferred: info.preferred,

            format: info.format,
            default_format: info.default_format,
            max_channels: info.max_channels,
            default_rate: info.default_rate,
            max_rate: info.max_rate,
            min_rate: info.min_rate,

            latency_lo: info.latency_lo,
            latency_hi: info.latency_hi,
        }
    }
}

impl From<DeviceInfo> for ffi::cubeb_device_info {
    fn from(info: DeviceInfo) -> Self {
        ffi::cubeb_device_info {
            devid: info.devid as _,
            device_id: opt_str(info.device_id),
            friendly_name: opt_str(info.friendly_name),
            group_id: opt_str(info.group_id),
            vendor_name: opt_str(info.vendor_name),

            device_type: info.device_type,
            state: info.state,
            preferred: info.preferred,

            format: info.format,
            default_format: info.default_format,
            max_channels: info.max_channels,
            default_rate: info.default_rate,
            max_rate: info.max_rate,
            min_rate: info.min_rate,

            latency_lo: info.latency_lo,
            latency_hi: info.latency_hi,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Serialize)]
pub struct StreamParams {
    pub format: ffi::cubeb_sample_format,
    pub rate: c_uint,
    pub channels: c_uint,
    pub layout: ffi::cubeb_channel_layout,
    pub prefs: ffi::cubeb_stream_prefs,
    pub input_params: ffi::cubeb_input_processing_params,
}

impl From<&cubeb::StreamParamsRef> for StreamParams {
    fn from(x: &cubeb::StreamParamsRef) -> StreamParams {
        unsafe { *(x.as_ptr() as *mut StreamParams) }
    }
}

impl StreamParams {
    pub fn frame_size_in_bytes(&self) -> usize {
        let format = self.format.into();
        let sample_size = match format {
            cubeb::SampleFormat::S16LE
            | cubeb::SampleFormat::S16BE
            | cubeb::SampleFormat::S16NE => 2usize,
            cubeb::SampleFormat::Float32LE
            | cubeb::SampleFormat::Float32BE
            | cubeb::SampleFormat::Float32NE => 4usize,
        };
        let channel_count = self.channels as usize;
        sample_size * channel_count
    }
}

#[derive(Debug, Serialize, Deserialize)]
pub struct StreamCreateParams {
    pub input_stream_params: Option<StreamParams>,
    pub output_stream_params: Option<StreamParams>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct StreamInitParams {
    #[serde(with = "serde_bytes")]
    pub stream_name: Option<Vec<u8>>,
    pub input_device: usize,
    pub input_stream_params: Option<StreamParams>,
    pub output_device: usize,
    pub output_stream_params: Option<StreamParams>,
    pub latency_frames: u32,
}

fn dup_str(s: *const c_char) -> Option<Vec<u8>> {
    if s.is_null() {
        None
    } else {
        let vec: Vec<u8> = unsafe { CStr::from_ptr(s) }.to_bytes().to_vec();
        Some(vec)
    }
}

fn opt_str(v: Option<Vec<u8>>) -> *mut c_char {
    match v {
        Some(v) => match CString::new(v) {
            Ok(s) => s.into_raw(),
            Err(_) => {
                debug!("Failed to convert bytes to CString");
                ptr::null_mut()
            }
        },
        None => ptr::null_mut(),
    }
}

#[derive(Debug, Serialize, Deserialize)]
pub struct StreamCreate {
    pub token: usize,
    pub shm_handle: SerializableHandle,
    pub shm_area_size: usize,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct RegisterDeviceCollectionChanged {
    pub platform_handle: SerializableHandle,
}

// Client -> Server messages.
// TODO: Callbacks should be different messages types so
// ServerConn::process_msg doesn't have a catch-all case.
#[derive(Debug, Serialize, Deserialize)]
pub enum ServerMessage {
    ClientConnect,
    ClientDisconnect,

    ContextGetBackendId,
    ContextGetMaxChannelCount,
    ContextGetMinLatency(StreamParams),
    ContextGetPreferredSampleRate,
    ContextGetSupportedInputProcessingParams,
    ContextGetDeviceEnumeration(ffi::cubeb_device_type),
    ContextSetupDeviceCollectionCallback,
    ContextRegisterDeviceCollectionChanged(ffi::cubeb_device_type, bool),

    StreamCreate(StreamCreateParams),
    StreamInit(usize, StreamInitParams),
    StreamDestroy(usize),

    StreamStart(usize),
    StreamStop(usize),
    StreamGetPosition(usize),
    StreamGetLatency(usize),
    StreamGetInputLatency(usize),
    StreamSetVolume(usize, f32),
    StreamSetName(usize, CString),
    StreamGetCurrentDevice(usize),
    StreamSetInputMute(usize, bool),
    StreamSetInputProcessingParams(usize, ffi::cubeb_input_processing_params),
    StreamRegisterDeviceChangeCallback(usize, bool),

    #[cfg(target_os = "linux")]
    PromoteThreadToRealTime([u8; std::mem::size_of::<RtPriorityThreadInfo>()]),
}

// Server -> Client messages.
// TODO: Streams need id.
#[derive(Debug, Serialize, Deserialize)]
pub enum ClientMessage {
    ClientConnected,
    ClientDisconnected,

    ContextBackendId(String),
    ContextMaxChannelCount(u32),
    ContextMinLatency(u32),
    ContextPreferredSampleRate(u32),
    ContextSupportedInputProcessingParams(ffi::cubeb_input_processing_params),
    ContextEnumeratedDevices(Vec<DeviceInfo>),
    ContextSetupDeviceCollectionCallback(RegisterDeviceCollectionChanged),
    ContextRegisteredDeviceCollectionChanged,

    StreamCreated(StreamCreate),
    StreamInitialized(SerializableHandle),
    StreamDestroyed,

    StreamStarted,
    StreamStopped,
    StreamPosition(u64),
    StreamLatency(u32),
    StreamInputLatency(u32),
    StreamVolumeSet,
    StreamNameSet,
    StreamCurrentDevice(Device),
    StreamInputMuteSet,
    StreamInputProcessingParamsSet,
    StreamRegisterDeviceChangeCallback,

    #[cfg(target_os = "linux")]
    ThreadPromoted,

    Error(c_int),
}

#[derive(Debug, Deserialize, Serialize)]
pub enum CallbackReq {
    Data { nframes: isize },
    State(ffi::cubeb_state),
    DeviceChange,
}

#[derive(Debug, Deserialize, Serialize)]
pub enum CallbackResp {
    Data(isize),
    State,
    DeviceChange,
    Error(c_int),
}

#[derive(Debug, Deserialize, Serialize)]
pub enum DeviceCollectionReq {
    DeviceChange(ffi::cubeb_device_type),
}

#[derive(Debug, Deserialize, Serialize)]
pub enum DeviceCollectionResp {
    DeviceChange,
}

// Represents a platform handle in various transitional states during serialization and remoting.
// The process of serializing and remoting handles and the ownership during various states differs
// between Windows and Unix.  SerializableHandle changes during IPC as follows:
//
// 1. Created in the initial state `Owned`, with a valid `target_pid`.
// 2. Ownership is transferred out for processing during IPC send, becoming `Empty` temporarily.
//    See `AssociateHandleForMessage::take_handle`.
//    - Windows: DuplicateHandle transfers the handle to the remote process.
//               This produces a new handle value in the local process representing the remote handle.
//               This value must be sent to the remote, so `AssociateHandleForMessage::set_remote_handle`
//               is used to transform the handle into a `SerializableValue`.
//    - Unix: sendmsg transfers the handle to the remote process.  The handle is left `Empty`.
//            (Note: this occurs later, when the serialized message buffer is sent)
// 3. Message containing `SerializableValue` or `Empty` (depending on handle processing in step 2)
//    is serialized and sent via IPC.
// 4. Message received and deserialized in target process.
//    - Windows: `AssociateHandleForMessage::set_local_handle converts the received `SerializableValue` into `Owned`, ready for use.
//    - Unix: Handle (with a new value in the target process) is received out-of-band via `recvmsg`
//            and converted to `Owned` via `AssociateHandleForMessage::set_local_handle`.
#[derive(Debug)]
pub enum SerializableHandle {
    // Owned handle, with optional target_pid on sending side.
    Owned(PlatformHandle, Option<u32>),
    // Transitional IPC states:
    SerializableValue(PlatformHandleType), // Windows
    Empty,                                 // Unix
}

// PlatformHandle is non-Send and contains a pointer (HANDLE) on Windows.
#[allow(clippy::non_send_fields_in_send_ty)]
unsafe impl Send for SerializableHandle {}

impl SerializableHandle {
    pub fn new(handle: PlatformHandle, target_pid: u32) -> SerializableHandle {
        SerializableHandle::Owned(handle, Some(target_pid))
    }

    // Called on the receiving side to take ownership of the handle.
    pub fn take_handle(&mut self) -> PlatformHandle {
        match std::mem::replace(self, SerializableHandle::Empty) {
            SerializableHandle::Owned(handle, target_pid) => {
                assert!(target_pid.is_none());
                handle
            }
            _ => panic!("take_handle called in invalid state"),
        }
    }

    // Called on the sending side to take ownership of the handle for
    // handling platform-specific remoting.
    fn take_handle_for_send(&mut self) -> RemoteHandle {
        match std::mem::replace(self, SerializableHandle::Empty) {
            SerializableHandle::Owned(handle, target_pid) => unsafe {
                RemoteHandle::new(
                    handle.into_raw(),
                    target_pid.expect("target process required"),
                )
            },
            _ => panic!("take_handle_for_send called in invalid state"),
        }
    }

    fn new_owned(handle: PlatformHandleType) -> SerializableHandle {
        SerializableHandle::Owned(PlatformHandle::new(handle), None)
    }

    #[cfg(windows)]
    fn make_owned(&mut self) {
        if let SerializableHandle::SerializableValue(handle) = self {
            *self = SerializableHandle::new_owned(*handle);
        } else {
            panic!("make_owned called in invalid state")
        }
    }

    fn new_serializable_value(handle: PlatformHandleType) -> SerializableHandle {
        SerializableHandle::SerializableValue(handle)
    }

    fn get_serializable_value(&self) -> PlatformHandleType {
        match *self {
            SerializableHandle::SerializableValue(handle) => handle,
            SerializableHandle::Empty => INVALID_HANDLE_VALUE,
            _ => panic!("get_remote_handle called in invalid state"),
        }
    }
}

// Raw handle values are serialized as i64.  Additional handling external to (de)serialization is required during IPC
// send/receive to convert these raw values into valid handles.
impl serde::Serialize for SerializableHandle {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let handle = self.get_serializable_value();
        serializer.serialize_i64(handle as i64)
    }
}

impl<'de> serde::Deserialize<'de> for SerializableHandle {
    fn deserialize<D>(deserializer: D) -> Result<SerializableHandle, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        deserializer.deserialize_i64(SerializableHandleVisitor)
    }
}

struct SerializableHandleVisitor;
impl serde::de::Visitor<'_> for SerializableHandleVisitor {
    type Value = SerializableHandle;

    fn expecting(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("an integer between -2^63 and 2^63")
    }

    fn visit_i64<E>(self, value: i64) -> Result<Self::Value, E>
    where
        E: serde::de::Error,
    {
        Ok(SerializableHandle::new_serializable_value(
            value as PlatformHandleType,
        ))
    }
}

// Represents a PlatformHandle in-flight between processes.
// On Unix platforms, this is just a plain owned Handle, closed on drop.
// On Windows, `RemoteHandle` also retains ownership of the `target_handle`
// in the `target` process.  Once the handle has been successfully sent
// to the remote, the sender should call `mark_sent()` to relinquish
// ownership of `target_handle` in the remote.
#[derive(Debug)]
pub struct RemoteHandle {
    pub(crate) handle: PlatformHandleType,
    #[cfg(windows)]
    pub(crate) target: u32,
    #[cfg(windows)]
    pub(crate) target_handle: Option<PlatformHandleType>,
}

impl RemoteHandle {
    #[allow(clippy::missing_safety_doc)]
    pub unsafe fn new(handle: PlatformHandleType, _target: u32) -> Self {
        RemoteHandle {
            handle,
            #[cfg(windows)]
            target: _target,
            #[cfg(windows)]
            target_handle: None,
        }
    }

    #[cfg(windows)]
    pub fn mark_sent(&mut self) {
        self.target_handle.take();
    }

    #[cfg(windows)]
    #[allow(clippy::missing_safety_doc)]
    pub unsafe fn send_to_target(&mut self) -> std::io::Result<PlatformHandleType> {
        let target_handle = crate::duplicate_platform_handle(self.handle, Some(self.target))?;
        self.target_handle = Some(target_handle);
        Ok(target_handle)
    }

    #[cfg(unix)]
    #[allow(clippy::missing_safety_doc)]
    pub unsafe fn take(self) -> PlatformHandleType {
        let h = self.handle;
        std::mem::forget(self);
        h
    }
}

impl Drop for RemoteHandle {
    fn drop(&mut self) {
        unsafe {
            crate::close_platform_handle(self.handle);
        }
        #[cfg(windows)]
        unsafe {
            if let Some(target_handle) = self.target_handle {
                if let Err(e) = crate::close_target_handle(target_handle, self.target) {
                    trace!("RemoteHandle failed to close target handle: {e:?}");
                }
            }
        }
    }
}

unsafe impl Send for RemoteHandle {}

pub trait AssociateHandleForMessage {
    // True if this item has an associated handle attached for remoting.
    fn has_associated_handle(&self) -> bool {
        false
    }

    // Take ownership of the associated handle, leaving the item's
    // associated handle empty.
    fn take_handle(&mut self) -> RemoteHandle {
        panic!("take_handle called on item without associated handle");
    }

    #[allow(clippy::missing_safety_doc)]
    // Replace an empty associated handle with a non-owning serializable value
    // indicating the value of the handle in the remote process.
    #[cfg(windows)]
    unsafe fn set_remote_handle(&mut self, _: PlatformHandleType) {
        panic!("set_remote_handle called on item without associated handle");
    }

    #[allow(clippy::missing_safety_doc)]
    // Replace a serialized associated handle value with an owned local handle.
    #[cfg(windows)]
    unsafe fn set_local_handle(&mut self) {
        panic!("set_local_handle called on item without associated handle");
    }

    #[allow(clippy::missing_safety_doc)]
    // Replace an empty associated handle with an owned local handle.
    #[cfg(unix)]
    unsafe fn set_local_handle(&mut self, _: PlatformHandleType) {
        panic!("set_local_handle called on item without associated handle");
    }
}

impl AssociateHandleForMessage for ClientMessage {
    fn has_associated_handle(&self) -> bool {
        matches!(
            *self,
            ClientMessage::StreamCreated(_)
                | ClientMessage::StreamInitialized(_)
                | ClientMessage::ContextSetupDeviceCollectionCallback(_)
        )
    }

    fn take_handle(&mut self) -> RemoteHandle {
        match *self {
            ClientMessage::StreamCreated(ref mut data) => data.shm_handle.take_handle_for_send(),
            ClientMessage::StreamInitialized(ref mut data) => data.take_handle_for_send(),
            ClientMessage::ContextSetupDeviceCollectionCallback(ref mut data) => {
                data.platform_handle.take_handle_for_send()
            }
            _ => panic!("take_handle called on item without associated handle"),
        }
    }

    #[cfg(windows)]
    unsafe fn set_remote_handle(&mut self, handle: PlatformHandleType) {
        match *self {
            ClientMessage::StreamCreated(ref mut data) => {
                data.shm_handle = SerializableHandle::new_serializable_value(handle);
            }
            ClientMessage::StreamInitialized(ref mut data) => {
                *data = SerializableHandle::new_serializable_value(handle);
            }
            ClientMessage::ContextSetupDeviceCollectionCallback(ref mut data) => {
                data.platform_handle = SerializableHandle::new_serializable_value(handle);
            }
            _ => panic!("set_remote_handle called on item without associated handle"),
        }
    }

    #[cfg(windows)]
    unsafe fn set_local_handle(&mut self) {
        match *self {
            ClientMessage::StreamCreated(ref mut data) => data.shm_handle.make_owned(),
            ClientMessage::StreamInitialized(ref mut data) => data.make_owned(),
            ClientMessage::ContextSetupDeviceCollectionCallback(ref mut data) => {
                data.platform_handle.make_owned()
            }
            _ => panic!("set_local_handle called on item without associated handle"),
        }
    }

    #[cfg(unix)]
    unsafe fn set_local_handle(&mut self, handle: PlatformHandleType) {
        match *self {
            ClientMessage::StreamCreated(ref mut data) => {
                data.shm_handle = SerializableHandle::new_owned(handle);
            }
            ClientMessage::StreamInitialized(ref mut data) => {
                *data = SerializableHandle::new_owned(handle);
            }
            ClientMessage::ContextSetupDeviceCollectionCallback(ref mut data) => {
                data.platform_handle = SerializableHandle::new_owned(handle);
            }
            _ => panic!("set_local_handle called on item without associated handle"),
        }
    }
}

impl AssociateHandleForMessage for ServerMessage {}

impl AssociateHandleForMessage for DeviceCollectionReq {}
impl AssociateHandleForMessage for DeviceCollectionResp {}

impl AssociateHandleForMessage for CallbackReq {}
impl AssociateHandleForMessage for CallbackResp {}

#[cfg(test)]
mod test {
    use super::StreamParams;
    use cubeb::ffi;
    use std::mem;

    #[test]
    fn stream_params_size_check() {
        assert_eq!(
            mem::size_of::<StreamParams>(),
            mem::size_of::<ffi::cubeb_stream_params>()
        )
    }

    #[test]
    fn stream_params_from() {
        let raw = ffi::cubeb_stream_params {
            format: ffi::CUBEB_SAMPLE_FLOAT32BE,
            rate: 96_000,
            channels: 32,
            layout: ffi::CUBEB_LAYOUT_3F1_LFE,
            prefs: ffi::CUBEB_STREAM_PREF_LOOPBACK,
            input_params: ffi::CUBEB_INPUT_PROCESSING_PARAM_NONE,
        };
        let wrapped = ::cubeb::StreamParams::from(raw);
        let params = StreamParams::from(wrapped.as_ref());
        assert_eq!(params.format, raw.format);
        assert_eq!(params.rate, raw.rate);
        assert_eq!(params.channels, raw.channels);
        assert_eq!(params.layout, raw.layout);
        assert_eq!(params.prefs, raw.prefs);
    }
}
