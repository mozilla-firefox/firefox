/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use crate::{ignore_eintr, AncillaryData, Pid};

use nix::{
    cmsg_space,
    errno::Errno,
    fcntl::{
        fcntl,
        FcntlArg::{F_GETFL, F_SETFD, F_SETFL},
        FdFlag, OFlag,
    },
    sys::socket::{
        getsockopt, recvmsg, sendmsg, socket, socketpair, sockopt::PeerCredentials, AddressFamily,
        ControlMessage, ControlMessageOwned, MsgFlags, SockFlag, SockType, UnixAddr,
    },
    Result,
};
use std::{
    env,
    io::{IoSlice, IoSliceMut},
    os::fd::{AsRawFd, BorrowedFd, OwnedFd, RawFd},
};

pub(crate) fn unix_socket() -> Result<OwnedFd> {
    socket(
        AddressFamily::Unix,
        SockType::SeqPacket,
        SockFlag::empty(),
        None,
    )
}

pub(crate) fn unix_socketpair() -> Result<(OwnedFd, OwnedFd)> {
    socketpair(
        AddressFamily::Unix,
        SockType::SeqPacket,
        None,
        SockFlag::empty(),
    )
}

pub(crate) fn set_socket_default_flags(socket: BorrowedFd) -> Result<()> {
    // All our sockets are in non-blocking mode.
    let fd = socket.as_raw_fd();
    let flags = OFlag::from_bits_retain(fcntl(fd, F_GETFL)?);
    fcntl(fd, F_SETFL(flags.union(OFlag::O_NONBLOCK))).map(|_res| ())
}

pub(crate) fn set_socket_cloexec(socket: BorrowedFd) -> Result<()> {
    fcntl(socket.as_raw_fd(), F_SETFD(FdFlag::FD_CLOEXEC)).map(|_res| ())
}

pub(crate) fn server_addr(pid: Pid) -> Result<UnixAddr> {
    let server_name = if let Ok(snap_instance_name) = env::var("SNAP_INSTANCE_NAME") {
        format!("snap.{snap_instance_name:}.gecko-crash-helper-pipe.{pid:}")
    } else {
        format!("gecko-crash-helper-pipe.{pid:}")
    };
    UnixAddr::new_abstract(server_name.as_bytes())
}

// Return the pid of the process connected to this socket.
pub(crate) fn connected_process_pid(socket: BorrowedFd) -> Result<Pid> {
    let pid = getsockopt(&socket, PeerCredentials)?.pid();

    Ok(pid)
}

pub(crate) fn send_nonblock(socket: RawFd, buff: &[u8], fd: Option<AncillaryData>) -> Result<()> {
    let iov = [IoSlice::new(buff)];
    let scm_fds: Vec<i32> = fd.map_or(vec![], |fd| vec![fd]);
    let scm = ControlMessage::ScmRights(&scm_fds);

    let res = ignore_eintr!(sendmsg::<()>(socket, &iov, &[scm], MsgFlags::empty(), None));

    match res {
        Ok(bytes_sent) => {
            if bytes_sent == buff.len() {
                Ok(())
            } else {
                // TODO: This should never happen but we might want to put a
                // better error message here.
                Err(Errno::EMSGSIZE)
            }
        }
        Err(code) => Err(code),
    }
}

pub(crate) fn recv_nonblock(
    socket: RawFd,
    expected_size: usize,
) -> Result<(Vec<u8>, Option<AncillaryData>)> {
    let mut buff: Vec<u8> = vec![0; expected_size];
    let mut cmsg_buffer = cmsg_space!(RawFd);
    let mut iov = [IoSliceMut::new(&mut buff)];

    let res = ignore_eintr!(recvmsg::<()>(
        socket,
        &mut iov,
        Some(&mut cmsg_buffer),
        MsgFlags::empty(),
    ))?;

    let fd = if let Some(cmsg) = res.cmsgs()?.next() {
        if let ControlMessageOwned::ScmRights(fds) = cmsg {
            fds.first().copied()
        } else {
            return Err(Errno::EBADMSG);
        }
    } else {
        None
    };

    if res.bytes != expected_size {
        // TODO: This should only ever happen if the other side has gone rogue,
        // we need a better error message here.
        return Err(Errno::EBADMSG);
    }

    Ok((buff, fd))
}
