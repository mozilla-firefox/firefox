/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use core::fmt::Write;
use std::borrow::Cow;

use idna::uts46::AsciiDenyList;
use idna::uts46::ErrorPolicy;
use idna::uts46::Hyphens;
use idna::uts46::ProcessingError;
use idna::uts46::ProcessingSuccess;
use idna::uts46::Uts46;
use nserror::*;
use nsstring::*;
use percent_encoding::percent_decode;

/// Deny only glyphless ASCII to accommodate legacy callers.
const GLYPHLESS: AsciiDenyList = AsciiDenyList::new(true, "");

extern "C" {
    #[allow(improper_ctypes)]
    // char is now actually guaranteed to have the same representation as u32
    fn mozilla_net_is_label_safe(
        label: *const char,
        label_len: usize,
        tld: *const char,
        tld_len: usize,
    ) -> bool;
}

#[no_mangle]
/// # Safety
///
/// `src` and `dst` must be valid, non-null pointers that do not alias.
pub unsafe extern "C" fn mozilla_net_domain_to_ascii_impl(
    src: *const nsACString,
    allow_any_glyphful_ascii: bool,
    dst: *mut nsACString,
) -> nsresult {
    debug_assert_ne!(src, dst as *const nsACString, "src and dst must not alias");
    process(|_, _, _| false, allow_any_glyphful_ascii, &*src, &mut *dst)
}

#[no_mangle]
/// # Safety
///
/// `src` and `dst` must be valid, non-null pointers that do not alias.
pub unsafe extern "C" fn mozilla_net_domain_to_unicode_impl(
    src: *const nsACString,
    allow_any_glyphful_ascii: bool,
    dst: *mut nsACString,
) -> nsresult {
    debug_assert_ne!(src, dst as *const nsACString, "src and dst must not alias");
    process(|_, _, _| true, allow_any_glyphful_ascii, &*src, &mut *dst)
}

#[no_mangle]
/// # Safety
///
/// `src` and `dst` must be valid, non-null pointers that do not alias.
pub unsafe extern "C" fn mozilla_net_domain_to_display_impl(
    src: *const nsACString,
    allow_any_glyphful_ascii: bool,
    dst: *mut nsACString,
) -> nsresult {
    debug_assert_ne!(src, dst as *const nsACString, "src and dst must not alias");
    // XXX do we want to change this not to fail fast?
    process(
        |label, tld, _| unsafe {
            debug_assert!(!label.is_empty());
            mozilla_net_is_label_safe(
                label.as_ptr(),
                label.len(),
                if tld.is_empty() {
                    core::ptr::null()
                } else {
                    tld.as_ptr()
                },
                tld.len(),
            )
        },
        allow_any_glyphful_ascii,
        &*src,
        &mut *dst,
    )
}

#[no_mangle]
/// # Safety
///
/// `src`, `dst`, and `ascii_dst` must be valid, non-null pointers.
/// `src` must not alias either output pointer, and the output pointers
/// must not alias each other.
pub unsafe extern "C" fn mozilla_net_domain_to_display_and_ascii_impl(
    src: *const nsACString,
    dst: *mut nsACString,
    ascii_dst: *mut nsACString,
) -> nsresult {
    debug_assert_ne!(src, dst as *const nsACString, "src and dst must not alias");
    debug_assert_ne!(
        src, ascii_dst as *const nsACString,
        "src and ascii_dst must not alias"
    );
    debug_assert_ne!(dst, ascii_dst, "dst and ascii_dst must not alias");
    {
        let src = &*src;
        let dst: &mut nsACString = &mut *dst;
        let ascii_dst: &mut nsACString = &mut *ascii_dst;
        dst.truncate();
        ascii_dst.truncate();
        #[cfg(feature = "mailnews")]
        {
            if src == "Local%20Folders" || src == "smart%20mailboxes" {
                dst.assign(src);
                return nserror::NS_OK;
            }
        }
        let unpercent: Cow<'_, [u8]> = percent_decode(src).into();
        let result = Uts46::new().process(
            &unpercent,
            AsciiDenyList::URL,
            Hyphens::Allow,
            ErrorPolicy::FailFast,
            |label, tld, _| unsafe {
                debug_assert!(!label.is_empty());
                mozilla_net_is_label_safe(
                    label.as_ptr(),
                    label.len(),
                    if tld.is_empty() {
                        core::ptr::null()
                    } else {
                        tld.as_ptr()
                    },
                    tld.len(),
                )
            },
            &mut IdnaWriteWrapper::new(dst),
            Some(&mut IdnaWriteWrapper::new(ascii_dst)),
        );
        match result {
            Ok(ProcessingSuccess::Passthrough) => {}
            Ok(ProcessingSuccess::WroteToSink) => return nserror::NS_OK,
            Err(ProcessingError::ValidityError) => {
                // https://url.spec.whatwg.org/#concept-domain-to-ascii
                // ASCII domains don't fail UTS46 validation; they are only
                // lowercased and checked for forbidden domain code points.
                if !unpercent.is_ascii()
                    || unpercent.iter().any(|&b| is_forbidden_domain_code_point(b))
                {
                    return nserror::NS_ERROR_MALFORMED_URI;
                }
                dst.truncate();
                ascii_dst.truncate();
                dst.append(&unpercent.to_ascii_lowercase());
                return nserror::NS_OK;
            }
            Err(ProcessingError::SinkError) => unreachable!(),
        }
        match unpercent {
            Cow::Borrowed(_) => dst.assign(src),
            Cow::Owned(vec) => dst.append(&vec),
        }
        nserror::NS_OK
    }
}

