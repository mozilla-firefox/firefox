// Copyright © 2017 Mozilla Foundation
//
// This program is made available under an ISC-style license.  See the
// accompanying file LICENSE for details

#[cfg(target_os = "linux")]
use audio_thread_priority::{promote_thread_to_real_time, RtPriorityThreadInfo};
use audioipc::messages::SerializableHandle;
use audioipc::messages::{
    CallbackReq, CallbackResp, ClientMessage, Device, DeviceCollectionReq, DeviceCollectionResp,
    DeviceInfo, RegisterDeviceCollectionChanged, ServerMessage, StreamCreate, StreamCreateParams,
    StreamInitParams, StreamParams,
};
use audioipc::shm::SharedMem;
use audioipc::{ipccore, rpccore, sys, PlatformHandle};
use cubeb::InputProcessingParams;
use cubeb_core as cubeb;
use cubeb_core::ffi;
use std::convert::{TryFrom, TryInto};
use std::ffi::CStr;
use std::mem::size_of;
use std::os::raw::{c_int, c_long, c_void};
use std::rc::Rc;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::{cell::RefCell, sync::Mutex};
use std::{panic, slice};

use audioipc::errors::Result;

fn error(error: cubeb::Error) -> ClientMessage {
    ClientMessage::Error(error as c_int)
}

struct CubebDeviceCollectionManager {
    servers: Mutex<Vec<(Rc<DeviceCollectionChangeCallback>, cubeb::DeviceType)>>,
}

impl CubebDeviceCollectionManager {
    fn new() -> CubebDeviceCollectionManager {
        CubebDeviceCollectionManager {
            servers: Mutex::new(Vec::new()),
        }
    }

    fn register(
        &self,
        context: &cubeb::Context,
        server: &Rc<DeviceCollectionChangeCallback>,
        devtype: cubeb::DeviceType,
    ) -> cubeb::Result<()> {
        let mut servers = self.servers.lock().unwrap();
        if servers.is_empty() {
            self.internal_register(context, true)?;
        }
        servers.push((server.clone(), devtype));
        Ok(())
    }

    fn unregister(
        &self,
        context: &cubeb::Context,
        server: &Rc<DeviceCollectionChangeCallback>,
        devtype: cubeb::DeviceType,
    ) -> cubeb::Result<()> {
        let mut servers = self.servers.lock().unwrap();
        servers.retain(|(s, d)| !Rc::ptr_eq(s, server) || d != &devtype);
        if servers.is_empty() {
            self.internal_register(context, false)?;
        }
        Ok(())
    }

    fn internal_register(&self, context: &cubeb::Context, enable: bool) -> cubeb::Result<()> {
        for &(dir, cb) in &[
            (
                cubeb::DeviceType::INPUT,
                device_collection_changed_input_cb_c as _,
            ),
            (
                cubeb::DeviceType::OUTPUT,
                device_collection_changed_output_cb_c as _,
            ),
        ] {
            unsafe {
                context.register_device_collection_changed(
                    dir,
                    if enable { Some(cb) } else { None },
                    if enable {
                        self as *const CubebDeviceCollectionManager as *mut c_void
                    } else {
                        std::ptr::null_mut()
                    },
                )?;
            }
        }
        Ok(())
    }

    unsafe fn device_collection_changed_callback(&self, device_type: ffi::cubeb_device_type) {
        let servers = self.servers.lock().unwrap();
        servers.iter().for_each(|(s, d)| {
            if d.contains(cubeb::DeviceType::from_bits_truncate(device_type)) {
                s.device_collection_changed_callback(device_type)
            }
        });
    }
}

impl Drop for CubebDeviceCollectionManager {
    fn drop(&mut self) {
        assert!(self.servers.lock().unwrap().is_empty());
    }
}

struct DevIdMap {
    devices: Vec<usize>,
}

// A cubeb_devid is an opaque type which may be implemented with a stable
// pointer in a cubeb backend.  cubeb_devids received remotely must be
// validated before use, so DevIdMap provides a simple 1:1 mapping between a
// cubeb_devid and an IPC-transportable value suitable for use as a unique
// handle.
impl DevIdMap {
    fn new() -> DevIdMap {
        let mut d = DevIdMap {
            devices: Vec::with_capacity(32),
        };
        // A null cubeb_devid is used for selecting the default device.
        // Pre-populate the mapping with 0 -> 0 to handle nulls.
        d.devices.push(0);
        d
    }

