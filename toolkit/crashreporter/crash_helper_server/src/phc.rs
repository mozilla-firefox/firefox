/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// The types here must match the ones in memory/build/PHC.h

use anyhow::{bail, Result};
use std::{
    ffi::{c_char, c_void},
    mem::{size_of, MaybeUninit},
    slice,
};

#[repr(C)]
#[derive(Clone, Copy, PartialEq)]
#[allow(dead_code)]
pub(crate) enum Kind {
    Unknown = 0,
    NeverAllocatedPage = 1,
    InUsePage = 2,
    FreedPage = 3,
    GuardPage = 4,
}

const MAX_FRAMES: usize = 16;

#[repr(C)]
pub(crate) struct StackTrace {
    pub(crate) length: usize,
    pub(crate) pcs: [*const c_void; MAX_FRAMES],
    pub(crate) has_stack: c_char,
}

#[repr(C)]
pub(crate) struct AddrInfo {
    pub(crate) kind: Kind,
    pub(crate) base_addr: *const c_void,
    pub(crate) usable_size: usize,
    pub(crate) alloc_stack: StackTrace,
    pub(crate) free_stack: StackTrace,
    pub(crate) phc_was_locked: c_char,
}

impl AddrInfo {
    pub(crate) fn from_bytes(buff: &[u8]) -> Result<AddrInfo> {
        if buff.len() != size_of::<AddrInfo>() {
            bail!(
                "PHC AddrInfo structure size {} doesn't match expected size {}",
                buff.len(),
                size_of::<AddrInfo>()
            );
        }

        let mut addr_info = MaybeUninit::<AddrInfo>::uninit();
        // SAFETY: MaybeUninit<u8> is always valid, even for padding bytes
        let uninit_addr_info = unsafe {
            slice::from_raw_parts_mut(
                addr_info.as_mut_ptr() as *mut MaybeUninit<u8>,
                size_of::<AddrInfo>(),
            )
        };

        for (index, &value) in buff.iter().enumerate() {
            uninit_addr_info[index].write(value);
        }

        let addr_info = unsafe { addr_info.assume_init() };
        if !addr_info.check_consistency() {
            bail!("PHC AddrInfo structure is inconsistent");
        }

        Ok(addr_info)
    }

    pub(crate) fn kind_as_str(&self) -> &'static str {
        match self.kind {
            Kind::Unknown => "Unknown(?!)",
            Kind::NeverAllocatedPage => "NeverAllocatedPage",
            Kind::InUsePage => "InUsePage(?!)",
            Kind::FreedPage => "FreedPage",
            Kind::GuardPage => "GuardPage",
        }
    }

    fn check_consistency(&self) -> bool {
        let kind_value = self.kind as u32;

        if (kind_value > Kind::GuardPage as u32)
            || (self.alloc_stack.length > MAX_FRAMES)
            || (self.free_stack.length > MAX_FRAMES)
            || (self.alloc_stack.has_stack > 1)
            || (self.free_stack.has_stack > 1)
            || (self.phc_was_locked > 1)
        {
            return false;
        }

        true
    }
}
