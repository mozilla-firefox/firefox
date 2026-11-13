use super::super::remote;
use crate::wrapper::Stat;
use crate::{Error, ProcessReaderKind, Result};
use core::cell::{Cell, RefCell};
use remote::{transport, wire};
use wire::{Operation, ops};

#[derive(Debug)]
pub struct Backend<T: transport::Backend> {
    transport_state: RefCell<TransportState<T>>,
    pid: Cell<Option<libc::pid_t>>,
    max_response_output_len: usize,
}

#[allow(clippy::large_enum_variant)]
#[derive(Debug)]
enum TransportState<T> {
    Ready(T),
    Poisoned,
}

impl<T> TransportState<T> {
    fn ready(&mut self) -> Result<&mut T> {
        match self {
            TransportState::Ready(transport) => Ok(transport),
            TransportState::Poisoned => Err(Error::TransportPoisoned),
        }
    }
}

impl<T: transport::Backend> Backend<T> {
    pub fn new(transport: T) -> Self {
        let max_response_output_len = transport.max_response_output_len();
        Self {
            transport_state: RefCell::new(TransportState::Ready(transport)),
            pid: Cell::new(None),
            max_response_output_len,
        }
    }

    fn empty_request<Op, Out>(&self) -> Result<Out>
    where
        Op: for<'args, 'output> Operation<Args<'args> = (), Output<'output> = Out>,
        Out: 'static,
    {
        self.simple_request::<Op, Out>(())
    }

    fn simple_request<Op, Out>(&self, args: Op::Args<'_>) -> Result<Out>
    where
        Op: for<'output> Operation<Output<'output> = Out>,
        Out: 'static,
    {
        self.mapped_request::<Op, _, _>(args, Ok)
    }

    fn mapped_request<Op, F, U>(&self, args: Op::Args<'_>, map_fn: F) -> Result<U>
    where
        Op: Operation,
        F: for<'output> FnOnce(Op::Output<'output>) -> Result<U>,
    {
        let mut transport_state = self.transport_state.borrow_mut();
        let transport = transport_state.ready()?;

        let e = match Op::request(transport, args) {
            Ok(response) => {
                let output = response.map_err(Error::Executor)?;
                return map_fn(output);
            }
            Err(e) => e,
        };

        *transport_state = TransportState::Poisoned;
        Err(Error::Transport(e))
    }
}

impl<T: transport::Backend> crate::Backend for Backend<T> {
    type ProcessReader<'a>
        = ProcessReader<'a, T>
    where
        Self: 'a;

    type FileReader<'a>
        = FileReader<'a, T>
    where
        Self: 'a;

    type DirReader<'a>
        = DirReader<'a, T>
    where
        Self: 'a;

    type MappedModuleMemoryReader<'a>
        = MappedModuleMemoryReader<'a, T>
    where
        Self: 'a;

    fn pid(&self) -> Result<libc::pid_t> {
        match self.pid.get() {
            Some(pid) => Ok(pid),
            None => {
                let pid = self.empty_request::<ops::GetPid, _>()?;
                self.pid.set(Some(pid));
                Ok(pid)
            }
        }
    }

    fn process_reader<'a>(&'a self) -> Self::ProcessReader<'a> {
        ProcessReader { backend: self }
    }

    fn stop_process(&self) -> Result<()> {
        self.empty_request::<ops::StopProcess, _>()
    }

    fn continue_process(&self) -> Result<()> {
        self.empty_request::<ops::ContinueProcess, _>()
    }

    fn suspend_thread(&self, tid: libc::pid_t) -> Result<()> {
        self.simple_request::<ops::SuspendThread, _>(tid)
    }

    fn resume_thread(&self, tid: libc::pid_t) -> Result<()> {
        self.simple_request::<ops::ResumeThread, _>(tid)
    }

    fn map_module_into_memory<'a>(
        &'a self,
        path: &core::ffi::CStr,
        offset: u64,
    ) -> Result<Self::MappedModuleMemoryReader<'a>> {
        self.mapped_request::<ops::MappedModuleMemoryReaderOpen, _, _>(
            wire::MappedModuleMemoryReaderOpenArgs {
                path: path.into(),
                offset,
            },
            |handle| {
                Ok(MappedModuleMemoryReader {
                    backend: self,
                    handle,
                })
            },
        )
    }

    fn stat_file(&self, path: &core::ffi::CStr) -> Result<Stat> {
        self.simple_request::<ops::StatFile, _>(path.into())
    }

    fn read_file<'a>(&'a self, path: &core::ffi::CStr) -> Result<Self::FileReader<'a>> {
        self.mapped_request::<ops::FileReaderOpen, _, _>(path.into(), |handle| {
            Ok(FileReader {
                backend: self,
                handle,
            })
        })
    }

    fn read_dir<'a>(&'a self, path: &core::ffi::CStr) -> Result<Self::DirReader<'a>> {
        self.mapped_request::<ops::DirReaderOpen, _, _>(path.into(), |handle| {
            Ok(DirReader {
                backend: self,
                handle,
            })
        })
    }

    fn read_link(&self, path: &core::ffi::CStr, buf: &mut [u8]) -> Result<usize> {
        self.mapped_request::<ops::ReadLink, _, _>(
            wire::ReadLinkArgs {
                path: path.into(),
                requested_len: usize::min(buf.len(), self.max_response_output_len),
            },
            |bytes| {
                buf[0..bytes.len()].copy_from_slice(bytes);
                Ok(bytes.len())
            },
        )
    }

    fn get_gen_regs(&self, tid: libc::pid_t) -> Result<crate::regs::GenRegs> {
        self.simple_request::<ops::GetGenRegs, _>(tid)
    }

    fn get_fp_regs(&self, tid: libc::pid_t) -> Result<crate::regs::FpRegs> {
        self.simple_request::<ops::GetFpRegs, _>(tid)
    }

    #[cfg(target_arch = "x86")]
    fn get_fpx_regs(&self, tid: libc::pid_t) -> Result<crate::regs::FpxRegs> {
        self.simple_request::<ops::GetFpxRegs, _>(tid)
    }

    #[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
    fn ptrace_peekuser(&self, addr: usize) -> Result<[u8; crate::PTRACE_DATA_LEN]> {
        self.simple_request::<ops::PtracePeekUser, _>(addr)
    }

    fn force_process_reader_kind(&mut self, kind: ProcessReaderKind) -> Result<()> {
        self.simple_request::<ops::ForceProcessReaderKind, _>(kind)
    }

    #[cfg(feature = "testing")]
    fn fail_one_syscall_with(&self, errno: core::ffi::c_int) {
        let _result = self.simple_request::<ops::FailOneSyscallWith, _>(errno);
    }
}