    // Given a cubeb_devid, return a unique stable value suitable for use
    // over IPC.
    fn make_handle(&mut self, devid: usize) -> usize {
        if let Some(i) = self.devices.iter().position(|&d| d == devid) {
            return i;
        }
        self.devices.push(devid);
        self.devices.len() - 1
    }

    // Given a handle produced by `make_handle`, return the associated
    // cubeb_devid.  Invalid handles result in a panic.
    fn handle_to_id(&self, handle: usize) -> usize {
        self.devices[handle]
    }
}

struct CubebContextState {
    // `manager` must be dropped before the `context` is destroyed.
    manager: CubebDeviceCollectionManager,
    context: cubeb::Result<cubeb::Context>,
}

thread_local!(static CONTEXT_KEY: RefCell<Option<CubebContextState>> = const { RefCell::new(None) });

fn cubeb_init_from_context_params() -> cubeb::Result<cubeb::Context> {
    let params = super::G_CUBEB_CONTEXT_PARAMS.lock().unwrap();
    let context_name = Some(params.context_name.as_c_str());
    let backend_name = params.backend_name.as_deref();
    let r = cubeb::Context::init(context_name, backend_name);
    r.inspect_err(|e| {
        info!("cubeb::Context::init failed r={e:?}");
    })
}

fn with_local_context<T, F>(f: F) -> T
where
    F: FnOnce(&cubeb::Result<cubeb::Context>, &mut CubebDeviceCollectionManager) -> T,
{
    CONTEXT_KEY.with(|k| {
        let mut state = k.borrow_mut();
        if state.is_none() {
            *state = Some(CubebContextState {
                manager: CubebDeviceCollectionManager::new(),
                context: cubeb_init_from_context_params(),
            });
        }
        let CubebContextState { manager, context } = state.as_mut().unwrap();
        // Always reattempt to initialize cubeb, OS config may have changed.
        if context.is_err() {
            *context = cubeb_init_from_context_params();
        }
        f(context, manager)
    })
}

struct DeviceCollectionClient;

impl rpccore::Client for DeviceCollectionClient {
    type ServerMessage = DeviceCollectionReq;
    type ClientMessage = DeviceCollectionResp;
}

struct CallbackClient;

impl rpccore::Client for CallbackClient {
    type ServerMessage = CallbackReq;
    type ClientMessage = CallbackResp;
}

struct ServerStreamCallbacks {
    /// Size of input frame in bytes
    input_frame_size: Option<usize>,
    /// Size of output frame in bytes
    output_frame_size: Option<usize>,
    /// Shared memory buffer for transporting audio data to/from client
    shm: SharedMem,
    /// RPC interface for data_callback (on OS audio thread) to server callback thread
    data_callback_rpc: rpccore::Proxy<CallbackReq, CallbackResp>,
    /// RPC interface for state_callback (on any thread) to server callback thread
    state_callback_rpc: rpccore::Proxy<CallbackReq, CallbackResp>,
    /// RPC interface for device_change_callback (on any thread) to server callback thread
    device_change_callback_rpc: rpccore::Proxy<CallbackReq, CallbackResp>,
    /// Indicates stream is connected to client side.  Callbacks received before
    /// the stream is in the connected state cannot be sent to the client side, so
    /// are logged and otherwise ignored.
    connected: AtomicBool,
}

