//! Extended attribute functions.

#![allow(unsafe_code)]

use crate::buffer::Buffer;
use crate::{backend, ffi, io, path};
use backend::c;
use backend::fd::AsFd;
use bitflags::bitflags;
use core::ptr::null_mut;

bitflags! {
    /// `XATTR_*` constants for use with [`setxattr`], and other `*setxattr`
    /// functions.
    #[repr(transparent)]
    #[derive(Copy, Clone, Eq, PartialEq, Hash, Debug)]
    pub struct XattrFlags: ffi::c_uint {
        /// `XATTR_CREATE`
        const CREATE = c::XATTR_CREATE as c::c_uint;

        /// `XATTR_REPLACE`
        const REPLACE = c::XATTR_REPLACE as c::c_uint;

        /// <https://docs.rs/bitflags/*/bitflags/#externally-defined-flags>
        const _ = !0;
    }
}

/// `getxattr(path, name, value)`—Get extended filesystem attributes.
///
/// Unlike the libc `getxattr` function, this function does not treat an
/// buffer with zero length as a way to return the size of the attribute.
/// To obtain the size, use [`getxattr_query`].
///
/// For a higher-level API to xattr functionality, see the [xattr] crate.
///
/// [xattr]: https://crates.io/crates/xattr
///
/// # References
///  - [Linux]
///  - [Apple]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/getxattr.2.html
/// [Apple]: https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/getxattr.2.html
#[inline]
pub fn getxattr<P: path::Arg, Name: path::Arg, Buf: Buffer<u8>>(
    path: P,
    name: Name,
    mut value: Buf,
) -> io::Result<Buf::Output> {
    path.into_with_c_str(|path| {
        name.into_with_c_str(|name| {
            let parts = value.parts_mut();
            // SAFETY: `getxattr` behaves.
            let len = unsafe { backend::fs::syscalls::getxattr(path, name, parts)? };
            if parts.1 == 0 {
                // `getxattr` special-cases an input size of 0 as a size
                // query, which we handle through a different API function.
                unsafe { Ok(value.assume_init(0)) }
            } else {
                // SAFETY: `getxattr` behaves, and we've handled the special
                // case of an empty buffer above.
                unsafe { Ok(value.assume_init(len)) }
            }
        })
    })
}

/// `getxattr(path, name, NULL, 0)`—Query the length of an extended filesystem
/// attribute.
///
/// This function uses `getxattr` with no buffer to just return the size of
/// the attribute. This can be used to determine the size of a buffer to
/// allocate for a subsequent call to [`getxattr`], however keep in mind
/// that other processes may modify attributes at any time.
///
/// # References
///  - [Linux]
///  - [Apple]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/getxattr.2.html
/// [Apple]: https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/getxattr.2.html
#[inline]
pub fn getxattr_query<P: path::Arg, Name: path::Arg>(path: P, name: Name) -> io::Result<usize> {
    path.into_with_c_str(|path| {
        name.into_with_c_str(|name| {
            // SAFETY: `getxattr` behaves.
            unsafe { backend::fs::syscalls::getxattr(path, name, (null_mut(), 0)) }
        })
    })
}

/// `lgetxattr(path, name, value.as_ptr(), value.len())`—Get extended
/// filesystem attributes, without following symlinks in the last path
/// component.
///
/// Unlike the libc `lgetxattr` function, this function does not treat an
/// buffer with zero length as a way to return the size of the attribute.
/// To obtain the size, use [`lgetxattr_query`].
///
/// # References
///  - [Linux]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/lgetxattr.2.html
#[inline]
pub fn lgetxattr<P: path::Arg, Name: path::Arg, Buf: Buffer<u8>>(
    path: P,
    name: Name,
    mut value: Buf,
) -> io::Result<Buf::Output> {
    path.into_with_c_str(|path| {
        name.into_with_c_str(|name| {
            let parts = value.parts_mut();
            // SAFETY: `lgetxattr` behaves.
            let len = unsafe { backend::fs::syscalls::lgetxattr(path, name, parts)? };
            if parts.1 == 0 {
                // `lgetxattr` special-cases an input size of 0 as a size
                // query, which we handle through a different API function.
                unsafe { Ok(value.assume_init(0)) }
            } else {
                // SAFETY: `lgetxattr` behaves, and we've handled the special
                // case of an empty buffer above.
                unsafe { Ok(value.assume_init(len)) }
            }
        })
    })
}

