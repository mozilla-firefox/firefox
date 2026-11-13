/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::{
    cmp::min,
    ffi::CString,
    fs,
    io::Error,
    mem::{size_of, MaybeUninit},
    os::fd::{IntoRawFd, OwnedFd},
    ptr::null_mut,
    slice,
};

use crate::{
    error::{ProcessReaderError, PtraceError, ReadError},
    ProcessHandle, ProcessReader,
};

use goblin::elf::{
    self,
    program_header::{PF_R, PT_NOTE},
    Elf, ProgramHeader,
};

use goblin::elf::note::Note;
use scroll::ctx::TryFromCtx;

use libc::{
    c_int, c_long, c_void, pid_t, ptrace, waitpid, __WALL, EINTR, PTRACE_ATTACH, PTRACE_DETACH,
    PTRACE_PEEKDATA,
};

use process_backend::{
    remote::{backend::Backend, io::UnixStream, transport::postcard},
    Backend as _, FileReader as _, ProcessReader as _,
};

pub type RemoteBackend = Backend<postcard::Backend<UnixStream, Vec<u8>>>;

impl ProcessReader {
    pub fn new(process: ProcessHandle) -> Result<ProcessReader, ProcessReaderError> {
        let pid: pid_t = process;

        ptrace_attach(pid)?;

        let mut status: i32 = 0;

        loop {
            let res = unsafe { waitpid(pid, &mut status as *mut _, __WALL) };
            if res < 0 {
                match get_errno() {
                    EINTR => continue,
                    _ => {
                        ptrace_detach(pid)?;
                        return Err(ProcessReaderError::WaitPidError);
                    }
                }
            } else {
                break;
            }
        }

        Ok(Self::Local(pid))
    }

    /// Read a process through a remote executor reachable via `socket`.
    /// `buffer_len` should match the size of the executor's output buffer for optimal performance
    pub fn new_remote(socket: OwnedFd, buffer_len: usize) -> Self {
        // SAFETY: by construction, `socket` is a valid file descriptor.
        let io = unsafe { UnixStream::from_raw_fd(socket.into_raw_fd()) };
        let transport = postcard::Backend::new(io, vec![0u8; buffer_len]);

        Self::Remote {
            backend: Backend::new(transport),
            buffer_len,
        }
    }

    fn read_maps(&self) -> Result<String, ProcessReaderError> {
        let (remote, buffer_len) = match self {
            Self::Local(pid) => return Ok(fs::read_to_string(format!("/proc/{pid}/maps"))?),
            Self::Remote {
                backend,
                buffer_len,
            } => (backend, *buffer_len),
        };

        let pid = remote.pid().map_err(ReadError::from)?;
        let path = CString::new(format!("/proc/{pid}/maps")).unwrap();
        let mut reader = remote.read_file(&path).map_err(ReadError::from)?;
        let mut contents = Vec::new();
        let mut buffer = vec![0u8; buffer_len];
        loop {
            match reader.read(&mut buffer).map_err(ReadError::from)? {
                0 => break,
                len => contents.extend_from_slice(&buffer[..len]),
            }
        }

        Ok(String::from_utf8_lossy(&contents).into_owned())
    }

    fn remote_read(remote: &RemoteBackend, src: usize, dst: &mut [u8]) -> Result<(), ReadError> {
        let reader = remote.process_reader();
        let mut offset = 0;
        while offset < dst.len() {
            let len = reader.read_at(src + offset, &mut dst[offset..])?;
            if len == 0 {
                return Err(ReadError::RemoteShortRead);
            }
            offset += len;
        }

        Ok(())
    }

    pub fn find_module(&self, module_name: &str) -> Result<usize, ProcessReaderError> {
        self.read_maps()?
            .lines()
            .map(parse_proc_maps_line)
            .filter_map(Result::ok)
            .find_map(|(name, address)| {
                let name = name?;
                if name == module_name {
                    return Some(address);
                }
                // Check whether the SO_NAME matches the module name.
                //
                // For now, only check the SO_NAME of Android APKS, because libraries may be mapped
                // directly from within an APK. See bug 1982902.
                (cfg!(target_os = "android")
                    && name.ends_with(".apk")
                    && self.so_name_eq(address, module_name))
                .then_some(address)
            })
            .ok_or(ProcessReaderError::ModuleNotFound)
    }

