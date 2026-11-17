// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use enum_map::Enum;
use serde::Serialize;
use tracing::debug;

use crate::{Error, Res};

pub type Wire = u32;

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq, Enum, Serialize)]
#[repr(u32)]
pub enum Version {
    Version2 = 0x6b33_43cf,
    #[default]
    Version1 = 1,
    #[cfg(feature = "draft-29")]
    Draft29 = 0xff00_0000 + 29,
}

impl Version {
    /// The first SCONE packet version.
    pub const SCONE1: u32 = 0x6f7d_c0fd;
    /// The second SCONE packet version.
    pub const SCONE2: u32 = 0xef7d_c0fd;

    #[must_use]
    pub const fn wire_version(self) -> Wire {
        self as u32
    }

    pub(crate) const fn initial_salt(self) -> &'static [u8] {
        const INITIAL_SALT_V2: &[u8] = &[
            0x0d, 0xed, 0xe3, 0xde, 0xf7, 0x00, 0xa6, 0xdb, 0x81, 0x93, 0x81, 0xbe, 0x6e, 0x26,
            0x9d, 0xcb, 0xf9, 0xbd, 0x2e, 0xd9,
        ];
        const INITIAL_SALT_V1: &[u8] = &[
            0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17, 0x9a, 0xe6, 0xa4, 0xc8,
            0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a,
        ];
        #[cfg(feature = "draft-29")]
        const INITIAL_SALT_29_32: &[u8] = &[
            0xaf, 0xbf, 0xec, 0x28, 0x99, 0x93, 0xd2, 0x4c, 0x9e, 0x97, 0x86, 0xf1, 0x9c, 0x61,
            0x11, 0xe0, 0x43, 0x90, 0xa8, 0x99,
        ];
        match self {
            Self::Version2 => INITIAL_SALT_V2,
            Self::Version1 => INITIAL_SALT_V1,
            #[cfg(feature = "draft-29")]
            Self::Draft29 => INITIAL_SALT_29_32,
        }
    }

    pub(crate) const fn label_prefix(self) -> &'static str {
        match self {
            Self::Version2 => "quicv2 ",
            Self::Version1 => "quic ",
            #[cfg(feature = "draft-29")]
            Self::Draft29 => "quic ",
        }
    }

    pub(crate) const fn retry_secret(self) -> &'static [u8] {
        const RETRY_SECRET_V2: &[u8] = &[
            0xc4, 0xdd, 0x24, 0x84, 0xd6, 0x81, 0xae, 0xfa, 0x4f, 0xf4, 0xd6, 0x9c, 0x2c, 0x20,
            0x29, 0x99, 0x84, 0xa7, 0x65, 0xa5, 0xd3, 0xc3, 0x19, 0x82, 0xf3, 0x8f, 0xc7, 0x41,
            0x62, 0x15, 0x5e, 0x9f,
        ];
        const RETRY_SECRET_V1: &[u8] = &[
            0xd9, 0xc9, 0x94, 0x3e, 0x61, 0x01, 0xfd, 0x20, 0x00, 0x21, 0x50, 0x6b, 0xcc, 0x02,
            0x81, 0x4c, 0x73, 0x03, 0x0f, 0x25, 0xc7, 0x9d, 0x71, 0xce, 0x87, 0x6e, 0xca, 0x87,
            0x6e, 0x6f, 0xca, 0x8e,
        ];
        #[cfg(feature = "draft-29")]
        const RETRY_SECRET_29: &[u8] = &[
            0x8b, 0x0d, 0x37, 0xeb, 0x85, 0x35, 0x02, 0x2e, 0xbc, 0x8d, 0x76, 0xa2, 0x07, 0xd8,
            0x0d, 0xf2, 0x26, 0x46, 0xec, 0x06, 0xdc, 0x80, 0x96, 0x42, 0xc3, 0x0a, 0x8b, 0xaa,
            0x2b, 0xaa, 0xff, 0x4c,
        ];
        match self {
            Self::Version2 => RETRY_SECRET_V2,
            Self::Version1 => RETRY_SECRET_V1,
            #[cfg(feature = "draft-29")]
            Self::Draft29 => RETRY_SECRET_29,
        }
    }

    #[cfg_attr(
        not(feature = "draft-29"),
        expect(clippy::unused_self, reason = "Only used with draft-29 feature.")
    )]
    pub(crate) const fn is_draft(self) -> bool {
        #[cfg(feature = "draft-29")]
        return matches!(self, Self::Draft29);
        #[cfg(not(feature = "draft-29"))]
        false
    }

    /// Determine if `self` can be upgraded to `other` compatibly.
    #[must_use]
    pub fn is_compatible(self, other: Self) -> bool {
        self == other
            || matches!(
                (self, other),
                (Self::Version1, Self::Version2) | (Self::Version2, Self::Version1)
            )
    }

    /// All supported real versions.
    #[must_use]
    pub fn all() -> Vec<Self> {
        vec![
            Self::Version2,
            Self::Version1,
            #[cfg(feature = "draft-29")]
            Self::Draft29,
        ]
    }

    pub fn compatible<'a, I: IntoIterator<Item = &'a Self>>(
        self,
        all: I,
    ) -> impl Iterator<Item = &'a Self> {
        all.into_iter().filter(move |&v| self.is_compatible(*v))
    }
}