/// https://url.spec.whatwg.org/#forbidden-domain-code-point
fn is_forbidden_domain_code_point(b: u8) -> bool {
    b <= 0x20 || b == 0x7F || b"%#/:<>?@[\\]^|".contains(&b)
}

type BufferString = arraystring::ArrayString<arraystring::typenum::U255>;

/// Buffering type to avoid atomic check of destination
/// `nsACString` on a per character basis.
struct IdnaWriteWrapper<'a> {
    sink: &'a mut nsACString,
    buffer: BufferString,
}

impl<'a> IdnaWriteWrapper<'a> {
    fn new(sink: &'a mut nsACString) -> IdnaWriteWrapper<'a> {
        IdnaWriteWrapper {
            sink,
            buffer: BufferString::new(),
        }
    }
}

impl<'a> Write for IdnaWriteWrapper<'a> {
    fn write_str(&mut self, s: &str) -> std::fmt::Result {
        if self.buffer.try_push_str(s).is_ok() {
            return Ok(());
        }
        if !self.buffer.is_empty() {
            self.sink.append(self.buffer.as_bytes());
            self.buffer.clear();
            if self.buffer.try_push_str(s).is_ok() {
                return Ok(());
            }
        }
        // Input too long to fit in the buffer.
        self.sink.append(s.as_bytes());
        Ok(())
    }
}

impl<'a> Drop for IdnaWriteWrapper<'a> {
    fn drop(&mut self) {
        if !self.buffer.is_empty() {
            self.sink.append(self.buffer.as_bytes());
        }
    }
}

fn process<OutputUnicode: FnMut(&[char], &[char], bool) -> bool>(
    output_as_unicode: OutputUnicode,
    allow_any_glyphful_ascii: bool,
    src: &nsACString,
    dst: &mut nsACString,
) -> nsresult {
    dst.truncate();
    #[cfg(feature = "mailnews")]
    {
        if src == "Local Folders" || src == "local folders" {
            dst.assign("Local%20Folders");
            return nserror::NS_OK;
        } else if src == "smart mailboxes" {
            dst.assign("smart%20mailboxes");
            return nserror::NS_OK;
        }
    }
    match Uts46::new().process(
        src,
        if allow_any_glyphful_ascii {
            GLYPHLESS
        } else {
            AsciiDenyList::URL
        },
        Hyphens::Allow,
        ErrorPolicy::FailFast,
        output_as_unicode,
        &mut IdnaWriteWrapper::new(dst),
        None,
    ) {
        Ok(ProcessingSuccess::Passthrough) => {
            // Let the borrow of `dst` inside `IdnaWriteWrapper` end and fall through.
        }
        Ok(ProcessingSuccess::WroteToSink) => return nserror::NS_OK,
        Err(ProcessingError::ValidityError) => return nserror::NS_ERROR_MALFORMED_URI,
        Err(ProcessingError::SinkError) => unreachable!(),
    }
    dst.assign(src);
    nserror::NS_OK
}

/// Not general-purpose! Only to be used from `nsDocShell::AttemptURIFixup`.
#[no_mangle]
/// # Safety
///
/// `src` and `dst` must be valid, non-null pointers.
pub unsafe extern "C" fn mozilla_net_recover_keyword_from_punycode(
    src: *const nsACString,
    dst: *mut nsACString,
) {
    let sink = &mut (*dst);
    let mut seen_label = false;
    for label in (*src).split(|b| *b == b'.') {
        if seen_label {
            sink.append(".");
        }
        seen_label = true;
        // We know the Punycode prefix is in lower case if we got it from
        // our own IDNA conversion code.
        if let Some(punycode) = label.strip_prefix(b"xn--") {
            // Not bothering to optimize this.
            // Just unwrap, since we know our IDNA conversion code gives
            // us ASCII here.
            let utf8 = std::str::from_utf8(punycode).unwrap();
            if let Some(decoded) = idna::punycode::decode_to_string(utf8) {
                sink.append(&decoded);
            } else {
                sink.append(label);
            }
        } else {
            sink.append(label);
        }
    }
}
