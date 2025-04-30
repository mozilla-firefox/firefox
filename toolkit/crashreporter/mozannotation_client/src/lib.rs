/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::{
    alloc::{self, Layout},
    cmp::min,
    ffi::{c_char, c_void, CStr},
    ptr::{copy_nonoverlapping, null_mut},
    sync::Mutex,
};

#[cfg(any(target_os = "linux", target_os = "android"))]
use std::arch::global_asm;

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum AnnotationContents {
    Empty,
    NSCStringPointer,
    CStringPointer,
    CString,
    ByteBuffer(u32),
    OwnedByteBuffer(u32),
}
#[repr(C)]
pub struct Annotation {
    pub id: u32,
    pub contents: AnnotationContents,
    pub address: usize,
}

impl Drop for Annotation {
    fn drop(&mut self) {
        match self.contents {
            AnnotationContents::OwnedByteBuffer(len) => {
                if (self.address != 0) && (len > 0) {
                    let align = min(usize::next_power_of_two(len as usize), 32);
                    unsafe {
                        let layout = Layout::from_size_align_unchecked(len as usize, align);
                        alloc::dealloc(self.address as *mut u8, layout);
                    }
                }
            }
            _ => {
                // Nothing to do
            }
        };
    }
}

pub struct AnnotationTable {
    data: Vec<Annotation>,
    magic_number: u32,
}

impl AnnotationTable {
    const fn new() -> AnnotationTable {
        AnnotationTable {
            data: Vec::new(),
            magic_number: ANNOTATION_TYPE,
        }
    }

    pub const fn verify(&self) -> bool {
        self.magic_number == ANNOTATION_TYPE
    }

    pub fn get_ptr(&self) -> *const Annotation {
        self.data.as_ptr()
    }

    pub fn len(&self) -> usize {
        self.data.len()
    }

    pub fn is_empty(&self) -> bool {
        self.data.is_empty()
    }
}

pub type AnnotationMutex = Mutex<AnnotationTable>;

#[cfg(target_os = "windows")]
#[link_section = "mozannot"]
static MOZANNOTATIONS: AnnotationMutex = Mutex::new(AnnotationTable::new());
#[cfg(any(target_os = "linux", target_os = "android"))]
static MOZANNOTATIONS: AnnotationMutex = Mutex::new(AnnotationTable::new());
#[cfg(target_os = "macos")]
#[link_section = "__DATA,mozannotation"]
static MOZANNOTATIONS: AnnotationMutex = Mutex::new(AnnotationTable::new());

#[no_mangle]
unsafe fn mozannotation_get() -> *const AnnotationMutex {
    &MOZANNOTATIONS as _
}

#[cfg(any(target_os = "linux", target_os = "android"))]
extern "C" {
    pub static MOZANNOTATION_NOTE_REFERENCE: &'static u32;
    pub static __ehdr_start: [u8; 0];
}

#[cfg(target_os = "windows")]
pub const ANNOTATION_SECTION: &[u8; 8] = b"mozannot";

#[cfg(target_os = "macos")]
pub const ANNOTATION_SECTION: &[u8; 16] = b"mozannotation\0\0\0";

// TODO: Use the following constants in the assembly below when constant
// expressions are stabilized: https://github.com/rust-lang/rust/issues/93332
#[cfg(any(target_os = "linux", target_os = "android"))]
const _ANNOTATION_NOTE_ALIGNMENT: u32 = 4;
#[cfg(any(target_os = "linux", target_os = "android"))]
pub const ANNOTATION_NOTE_NAME: &str = "mozannotation";
pub const ANNOTATION_TYPE: u32 = u32::from_le_bytes(*b"MOZA");

// We use the crashpad crash info trick here. We create a program note which
// we'll use to find the location of the MOZANNOTATIONS static. Since program
// headers are always available we'll always be able to find this note in the
// memory of the crashed program, even if it's stripped or the backing file on
// disk has been deleted.
//
// We'll set the note type and name so we can easily recognize it (see the
// constants above). In the note's desc field we'll have the linker store the
// offset between the address of the MOZANNOTATIONS static and the desc field
// itself.
//
// At runtime we'll localize the note in the target process' memory, find the
// address of the `desc` field, load its contents (that is the offset we stored
// at link time) and add them together. The resulting address is the location of
// the MOZANNOTATIONS static in memory.
//
// When elfhack is used, the note might be moved after the aforementioned offset
// is calculated, without it being updated. To compensate for this we store the
// offset between the `ehdr` field and the ELF header. At runtime we can
// use this offset to adjust for the shift of the `desc` field.
#[cfg(all(
    target_pointer_width = "64",
    any(target_os = "linux", target_os = "android")
))]
global_asm!(
    // The section holding the note will be called '.note.moz.annotation'. We
    // create a program note that's allocated ('a' option) in the target binary
    // so that it's loaded into memory.
    "  .section .note.moz.annotation,\"a\",%note",
    // Note alignment must be 4 bytes because that's the default alignment for
    // that section. If a different alignment is chosen the note will end up in
    // its own section which we don't want.
    "  .balign 4", // TODO: _ANNOTATION_NOTE_ALIGNMENT
    "MOZANNOTATION_NOTE:",
    "  .long name_end - name", // size in bytes of the note's name
    "  .long desc_end - desc", // size in bytes of the note's desc field
    "  .long 0x415a4f4d",      // TODO: _ANNOTATION_TYPE, MOZA in reverse
    "name:",
    "  .asciz \"mozannotation\"", // TODO: _ANNOTATION_NOTE_NAME
    "name_end:",
    "  .balign 4", // TODO: _ANNOTATION_NOTE_ALIGNMENT
    "desc:",
    "  .quad {mozannotation_symbol} - desc",
    "ehdr:",
    "  .quad {__ehdr_start} - ehdr",
    "desc_end:",
    "  .size MOZANNOTATION_NOTE, .-MOZANNOTATION_NOTE",
    mozannotation_symbol = sym MOZANNOTATIONS,
    __ehdr_start = sym __ehdr_start
);

