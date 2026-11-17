// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![cfg_attr(all(coverage_nightly, test), feature(coverage_attribute))]

//! A crate to return the name and maximum transmission unit (MTU) of the local network interface
//! towards a given destination `SocketAddr`, optionally from a given local `SocketAddr`.
//!
//! # Usage
//!
//! This crate exports a single function `interface_and_mtu` that returns the name and
//! [maximum transmission unit (MTU)](https://en.wikipedia.org/wiki/Maximum_transmission_unit)
//! of the outgoing network interface towards a remote destination identified by an `IpAddr`.
//!
//! # Example
//!
//! ```
//! # use std::net::{IpAddr, Ipv4Addr};
//! let destination = IpAddr::V4(Ipv4Addr::LOCALHOST);
//! let (name, mtu): (String, usize) = mtu::interface_and_mtu(destination).unwrap();
//! println!("MTU towards {destination} is {mtu} on {name}");
//! ```
//!
//! # Supported Platforms
//!
//! * Linux
//! * Android
//! * macOS
//! * Windows
//! * FreeBSD
//! * NetBSD
//! * OpenBSD
//! * Solaris
//!
//! # Notes
//!
//! The returned MTU may exceed the maximum IP packet size of 65,535 bytes on some platforms for
//! some remote destinations. (For example, loopback destinations on Windows.)
//!
//! The returned interface name is obtained from the operating system.
//!
//! # Contributing
//!
//! We're happy to receive PRs that improve this crate. Please take a look at our [community
//! guidelines](https://github.com/mozilla/neqo/blob/main/CODE_OF_CONDUCT.md) beforehand.

use std::{
    io::{Error, ErrorKind, Result},
    net::IpAddr,
};

#[cfg(not(target_os = "windows"))]
macro_rules! asserted_const_with_type {
    ($name:ident, $t1:ty, $e:expr, $t2:ty) => {
        #[allow(
            clippy::allow_attributes,
            clippy::cast_possible_truncation,
            clippy::cast_possible_wrap,
            reason = "Guarded by the following `assert!`."
        )]
        const $name: $t1 = $e as $t1;
        const _: () = assert!($name as $t2 == $e);
    };
}

#[cfg(any(target_os = "macos", bsd))]
mod bsd;

#[cfg(any(target_os = "linux", target_os = "android"))]
mod linux;

#[cfg(target_os = "windows")]
mod windows;

#[cfg(not(target_os = "windows"))]
mod routesocket;

#[cfg(any(target_os = "macos", bsd))]
use bsd::interface_and_mtu_impl;
#[cfg(any(target_os = "linux", target_os = "android"))]
use linux::interface_and_mtu_impl;
#[cfg(target_os = "windows")]
use windows::interface_and_mtu_impl;

/// Prepare a default error.
fn default_err() -> Error {
    Error::new(ErrorKind::NotFound, "Local interface MTU not found")
}

/// Prepare an error for cases that "should never happen".
#[cfg(not(target_os = "windows"))]
fn unlikely_err(msg: String) -> Error {
    debug_assert!(false, "{msg}");
    Error::other(msg)
}

/// Align `size` to the next multiple of `align` (which needs to be a power of two).
#[cfg(not(target_os = "windows"))]
const fn aligned_by(size: usize, align: usize) -> usize {
    if size == 0 {
        align
    } else {
        1 + ((size - 1) | (align - 1))
    }
}

// Platforms currently not supported.
//
// See <https://github.com/mozilla/mtu/issues/82>.
#[cfg(any(
    target_os = "ios",
    target_os = "tvos",
    target_os = "visionos",
    target_os = "redox"
))]
pub fn interface_and_mtu_impl(remote: IpAddr) -> Result<(String, usize)> {
    return Err(default_err());
}

