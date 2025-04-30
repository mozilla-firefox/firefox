//! Convert formats 1-10 in [Time Values](https://sqlite.org/lang_datefunc.html#time_values) to time types.
//! [`ToSql`] and [`FromSql`] implementation for [`OffsetDateTime`].
//! [`ToSql`] and [`FromSql`] implementation for [`PrimitiveDateTime`].
//! [`ToSql`] and [`FromSql`] implementation for [`Date`].
//! [`ToSql`] and [`FromSql`] implementation for [`Time`].
//! Time Strings in:
//!  - Format 2: "YYYY-MM-DD HH:MM"
//!  - Format 5: "YYYY-MM-DDTHH:MM"
//!  - Format 8: "HH:MM"
//!
//! without an explicit second value will assume 0 seconds.
//! Time String that contain an optional timezone without an explicit date are unsupported.
//! All other assumptions described in [Time Values](https://sqlite.org/lang_datefunc.html#time_values) section are unsupported.

use crate::types::{FromSql, FromSqlError, FromSqlResult, ToSql, ToSqlOutput, ValueRef};
use crate::{Error, Result};
use time::format_description::FormatItem;
use time::macros::format_description;
use time::{Date, OffsetDateTime, PrimitiveDateTime, Time};

const OFFSET_DATE_TIME_ENCODING: &[FormatItem<'_>] = format_description!(
    version = 2,
    "[year]-[month]-[day] [hour]:[minute]:[second].[subsecond][offset_hour sign:mandatory]:[offset_minute]"
);
const PRIMITIVE_DATE_TIME_ENCODING: &[FormatItem<'_>] = format_description!(
    version = 2,
    "[year]-[month]-[day] [hour]:[minute]:[second].[subsecond]"
);
const TIME_ENCODING: &[FormatItem<'_>] =
    format_description!(version = 2, "[hour]:[minute]:[second].[subsecond]");

const DATE_FORMAT: &[FormatItem<'_>] = format_description!(version = 2, "[year]-[month]-[day]");
const TIME_FORMAT: &[FormatItem<'_>] = format_description!(
    version = 2,
    "[hour]:[minute][optional [:[second][optional [.[subsecond]]]]]"
);
const PRIMITIVE_DATE_TIME_FORMAT: &[FormatItem<'_>] = format_description!(
    version = 2,
    "[year]-[month]-[day][first [ ][T]][hour]:[minute][optional [:[second][optional [.[subsecond]]]]]"
);
const UTC_DATE_TIME_FORMAT: &[FormatItem<'_>] = format_description!(
    version = 2,
    "[year]-[month]-[day][first [ ][T]][hour]:[minute][optional [:[second][optional [.[subsecond]]]]][optional [Z]]"
);
const OFFSET_DATE_TIME_FORMAT: &[FormatItem<'_>] = format_description!(
    version = 2,
    "[year]-[month]-[day][first [ ][T]][hour]:[minute][optional [:[second][optional [.[subsecond]]]]][offset_hour sign:mandatory]:[offset_minute]"
);
const LEGACY_DATE_TIME_FORMAT: &[FormatItem<'_>] = format_description!(
    version = 2,
    "[year]-[month]-[day] [hour]:[minute]:[second]:[subsecond] [offset_hour sign:mandatory]:[offset_minute]"
);

/// `OffsetDatetime` => RFC3339 format ("YYYY-MM-DD HH:MM:SS.SSS[+-]HH:MM")
impl ToSql for OffsetDateTime {
    #[inline]
    fn to_sql(&self) -> Result<ToSqlOutput<'_>> {
        let time_string = self
            .format(&OFFSET_DATE_TIME_ENCODING)
            .map_err(|err| Error::ToSqlConversionFailure(err.into()))?;
        Ok(ToSqlOutput::from(time_string))
    }
}

// Supports parsing formats 2-7 from https://www.sqlite.org/lang_datefunc.html
// Formats 2-7 without a timezone assumes UTC
impl FromSql for OffsetDateTime {
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_str().and_then(|s| {
            if let Some(b' ') = s.as_bytes().get(23) {
                // legacy
                return Self::parse(s, &LEGACY_DATE_TIME_FORMAT)
                    .map_err(|err| FromSqlError::Other(Box::new(err)));
            }
            if s[8..].contains('+') || s[8..].contains('-') {
                // Formats 2-7 with timezone
                return Self::parse(s, &OFFSET_DATE_TIME_FORMAT)
                    .map_err(|err| FromSqlError::Other(Box::new(err)));
            }
            // Formats 2-7 without timezone
            PrimitiveDateTime::parse(s, &UTC_DATE_TIME_FORMAT)
                .map(|p| p.assume_utc())
                .map_err(|err| FromSqlError::Other(Box::new(err)))
        })
    }
}