impl<T: transport::Backend> Drop for Backend<T> {
    fn drop(&mut self) {
        if let Err(e) = self.empty_request::<ops::Quit, _>() {
            report_drop_failed!("failed to drop backend: {e:#?}");
        }
    }
}

#[derive(Debug)]
pub struct ProcessReader<'a, T: transport::Backend> {
    backend: &'a Backend<T>,
}

impl<'a, T: transport::Backend> crate::ProcessReader for ProcessReader<'a, T> {
    fn read_at(&self, address: usize, buf: &mut [u8]) -> Result<usize> {
        self.backend
            .mapped_request::<ops::ProcessReaderReadAt, _, _>(
                wire::ProcessReaderReadAtArgs {
                    address,
                    requested_len: usize::min(buf.len(), self.backend.max_response_output_len),
                },
                |bytes| {
                    buf[0..bytes.len()].copy_from_slice(bytes);
                    Ok(bytes.len())
                },
            )
    }
}

#[derive(Debug)]
pub struct FileReader<'a, T: transport::Backend> {
    backend: &'a Backend<T>,
    handle: usize,
}

impl<'a, T: transport::Backend> crate::FileReader for FileReader<'a, T> {
    fn read(&mut self, buf: &mut [u8]) -> Result<usize> {
        self.backend.mapped_request::<ops::FileReaderRead, _, _>(
            wire::FileReaderArgs {
                handle: self.handle,
                requested_len: usize::min(buf.len(), self.backend.max_response_output_len),
            },
            |bytes| {
                buf[0..bytes.len()].copy_from_slice(bytes);
                Ok(bytes.len())
            },
        )
    }
}

impl<'a, T: transport::Backend> Drop for FileReader<'a, T> {
    fn drop(&mut self) {
        if let Err(e) = self
            .backend
            .simple_request::<ops::FileReaderClose, _>(self.handle)
        {
            report_drop_failed!("failed to close FileReader: {e:#?}");
        }
    }
}

#[derive(Debug)]
pub struct DirReader<'a, T: transport::Backend> {
    backend: &'a Backend<T>,
    handle: usize,
}

impl<'a, T: transport::Backend> crate::DirReader for DirReader<'a, T> {
    fn read_next_name(&mut self, buf: &mut [u8]) -> Result<usize> {
        self.backend
            .mapped_request::<ops::DirReaderRead, _, _>(self.handle, |maybe_bytes| {
                if let Some(bytes) = maybe_bytes {
                    buf[0..bytes.len()].copy_from_slice(bytes);
                    Ok(bytes.len())
                } else {
                    Ok(0)
                }
            })
    }
}

impl<'a, T: transport::Backend> Drop for DirReader<'a, T> {
    fn drop(&mut self) {
        if let Err(e) = self
            .backend
            .simple_request::<ops::DirReaderClose, _>(self.handle)
        {
            report_drop_failed!("failed to close DirReader: {e:#?}");
        }
    }
}

#[derive(Debug)]
pub struct MappedModuleMemoryReader<'a, T: transport::Backend> {
    backend: &'a Backend<T>,
    handle: usize,
}

impl<'a, T: transport::Backend> crate::MappedModuleMemoryReader
    for MappedModuleMemoryReader<'a, T>
{
    fn read_at(&self, offset: usize, buf: &mut [u8]) -> Result<usize> {
        self.backend
            .mapped_request::<ops::MappedModuleMemoryReaderReadAt, _, _>(
                wire::MappedModuleMemoryReaderReadAtArgs {
                    handle: self.handle,
                    offset,
                    requested_len: usize::min(buf.len(), self.backend.max_response_output_len),
                },
                |bytes| {
                    buf[0..bytes.len()].copy_from_slice(bytes);
                    Ok(bytes.len())
                },
            )
    }

    fn len(&self) -> Result<usize> {
        self.backend
            .simple_request::<ops::MappedModuleMemoryReaderLen, _>(self.handle)
    }

    fn is_empty(&self) -> Result<bool> {
        self.len().map(|len| len == 0)
    }
}

impl<'a, T: transport::Backend> Drop for MappedModuleMemoryReader<'a, T> {
    fn drop(&mut self) {
        if let Err(e) = self
            .backend
            .simple_request::<ops::MappedModuleMemoryReaderClose, _>(self.handle)
        {
            report_drop_failed!("failed to close MappedModuleMemoryReader: {e:#?}");
        }
    }
}