impl ServerStreamCallbacks {
    fn data_callback(&mut self, input: &[u8], output: &mut [u8], nframes: isize) -> isize {
        trace!(
            "Stream data callback: {} {} {}",
            nframes,
            input.len(),
            output.len()
        );
        if !self.connected.load(Ordering::Acquire) {
            warn!("Stream data callback triggered before stream connected");
            return cubeb::ffi::CUBEB_ERROR.try_into().unwrap();
        }

        if self.input_frame_size.is_some() {
            if input.len() > self.shm.get_size() {
                debug!(
                    "bad input size: input={} shm={}",
                    input.len(),
                    self.shm.get_size()
                );
                return cubeb::ffi::CUBEB_ERROR.try_into().unwrap();
            }
            unsafe {
                self.shm
                    .get_mut_slice(input.len())
                    .unwrap()
                    .copy_from_slice(input);
            }
        }

        if self.output_frame_size.is_some()
            && (output.is_empty() || output.len() > self.shm.get_size())
        {
            debug!(
                "bad output size: output={} shm={}",
                output.len(),
                self.shm.get_size()
            );
            return cubeb::ffi::CUBEB_ERROR.try_into().unwrap();
        }

        if nframes == 0 {
            // Optimization: skip the RPC call when there are no frames.
            return 0;
        }

        let r = self.data_callback_rpc.call(CallbackReq::Data { nframes });

        match r {
            Ok(CallbackResp::Data(frames)) => {
                if frames < 0 || frames > nframes {
                    debug!(
                        "bad callback response: frames={} nframes={}",
                        frames, nframes
                    );
                    return cubeb::ffi::CUBEB_ERROR.try_into().unwrap();
                }
                if let (Ok(frames), Some(output_frame_size)) =
                    (usize::try_from(frames), self.output_frame_size)
                {
                    let Some(nbytes) = frames.checked_mul(output_frame_size) else {
                        return cubeb::ffi::CUBEB_ERROR.try_into().unwrap();
                    };
                    if nbytes > output.len() || nbytes > self.shm.get_size() {
                        debug!(
                            "bad callback response: nbytes={} output={} shm={}",
                            nbytes,
                            output.len(),
                            self.shm.get_size()
                        );
                        return cubeb::ffi::CUBEB_ERROR.try_into().unwrap();
                    }
                    unsafe {
                        output[..nbytes].copy_from_slice(self.shm.get_slice(nbytes).unwrap());
                    }
                }
                frames
            }
            _ => {
                debug!("Unexpected message {r:?} during data_callback");
                cubeb::ffi::CUBEB_ERROR.try_into().unwrap()
            }
        }
    }

    fn state_callback(&self, state: cubeb::State) {
        trace!("Stream state callback: {state:?}");
        if !self.connected.load(Ordering::Acquire) {
            warn!("Stream state callback triggered before stream connected");
            return;
        }

        let r = self
            .state_callback_rpc
            .call(CallbackReq::State(state.into()));
        match r {
            Ok(CallbackResp::State) => {}
            _ => {
                debug!("Unexpected message {r:?} during state callback");
            }
        }
    }

    fn device_change_callback(&self) {
        trace!("Stream device change callback");
        if !self.connected.load(Ordering::Acquire) {
            warn!("Stream device_change callback triggered before stream connected");
            return;
        }
        let r = self
            .device_change_callback_rpc
            .call(CallbackReq::DeviceChange);
        match r {
            Ok(CallbackResp::DeviceChange) => {}
            _ => {
                debug!("Unexpected message {r:?} during device change callback");
            }
        }
    }
}

static SHM_ID: AtomicUsize = AtomicUsize::new(0);

// Generate a temporary shm_id fragment that is unique to the process.  This
// path is used temporarily to create a shm segment, which is then
// immediately deleted from the filesystem while retaining handles to the
// shm to be shared between the server and client.
fn get_shm_id() -> String {
    format!(
        "cubeb-shm-{}-{}",
        std::process::id(),
        SHM_ID.fetch_add(1, Ordering::SeqCst)
    )
}

struct ServerStream {
    stream: Option<cubeb::Stream>,
    cbs: Box<ServerStreamCallbacks>,
    client_pipe: Option<PlatformHandle>,
}

impl Drop for ServerStream {
    fn drop(&mut self) {
        // `stream` *must* be dropped before `cbs`.
        drop(self.stream.take());
    }
}

struct DeviceCollectionChangeCallback {
    rpc: rpccore::Proxy<DeviceCollectionReq, DeviceCollectionResp>,
}

impl DeviceCollectionChangeCallback {
    fn device_collection_changed_callback(&self, device_type: ffi::cubeb_device_type) {
        // TODO: Assert device_type is in devtype.
        debug!("Sending device collection ({device_type:?}) changed event");
        let _ = self
            .rpc
            .call(DeviceCollectionReq::DeviceChange(device_type));
    }
}

