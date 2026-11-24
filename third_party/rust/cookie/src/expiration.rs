use time::OffsetDateTime;

/// A cookie's expiration: either session or a date-time.
///
/// An `Expiration` is constructible via `Expiration::from()` with an
/// `Option<OffsetDateTime>` or an `OffsetDateTime`:
///
///   * `None` -> `Expiration::Session`
///   * `Some(OffsetDateTime)` -> `Expiration::DateTime`
///   * `OffsetDateTime` -> `Expiration::DateTime`
///
/// ```rust
/// use cookie::Expiration;
/// use time::OffsetDateTime;
///
/// let expires = Expiration::from(None);
/// assert_eq!(expires, Expiration::Session);
///
/// let now = OffsetDateTime::now_utc();
/// let expires = Expiration::from(now);
/// assert_eq!(expires, Expiration::DateTime(now));
///
/// let expires = Expiration::from(Some(now));
/// assert_eq!(expires, Expiration::DateTime(now));
/// ```
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Expiration {
    /// Expiration for a "permanent" cookie at a specific date-time.
    DateTime(OffsetDateTime),
    /// Expiration for a "session" cookie. Browsers define the notion of a
    /// "session" and will automatically expire session cookies when they deem
    /// the "session" to be over. This is typically, but need not be, when the
    /// browser is closed.
    Session,
}

impl Expiration {
    /// Returns `true` if `self` is an `Expiration::DateTime`.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Expiration;
    /// use time::OffsetDateTime;
    ///
    /// let expires = Expiration::from(None);
    /// assert!(!expires.is_datetime());
    ///
    /// let expires = Expiration::from(OffsetDateTime::now_utc());
    /// assert!(expires.is_datetime());
    /// ```
    pub fn is_datetime(&self) -> bool {
        match self {
            Expiration::DateTime(_) => true,
            Expiration::Session => false
        }
    }

    /// Returns `true` if `self` is an `Expiration::Session`.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Expiration;
    /// use time::OffsetDateTime;
    ///
    /// let expires = Expiration::from(None);
    /// assert!(expires.is_session());
    ///
    /// let expires = Expiration::from(OffsetDateTime::now_utc());
    /// assert!(!expires.is_session());
    /// ```
    pub fn is_session(&self) -> bool {
        match self {
            Expiration::DateTime(_) => false,
            Expiration::Session => true
        }
    }

    /// Returns the inner `OffsetDateTime` if `self` is a `DateTime`.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Expiration;
    /// use time::OffsetDateTime;
    ///
    /// let expires = Expiration::from(None);
    /// assert!(expires.datetime().is_none());
    ///
    /// let now = OffsetDateTime::now_utc();
    /// let expires = Expiration::from(now);
    /// assert_eq!(expires.datetime(), Some(now));
    /// ```
    pub fn datetime(self) -> Option<OffsetDateTime> {
        match self {
            Expiration::Session => None,
            Expiration::DateTime(v) => Some(v)
        }
    }

    /// Applied `f` to the inner `OffsetDateTime` if `self` is a `DateTime` and
    /// returns the mapped `Expiration`.
    ///
    /// # Example
    ///
    /// ```rust
    /// use cookie::Expiration;
    /// use time::{OffsetDateTime, Duration};
    ///
    /// let now = OffsetDateTime::now_utc();
    /// let one_week = Duration::weeks(1);
    ///
    /// let expires = Expiration::from(now);
    /// assert_eq!(expires.map(|t| t + one_week).datetime(), Some(now + one_week));
    ///
    /// let expires = Expiration::from(None);
    /// assert_eq!(expires.map(|t| t + one_week).datetime(), None);
    /// ```
    pub fn map<F>(self, f: F) -> Self
        where F: FnOnce(OffsetDateTime) -> OffsetDateTime
    {
        match self {
            Expiration::Session => Expiration::Session,
            Expiration::DateTime(v) => Expiration::DateTime(f(v)),
        }
    }
}

impl<T: Into<Option<OffsetDateTime>>> From<T> for Expiration {
    fn from(option: T) -> Self {
        match option.into() {
            Some(value) => Expiration::DateTime(value),
            None => Expiration::Session
        }
    }
}