/// `lgetxattr(path, name, NULL, 0)`—Query the length of an extended
/// filesystem attribute, without following symlinks in the last path
/// component.
///
/// This function uses `lgetxattr` with no buffer to just return the size of
/// the attribute. This can be used to determine the size of a buffer to
/// allocate for a subsequent call to [`lgetxattr`], however keep in mind
/// that other processes may modify attributes at any time.
///
/// # References
///  - [Linux]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/lgetxattr.2.html
#[inline]
pub fn lgetxattr_query<P: path::Arg, Name: path::Arg>(path: P, name: Name) -> io::Result<usize> {
    path.into_with_c_str(|path| {
        name.into_with_c_str(|name| {
            // SAFETY: `lgetxattr` behaves.
            unsafe { backend::fs::syscalls::lgetxattr(path, name, (null_mut(), 0)) }
        })
    })
}

/// `fgetxattr(fd, name, value.as_ptr(), value.len())`—Get extended
/// filesystem attributes on an open file descriptor.
///
/// Unlike the libc `fgetxattr` function, this function does not treat an
/// buffer with zero length as a way to return the size of the attribute.
/// To obtain the size, use [`fgetxattr_query`].
///
/// # References
///  - [Linux]
///  - [Apple]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/fgetxattr.2.html
/// [Apple]: https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fgetxattr.2.html
#[inline]
pub fn fgetxattr<Fd: AsFd, Name: path::Arg, Buf: Buffer<u8>>(
    fd: Fd,
    name: Name,
    mut value: Buf,
) -> io::Result<Buf::Output> {
    name.into_with_c_str(|name| {
        let parts = value.parts_mut();
        // SAFETY: `fgetxattr` behaves.
        let len = unsafe { backend::fs::syscalls::fgetxattr(fd.as_fd(), name, parts)? };
        if parts.1 == 0 {
            // `fgetxattr` special-cases an input size of 0 as a size
            // query, which we handle through a different API function.
            unsafe { Ok(value.assume_init(0)) }
        } else {
            // SAFETY: `fgetxattr` behaves, and we've handled the special
            // case of an empty buffer above.
            unsafe { Ok(value.assume_init(len)) }
        }
    })
}

/// `fgetxattr(fd, name, NULL, 0)`—Query the length of an extended
/// filesystem attribute on an open file descriptor.
///
/// This function uses `fgetxattr` with no buffer to just return the size of
/// the attribute. This can be used to determine the size of a buffer to
/// allocate for a subsequent call to [`fgetxattr`], however keep in mind
/// that other processes may modify attributes at any time.
///
/// # References
///  - [Linux]
///  - [Apple]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/fgetxattr.2.html
/// [Apple]: https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fgetxattr.2.html
#[inline]
pub fn fgetxattr_query<Fd: AsFd, Name: path::Arg>(fd: Fd, name: Name) -> io::Result<usize> {
    name.into_with_c_str(|name| {
        // SAFETY: `fgetxattr` behaves.
        unsafe { backend::fs::syscalls::fgetxattr(fd.as_fd(), name, (null_mut(), 0)) }
    })
}

/// `setxattr(path, name, value.as_ptr(), value.len(), flags)`—Set extended
/// filesystem attributes.
///
/// # References
///  - [Linux]
///  - [Apple]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/setxattr.2.html
/// [Apple]: https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/setxattr.2.html
#[inline]
pub fn setxattr<P: path::Arg, Name: path::Arg>(
    path: P,
    name: Name,
    value: &[u8],
    flags: XattrFlags,
) -> io::Result<()> {
    path.into_with_c_str(|path| {
        name.into_with_c_str(|name| backend::fs::syscalls::setxattr(path, name, value, flags))
    })
}