pub struct CubebServer {
    callback_thread: ipccore::EventLoopHandle,
    device_collection_thread: ipccore::EventLoopHandle,
    streams: slab::Slab<ServerStream>,
    remote_pid: Option<u32>,
    device_collection_change_callbacks: Option<Rc<DeviceCollectionChangeCallback>>,
    devidmap: DevIdMap,
    shm_area_size: usize,
}

impl Drop for CubebServer {
    fn drop(&mut self) {
        if let Some(device_collection_change_callbacks) = &self.device_collection_change_callbacks {
            debug!("CubebServer: dropped with device_collection_change_callbacks registered");
            CONTEXT_KEY.with(|k| {
                let mut state = k.borrow_mut();
                if let Some(CubebContextState {
                    manager,
                    context: Ok(context),
                }) = state.as_mut()
                {
                    for devtype in [cubeb::DeviceType::INPUT, cubeb::DeviceType::OUTPUT] {
                        let r = manager.unregister(
                            context,
                            device_collection_change_callbacks,
                            devtype,
                        );
                        if r.is_err() {
                            debug!("CubebServer: unregister failed: {r:?}");
                        }
                    }
                }
            })
        }
    }
}

#[allow(unknown_lints)] // non_send_fields_in_send_ty is Nightly-only as of 2021-11-29.
#[allow(clippy::non_send_fields_in_send_ty)]
// XXX: required for server setup, verify this is safe.
unsafe impl Send for CubebServer {}

impl rpccore::Server for CubebServer {
    type ServerMessage = ServerMessage;
    type ClientMessage = ClientMessage;

    fn process(&mut self, req: Self::ServerMessage) -> Self::ClientMessage {
        with_local_context(|context, manager| match *context {
            Err(_) => error(cubeb::Error::Error),
            Ok(ref context) => self.process_msg(context, manager, &req),
        })
    }
}

// Debugging for BMO 1594216/1612044.
macro_rules! try_stream {
    ($self:expr, $stm_tok:expr) => {
        if $self.streams.contains($stm_tok) {
            $self.streams[$stm_tok]
                .stream
                .as_mut()
                .expect("uninitialized stream")
        } else {
            error!(
                "{}:{}:{} - Stream({}): invalid token",
                file!(),
                line!(),
                column!(),
                $stm_tok
            );
            return error(cubeb::Error::InvalidParameter);
        }
    };
}

impl CubebServer {
    pub fn new(
        callback_thread: ipccore::EventLoopHandle,
        device_collection_thread: ipccore::EventLoopHandle,
        remote_pid: u32,
        shm_area_size: usize,
    ) -> Self {
        CubebServer {
            callback_thread,
            device_collection_thread,
            streams: slab::Slab::<ServerStream>::new(),
            remote_pid: Some(remote_pid),
            device_collection_change_callbacks: None,
            devidmap: DevIdMap::new(),
            shm_area_size,
        }
    }

