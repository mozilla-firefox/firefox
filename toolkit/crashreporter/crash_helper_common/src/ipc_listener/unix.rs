/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#[cfg(any(target_os = "android", target_os = "linux"))]
use crate::platform::linux::{
    server_addr, set_socket_cloexec, set_socket_default_flags, unix_socket,
};
#[cfg(target_os = "macos")]
use crate::platform::macos::{
    server_addr, set_socket_cloexec, set_socket_default_flags, unix_socket,
};
use crate::{errors::IPCError, IPCConnector, Pid};

use nix::sys::socket::{accept, bind, listen, Backlog};
use std::{
    ffi::{CStr, CString},
    os::fd::{AsFd, AsRawFd, BorrowedFd, FromRawFd, OwnedFd, RawFd},
    str::FromStr,
};

pub struct IPCListener {
    socket: OwnedFd,
}

impl IPCListener {
    /// Create a new listener with an address based on `pid`. The underlying
    /// socket will not have the `FD_CLOEXEC` flag set and thus can be
    /// inherited by child processes.
    pub fn new(pid: Pid) -> Result<IPCListener, IPCError> {
        let socket = unix_socket().map_err(IPCError::System)?;
        set_socket_default_flags(socket.as_fd()).map_err(IPCError::System)?;

        let server_addr = server_addr(pid).map_err(IPCError::System)?;
        bind(socket.as_fd().as_raw_fd(), &server_addr).map_err(IPCError::System)?;
        listen(&socket, Backlog::new(1).unwrap()).map_err(IPCError::ListenFailed)?;

        Ok(IPCListener { socket })
    }

    /// Create a new listener using an already prepared socket. The listener
    /// must have been  bound to the appropriate address and should already be
    /// listening on incoming connections. This will set the `FD_CLOEXEC` flag
    /// on the underlying socket and thus will make this litener not inheritable
    /// by child processes.
    pub fn from_fd(_pid: Pid, socket: OwnedFd) -> Result<IPCListener, IPCError> {
        set_socket_cloexec(socket.as_fd()).map_err(IPCError::System)?;

        Ok(IPCListener { socket })
    }

    /// Serialize this listener into a string that can be passed on the
    /// command-line to a child process. This only works for newly
    /// created listeners because they are explicitly created as inheritable.
    pub fn serialize(&self) -> CString {
        CString::new(self.socket.as_raw_fd().to_string()).unwrap()
    }

    /// Deserialize a listener from an argument passed on the command-line.
    /// The resulting listener is ready to accept new connections.
    pub fn deserialize(string: &CStr, _pid: Pid) -> Result<IPCListener, IPCError> {
        let string = string.to_str().map_err(|_e| IPCError::ParseError)?;
        let fd = RawFd::from_str(string).map_err(|_e| IPCError::ParseError)?;
        // SAFETY: This is a file descriptor we passed in ourselves.
        let socket = unsafe { OwnedFd::from_raw_fd(fd) };
        Ok(IPCListener { socket })
    }

    pub fn accept(&self) -> Result<IPCConnector, IPCError> {
        let socket = accept(self.socket.as_fd().as_raw_fd()).map_err(IPCError::AcceptFailed)?;
        // SAFETY: `socket` is guaranteed to be valid at this point.
        IPCConnector::from_fd(unsafe { OwnedFd::from_raw_fd(socket) })
    }

    pub fn as_raw_ref(&self) -> BorrowedFd {
        self.socket.as_fd()
    }
}
