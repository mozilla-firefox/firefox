//! Parser for the Android-specific tzdata file.

mod tzdata;

/// Tries to locate the `tzdata` file, parse it, and return the entry for the
/// requested time zone.
///
/// # Errors
///
/// Returns an [std::io::Error] if the `tzdata` file cannot be found and parsed, or
/// if it does not contain the requested timezone entry.
///
/// # Example
///
///  ```rust
/// # use std::error::Error;
/// # use android_tzdata::find_tz_data;
/// #
/// # fn main() -> Result<(), Box<dyn Error>> {
/// let tz_data = find_tz_data("Europe/Kiev")?;
/// // Check it's version 2 of the [Time Zone Information Format](https://www.ietf.org/archive/id/draft-murchison-rfc8536bis-02.html).
/// assert!(tz_data.starts_with(b"TZif2"));
/// #     Ok(())
/// # }
/// ```
pub fn find_tz_data(tz_name: impl AsRef<str>) -> Result<Vec<u8>, std::io::Error> {
    let mut file = tzdata::find_file()?;
    tzdata::find_tz_data_in_file(&mut file, tz_name.as_ref())
}
