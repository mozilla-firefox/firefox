// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use neqo_transport::streams::SendGroupId;

/// Mints connection-unique send-group [`SendGroupId`]s. Owned by the connection rather than a
/// global, so uniqueness holds within a connection no matter which thread it runs on.
#[derive(Debug, Default)]
pub struct Generator(u64);

impl Generator {
    /// Mint the next send-group [`SendGroupId`]. IDs start at 1; 0 is reserved as a sentinel.
    ///
    /// # Panics
    ///
    /// Panics if the counter overflows `u64::MAX`.
    pub const fn next_id(&mut self) -> SendGroupId {
        let id = self.0.strict_add(1);
        self.0 = id;
        SendGroupId::new(id)
    }
}