    // Process a request coming from the client.
    fn process_msg(
        &mut self,
        context: &cubeb::Context,
        manager: &mut CubebDeviceCollectionManager,
        msg: &ServerMessage,
    ) -> ClientMessage {
        let resp: ClientMessage = match *msg {
            ServerMessage::ClientConnect => {
                assert!(self.remote_pid.is_some());
                ClientMessage::ClientConnected
            }

            ServerMessage::ClientDisconnect => {
                // TODO:
                //self.connection.client_disconnect();
                ClientMessage::ClientDisconnected
            }

            ServerMessage::ContextGetBackendId => {
                ClientMessage::ContextBackendId(context.backend_id().to_string())
            }

            ServerMessage::ContextGetMaxChannelCount => context
                .max_channel_count()
                .map(ClientMessage::ContextMaxChannelCount)
                .unwrap_or_else(error),

            ServerMessage::ContextGetMinLatency(ref params) => {
                let format = cubeb::SampleFormat::from(params.format);
                let layout = cubeb::ChannelLayout::from(params.layout);

                let params = cubeb::StreamParamsBuilder::new()
                    .format(format)
                    .rate(params.rate)
                    .channels(params.channels)
                    .layout(layout)
                    .take();

                context
                    .min_latency(&params)
                    .map(ClientMessage::ContextMinLatency)
                    .unwrap_or_else(error)
            }

            ServerMessage::ContextGetPreferredSampleRate => context
                .preferred_sample_rate()
                .map(ClientMessage::ContextPreferredSampleRate)
                .unwrap_or_else(error),

            ServerMessage::ContextGetSupportedInputProcessingParams => context
                .supported_input_processing_params()
                .map(|params| ClientMessage::ContextSupportedInputProcessingParams(params.bits()))
                .unwrap_or_else(error),

            ServerMessage::ContextGetDeviceEnumeration(device_type) => context
                .enumerate_devices(cubeb::DeviceType::from_bits_truncate(device_type))
                .map(|devices| {
                    let v: Vec<DeviceInfo> = devices
                        .iter()
                        .map(|i| {
                            let mut tmp: DeviceInfo = i.as_ref().into();
                            // Replace each cubeb_devid with a unique handle suitable for IPC.
                            tmp.devid = self.devidmap.make_handle(tmp.devid);
                            tmp
                        })
                        .collect();
                    ClientMessage::ContextEnumeratedDevices(v)
                })
                .unwrap_or_else(error),

            ServerMessage::StreamCreate(ref params) => self
                .process_stream_create(params)
                .unwrap_or_else(|_| error(cubeb::Error::Error)),

            ServerMessage::StreamInit(stm_tok, ref params) => self
                .process_stream_init(context, stm_tok, params)
                .unwrap_or_else(|_| error(cubeb::Error::Error)),

            ServerMessage::StreamDestroy(stm_tok) => {
                if self.streams.contains(stm_tok) {
                    debug!("Unregistering stream {stm_tok:?}");
                    self.streams.remove(stm_tok);
                } else {
                    // Debugging for BMO 1594216/1612044.
                    error!("StreamDestroy({stm_tok}): invalid token");
                    return error(cubeb::Error::InvalidParameter);
                }
                ClientMessage::StreamDestroyed
            }

            ServerMessage::StreamStart(stm_tok) => try_stream!(self, stm_tok)
                .start()
                .map(|_| ClientMessage::StreamStarted)
                .unwrap_or_else(error),

            ServerMessage::StreamStop(stm_tok) => try_stream!(self, stm_tok)
                .stop()
                .map(|_| ClientMessage::StreamStopped)
                .unwrap_or_else(error),

            ServerMessage::StreamGetPosition(stm_tok) => try_stream!(self, stm_tok)
                .position()
                .map(ClientMessage::StreamPosition)
                .unwrap_or_else(error),

            ServerMessage::StreamGetLatency(stm_tok) => try_stream!(self, stm_tok)
                .latency()
                .map(ClientMessage::StreamLatency)
                .unwrap_or_else(error),

            ServerMessage::StreamGetInputLatency(stm_tok) => try_stream!(self, stm_tok)
                .input_latency()
                .map(ClientMessage::StreamInputLatency)
                .unwrap_or_else(error),

            ServerMessage::StreamSetVolume(stm_tok, volume) => try_stream!(self, stm_tok)
                .set_volume(volume)
                .map(|_| ClientMessage::StreamVolumeSet)
                .unwrap_or_else(error),

            ServerMessage::StreamSetName(stm_tok, ref name) => try_stream!(self, stm_tok)
                .set_name(name)
                .map(|_| ClientMessage::StreamNameSet)
                .unwrap_or_else(error),

            ServerMessage::StreamGetCurrentDevice(stm_tok) => try_stream!(self, stm_tok)
                .current_device()
                .map(|device| ClientMessage::StreamCurrentDevice(Device::from(device)))
                .unwrap_or_else(error),

            ServerMessage::StreamSetInputMute(stm_tok, mute) => try_stream!(self, stm_tok)
                .set_input_mute(mute)
                .map(|_| ClientMessage::StreamInputMuteSet)
                .unwrap_or_else(error),

            ServerMessage::StreamSetInputProcessingParams(stm_tok, params) => {
                try_stream!(self, stm_tok)
                    .set_input_processing_params(InputProcessingParams::from_bits_truncate(params))
                    .map(|_| ClientMessage::StreamInputProcessingParamsSet)
                    .unwrap_or_else(error)
            }

            ServerMessage::StreamRegisterDeviceChangeCallback(stm_tok, enable) => {
                try_stream!(self, stm_tok)
                    .register_device_changed_callback(if enable {
                        Some(device_change_cb_c)
                    } else {
                        None
                    })
                    .map(|_| ClientMessage::StreamRegisterDeviceChangeCallback)
                    .unwrap_or_else(error)
            }

            ServerMessage::ContextSetupDeviceCollectionCallback => {
                let (server_pipe, client_pipe) = match sys::make_pipe_pair() {
                    Ok((server_pipe, client_pipe)) => (server_pipe, client_pipe),
                    Err(e) => {
                        debug!(
                            "ContextSetupDeviceCollectionCallback - make_pipe_pair failed: {e:?}"
                        );
                        return error(cubeb::Error::Error);
                    }
                };

                // TODO: this should bind the client_pipe and send the server_pipe to the remote, but
                //       additional work is required as it's not possible to convert a Windows sys::Pipe into a raw handle.
                // TODO: Use the rpc_thread instead of an extra device_collection_thread, but a reentrant bind_client
                //       is required to support that.
                let rpc = match self
                    .device_collection_thread
                    .bind_client::<DeviceCollectionClient>(server_pipe)
                {
                    Ok(rpc) => rpc,
                    Err(e) => {
                        debug!("ContextSetupDeviceCollectionCallback - bind_client: {e:?}");
                        return error(cubeb::Error::Error);
                    }
                };

                self.device_collection_change_callbacks =
                    Some(Rc::new(DeviceCollectionChangeCallback { rpc }));
                let fd = RegisterDeviceCollectionChanged {
                    platform_handle: SerializableHandle::new(client_pipe, self.remote_pid.unwrap()),
                };

                ClientMessage::ContextSetupDeviceCollectionCallback(fd)
            }

            ServerMessage::ContextRegisterDeviceCollectionChanged(device_type, enable) => self
                .process_register_device_collection_changed(
                    context,
                    manager,
                    cubeb::DeviceType::from_bits_truncate(device_type),
                    enable,
                )
                .unwrap_or_else(error),

            #[cfg(target_os = "linux")]
            ServerMessage::PromoteThreadToRealTime(thread_info) => {
                let info = RtPriorityThreadInfo::deserialize(thread_info);
                if info.pid() as u32 != self.remote_pid.unwrap() {
                    warn!(
                        "PromoteThreadToRealTime: client supplied pid {} doesn't match trusted pid {}",
                        info.pid(),
                        self.remote_pid.unwrap()
                    );
                } else {
                    match promote_thread_to_real_time(info, 0, 48000) {
                        Ok(_) => {
                            info!("Promotion of content process thread to real-time OK");
                        }
                        Err(_) => {
                            warn!("Promotion of content process thread to real-time error");
                        }
                    }
                }
                ClientMessage::ThreadPromoted
            }
        };

        trace!("process_msg: req={msg:?}, resp={resp:?}");

        resp
    }

