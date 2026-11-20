// Copyright © 2017 Mozilla Foundation
//
// This program is made available under an ISC-style license.  See the
// accompanying file LICENSE for details.

use std::mem;
use std::os::raw::c_void;

// Note: For all clippy allowed warnings, see https://github.com/mozilla/cubeb-pulse-rs/issues/95
// for the effort to fix them.

fn wrap_defer_cb<F>(_: F) -> ffi::pa_defer_event_cb_t
where
    F: Fn(&MainloopApi, *mut ffi::pa_defer_event, *mut c_void),
{
    assert!(mem::size_of::<F>() == 0);

    unsafe extern "C" fn wrapped<F>(
        m: *mut ffi::pa_mainloop_api,
        e: *mut ffi::pa_defer_event,
        userdata: *mut c_void,
    ) where
        F: Fn(&MainloopApi, *mut ffi::pa_defer_event, *mut c_void),
    {
        let api = from_raw_ptr(m);
        #[allow(clippy::missing_transmute_annotations)]
        mem::transmute::<_, &F>(&())(&api, e, userdata);
        #[allow(clippy::forget_non_drop)]
        mem::forget(api);
    }

    Some(wrapped::<F>)
}

pub struct MainloopApi(*mut ffi::pa_mainloop_api);

impl MainloopApi {
    #[allow(clippy::mut_from_ref)]
    pub fn raw_mut(&self) -> &mut ffi::pa_mainloop_api {
        unsafe { &mut *self.0 }
    }

    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    pub fn defer_new<CB>(&self, cb: CB, userdata: *mut c_void) -> *mut ffi::pa_defer_event
    where
        CB: Fn(&MainloopApi, *mut ffi::pa_defer_event, *mut c_void),
    {
        let wrapped = wrap_defer_cb(cb);
        unsafe {
            let api = self.raw_mut();
            api.defer_new
                .expect("mainloop does not support defer events")(api, wrapped, userdata)
        }
    }

    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    pub fn defer_free(&self, e: *mut ffi::pa_defer_event) {
        unsafe {
            self.raw_mut()
                .defer_free
                .expect("mainloop does not support freeing defer events")(e);
        }
    }

    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    pub fn time_free(&self, e: *mut ffi::pa_time_event) {
        unsafe {
            if let Some(f) = self.raw_mut().time_free {
                f(e);
            }
        }
    }
}

pub unsafe fn from_raw_ptr(raw: *mut ffi::pa_mainloop_api) -> MainloopApi {
    MainloopApi(raw)
}