impl TryFrom<Wire> for Version {
    type Error = Error;

    fn try_from(wire: Wire) -> Res<Self> {
        if wire == 1 {
            Ok(Self::Version1)
        } else if wire == 0x6b33_43cf {
            Ok(Self::Version2)
        } else {
            #[cfg(feature = "draft-29")]
            if wire == 0xff00_0000 + 29 {
                return Ok(Self::Draft29);
            }
            Err(Error::VersionNegotiation)
        }
    }
}

#[derive(Debug, Clone)]
pub struct Config {
    /// The version that a client uses to establish a connection.
    ///
    /// For a client, this is the version that is sent out in an Initial packet.
    /// A client that resumes will set this to the version from the original
    /// connection.
    /// A client that handles a Version Negotiation packet will be initialized with
    /// a version chosen from the packet, but it will then have this value overridden
    /// to match the original configuration so that the version negotiation can be
    /// authenticated.
    ///
    /// For a server `Connection`, this is the only type of Initial packet that
    /// can be accepted; the correct value is set by `Server`, see below.
    ///
    /// For a `Server`, this value is not used; if an Initial packet is received
    /// in a supported version (as listed in `versions`), new instances of
    /// `Connection` will be created with this value set to match what was received.
    ///
    /// An invariant here is that this version is always listed in `all`.
    initial: Version,
    /// The set of versions that are enabled, in preference order.  For a server,
    /// only the relative order of compatible versions matters.
    all: Vec<Version>,
}

impl Config {
    /// # Panics
    /// When `all` does not include `initial`.
    #[must_use]
    pub fn new(initial: Version, all: Vec<Version>) -> Self {
        assert!(all.contains(&initial));
        Self { initial, all }
    }

    #[must_use]
    pub const fn initial(&self) -> Version {
        self.initial
    }

    #[must_use]
    pub fn all(&self) -> &[Version] {
        &self.all
    }

    /// Overwrite the initial value; used by the `Server` when handling new connections
    /// and by the client on resumption.
    pub(crate) fn set_initial(&mut self, initial: Version) {
        debug!(
            "Overwrite initial version {:?} ==> {initial:?}",
            self.initial
        );
        assert!(self.all.contains(&initial));
        self.initial = initial;
    }

    pub fn compatible(&self) -> impl Iterator<Item = &Version> {
        self.initial.compatible(&self.all)
    }

    fn find_preferred<'a>(
        preferences: impl IntoIterator<Item = &'a Version>,
        vn: &[Wire],
    ) -> Option<Version> {
        for v in preferences {
            if vn.contains(&v.wire_version()) {
                return Some(*v);
            }
        }
        None
    }

    /// Determine the preferred version based on a version negotiation packet.
    pub(crate) fn preferred(&self, vn: &[Wire]) -> Option<Version> {
        Self::find_preferred(&self.all, vn)
    }

    /// Determine the preferred version based on a set of compatible versions.
    pub(crate) fn preferred_compatible(&self, vn: &[Wire]) -> Option<Version> {
        Self::find_preferred(self.compatible(), vn)
    }
}

impl Default for Config {
    fn default() -> Self {
        Self::new(Version::default(), Version::all())
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use super::Version;

    #[test]
    fn version1_is_not_draft() {
        assert!(!Version::Version1.is_draft());
    }

    #[cfg(feature = "draft-29")]
    #[test]
    fn draft29_is_draft() {
        assert!(Version::Draft29.is_draft());
        assert!(!Version::Version1.is_draft());
    }
}