    fn process_register_device_collection_changed(
        &mut self,
        context: &cubeb::Context,
        manager: &mut CubebDeviceCollectionManager,
        devtype: cubeb::DeviceType,
        enable: bool,
    ) -> cubeb::Result<ClientMessage> {
        if devtype == cubeb::DeviceType::UNKNOWN {
            return Err(cubeb::Error::InvalidParameter);
        }

        assert!(self.device_collection_change_callbacks.is_some());
        let cbs = self.device_collection_change_callbacks.as_ref().unwrap();

        if enable {
            manager.register(context, cbs, devtype)
        } else {
            manager.unregister(context, cbs, devtype)
        }
        .map(|_| ClientMessage::ContextRegisteredDeviceCollectionChanged)
    }

    // Stream create is special, so it's been separated from process_msg.
    fn process_stream_create(&mut self, params: &StreamCreateParams) -> Result<ClientMessage> {
        // Create the callback handling struct which is attached the cubeb stream.
        let input_frame_size = params
            .input_stream_params
            .as_ref()
            .map(StreamParams::frame_size_in_bytes);
        let output_frame_size = params
            .output_stream_params
            .as_ref()
            .map(StreamParams::frame_size_in_bytes);

        // Estimate a safe shmem size for this stream configuration.  If the server was configured with a fixed
        // shm_area_size override, use that instead.
        // TODO: Add a new cubeb API to query the precise buffer size required for a given stream config.
        // https://github.com/mozilla/audioipc-2/issues/124
        let shm_area_size = if self.shm_area_size == 0 {
            let in_fs = input_frame_size.unwrap_or(0);
            let out_fs = output_frame_size.unwrap_or(0);
            let frame_size = out_fs.max(in_fs) as u32;
            let in_rate = params.input_stream_params.map(|p| p.rate).unwrap_or(0);
            let out_rate = params.output_stream_params.map(|p| p.rate).unwrap_or(0);
            let rate = out_rate.max(in_rate);
            // 1s of audio, rounded up to the nearest 64kB.
            // Stream latency is capped at 1s in process_stream_init.
            (((rate * frame_size) + 0xffff) & !0xffff) as usize
        } else {
            self.shm_area_size
        };
        debug!("shm_area_size = {shm_area_size}");

        let shm = SharedMem::new(&get_shm_id(), shm_area_size)?;
        let shm_handle = unsafe { shm.make_handle()? };

        let (server_pipe, client_pipe) = sys::make_pipe_pair()?;
        // TODO: this should bind the client_pipe and send the server_pipe to the remote, but
        //       additional work is required as it's not possible to convert a Windows sys::Pipe into a raw handle.
        let rpc = self
            .callback_thread
            .bind_client::<CallbackClient>(server_pipe)?;

        let cbs = Box::new(ServerStreamCallbacks {
            input_frame_size,
            output_frame_size,
            shm,
            state_callback_rpc: rpc.clone(),
            device_change_callback_rpc: rpc.clone(),
            data_callback_rpc: rpc,
            connected: AtomicBool::new(false),
        });

        let entry = self.streams.vacant_entry();
        let key = entry.key();
        debug!("Registering stream {key:?}");

        entry.insert(ServerStream {
            stream: None,
            cbs,
            client_pipe: Some(client_pipe),
        });

        Ok(ClientMessage::StreamCreated(StreamCreate {
            token: key,
            shm_handle: SerializableHandle::new(shm_handle, self.remote_pid.unwrap()),
            shm_area_size,
        }))
    }

