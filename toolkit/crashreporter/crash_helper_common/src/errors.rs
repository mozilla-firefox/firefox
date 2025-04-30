/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::{
    array::TryFromSliceError,
    ffi::{FromBytesWithNulError, NulError},
};
use thiserror::Error;

#[cfg(not(target_os = "windows"))]
use nix::errno::Errno as SystemError;
#[cfg(target_os = "windows")]
use windows_sys::Win32::Foundation::WIN32_ERROR as SystemError;

#[derive(Debug, Error)]
pub enum IPCError {
    #[error("Message error")]
    BadMessage(#[from] MessageError),
    #[error("Generic system error: {0}")]
    System(SystemError),
    #[error("Could not bind socket to an address, error: {0}")]
    BindFailed(SystemError),
    #[error("Could not listen on a socket, error: {0}")]
    ListenFailed(SystemError),
    #[error("Could not accept an incoming connection, error: {0}")]
    AcceptFailed(SystemError),
    #[error("Could not connect to a socket, error: {0}")]
    ConnectionFailure(SystemError),
    #[error("Could not send data, error: {0}")]
    TransmissionFailure(SystemError),
    #[error("Could not receive data, error: {0}")]
    ReceptionFailure(SystemError),
    #[error("Error while waiting for events, error: {0:?}")]
    WaitingFailure(Option<SystemError>),
    #[error("Error while parsing a file descriptor string")]
    ParseError,
}

#[derive(Debug, Error)]
pub enum MessageError {
    #[error("Truncated message")]
    Truncated,
    #[error("Message kind is invalid")]
    InvalidKind,
    #[error("The message contained an invalid payload")]
    InvalidData,
    #[error("Missing ancillary data")]
    MissingAncillary,
    #[error("Invalid message size")]
    InvalidSize(#[from] TryFromSliceError),
    #[error("Missing nul terminator")]
    MissingNul(#[from] FromBytesWithNulError),
    #[error("Missing nul terminator")]
    InteriorNul(#[from] NulError),
}