/// ISO 8601 calendar date without timezone => "YYYY-MM-DD"
impl ToSql for Date {
    #[inline]
    fn to_sql(&self) -> Result<ToSqlOutput<'_>> {
        let date_str = self
            .format(&DATE_FORMAT)
            .map_err(|err| Error::ToSqlConversionFailure(err.into()))?;
        Ok(ToSqlOutput::from(date_str))
    }
}

/// "YYYY-MM-DD" => ISO 8601 calendar date without timezone.
impl FromSql for Date {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_str().and_then(|s| {
            Self::parse(s, &DATE_FORMAT).map_err(|err| FromSqlError::Other(err.into()))
        })
    }
}

/// ISO 8601 time without timezone => "HH:MM:SS.SSS"
impl ToSql for Time {
    #[inline]
    fn to_sql(&self) -> Result<ToSqlOutput<'_>> {
        let time_str = self
            .format(&TIME_ENCODING)
            .map_err(|err| Error::ToSqlConversionFailure(err.into()))?;
        Ok(ToSqlOutput::from(time_str))
    }
}

/// "HH:MM"/"HH:MM:SS"/"HH:MM:SS.SSS" => ISO 8601 time without timezone.
impl FromSql for Time {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_str().and_then(|s| {
            Self::parse(s, &TIME_FORMAT).map_err(|err| FromSqlError::Other(err.into()))
        })
    }
}

/// ISO 8601 combined date and time without timezone => "YYYY-MM-DD HH:MM:SS.SSS"
impl ToSql for PrimitiveDateTime {
    #[inline]
    fn to_sql(&self) -> Result<ToSqlOutput<'_>> {
        let date_time_str = self
            .format(&PRIMITIVE_DATE_TIME_ENCODING)
            .map_err(|err| Error::ToSqlConversionFailure(err.into()))?;
        Ok(ToSqlOutput::from(date_time_str))
    }
}

/// YYYY-MM-DD HH:MM
/// YYYY-MM-DDTHH:MM
/// YYYY-MM-DD HH:MM:SS
/// YYYY-MM-DDTHH:MM:SS
/// YYYY-MM-DD HH:MM:SS.SSS
/// YYYY-MM-DDTHH:MM:SS.SSS
/// => ISO 8601 combined date and time with timezone
impl FromSql for PrimitiveDateTime {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_str().and_then(|s| {
            Self::parse(s, &PRIMITIVE_DATE_TIME_FORMAT)
                .map_err(|err| FromSqlError::Other(err.into()))
        })
    }
}

#[cfg(test)]
mod test {
    use crate::{Connection, Result};
    use time::macros::{date, datetime, time};
    use time::{Date, OffsetDateTime, PrimitiveDateTime, Time};

    fn checked_memory_handle() -> Result<Connection> {
        let db = Connection::open_in_memory()?;
        db.execute_batch("CREATE TABLE foo (t TEXT, i INTEGER, f FLOAT, b BLOB)")?;
        Ok(db)
    }

    #[test]
    fn test_offset_date_time() -> Result<()> {
        let db = checked_memory_handle()?;

        let mut ts_vec = vec![];

        let make_datetime = |secs: i128, nanos: i128| {
            OffsetDateTime::from_unix_timestamp_nanos(1_000_000_000 * secs + nanos).unwrap()
        };

        ts_vec.push(make_datetime(10_000, 0)); //January 1, 1970 2:46:40 AM
        ts_vec.push(make_datetime(10_000, 1000)); //January 1, 1970 2:46:40 AM (and one microsecond)
        ts_vec.push(make_datetime(1_500_391_124, 1_000_000)); //July 18, 2017
        ts_vec.push(make_datetime(2_000_000_000, 2_000_000)); //May 18, 2033
        ts_vec.push(make_datetime(3_000_000_000, 999_999_999)); //January 24, 2065
        ts_vec.push(make_datetime(10_000_000_000, 0)); //November 20, 2286

        for ts in ts_vec {
            db.execute("INSERT INTO foo(t) VALUES (?1)", [ts])?;

            let from: OffsetDateTime = db.one_column("SELECT t FROM foo")?;

            db.execute("DELETE FROM foo", [])?;

            assert_eq!(from, ts);
        }
        Ok(())
    }