    // Stream init is special, so it's been separated from process_msg.
    fn process_stream_init(
        &mut self,
        context: &cubeb::Context,
        stm_tok: usize,
        params: &StreamInitParams,
    ) -> Result<ClientMessage> {
        fn valid_stream_params(p: &StreamParams) -> bool {
            const MAX_CHANNELS: u32 = 64;
            const MIN_RATE: u32 = 1_000;
            const MAX_RATE: u32 = 768_000;
            p.channels >= 1
                && p.channels <= MAX_CHANNELS
                && p.rate >= MIN_RATE
                && p.rate <= MAX_RATE
        }
        for p in params
            .input_stream_params
            .iter()
            .chain(params.output_stream_params.iter())
        {
            if !valid_stream_params(p) {
                self.streams.remove(stm_tok);
                return Err(cubeb::Error::InvalidParameter.into());
            }
        }

        // Create cubeb stream from params
        let stream_name = params
            .stream_name
            .as_ref()
            .and_then(|name| CStr::from_bytes_with_nul(name).ok());

        // Map IPC handle back to cubeb_devid.
        let input_device = self.devidmap.handle_to_id(params.input_device) as *const _;
        let input_stream_params = params.input_stream_params.as_ref().map(|isp| unsafe {
            cubeb::StreamParamsRef::from_ptr(isp as *const StreamParams as *mut _)
        });

        // Map IPC handle back to cubeb_devid.
        let output_device = self.devidmap.handle_to_id(params.output_device) as *const _;
        let output_stream_params = params.output_stream_params.as_ref().map(|osp| unsafe {
            cubeb::StreamParamsRef::from_ptr(osp as *const StreamParams as *mut _)
        });

        // TODO: Manage stream latency requests with respect to the RT deadlines the callback_thread was configured for.
        fn round_up_pow2(v: u32) -> u32 {
            debug_assert!(v >= 1);
            1 << (32 - (v - 1).leading_zeros())
        }
        let rate = params
            .output_stream_params
            .map(|p| p.rate)
            .unwrap_or_else(|| params.input_stream_params.map(|p| p.rate).unwrap());
        // Note: minimum latency supported by AudioIPC is currently ~5ms.  This restriction may be reduced by later IPC improvements.
        let min_latency = round_up_pow2(5 * rate / 1000);
        // Note: maximum latency is restricted by the SharedMem size.
        let max_latency = rate;
        let latency = params.latency_frames.clamp(min_latency, max_latency);
        trace!(
            "stream rate={} latency requested={} calculated={}",
            rate,
            params.latency_frames,
            latency
        );

        let server_stream = &mut self.streams[stm_tok];
        assert!(size_of::<Box<ServerStreamCallbacks>>() == size_of::<usize>());
        let user_ptr = server_stream.cbs.as_ref() as *const ServerStreamCallbacks as *mut c_void;

        let stream = unsafe {
            let stream = context.stream_init(
                stream_name,
                input_device,
                input_stream_params,
                output_device,
                output_stream_params,
                latency,
                Some(data_cb_c),
                Some(state_cb_c),
                user_ptr,
            );
            match stream {
                Ok(stream) => stream,
                Err(e) => {
                    debug!("Unregistering stream {stm_tok:?} (stream error {e:?})");
                    self.streams.remove(stm_tok);
                    return Err(e.into());
                }
            }
        };

        server_stream.stream = Some(stream);

        let client_pipe = server_stream
            .client_pipe
            .take()
            .expect("invalid state after StreamCreated");
        server_stream.cbs.connected.store(true, Ordering::Release);
        Ok(ClientMessage::StreamInitialized(SerializableHandle::new(
            client_pipe,
            self.remote_pid.unwrap(),
        )))
    }
}