    fn read_elf_program_headers(
        &self,
        module_address: usize,
    ) -> Result<(goblin::container::Ctx, Vec<ProgramHeader>), ProcessReaderError> {
        let header_bytes = self.copy_array(module_address, size_of::<elf::Header>())?;
        let elf_header = Elf::parse_header(&header_bytes)?;
        let elf = Elf::lazy_parse(elf_header)?;

        let program_header_bytes = self.copy_array(
            module_address + (elf_header.e_phoff as usize),
            (elf_header.e_phnum as usize) * (elf_header.e_phentsize as usize),
        )?;

        let context = goblin::container::Ctx {
            container: elf.header.container()?,
            le: elf.header.endianness()?,
        };

        let headers = ProgramHeader::parse(
            &program_header_bytes,
            0,
            elf_header.e_phnum as usize,
            context,
        )?;
        Ok((context, headers))
    }

    fn so_name_eq(&self, module_address: usize, target_module_name: &str) -> bool {
        let name = match self.read_so_name(module_address) {
            Ok(n) => n,
            Err(e) => {
                log::debug!("error reading SO_NAME: {e}");
                return false;
            }
        };
        if name
            .to_str()
            .map(|s| s == target_module_name)
            .unwrap_or(false)
        {
            return true;
        }
        false
    }

    fn read_so_name(&self, module_address: usize) -> Result<std::ffi::CString, ProcessReaderError> {
        let (context, program_headers) = self.read_elf_program_headers(module_address)?;

        for program_header in program_headers {
            if program_header.p_type == elf::program_header::PT_DYNAMIC {
                // The dynamic segment contains SONAME information
                let dyn_address = module_address + program_header.p_vaddr as usize;
                let dyn_size = program_header.p_memsz as usize;
                let dyn_segment = self.copy_array(dyn_address, dyn_size)?;

                let mut soname_strtab_offset = None;
                let mut strtab_addr = None;
                let mut strtab_size = None;
                for dyn_ in dyn_iter::DynIter::new(&dyn_segment, context) {
                    let dyn_ = dyn_?;
                    match dyn_.d_tag {
                        elf::dynamic::DT_SONAME => soname_strtab_offset = Some(dyn_.d_val),
                        elf::dynamic::DT_STRTAB => strtab_addr = Some(dyn_.d_val),
                        elf::dynamic::DT_STRSZ => strtab_size = Some(dyn_.d_val),
                        _ => (),
                    }
                }

                if let (Some(offset), Some(addr), Some(size)) =
                    (soname_strtab_offset, strtab_addr, strtab_size)
                {
                    if offset < size {
                        return self
                            .copy_null_terminated_string(module_address + (addr + offset) as usize)
                            .map_err(|e| e.into());
                    }
                }
            }
        }

        Err(ProcessReaderError::SoNameNotFound)
    }

    pub fn find_program_note(
        &self,
        module_address: usize,
        note_type: u32,
        note_size: usize,
        note_name: &str,
    ) -> Result<usize, ProcessReaderError> {
        let (context, program_headers) = self.read_elf_program_headers(module_address)?;

        for program_header in program_headers {
            // We're looking for a note in the program headers, it needs to be
            // readable and it needs to be at least as large as the
            // requested size.
            if (program_header.p_type == PT_NOTE)
                && ((program_header.p_flags & PF_R) != 0
                    && (program_header.p_memsz as usize >= note_size))
            {
                // Iterate over the notes
                let notes_address = module_address + program_header.p_offset as usize;
                let mut notes_offset = 0;
                let notes_size = program_header.p_memsz as usize;
                let notes_bytes = self.copy_array(notes_address, notes_size)?;
                while notes_offset < notes_size {
                    if let Ok((note, size)) =
                        Note::try_from_ctx(&notes_bytes[notes_offset..notes_size], (4, context))
                    {
                        if note.n_type == note_type && note.name == note_name {
                            return Ok(notes_address + notes_offset);
                        }

                        notes_offset += size;
                    } else {
                        break;
                    }
                }
            }
        }

        Err(ProcessReaderError::NoteNotFound)
    }