/// `setxattr(path, name, value.as_ptr(), value.len(), flags)`—Set extended
/// filesystem attributes, without following symlinks in the last path
/// component.
///
/// # References
///  - [Linux]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/lsetxattr.2.html
#[inline]
pub fn lsetxattr<P: path::Arg, Name: path::Arg>(
    path: P,
    name: Name,
    value: &[u8],
    flags: XattrFlags,
) -> io::Result<()> {
    path.into_with_c_str(|path| {
        name.into_with_c_str(|name| backend::fs::syscalls::lsetxattr(path, name, value, flags))
    })
}

/// `fsetxattr(fd, name, value.as_ptr(), value.len(), flags)`—Set extended
/// filesystem attributes on an open file descriptor.
///
/// # References
///  - [Linux]
///  - [Apple]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/fsetxattr.2.html
/// [Apple]: https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fsetxattr.2.html
#[inline]
pub fn fsetxattr<Fd: AsFd, Name: path::Arg>(
    fd: Fd,
    name: Name,
    value: &[u8],
    flags: XattrFlags,
) -> io::Result<()> {
    name.into_with_c_str(|name| backend::fs::syscalls::fsetxattr(fd.as_fd(), name, value, flags))
}

/// `listxattr(path, list.as_ptr(), list.len())`—List extended filesystem
/// attributes.
///
/// Unlike the libc `listxattr` function, this function does not treat an
/// buffer with zero length as a way to return the size of the attribute.
/// To obtain the size, use [`listxattr_query`].
///
/// # References
///  - [Linux]
///  - [Apple]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/listxattr.2.html
/// [Apple]: https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/listxattr.2.html
#[inline]
pub fn listxattr<P: path::Arg, Buf: Buffer<u8>>(path: P, mut list: Buf) -> io::Result<Buf::Output> {
    path.into_with_c_str(|path| {
        let parts = list.parts_mut();
        // SAFETY: `listxattr` behaves.
        let len = unsafe { backend::fs::syscalls::listxattr(path, parts)? };
        if parts.1 == 0 {
            // `listxattr` special-cases an input size of 0 as a size
            // query, which we handle through a different API function.
            unsafe { Ok(list.assume_init(0)) }
        } else {
            // SAFETY: `listxattr` behaves, and we've handled the special
            // case of an empty buffer above.
            unsafe { Ok(list.assume_init(len)) }
        }
    })
}

/// `listxattr(path, NULL, 0)`—Query the length of the list of extended
/// filesystem attributes.
///
/// This function uses `listxattr` with no buffer to just return the size of
/// the list. This can be used to determine the size of a buffer to
/// allocate for a subsequent call to [`listxattr`], however keep in mind
/// that other processes may modify attributes at any time.
///
/// # References
///  - [Linux]
///  - [Apple]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/listxattr.2.html
/// [Apple]: https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/listxattr.2.html
#[inline]
pub fn listxattr_query<P: path::Arg>(path: P) -> io::Result<usize> {
    path.into_with_c_str(|path| {
        // SAFETY: `listxattr` behaves.
        unsafe { backend::fs::syscalls::listxattr(path, (null_mut(), 0)) }
    })
}

/// `llistxattr(path, list.as_ptr(), list.len())`—List extended filesystem
/// attributes, without following symlinks in the last path component.
///
/// Unlike the libc `llistxattr` function, this function does not treat an
/// buffer with zero length as a way to return the size of the attribute.
/// To obtain the size, use [`llistxattr_query`].
///
/// # References
///  - [Linux]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/llistxattr.2.html
#[inline]
pub fn llistxattr<P: path::Arg, Buf: Buffer<u8>>(
    path: P,
    mut list: Buf,
) -> io::Result<Buf::Output> {
    path.into_with_c_str(|path| {
        let parts = list.parts_mut();
        // SAFETY: `llistxattr` behaves.
        let len = unsafe { backend::fs::syscalls::llistxattr(path, parts)? };
        if parts.1 == 0 {
            // `llistxattr` special-cases an input size of 0 as a size
            // query, which we handle through a different API function.
            unsafe { Ok(list.assume_init(0)) }
        } else {
            // SAFETY: `llistxattr` behaves, and we've handled the special
            // case of an empty buffer above.
            unsafe { Ok(list.assume_init(len)) }
        }
    })
}