// C callable callbacks
unsafe extern "C" fn data_cb_c(
    _: *mut ffi::cubeb_stream,
    user_ptr: *mut c_void,
    input_buffer: *const c_void,
    output_buffer: *mut c_void,
    nframes: c_long,
) -> c_long {
    let ok = panic::catch_unwind(|| {
        let cbs = &mut *(user_ptr as *mut ServerStreamCallbacks);
        let input = if input_buffer.is_null() {
            &[]
        } else {
            let nbytes = nframes * cbs.input_frame_size.unwrap_or(0) as c_long;
            slice::from_raw_parts(input_buffer as *const u8, nbytes as usize)
        };
        let output: &mut [u8] = if output_buffer.is_null() {
            &mut []
        } else {
            let nbytes = nframes * cbs.output_frame_size.unwrap_or(0) as c_long;
            slice::from_raw_parts_mut(output_buffer as *mut u8, nbytes as usize)
        };
        cbs.data_callback(input, output, nframes as isize) as c_long
    });
    ok.unwrap_or(cubeb::ffi::CUBEB_ERROR as c_long)
}

unsafe extern "C" fn state_cb_c(
    _: *mut ffi::cubeb_stream,
    user_ptr: *mut c_void,
    state: ffi::cubeb_state,
) {
    let ok = panic::catch_unwind(|| {
        let state = cubeb::State::from(state);
        let cbs = &mut *(user_ptr as *mut ServerStreamCallbacks);
        cbs.state_callback(state);
    });
    ok.expect("State callback panicked");
}

unsafe extern "C" fn device_change_cb_c(user_ptr: *mut c_void) {
    let ok = panic::catch_unwind(|| {
        let cbs = &mut *(user_ptr as *mut ServerStreamCallbacks);
        cbs.device_change_callback();
    });
    ok.expect("Device change callback panicked");
}

unsafe extern "C" fn device_collection_changed_input_cb_c(
    _: *mut ffi::cubeb,
    user_ptr: *mut c_void,
) {
    let ok = panic::catch_unwind(|| {
        let manager = &mut *(user_ptr as *mut CubebDeviceCollectionManager);
        manager.device_collection_changed_callback(ffi::CUBEB_DEVICE_TYPE_INPUT);
    });
    ok.expect("Collection changed (input) callback panicked");
}

unsafe extern "C" fn device_collection_changed_output_cb_c(
    _: *mut ffi::cubeb,
    user_ptr: *mut c_void,
) {
    let ok = panic::catch_unwind(|| {
        let manager = &mut *(user_ptr as *mut CubebDeviceCollectionManager);
        manager.device_collection_changed_callback(ffi::CUBEB_DEVICE_TYPE_OUTPUT);
    });
    ok.expect("Collection changed (output) callback panicked");
}
