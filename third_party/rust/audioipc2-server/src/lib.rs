// Copyright © 2017 Mozilla Foundation
//
// This program is made available under an ISC-style license.  See the
// accompanying file LICENSE for details
#![warn(unused_extern_crates)]

#[macro_use]
extern crate log;

use audio_thread_priority::promote_current_thread_to_real_time;
use audioipc::ipccore;
use audioipc::sys;
use audioipc::PlatformHandleType;
use once_cell::sync::Lazy;
use std::ffi::{CStr, CString};
use std::os::raw::c_void;
use std::ptr;
use std::sync::Mutex;
use std::thread;

mod server;

struct CubebContextParams {
    context_name: CString,
    backend_name: Option<CString>,
}

static G_CUBEB_CONTEXT_PARAMS: Lazy<Mutex<CubebContextParams>> = Lazy::new(|| {
    Mutex::new(CubebContextParams {
        context_name: CString::new("AudioIPC Server").unwrap(),
        backend_name: None,
    })
});

use audioipc::errors::Result;

struct ServerWrapper {
    rpc_thread: ipccore::EventLoopThread,
    callback_thread: ipccore::EventLoopThread,
    device_collection_thread: ipccore::EventLoopThread,
}

fn register_thread(callback: Option<extern "C" fn(*const ::std::os::raw::c_char)>) {
    if let Some(func) = callback {
        let name = CString::new(thread::current().name().unwrap()).unwrap();
        func(name.as_ptr());
    }
}

fn unregister_thread(callback: Option<extern "C" fn()>) {
    if let Some(func) = callback {
        func();
    }
}

fn init_threads(
    thread_create_callback: Option<extern "C" fn(*const ::std::os::raw::c_char)>,
    thread_destroy_callback: Option<extern "C" fn()>,
) -> Result<ServerWrapper> {
    let rpc_name = "AudioIPC Server RPC";
    let rpc_thread = ipccore::EventLoopThread::new(
        rpc_name.to_string(),
        None,
        move || {
            trace!("Starting {rpc_name} thread");
            register_thread(thread_create_callback);
            audioipc::server_platform_init();
        },
        move || {
            unregister_thread(thread_destroy_callback);
            trace!("Stopping {rpc_name} thread");
        },
    )
    .map_err(|e| {
        debug!("Failed to start {rpc_name} thread: {e:?}");
        e
    })?;

    let callback_name = "AudioIPC Server Callback";
    let callback_thread = ipccore::EventLoopThread::new(
        callback_name.to_string(),
        None,
        move || {
            trace!("Starting {callback_name} thread");
            if let Err(e) = promote_current_thread_to_real_time(0, 48000) {
                debug!("Failed to promote {callback_name} thread to real-time: {e:?}");
            }
            register_thread(thread_create_callback);
        },
        move || {
            unregister_thread(thread_destroy_callback);
            trace!("Stopping {callback_name} thread");
        },
    )
    .map_err(|e| {
        debug!("Failed to start {callback_name} thread: {e:?}");
        e
    })?;

    let device_collection_name = "AudioIPC DeviceCollection RPC";
    let device_collection_thread = ipccore::EventLoopThread::new(
        device_collection_name.to_string(),
        None,
        move || {
            trace!("Starting {device_collection_name} thread");
            register_thread(thread_create_callback);
        },
        move || {
            unregister_thread(thread_destroy_callback);
            trace!("Stopping {device_collection_name} thread");
        },
    )
    .map_err(|e| {
        debug!("Failed to start {device_collection_name} thread: {e:?}");
        e
    })?;

    Ok(ServerWrapper {
        rpc_thread,
        callback_thread,
        device_collection_thread,
    })
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct AudioIpcServerInitParams {
    // Fields only need to be public for ipctest.
    pub thread_create_callback: Option<extern "C" fn(*const ::std::os::raw::c_char)>,
    pub thread_destroy_callback: Option<extern "C" fn()>,
}

#[allow(clippy::missing_safety_doc)]
#[no_mangle]
pub unsafe extern "C" fn audioipc2_server_start(
    context_name: *const std::os::raw::c_char,
    backend_name: *const std::os::raw::c_char,
    init_params: *const AudioIpcServerInitParams,
) -> *mut c_void {
    assert!(!init_params.is_null());
    let mut params = G_CUBEB_CONTEXT_PARAMS.lock().unwrap();
    if !context_name.is_null() {
        CStr::from_ptr(context_name).clone_into(&mut params.context_name);
    }
    if !backend_name.is_null() {
        let backend_string = CStr::from_ptr(backend_name).to_owned();
        params.backend_name = Some(backend_string);
    }
    match init_threads(
        (*init_params).thread_create_callback,
        (*init_params).thread_destroy_callback,
    ) {
        Ok(server) => Box::into_raw(Box::new(server)) as *mut _,
        Err(_) => ptr::null_mut() as *mut _,
    }
}

// A `shm_area_size` of 0 allows the server to calculate an appropriate shm size for each stream.
// A non-zero `shm_area_size` forces all allocations to the specified size.
#[no_mangle]
pub extern "C" fn audioipc2_server_new_client(
    p: *mut c_void,
    remote_pid: u32,
    shm_area_size: usize,
) -> PlatformHandleType {
    let wrapper: &ServerWrapper = unsafe { &*(p as *mut _) };

    // We create a connected pair of anonymous IPC endpoints. One side
    // is registered with the event loop core, the other side is returned
    // to the caller to be remoted to the client to complete setup.
    let (server_pipe, client_pipe) = match sys::make_pipe_pair() {
        Ok((server_pipe, client_pipe)) => (server_pipe, client_pipe),
        Err(e) => {
            error!("audioipc_server_new_client - make_pipe_pair failed: {e:?}");
            return audioipc::INVALID_HANDLE_VALUE;
        }
    };

    let rpc_thread = wrapper.rpc_thread.handle();
    let callback_thread = wrapper.callback_thread.handle();
    let device_collection_thread = wrapper.device_collection_thread.handle();

    let server = server::CubebServer::new(
        callback_thread.clone(),
        device_collection_thread.clone(),
        remote_pid,
        shm_area_size,
    );
    if let Err(e) = rpc_thread.bind_server_async(server, server_pipe) {
        error!("audioipc_server_new_client - bind_server failed: {e:?}");
        return audioipc::INVALID_HANDLE_VALUE;
    }

    unsafe { client_pipe.into_raw() }
}

#[no_mangle]
pub extern "C" fn audioipc2_server_stop(p: *mut c_void) {
    let wrapper = unsafe { Box::<ServerWrapper>::from_raw(p as *mut _) };
    drop(wrapper);
}