/// Return the name and maximum transmission unit (MTU) of the outgoing network interface towards a
/// remote destination identified by an [`IpAddr`],
///
/// The returned MTU may exceed the maximum IP packet size of 65,535 bytes on some platforms for
/// some remote destinations. (For example, loopback destinations on Windows.)
///
/// The returned interface name is obtained from the operating system.
///
/// # Errors
///
/// This function returns an error if the local interface MTU cannot be determined.
pub fn interface_and_mtu(remote: IpAddr) -> Result<(String, usize)> {
    interface_and_mtu_impl(remote)
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod test {
    use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

    use crate::interface_and_mtu;

    #[derive(Debug)]
    struct NameMtu<'a>(Option<&'a str>, usize);

    impl PartialEq<NameMtu<'_>> for (String, usize) {
        fn eq(&self, other: &NameMtu<'_>) -> bool {
            other.0.is_none_or(|name| name == self.0) && other.1 == self.1
        }
    }

    const LOOPBACK: [NameMtu; 2] = // [IPv4, IPv6]
        cfg_select! {
            any(target_os = "macos", target_os = "freebsd") => {
                [NameMtu(Some("lo0"), 16_384), NameMtu(Some("lo0"), 16_384)]
            }
            any(target_os = "linux", target_os = "android") => {
                [NameMtu(Some("lo"), 65_536), NameMtu(Some("lo"), 65_536)]
            }
            target_os = "windows" => [
                NameMtu(Some("loopback_0"), 4_294_967_295),
                NameMtu(Some("loopback_0"), 4_294_967_295),
            ],
            target_os = "openbsd" => [NameMtu(Some("lo0"), 32_768), NameMtu(Some("lo0"), 32_768)],
            target_os = "netbsd" => [NameMtu(Some("lo0"), 33_624), NameMtu(Some("lo0"), 33_624)],
            target_os = "solaris" => {
                // Note: Different loopback MTUs for IPv4 and IPv6?!
                [NameMtu(Some("lo0"), 8_232), NameMtu(Some("lo0"), 8_252)]
            }
        };

    // Non-loopback interface names are unpredictable, so we only check the MTU.
    const INET: NameMtu = NameMtu(None, 1_500);

    #[test]
    fn loopback_v4() {
        assert_eq!(
            interface_and_mtu(IpAddr::V4(Ipv4Addr::LOCALHOST)).unwrap(),
            LOOPBACK[0]
        );
    }

    #[test]
    fn loopback_v6() {
        assert_eq!(
            interface_and_mtu(IpAddr::V6(Ipv6Addr::LOCALHOST)).unwrap(),
            LOOPBACK[1]
        );
    }

    #[test]
    fn inet_v4() {
        assert_eq!(
            interface_and_mtu(IpAddr::V4(Ipv4Addr::new(
                104, 16, 132, 229 // cloudflare.com
            )))
            .unwrap(),
            INET
        );
    }

    #[test]
    #[cfg(not(target_os = "windows"))]
    fn aligned_by() {
        for (size, align, expected) in [
            (0, 8, 8),
            (1, 8, 8),
            (7, 8, 8),
            (8, 8, 8),
            (9, 8, 16),
            (17, 8, 24),
        ] {
            assert_eq!(crate::aligned_by(size, align), expected);
        }
    }

    #[test]
    fn inet_v6() {
        let res = interface_and_mtu(IpAddr::V6(Ipv6Addr::new(
            0x2606, 0x4700, 0, 0, 0, 0, 0x6810, 0x84e5, // cloudflare.com
        )));
        match res {
            Ok(res) => assert_eq!(res, INET),
            Err(e) => {
                #[cfg(not(target_os = "windows"))]
                let no_ipv6 = matches!(e.raw_os_error(), Some(libc::ENETUNREACH | libc::ESRCH));
                #[cfg(target_os = "windows")]
                let no_ipv6 = e.raw_os_error()
                    == Some(
                        windows::Win32::Foundation::ERROR_NETWORK_UNREACHABLE
                            .0
                            .try_into()
                            .unwrap(),
                    );
                if no_ipv6 {
                    eprintln!("skipping IPv6 test due to lack of IPv6 connectivity: {e}");
                } else {
                    panic!("unexpected error on IPv6 interface_and_mtu lookup: {e}");
                }
            }
        }
    }

    #[test]
    fn default_error() {
        let err = crate::default_err();
        assert_eq!(err.kind(), std::io::ErrorKind::NotFound);
        assert!(err.to_string().contains("Local interface MTU not found"));
    }

    #[test]
    #[should_panic(expected = "test error")]
    #[cfg(not(target_os = "windows"))]
    #[cfg(debug_assertions)]
    fn unlikely_error_panics() {
        crate::unlikely_err("test error".to_string());
    }
}
