#![no_std]
#![cfg(any(target_os = "linux", target_os = "android"))]

use core::ffi::CStr;
use regs::*;

pub use drop_fail_handler::Handler as DropFailHandler;
pub use wrapper::Stat;

#[macro_use]
mod drop_fail_handler;

pub mod local;
pub mod regs;
pub mod remote;

mod wrapper;

pub type Result<T> = core::result::Result<T, Error>;

#[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
pub const PTRACE_DATA_LEN: usize = core::mem::size_of::<libc::c_long>();

pub trait Backend: core::fmt::Debug {
    type ProcessReader<'a>: ProcessReader
    where
        Self: 'a;
    type FileReader<'a>: FileReader
    where
        Self: 'a;
    type DirReader<'a>: DirReader
    where
        Self: 'a;
    type MappedModuleMemoryReader<'a>: MappedModuleMemoryReader
    where
        Self: 'a;

    fn pid(&self) -> Result<libc::pid_t>;
    fn process_reader<'a>(&'a self) -> Self::ProcessReader<'a>;
    fn stop_process(&self) -> Result<()>;
    fn continue_process(&self) -> Result<()>;
    fn suspend_thread(&self, tid: libc::pid_t) -> Result<()>;
    fn resume_thread(&self, tid: libc::pid_t) -> Result<()>;
    fn map_module_into_memory<'a>(
        &'a self,
        path: &CStr,
        offset: u64,
    ) -> Result<Self::MappedModuleMemoryReader<'a>>;
    fn stat_file(&self, path: &CStr) -> Result<Stat>;
    fn read_file<'a>(&'a self, path: &CStr) -> Result<Self::FileReader<'a>>;
    fn read_dir<'a>(&'a self, path: &CStr) -> Result<Self::DirReader<'a>>;
    fn read_link(&self, path: &CStr, buf: &mut [u8]) -> Result<usize>;
    fn get_gen_regs(&self, tid: libc::pid_t) -> Result<GenRegs>;
    fn get_fp_regs(&self, tid: libc::pid_t) -> Result<FpRegs>;

    #[cfg(target_arch = "x86")]
    fn get_fpx_regs(&self, tid: libc::pid_t) -> Result<FpxRegs>;

    #[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
    fn ptrace_peekuser(&self, addr: usize) -> Result<[u8; PTRACE_DATA_LEN]>;

    fn force_process_reader_kind(&mut self, kind: ProcessReaderKind) -> Result<()>;

    #[cfg(feature = "testing")]
    fn fail_one_syscall_with(&self, errno: core::ffi::c_int);
}

pub trait FileReader: core::fmt::Debug {
    fn read(&mut self, buf: &mut [u8]) -> Result<usize>;
}

pub trait DirReader: core::fmt::Debug {
    fn read_next_name(&mut self, buf: &mut [u8]) -> Result<usize>;
}

pub trait ProcessReader: core::fmt::Debug {
    fn read_at(&self, address: usize, buf: &mut [u8]) -> Result<usize>;
}

pub trait MappedModuleMemoryReader: core::fmt::Debug {
    fn read_at(&self, offset: usize, buf: &mut [u8]) -> Result<usize>;
    fn len(&self) -> Result<usize>;
    fn is_empty(&self) -> Result<bool>;
}

#[derive(Debug, serde::Deserialize, serde::Serialize)]
pub enum ProcessReaderKind {
    Unspecified,
    VirtualMem,
    File,
    Ptrace,
}

#[derive(Debug, thiserror::Error, serde::Deserialize, serde::Serialize)]
pub enum Error {
    #[error("there was a failure making a direct syscall")]
    Local(#[source] local::Error),
    #[error("the remote executor returned an error")]
    Executor(#[source] remote::executor::Error),
    #[error("there was an error transporting the request to/from the remote executor")]
    Transport(#[source] remote::transport::BackendError),
    #[error("buffer was too small to contain the result")]
    BufferTooSmall,
    #[error("the remote executor is unusable after a previous error")]
    TransportPoisoned,
}