    pub fn copy_object_shallow<T>(&self, src: usize) -> Result<MaybeUninit<T>, ReadError> {
        let data = self.copy_array(src, size_of::<T>())?;
        let mut object = MaybeUninit::<T>::uninit();
        let uninitialized_object = uninit_as_bytes_mut(&mut object);

        for (index, &value) in data.iter().enumerate() {
            uninitialized_object[index].write(value);
        }

        Ok(object)
    }

    pub fn copy_object<T>(&self, src: usize) -> Result<T, ReadError> {
        self.copy_object_shallow(src)
            .map(|object| unsafe { object.assume_init() })
    }

    pub fn copy_array<T>(&self, src: usize, num: usize) -> Result<Vec<T>, ReadError> {
        let mut array = Vec::<MaybeUninit<T>>::new();
        array
            .try_reserve_exact(num)
            .map_err(|_| ReadError::TooLarge)?;
        let num_bytes = num * size_of::<T>();
        let mut array_buffer = array.as_mut_ptr() as *mut u8;

        match self {
            Self::Local(pid) => {
                let mut index = 0;
                while index < num_bytes {
                    let word = ptrace_read(*pid, src + index)?;
                    let len = min(size_of::<c_long>(), num_bytes - index);
                    let word_as_bytes = word.to_ne_bytes();
                    for &byte in word_as_bytes.iter().take(len) {
                        unsafe {
                            array_buffer.write(byte);
                            array_buffer = array_buffer.add(1);
                        }
                    }

                    index += size_of::<c_long>();
                }
            }
            Self::Remote { backend, .. } => {
                // SAFETY: We reserved `num_bytes` bytes, which we zero before use.
                let bytes = unsafe {
                    array_buffer.write_bytes(0, num_bytes);
                    slice::from_raw_parts_mut(array_buffer, num_bytes)
                };
                Self::remote_read(backend, src, bytes)?;
            }
        }

        unsafe {
            array.set_len(num);
            Ok(std::mem::transmute::<
                std::vec::Vec<std::mem::MaybeUninit<T>>,
                std::vec::Vec<T>,
            >(array))
        }
    }
}

impl Drop for ProcessReader {
    fn drop(&mut self) {
        if let Self::Local(pid) = *self {
            let _ignored = ptrace_detach(pid);
        }
    }
}

mod dyn_iter {
    use goblin::container::Ctx;
    use goblin::elf::dynamic::{Dyn, DT_NULL};
    use goblin::error::Error;
    use scroll::Pread;

    pub struct DynIter<'a> {
        data: &'a [u8],
        offset: usize,
        ctx: Ctx,
    }

    impl<'a> DynIter<'a> {
        pub fn new(data: &'a [u8], ctx: Ctx) -> Self {
            DynIter {
                data,
                offset: 0,
                ctx,
            }
        }
    }

    impl Iterator for DynIter<'_> {
        type Item = Result<Dyn, Error>;

        fn next(&mut self) -> Option<Self::Item> {
            let dyn_: Dyn = match self.data.gread_with(&mut self.offset, self.ctx) {
                Ok(v) => v,
                Err(e) => return Some(Err(e)),
            };
            if dyn_.d_tag == DT_NULL {
                None
            } else {
                Some(Ok(dyn_))
            }
        }
    }
}

