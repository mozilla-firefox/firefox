/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use thiserror::Error;

#[derive(Debug, Error)]
pub enum ProcessReaderError {
    #[error("Could not convert address {0}")]
    ConvertAddressError(#[from] std::num::TryFromIntError),
    #[error("Could not parse address {0}")]
    ParseAddressError(#[from] std::num::ParseIntError),
    #[cfg(target_os = "windows")]
    #[error("Cannot enumerate the target process's modules")]
    EnumProcessModulesError,
    #[error("goblin failed to parse a module")]
    GoblinError(#[from] goblin::error::Error),
    #[error("Address was out of bounds")]
    InvalidAddress,
    #[error("Could not read from the target process address space")]
    ReadFromProcessError(#[from] ReadError),
    #[cfg(any(target_os = "windows", target_os = "macos"))]
    #[error("Section was not found")]
    SectionNotFound,
    #[cfg(any(target_os = "linux", target_os = "android"))]
    #[error("Could not attach to the target process")]
    AttachError(#[from] PtraceError),
    #[cfg(any(target_os = "linux", target_os = "android"))]
    #[error("Note not found")]
    NoteNotFound,
    #[cfg(any(target_os = "linux", target_os = "android"))]
    #[error("SONAME not found")]
    SoNameNotFound,
    #[cfg(any(target_os = "linux", target_os = "android"))]
    #[error("waitpid() failed when attaching to the process")]
    WaitPidError,
    #[cfg(any(target_os = "linux", target_os = "android"))]
    #[error("Could not parse a line in /proc/<pid>/maps")]
    ProcMapsParseError,
    #[error("Module not found")]
    ModuleNotFound,
    #[cfg(any(target_os = "linux", target_os = "android"))]
    #[error("IO error for file {0}")]
    IOError(#[from] std::io::Error),
    #[cfg(target_os = "macos")]
    #[error("Failure when requesting the task information")]
    TaskInfoError,
    #[cfg(target_os = "macos")]
    #[error("The task dyld information format is unknown or invalid")]
    ImageFormatError,
}

#[derive(Debug, Error)]
pub enum ReadError {
    #[cfg(target_os = "macos")]
    #[error("mach call failed")]
    MachError,
    #[cfg(any(target_os = "linux", target_os = "android"))]
    #[error("ptrace-specific error")]
    PtraceError(#[from] PtraceError),
    #[cfg(target_os = "windows")]
    #[error("ReadProcessMemory failed")]
    ReadProcessMemoryError,
    #[error("The requested read is too large")]
    TooLarge,
    #[cfg(any(target_os = "linux", target_os = "android"))]
    #[error("The remote executor failed")]
    RemoteError(#[from] process_backend::Error),
    #[cfg(any(target_os = "linux", target_os = "android"))]
    #[error("The remote executor returned a short read")]
    RemoteShortRead,
}

#[cfg(any(target_os = "linux", target_os = "android"))]
#[derive(Debug, Error)]
pub enum PtraceError {
    #[error("Could not read from the target process address space")]
    ReadError(#[source] std::io::Error),
    #[error("Could not trace the process")]
    TraceError(#[source] std::io::Error),
}
