/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Widely copied from dbus-rs/dbus/examples/rtkit.rs */

extern crate dbus;
extern crate libc;

use std::cmp;
use std::convert::TryInto;
use std::error::Error;
use std::io::Error as OSError;

use dbus::{BusType, Connection, Message, MessageItem, Props};

use crate::AudioThreadPriorityError;

const DBUS_SOCKET_TIMEOUT: i32 = 10_000;
const RT_PRIO_DEFAULT: u32 = 10;
// This is different from libc::pid_t, which is 32 bits, and is defined in sys/types.h.
#[allow(non_camel_case_types)]
type kernel_pid_t = libc::c_long;

impl From<dbus::Error> for AudioThreadPriorityError {
    fn from(error: dbus::Error) -> Self {
        AudioThreadPriorityError::new(&format!(
            "{}:{}",
            error.name().unwrap_or("?"),
            error.message().unwrap_or("?")
        ))
    }
}

impl From<Box<dyn Error>> for AudioThreadPriorityError {
    fn from(error: Box<dyn Error>) -> Self {
        AudioThreadPriorityError::new(&error.to_string())
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RtPriorityThreadInfoInternal {
    /// System-wise thread id, use to promote the thread via dbus.
    thread_id: kernel_pid_t,
    /// Process-local thread id, used to restore scheduler characteristics. This information is not
    /// useful in another process, but is useful tied to the `thread_id`, when back into the first
    /// process.
    pthread_id: libc::pthread_t,
    /// The PID of the process containing `thread_id` below.
    pid: libc::pid_t,
    /// ...
    policy: libc::c_int,
}

impl RtPriorityThreadInfoInternal {
    /// Serialize a RtPriorityThreadInfoInternal to a byte buffer.
    pub fn serialize(&self) -> [u8; std::mem::size_of::<Self>()] {
        unsafe { std::mem::transmute::<Self, [u8; std::mem::size_of::<Self>()]>(*self) }
    }
    /// Get an RtPriorityThreadInfoInternal from a byte buffer.
    pub fn deserialize(bytes: [u8; std::mem::size_of::<Self>()]) -> Self {
        unsafe { std::mem::transmute::<[u8; std::mem::size_of::<Self>()], Self>(bytes) }
    }
    /// Returns the PID of the process containing the thread.
    pub fn pid(&self) -> libc::pid_t {
        self.pid
    }
}

impl PartialEq for RtPriorityThreadInfoInternal {
    fn eq(&self, other: &Self) -> bool {
        self.thread_id == other.thread_id && self.pthread_id == other.pthread_id
    }
}

/*#[derive(Debug)]*/
pub struct RtPriorityHandleInternal {
    thread_info: RtPriorityThreadInfoInternal,
}

fn item_as_i64(i: MessageItem) -> Result<i64, AudioThreadPriorityError> {
    match i {
        MessageItem::Int32(i) => Ok(i as i64),
        MessageItem::Int64(i) => Ok(i),
        _ => Err(AudioThreadPriorityError::new(&format!(
            "Property is not integer ({i:?})"
        ))),
    }
}

fn rtkit_set_realtime(thread: u64, pid: u64, prio: u32) -> Result<(), Box<dyn Error>> {
    let m = if unsafe { libc::getpid() as u64 } == pid {
        let mut m = Message::new_method_call(
            "org.freedesktop.RealtimeKit1",
            "/org/freedesktop/RealtimeKit1",
            "org.freedesktop.RealtimeKit1",
            "MakeThreadRealtime",
        )?;
        m.append_items(&[thread.into(), prio.into()]);
        m
    } else {
        let mut m = Message::new_method_call(
            "org.freedesktop.RealtimeKit1",
            "/org/freedesktop/RealtimeKit1",
            "org.freedesktop.RealtimeKit1",
            "MakeThreadRealtimeWithPID",
        )?;
        m.append_items(&[pid.into(), thread.into(), prio.into()]);
        m
    };
    let c = Connection::get_private(BusType::System)?;
    c.send_with_reply_and_block(m, DBUS_SOCKET_TIMEOUT)?;
    Ok(())
}

/// Returns the maximum priority, maximum real-time time slice, and the current real-time time
/// slice for this process.
fn get_limits() -> Result<(i64, u64, libc::rlimit), AudioThreadPriorityError> {
    let c = Connection::get_private(BusType::System)?;

    let p = Props::new(
        &c,
        "org.freedesktop.RealtimeKit1",
        "/org/freedesktop/RealtimeKit1",
        "org.freedesktop.RealtimeKit1",
        DBUS_SOCKET_TIMEOUT,
    );
    let mut current_limit = libc::rlimit {
        rlim_cur: 0,
        rlim_max: 0,
    };

    let max_prio = item_as_i64(p.get("MaxRealtimePriority")?)?;
    if max_prio < 0 {
        return Err(AudioThreadPriorityError::new(
            "invalid negative MaxRealtimePriority",
        ));
    }

    let max_rttime = item_as_i64(p.get("RTTimeUSecMax")?)?;
    if max_rttime < 0 {
        return Err(AudioThreadPriorityError::new(
            "invalid negative RTTimeUSecMax",
        ));
    }

    if unsafe { libc::getrlimit(libc::RLIMIT_RTTIME, &mut current_limit) } < 0 {
        return Err(AudioThreadPriorityError::new_with_inner(
            "getrlimit",
            Box::new(OSError::last_os_error()),
        ));
    }

    Ok((max_prio, (max_rttime as u64), current_limit))
}

fn set_limits(request: u64, max: u64) -> Result<(), AudioThreadPriorityError> {
    // Set a soft limit to the limit requested, to be able to handle going over the limit using
    // SIGXCPU. Set the hard limit to the maximum slice to prevent getting SIGKILL.
    #[allow(clippy::useless_conversion)]
    let new_limit = libc::rlimit {
        rlim_cur: request
            .try_into()
            .map_err(|_| AudioThreadPriorityError::new("setrlimit"))?,
        rlim_max: max
            .try_into()
            .map_err(|_| AudioThreadPriorityError::new("setrlimit"))?,
    };
    if unsafe { libc::setrlimit(libc::RLIMIT_RTTIME, &new_limit) } < 0 {
        return Err(AudioThreadPriorityError::new_with_inner(
            "setrlimit",
            Box::new(OSError::last_os_error()),
        ));
    }

    Ok(())
}

pub fn promote_current_thread_to_real_time_internal(
    audio_buffer_frames: u32,
    audio_samplerate_hz: u32,
) -> Result<RtPriorityHandleInternal, AudioThreadPriorityError> {
    let thread_info = get_current_thread_info_internal()?;
    promote_thread_to_real_time_internal(thread_info, audio_buffer_frames, audio_samplerate_hz)
}

pub fn demote_current_thread_from_real_time_internal(
    rt_priority_handle: RtPriorityHandleInternal,
) -> Result<(), AudioThreadPriorityError> {
    assert!(unsafe { libc::pthread_self() } == rt_priority_handle.thread_info.pthread_id);

    let param = unsafe { std::mem::zeroed::<libc::sched_param>() };

    if unsafe {
        libc::pthread_setschedparam(
            rt_priority_handle.thread_info.pthread_id,
            rt_priority_handle.thread_info.policy,
            &param,
        )
    } < 0
    {
        return Err(AudioThreadPriorityError::new_with_inner(
            "could not demote thread",
            Box::new(OSError::last_os_error()),
        ));
    }
    Ok(())
}

/// This can be called by sandboxed code, it only restores priority to what they were.
pub fn demote_thread_from_real_time_internal(
    thread_info: RtPriorityThreadInfoInternal,
) -> Result<(), AudioThreadPriorityError> {
    let param = unsafe { std::mem::zeroed::<libc::sched_param>() };

    // https://github.com/rust-lang/libc/issues/1511
    const SCHED_RESET_ON_FORK: libc::c_int = 0x40000000;

    if unsafe {
        libc::pthread_setschedparam(
            thread_info.pthread_id,
            libc::SCHED_OTHER | SCHED_RESET_ON_FORK,
            &param,
        )
    } < 0
    {
        return Err(AudioThreadPriorityError::new_with_inner(
            "could not demote thread",
            Box::new(OSError::last_os_error()),
        ));
    }
    Ok(())
}

/// Get the current thread information, as an opaque struct, that can be serialized and sent
/// accross processes. This is enough to capture the current state of the scheduling policy, and
/// an identifier to have another thread promoted to real-time.
pub fn get_current_thread_info_internal(
) -> Result<RtPriorityThreadInfoInternal, AudioThreadPriorityError> {
    let thread_id = unsafe { libc::syscall(libc::SYS_gettid) };
    let pthread_id = unsafe { libc::pthread_self() };
    let mut param = unsafe { std::mem::zeroed::<libc::sched_param>() };
    let mut policy = 0;

    if unsafe { libc::pthread_getschedparam(pthread_id, &mut policy, &mut param) } < 0 {
        return Err(AudioThreadPriorityError::new_with_inner(
            "pthread_getschedparam",
            Box::new(OSError::last_os_error()),
        ));
    }

    let pid = unsafe { libc::getpid() };

    Ok(RtPriorityThreadInfoInternal {
        pid,
        thread_id,
        pthread_id,
        policy,
    })
}

/// This set the RLIMIT_RTTIME resource to something other than "unlimited". It's necessary for the
/// rtkit request to succeed, and needs to hapen in the child. We can't get the real limit here,
/// because we don't have access to DBUS, so it is hardcoded to 200ms, which is the default in the
/// rtkit package.
pub fn set_real_time_hard_limit_internal(
    audio_buffer_frames: u32,
    audio_samplerate_hz: u32,
) -> Result<(), AudioThreadPriorityError> {
    let buffer_frames = if audio_buffer_frames > 0 {
        audio_buffer_frames
    } else {
        // 50ms slice. This "ought to be enough for anybody".
        audio_samplerate_hz / 20
    };
    let budget_us = buffer_frames as u64 * 1_000_000 / audio_samplerate_hz as u64;

    // It's only necessary to set RLIMIT_RTTIME to something when in the child, skip it if it's a
    // remoting call.
    let (_, max_rttime, _) = get_limits()?;

    // Only take what we need, or cap at the system limit, no further.
    let rttime_request = cmp::min(budget_us, max_rttime);
    set_limits(rttime_request, max_rttime)?;

    Ok(())
}

/// Promote a thread (possibly in another process) identified by its tid, to real-time.
pub fn promote_thread_to_real_time_internal(
    thread_info: RtPriorityThreadInfoInternal,
    audio_buffer_frames: u32,
    audio_samplerate_hz: u32,
) -> Result<RtPriorityHandleInternal, AudioThreadPriorityError> {
    let RtPriorityThreadInfoInternal { pid, thread_id, .. } = thread_info;

    let handle = RtPriorityHandleInternal { thread_info };

    set_real_time_hard_limit_internal(audio_buffer_frames, audio_samplerate_hz)?;

    let r = rtkit_set_realtime(thread_id as u64, pid as u64, RT_PRIO_DEFAULT);

    match r {
        Ok(_) => Ok(handle),
        Err(e) => {
            let (_, _, limits) = get_limits()?;
            if limits.rlim_cur != libc::RLIM_INFINITY
                && unsafe { libc::setrlimit(libc::RLIMIT_RTTIME, &limits) } < 0
            {
                return Err(AudioThreadPriorityError::new_with_inner(
                    "setrlimit",
                    Box::new(OSError::last_os_error()),
                ));
            }
            Err(AudioThreadPriorityError::new_with_inner(
                "Thread promotion error",
                e,
            ))
        }
    }
}