// The following global_asm!() expressions for other targets because Rust's
// support for putting statements within expressions is still experimental-only.
// Once https://github.com/rust-lang/rust/issues/15701 is fixed this can be
// folded in the `global_asm!()` statement above.

#[cfg(all(
    target_pointer_width = "32",
    any(target_os = "linux", target_os = "android")
))]
global_asm!(
    "  .section .note.moz.annotation,\"a\",%note",
    "  .balign 4",
    "MOZANNOTATION_NOTE:",
    "  .long name_end - name",
    "  .long desc_end - desc",
    "  .long 0x415a4f4d",
    "name:",
    "  .asciz \"mozannotation\"",
    "name_end:",
    "  .balign 4",
    "desc:",
    "  .long {mozannotation_symbol} - desc",
    "ehdr:",
    "  .long {__ehdr_start} - ehdr",
    "desc_end:",
    "  .size MOZANNOTATION_NOTE, .-MOZANNOTATION_NOTE",
    mozannotation_symbol = sym MOZANNOTATIONS,
    __ehdr_start = sym __ehdr_start
);

#[cfg(all(
    any(target_os = "linux", target_os = "android"),
    not(target_arch = "arm")
))]
global_asm!(
    // MOZANNOTATION_NOTE can't be referenced directly because the relocation
    // used to make the reference may require that the address be 8-byte aligned
    // and notes must have 4-byte alignment.
    "  .section .rodata,\"a\",%progbits",
    "  .balign 8",
    // .globl indicates that it's available to link against other .o files.
    // .hidden indicates that it will not appear in the executable's symbol
    // table.
    "  .globl",
    "  .hidden MOZANNOTATION_NOTE_REFERENCE",
    "  .type MOZANNOTATION_NOTE_REFERENCE, %object",
    "MOZANNOTATION_NOTE_REFERENCE:",
    // The value of this quad isn't important. It exists to reference
    // MOZANNOTATION_NOTE, causing the linker to include the note into the
    // binary linking the mozannotation_client crate. The subtraction from name
    // is a convenience to allow the value to be computed statically.
    "  .quad name - MOZANNOTATION_NOTE",
);

// In theory we could have used the statement above for ARM targets but for some
// reason the current rust compiler rejects the .quad directive. As with the other
// duplicate code above we could replace this with a single conditional line once
// once https://github.com/rust-lang/rust/issues/15701 is fixed.

#[cfg(all(any(target_os = "linux", target_os = "android"), target_arch = "arm"))]
global_asm!(
    "  .section .rodata,\"a\",%progbits",
    "  .balign 8",
    "  .globl",
    "  .hidden MOZANNOTATION_NOTE_REFERENCE",
    "  .type MOZANNOTATION_NOTE_REFERENCE, %object",
    "MOZANNOTATION_NOTE_REFERENCE:",
    "  .long name - MOZANNOTATION_NOTE",
);

/// This structure mirrors the contents of the note declared above in the
/// assembly blocks. It is used to copy the contents of the note out of the
/// target process.
#[cfg(any(target_os = "linux", target_os = "android"))]
#[allow(dead_code)]
#[repr(C, packed(4))]
pub struct MozAnnotationNote {
    pub namesz: u32,
    pub descsz: u32,
    pub note_type: u32,
    pub name: [u8; 16], // "mozannotation" plus padding to next 4-bytes boundary
    pub desc: usize,
    pub ehdr: isize,
}