    #[test]
    fn test_offset_date_time_parsing() -> Result<()> {
        let db = checked_memory_handle()?;
        let tests = vec![
            // Rfc3339
            (
                "2013-10-07T08:23:19.123456789Z",
                datetime!(2013-10-07 8:23:19.123456789 UTC),
            ),
            (
                "2013-10-07 08:23:19.123456789Z",
                datetime!(2013-10-07 8:23:19.123456789 UTC),
            ),
            // Format 2
            ("2013-10-07 08:23", datetime!(2013-10-07 8:23 UTC)),
            ("2013-10-07 08:23Z", datetime!(2013-10-07 8:23 UTC)),
            ("2013-10-07 08:23+04:00", datetime!(2013-10-07 8:23 +4)),
            // Format 3
            ("2013-10-07 08:23:19", datetime!(2013-10-07 8:23:19 UTC)),
            ("2013-10-07 08:23:19Z", datetime!(2013-10-07 8:23:19 UTC)),
            (
                "2013-10-07 08:23:19+04:00",
                datetime!(2013-10-07 8:23:19 +4),
            ),
            // Format 4
            (
                "2013-10-07 08:23:19.123",
                datetime!(2013-10-07 8:23:19.123 UTC),
            ),
            (
                "2013-10-07 08:23:19.123Z",
                datetime!(2013-10-07 8:23:19.123 UTC),
            ),
            (
                "2013-10-07 08:23:19.123+04:00",
                datetime!(2013-10-07 8:23:19.123 +4),
            ),
            // Format 5
            ("2013-10-07T08:23", datetime!(2013-10-07 8:23 UTC)),
            ("2013-10-07T08:23Z", datetime!(2013-10-07 8:23 UTC)),
            ("2013-10-07T08:23+04:00", datetime!(2013-10-07 8:23 +4)),
            // Format 6
            ("2013-10-07T08:23:19", datetime!(2013-10-07 8:23:19 UTC)),
            ("2013-10-07T08:23:19Z", datetime!(2013-10-07 8:23:19 UTC)),
            (
                "2013-10-07T08:23:19+04:00",
                datetime!(2013-10-07 8:23:19 +4),
            ),
            // Format 7
            (
                "2013-10-07T08:23:19.123",
                datetime!(2013-10-07 8:23:19.123 UTC),
            ),
            (
                "2013-10-07T08:23:19.123Z",
                datetime!(2013-10-07 8:23:19.123 UTC),
            ),
            (
                "2013-10-07T08:23:19.123+04:00",
                datetime!(2013-10-07 8:23:19.123 +4),
            ),
            // Legacy
            (
                "2013-10-07 08:23:12:987 -07:00",
                datetime!(2013-10-07 8:23:12.987 -7),
            ),
        ];

        for (s, t) in tests {
            let result: OffsetDateTime = db.query_row("SELECT ?1", [s], |r| r.get(0))?;
            assert_eq!(result, t);
        }
        Ok(())
    }

    #[test]
    fn test_date() -> Result<()> {
        let db = checked_memory_handle()?;
        let date = date!(2016 - 02 - 23);
        db.execute("INSERT INTO foo (t) VALUES (?1)", [date])?;

        let s: String = db.one_column("SELECT t FROM foo")?;
        assert_eq!("2016-02-23", s);
        let t: Date = db.one_column("SELECT t FROM foo")?;
        assert_eq!(date, t);
        Ok(())
    }

    #[test]
    fn test_time() -> Result<()> {
        let db = checked_memory_handle()?;
        let time = time!(23:56:04.00001);
        db.execute("INSERT INTO foo (t) VALUES (?1)", [time])?;

        let s: String = db.one_column("SELECT t FROM foo")?;
        assert_eq!("23:56:04.00001", s);
        let v: Time = db.one_column("SELECT t FROM foo")?;
        assert_eq!(time, v);
        Ok(())
    }

