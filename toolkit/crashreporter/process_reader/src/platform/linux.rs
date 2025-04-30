/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::{
    cmp::min,
    fs::File,
    io::{BufRead, BufReader, Error},
    mem::{size_of, MaybeUninit},
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
    c_int, c_long, c_void, pid_t, ptrace, waitpid, EINTR, PTRACE_ATTACH, PTRACE_DETACH,
    PTRACE_PEEKDATA, __WALL,
};

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

        Ok(ProcessReader { process: pid })
    }

    pub fn find_module(&self, module_name: &str) -> Result<usize, ProcessReaderError> {
        let maps_file = File::open(format!("/proc/{}/maps", self.process))?;

        BufReader::new(maps_file)
            .lines()
            .map_while(Result::ok)
            .map(|line| parse_proc_maps_line(&line))
            .filter_map(Result::ok)
            .find_map(|(name, address)| {
                if name.is_some_and(|name| name.eq(module_name)) {
                    Some(address)
                } else {
                    None
                }
            })
            .ok_or(ProcessReaderError::ModuleNotFound)
    }

    pub fn find_program_note(
        &self,
        module_address: usize,
        note_type: u32,
        note_size: usize,
        note_name: &str,
    ) -> Result<usize, ProcessReaderError> {
        let header_bytes = self.copy_array(module_address, size_of::<elf::Header>())?;
        let elf_header = Elf::parse_header(&header_bytes)?;

        let program_header_bytes = self.copy_array(
            module_address + (elf_header.e_phoff as usize),
            (elf_header.e_phnum as usize) * (elf_header.e_phentsize as usize),
        )?;

        let mut elf = Elf::lazy_parse(elf_header)?;
        let context = goblin::container::Ctx {
            container: elf.header.container()?,
            le: elf.header.endianness()?,
        };

        elf.program_headers = ProgramHeader::parse(
            &program_header_bytes,
            0,
            elf_header.e_phnum as usize,
            context,
        )?;

        self.find_note_in_headers(
            &elf,
            module_address,
            note_type,
            note_size,
            note_name,
            context,
        )
    }

    fn find_note_in_headers(
        &self,
        elf: &Elf,
        address: usize,
        note_type: u32,
        note_size: usize,
        note_name: &str,
        context: goblin::container::Ctx,
    ) -> Result<usize, ProcessReaderError> {
        for program_header in elf.program_headers.iter() {
            // We're looking for a note in the program headers, it needs to be
            // readable and it needs to be at least as large as the
            // requested size.
            if (program_header.p_type == PT_NOTE)
                && ((program_header.p_flags & PF_R) != 0
                    && (program_header.p_memsz as usize >= note_size))
            {
                // Iterate over the notes
                let notes_address = address + program_header.p_offset as usize;
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
        let mut array = Vec::<MaybeUninit<T>>::with_capacity(num);
        let num_bytes = num * size_of::<T>();
        let mut array_buffer = array.as_mut_ptr() as *mut u8;
        let mut index = 0;

        while index < num_bytes {
            let word = ptrace_read(self.process, src + index)?;
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
        let _ignored = ptrace_detach(self.process);
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
