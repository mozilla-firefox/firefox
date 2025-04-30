/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use crate::{ignore_eintr, AncillaryData, Pid};

use nix::{
    errno::Errno,
    fcntl::{
        fcntl,
        FcntlArg::{F_GETFL, F_SETFD, F_SETFL},
        FdFlag, OFlag,
    },
    libc::{setsockopt, SOL_SOCKET, SO_NOSIGPIPE},
    sys::socket::{
        getsockopt, recv, send, socket, socketpair, sockopt::LocalPeerPid, AddressFamily, MsgFlags,
        SockFlag, SockType, UnixAddr,
    },
    Result,
};
use std::{
    mem::size_of,
    os::fd::{AsRawFd, BorrowedFd, OwnedFd, RawFd},
    path::PathBuf,
    str::FromStr,
};

pub(crate) fn unix_socket() -> Result<OwnedFd> {
    socket(
        AddressFamily::Unix,
        SockType::Stream,
        SockFlag::empty(),
        None,
    )
}

pub(crate) fn unix_socketpair() -> Result<(OwnedFd, OwnedFd)> {
    socketpair(
        AddressFamily::Unix,
        SockType::Stream,
        None,
        SockFlag::empty(),
    )
}

pub(crate) fn set_socket_default_flags(socket: BorrowedFd) -> Result<()> {
    // All our sockets are in non-blocking mode.
    let fd = socket.as_raw_fd();
    let flags = OFlag::from_bits_retain(fcntl(fd, F_GETFL)?);
    fcntl(fd, F_SETFL(flags.union(OFlag::O_NONBLOCK)))?;

    // TODO: nix doesn't have a safe wrapper for SO_NOSIGPIPE yet, but we need
    // to set this flag because we're using stream sockets unlike Linux, where
    // we use sequential packets that don't raise SIGPIPE.
    let res = unsafe {
        setsockopt(
            fd,
            SOL_SOCKET,
            SO_NOSIGPIPE,
            (&1 as *const i32).cast(),
            size_of::<i32>() as _,
        )
    };

    if res < 0 {
        return Err(Errno::last());
    }

    Ok(())
}

pub(crate) fn set_socket_cloexec(socket: BorrowedFd) -> Result<()> {
    fcntl(socket.as_raw_fd(), F_SETFD(FdFlag::FD_CLOEXEC)).map(|_res| ())
}

pub(crate) fn server_addr(pid: Pid) -> Result<UnixAddr> {
    // macOS doesn't seem to support abstract paths as addresses for Unix
    // protocol sockets, so this needs to be the path of an actual file.
    let server_name = format!("/tmp/gecko-crash-helper-pipe.{pid:}");
    let server_path = PathBuf::from_str(&server_name).unwrap();
    UnixAddr::new(&server_path)
}

// Return the pid of the process connected to this socket.
pub(crate) fn connected_process_pid(socket: BorrowedFd) -> Result<Pid> {
    let pid = getsockopt(&socket, LocalPeerPid)?;

    Ok(pid)
}

// We're using plain recv()/send() calls here and we're calling them in a loop
// until all the data expected to be in a message gets retrieved or sent. Note
// however we'll fail whenever we hit an EAGAIN condition. That's because IPC
// calls are essentially symmetric, so if we're hitting EAGAIN it means one of
// the sides isn't responding so better to fail right away.

pub(crate) fn send_nonblock(socket: RawFd, buff: &[u8], fd: Option<AncillaryData>) -> Result<()> {
    // We don't send file descriptors on macOS
    if fd.is_some() {
        return Err(Errno::EINVAL);
    }

    let mut bytes_sent = 0;

    while bytes_sent != buff.len() {
        let res = ignore_eintr!(send(socket, &buff[bytes_sent..], MsgFlags::empty()));

        match res {
            Ok(size) => {
                bytes_sent += size;
            }
            Err(error) => {
                return Err(error);
            }
        }
    }

    Ok(())
}

pub(crate) fn recv_nonblock(
    socket: RawFd,
    expected_size: usize,
) -> Result<(Vec<u8>, Option<AncillaryData>)> {
    let mut buff: Vec<u8> = vec![0; expected_size];
    let mut bytes_received = 0;

    while bytes_received != expected_size {
        let res = ignore_eintr!(recv(socket, &mut buff[bytes_received..], MsgFlags::empty()));

        match res {
            Ok(size) => {
                bytes_received += size;

                if size == 0 {
                    // This means the other end was disconnected
                    return Err(Errno::EBADMSG);
                }
            }
            Err(error) => {
                return Err(error);
            }
        }
    }

    Ok((buff, None))
}