    #[test]
    fn test_primitive_date_time() -> Result<()> {
        let db = checked_memory_handle()?;
        let dt = date!(2016 - 02 - 23).with_time(time!(23:56:04));

        db.execute("INSERT INTO foo (t) VALUES (?1)", [dt])?;

        let s: String = db.one_column("SELECT t FROM foo")?;
        assert_eq!("2016-02-23 23:56:04.0", s);
        let v: PrimitiveDateTime = db.one_column("SELECT t FROM foo")?;
        assert_eq!(dt, v);

        db.execute("UPDATE foo set b = datetime(t)", [])?; // "YYYY-MM-DD HH:MM:SS"
        let hms: PrimitiveDateTime = db.one_column("SELECT b FROM foo")?;
        assert_eq!(dt, hms);
        Ok(())
    }

    #[test]
    fn test_date_parsing() -> Result<()> {
        let db = checked_memory_handle()?;
        let result: Date = db.query_row("SELECT ?1", ["2013-10-07"], |r| r.get(0))?;
        assert_eq!(result, date!(2013 - 10 - 07));
        Ok(())
    }

    #[test]
    fn test_time_parsing() -> Result<()> {
        let db = checked_memory_handle()?;
        let tests = vec![
            ("08:23", time!(08:23)),
            ("08:23:19", time!(08:23:19)),
            ("08:23:19.111", time!(08:23:19.111)),
        ];

        for (s, t) in tests {
            let result: Time = db.query_row("SELECT ?1", [s], |r| r.get(0))?;
            assert_eq!(result, t);
        }
        Ok(())
    }

    #[test]
    fn test_primitive_date_time_parsing() -> Result<()> {
        let db = checked_memory_handle()?;

        let tests = vec![
            ("2013-10-07T08:23", datetime!(2013-10-07 8:23)),
            ("2013-10-07T08:23:19", datetime!(2013-10-07 8:23:19)),
            ("2013-10-07T08:23:19.111", datetime!(2013-10-07 8:23:19.111)),
            ("2013-10-07 08:23", datetime!(2013-10-07 8:23)),
            ("2013-10-07 08:23:19", datetime!(2013-10-07 8:23:19)),
            ("2013-10-07 08:23:19.111", datetime!(2013-10-07 8:23:19.111)),
        ];

        for (s, t) in tests {
            let result: PrimitiveDateTime = db.query_row("SELECT ?1", [s], |r| r.get(0))?;
            assert_eq!(result, t);
        }
        Ok(())
    }

    #[test]
    fn test_sqlite_functions() -> Result<()> {
        let db = checked_memory_handle()?;
        db.one_column::<Time>("SELECT CURRENT_TIME")?;
        db.one_column::<Date>("SELECT CURRENT_DATE")?;
        db.one_column::<PrimitiveDateTime>("SELECT CURRENT_TIMESTAMP")?;
        db.one_column::<OffsetDateTime>("SELECT CURRENT_TIMESTAMP")?;
        Ok(())
    }

    #[test]
    fn test_time_param() -> Result<()> {
        let db = checked_memory_handle()?;
        let now = OffsetDateTime::now_utc().time();
        let result: Result<bool> = db.query_row(
            "SELECT 1 WHERE ?1 BETWEEN time('now', '-1 minute') AND time('now', '+1 minute')",
            [now],
            |r| r.get(0),
        );
        result?;
        Ok(())
    }

    #[test]
    fn test_date_param() -> Result<()> {
        let db = checked_memory_handle()?;
        let now = OffsetDateTime::now_utc().date();
        let result: Result<bool> = db.query_row(
            "SELECT 1 WHERE ?1 BETWEEN date('now', '-1 day') AND date('now', '+1 day')",
            [now],
            |r| r.get(0),
        );
        result?;
        Ok(())
    }

    #[test]
    fn test_primitive_date_time_param() -> Result<()> {
        let db = checked_memory_handle()?;
        let now = PrimitiveDateTime::new(
            OffsetDateTime::now_utc().date(),
            OffsetDateTime::now_utc().time(),
        );
        let result: Result<bool> = db.query_row(
            "SELECT 1 WHERE ?1 BETWEEN datetime('now', '-1 minute') AND datetime('now', '+1 minute')",
            [now],
            |r| r.get(0),
        );
        result?;
        Ok(())
    }

    #[test]
    fn test_offset_date_time_param() -> Result<()> {
        let db = checked_memory_handle()?;
        let result: Result<bool> = db.query_row(
            "SELECT 1 WHERE ?1 BETWEEN datetime('now', '-1 minute') AND datetime('now', '+1 minute')",
            [OffsetDateTime::now_utc()],
            |r| r.get(0),
        );
        result?;
        Ok(())
    }
}
