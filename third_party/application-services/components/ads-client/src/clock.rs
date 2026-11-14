/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use chrono::{DateTime, Utc};
#[cfg(test)]
use std::{sync::Arc, time::Duration};

/// Where the current time comes from. Outside tests it is always the system
/// clock: the mock only exists in test builds.
#[derive(Clone, Default)]
pub(crate) struct Clock(Source);

#[derive(Clone, Default)]
enum Source {
    #[default]
    System,
    #[cfg(test)]
    Mock(MockClock),
}

impl Clock {
    pub(crate) fn now(&self) -> DateTime<Utc> {
        match &self.0 {
            Source::System => Utc::now(),
            #[cfg(test)]
            Source::Mock(mock) => mock.now(),
        }
    }
}

#[cfg(test)]
impl From<MockClock> for Clock {
    fn from(mock: MockClock) -> Self {
        Self(Source::Mock(mock))
    }
}

/// A clock that stands still at the second it was created until advanced.
/// Clones share the same time, so a test keeps one to move the clock it
/// handed over.
#[cfg(test)]
#[derive(Clone)]
pub(crate) struct MockClock(Arc<parking_lot::Mutex<DateTime<Utc>>>);

#[cfg(test)]
impl MockClock {
    pub(crate) fn new() -> Self {
        use chrono::SubsecRound;

        Self(Arc::new(parking_lot::Mutex::new(
            Utc::now().trunc_subsecs(0),
        )))
    }

    pub(crate) fn advance(&self, by: Duration) {
        *self.0.lock() += by;
    }

    pub(crate) fn now(&self) -> DateTime<Utc> {
        *self.0.lock()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mock_moves_only_when_advanced() {
        let mock = MockClock::new();
        let start = mock.now();
        assert_eq!(mock.now(), start);

        mock.advance(Duration::from_secs(90));
        assert_eq!(mock.now(), start + Duration::from_secs(90));
    }

    #[test]
    fn clock_follows_the_mock_it_was_given() {
        let mock = MockClock::new();
        let clock = Clock::from(mock.clone());
        mock.advance(Duration::from_secs(5));
        assert_eq!(clock.now(), mock.now());
    }
}