fn parse_proc_maps_line(line: &str) -> Result<(Option<String>, usize), ProcessReaderError> {
    let mut splits = line.trim().splitn(6, ' ');
    let address_str = splits
        .next()
        .ok_or(ProcessReaderError::ProcMapsParseError)?;
    let _perms_str = splits
        .next()
        .ok_or(ProcessReaderError::ProcMapsParseError)?;
    let _offset_str = splits
        .next()
        .ok_or(ProcessReaderError::ProcMapsParseError)?;
    let _dev_str = splits
        .next()
        .ok_or(ProcessReaderError::ProcMapsParseError)?;
    let _inode_str = splits
        .next()
        .ok_or(ProcessReaderError::ProcMapsParseError)?;
    let path_str = splits
        .next()
        .ok_or(ProcessReaderError::ProcMapsParseError)?;

    let address = get_proc_maps_address(address_str)?;

    // Note that we don't care if the mapped file has been deleted because
    // we're reading everything from memory.
    let name = path_str
        .trim_end_matches(" (deleted)")
        .rsplit('/')
        .next()
        .map(String::from);

    Ok((name, address))
}

fn get_proc_maps_address(addresses: &str) -> Result<usize, ProcessReaderError> {
    let begin = addresses
        .split('-')
        .next()
        .ok_or(ProcessReaderError::ProcMapsParseError)?;
    usize::from_str_radix(begin, 16).map_err(ProcessReaderError::from)
}

fn uninit_as_bytes_mut<T>(elem: &mut MaybeUninit<T>) -> &mut [MaybeUninit<u8>] {
    // SAFETY: MaybeUninit<u8> is always valid, even for padding bytes
    unsafe { slice::from_raw_parts_mut(elem.as_mut_ptr() as *mut MaybeUninit<u8>, size_of::<T>()) }
}

/***********************************************************************
 ***** libc helpers                                                *****
 ***********************************************************************/

fn get_errno() -> c_int {
    #[cfg(target_os = "linux")]
    unsafe {
        *libc::__errno_location()
    }
    #[cfg(target_os = "android")]
    unsafe {
        *libc::__errno()
    }
}

fn clear_errno() {
    #[cfg(target_os = "linux")]
    unsafe {
        *libc::__errno_location() = 0;
    }
    #[cfg(target_os = "android")]
    unsafe {
        *libc::__errno() = 0;
    }
}

#[derive(Clone, Copy)]
enum PTraceOperation {
    Attach,
    Detach,
    PeekData,
}

#[cfg(all(target_os = "linux", target_env = "gnu"))]
type PTraceOperationNative = libc::c_uint;
#[cfg(all(target_os = "linux", target_env = "musl"))]
type PTraceOperationNative = libc::c_int;
#[cfg(target_os = "android")]
type PTraceOperationNative = c_int;

impl From<PTraceOperation> for PTraceOperationNative {
    fn from(val: PTraceOperation) -> Self {
        match val {
            PTraceOperation::Attach => PTRACE_ATTACH,
            PTraceOperation::Detach => PTRACE_DETACH,
            PTraceOperation::PeekData => PTRACE_PEEKDATA,
        }
    }
}

fn ptrace_attach(pid: pid_t) -> Result<(), PtraceError> {
    ptrace_helper(pid, PTraceOperation::Attach, 0).map(|_r| ())
}

fn ptrace_detach(pid: pid_t) -> Result<(), PtraceError> {
    ptrace_helper(pid, PTraceOperation::Detach, 0).map(|_r| ())
}

fn ptrace_read(pid: libc::pid_t, addr: usize) -> Result<c_long, PtraceError> {
    ptrace_helper(pid, PTraceOperation::PeekData, addr)
}

fn ptrace_helper(pid: pid_t, op: PTraceOperation, addr: usize) -> Result<c_long, PtraceError> {
    clear_errno();
    let result = unsafe { ptrace(op.into(), pid, addr, null_mut::<c_void>()) };

    if result == -1 {
        let errno = get_errno();
        if errno != 0 {
            let error = match op {
                PTraceOperation::Attach => PtraceError::TraceError(Error::from_raw_os_error(errno)),
                PTraceOperation::Detach => PtraceError::TraceError(Error::from_raw_os_error(errno)),
                PTraceOperation::PeekData => {
                    PtraceError::ReadError(Error::from_raw_os_error(errno))
                }
            };
            Err(error)
        } else {
            Ok(result)
        }
    } else {
        Ok(result)
    }
}