fn store_annotation<T>(id: u32, contents: AnnotationContents, address: *const T) -> *const T {
    let address = match contents {
        AnnotationContents::OwnedByteBuffer(len) => {
            if !address.is_null() && (len > 0) {
                // Copy the contents of this annotation, we'll own the copy
                let len = len as usize;
                let align = min(usize::next_power_of_two(len), 32);
                unsafe {
                    let layout = Layout::from_size_align_unchecked(len, align);
                    let src = address as *mut u8;
                    let dst = alloc::alloc(layout);
                    copy_nonoverlapping(src, dst, len);
                    dst
                }
            } else {
                null_mut()
            }
        }
        _ => address as *mut u8,
    };

    let annotations = &mut MOZANNOTATIONS.lock().unwrap().data;
    let old = if let Some(existing) = annotations.iter_mut().find(|e| e.id == id) {
        let old = match existing.contents {
            AnnotationContents::OwnedByteBuffer(len) => {
                // If we owned the previous value of this annotation we must free it.
                if (existing.address != 0) && (len > 0) {
                    let len = len as usize;
                    let align = min(usize::next_power_of_two(len), 32);
                    unsafe {
                        let layout = Layout::from_size_align_unchecked(len, align);
                        alloc::dealloc(existing.address as *mut u8, layout);
                    }
                }
                null_mut::<T>()
            }
            _ => existing.address as *mut T,
        };

        existing.contents = contents;
        existing.address = address as usize;
        old
    } else {
        annotations.push(Annotation {
            id,
            contents,
            address: address as usize,
        });
        null_mut::<T>()
    };

    old
}

// Dummy definition so that we don't have to import the nsstring crate.
#[allow(non_camel_case_types)]
pub struct nsCString {}

/// Register a pointer to an nsCString string.
///
/// Returns the value of the previously registered annotation or null.
///
/// This function will be exposed to C++
#[no_mangle]
pub extern "C" fn mozannotation_register_nscstring(
    id: u32,
    address: *const nsCString,
) -> *const nsCString {
    store_annotation(id, AnnotationContents::NSCStringPointer, address)
}

/// Create a copy of the provided string with a specified size that will be
/// owned by the crate, and register a pointer to it.
///
/// This function will be exposed to C++
#[no_mangle]
pub extern "C" fn mozannotation_record_nscstring_from_raw_parts(
    id: u32,
    address: *const u8,
    size: usize,
) {
    store_annotation(
        id,
        AnnotationContents::OwnedByteBuffer(size as u32),
        address,
    );
}

/// Register a pointer to a pointer to a nul-terminated string.
///
/// Returns the value of the previously registered annotation or null.
///
/// This function will be exposed to C++
#[no_mangle]
pub extern "C" fn mozannotation_register_cstring_ptr(
    id: u32,
    address: *const *const c_char,
) -> *const *const c_char {
    store_annotation(id, AnnotationContents::CStringPointer, address)
}

/// Register a pointer to a nul-terminated string.
///
/// Returns the value of the previously registered annotation or null.
///
/// This function will be exposed to C++
#[no_mangle]
pub extern "C" fn mozannotation_register_cstring(id: u32, address: *const c_char) -> *const c_char {
    store_annotation(id, AnnotationContents::CString, address)
}

/// Create a copy of the provided nul-terminated string which will be owned by
/// the crate, and register a pointer to it.
///
/// This function will be exposed to C++
///
/// # Safety
///
/// `address` must point to a valid nul-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn mozannotation_record_cstring(id: u32, address: *const c_char) {
    let len = unsafe { CStr::from_ptr(address).to_bytes().len() };
    store_annotation(id, AnnotationContents::OwnedByteBuffer(len as u32), address);
}

/// Register a pointer to a fixed size buffer.
///
/// Returns the value of the previously registered annotation or null.
///
/// This function will be exposed to C++
#[no_mangle]
pub extern "C" fn mozannotation_register_bytebuffer(
    id: u32,
    address: *const c_void,
    size: u32,
) -> *const c_void {
    store_annotation(id, AnnotationContents::ByteBuffer(size), address)
}

/// Create a copy of the provided buffer which will be owned by the crate, and
/// register a pointer to it.
///
/// This function will be exposed to C++
#[no_mangle]
pub extern "C" fn mozannotation_record_bytebuffer(id: u32, address: *const c_void, size: u32) {
    store_annotation(id, AnnotationContents::OwnedByteBuffer(size), address);
}

/// Unregister a crash annotation. Returns the previously registered pointer or
/// null if none was present. Return null also if the crate owned the
/// annotations' buffer.
///
/// This function will be exposed to C++
#[no_mangle]
pub extern "C" fn mozannotation_unregister(id: u32) -> *const c_void {
    store_annotation(id, AnnotationContents::Empty, null_mut())
}

/// Returns the raw address of an annotation if it has been registered or NULL
/// if it hasn't.
///
/// This function will be exposed to C++
///
/// # Safety
///
/// `contents` must point to an object of type [`AnnotationContents`]
#[no_mangle]
pub unsafe extern "C" fn mozannotation_get_contents(
    id: u32,
    contents: *mut AnnotationContents,
) -> usize {
    let annotations = &MOZANNOTATIONS.lock().unwrap().data;
    if let Some(annotation) = annotations.iter().find(|e| e.id == id) {
        if annotation.contents == AnnotationContents::Empty {
            return 0;
        }

        unsafe { *contents = annotation.contents };
        return annotation.address;
    }

    0
}

#[no_mangle]
pub extern "C" fn mozannotation_clear_all() {
    let annotations = &mut MOZANNOTATIONS.lock().unwrap().data;
    annotations.clear();
}