/// `llistxattr(path, NULL, 0)`—Query the length of the list of extended
/// filesystem attributes, without following symlinks in the last path
/// component.
///
/// This function uses `llistxattr` with no buffer to just return the size of
/// the list. This can be used to determine the size of a buffer to
/// allocate for a subsequent call to [`llistxattr`], however keep in mind
/// that other processes may modify attributes at any time.
///
/// # References
///  - [Linux]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/llistxattr.2.html
#[inline]
pub fn llistxattr_query<P: path::Arg>(path: P) -> io::Result<usize> {
    path.into_with_c_str(|path| {
        // SAFETY: `llistxattr` behaves.
        unsafe { backend::fs::syscalls::llistxattr(path, (null_mut(), 0)) }
    })
}

/// `flistxattr(fd, list.as_ptr(), list.len())`—List extended filesystem
/// attributes on an open file descriptor.
///
/// Unlike the libc `flistxattr` function, this function does not treat an
/// buffer with zero length as a way to return the size of the attribute.
/// To obtain the size, use [`flistxattr_query`].
///
/// # References
///  - [Linux]
///  - [Apple]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/flistxattr.2.html
/// [Apple]: https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/flistxattr.2.html
#[inline]
pub fn flistxattr<Fd: AsFd, Buf: Buffer<u8>>(fd: Fd, mut list: Buf) -> io::Result<Buf::Output> {
    let parts = list.parts_mut();
    // SAFETY: `flistxattr` behaves.
    let len = unsafe { backend::fs::syscalls::flistxattr(fd.as_fd(), parts)? };
    if parts.1 == 0 {
        // `flistxattr` special-cases an input size of 0 as a size
        // query, which we handle through a different API function.
        unsafe { Ok(list.assume_init(0)) }
    } else {
        // SAFETY: `flistxattr` behaves, and we've handled the special
        // case of an empty buffer above.
        unsafe { Ok(list.assume_init(len)) }
    }
}

/// `flistxattr(fd, list.as_ptr(), list.len())`—Query the length of the
/// list of extended filesystem attributes on an open file descriptor.
///
/// This function uses `flistxattr` with no buffer to just return the size of
/// the list. This can be used to determine the size of a buffer to
/// allocate for a subsequent call to [`flistxattr`], however keep in mind
/// that other processes may modify attributes at any time.
///
/// # References
///  - [Linux]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/flistxattr.2.html
#[inline]
pub fn flistxattr_query<Fd: AsFd>(fd: Fd) -> io::Result<usize> {
    // SAFETY: `flistxattr` behaves.
    unsafe { backend::fs::syscalls::flistxattr(fd.as_fd(), (null_mut(), 0)) }
}

/// `removexattr(path, name)`—Remove an extended filesystem attribute.
///
/// # References
///  - [Linux]
///  - [Apple]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/removexattr.2.html
/// [Apple]: https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/removexattr.2.html
pub fn removexattr<P: path::Arg, Name: path::Arg>(path: P, name: Name) -> io::Result<()> {
    path.into_with_c_str(|path| {
        name.into_with_c_str(|name| backend::fs::syscalls::removexattr(path, name))
    })
}

/// `lremovexattr(path, name)`—Remove an extended filesystem attribute,
/// without following symlinks in the last path component.
///
/// # References
///  - [Linux]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/lremovexattr.2.html
pub fn lremovexattr<P: path::Arg, Name: path::Arg>(path: P, name: Name) -> io::Result<()> {
    path.into_with_c_str(|path| {
        name.into_with_c_str(|name| backend::fs::syscalls::lremovexattr(path, name))
    })
}

/// `fremovexattr(fd, name)`—Remove an extended filesystem attribute on an
/// open file descriptor.
///
/// # References
///  - [Linux]
///  - [Apple]
///
/// [Linux]: https://man7.org/linux/man-pages/man2/fremovexattr.2.html
/// [Apple]: https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fremovexattr.2.html
pub fn fremovexattr<Fd: AsFd, Name: path::Arg>(fd: Fd, name: Name) -> io::Result<()> {
    name.into_with_c_str(|name| backend::fs::syscalls::fremovexattr(fd.as_fd(), name))
}
