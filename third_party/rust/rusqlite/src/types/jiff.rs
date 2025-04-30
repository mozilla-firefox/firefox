//! Convert some `jiff` types.

use jiff::civil::{Date, DateTime, Time};
use std::str::FromStr;

use crate::types::{FromSql, FromSqlError, FromSqlResult, ToSql, ToSqlOutput, ValueRef};
use crate::Result;

/// Gregorian calendar date => "YYYY-MM-DD"
impl ToSql for Date {
    #[inline]
    fn to_sql(&self) -> Result<ToSqlOutput<'_>> {
        let s = self.to_string();
        Ok(ToSqlOutput::from(s))
    }
}

/// "YYYY-MM-DD" => Gregorian calendar date.
impl FromSql for Date {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_str().and_then(|s| match Self::from_str(s) {
            Ok(d) => Ok(d),
            Err(err) => Err(FromSqlError::Other(Box::new(err))),
        })
    }
}
/// time => "HH:MM:SS.SSS"
impl ToSql for Time {
    #[inline]
    fn to_sql(&self) -> Result<ToSqlOutput<'_>> {
        let date_str = self.to_string();
        Ok(ToSqlOutput::from(date_str))
    }
}

/// "HH:MM:SS.SSS" => time.
impl FromSql for Time {
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_str().and_then(|s| match Self::from_str(s) {
            Ok(t) => Ok(t),
            Err(err) => Err(FromSqlError::Other(Box::new(err))),
        })
    }
}

/// Gregorian datetime => "YYYY-MM-DDTHH:MM:SS.SSS"
impl ToSql for DateTime {
    #[inline]
    fn to_sql(&self) -> Result<ToSqlOutput<'_>> {
        let s = self.to_string();
        Ok(ToSqlOutput::from(s))
    }
}

/// "YYYY-MM-DDTHH:MM:SS.SSS" => Gregorian datetime.
impl FromSql for DateTime {
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_str().and_then(|s| match Self::from_str(s) {
            Ok(dt) => Ok(dt),
            Err(err) => Err(FromSqlError::Other(Box::new(err))),
        })
    }
}

#[cfg(test)]
mod test {
    use crate::{Connection, Result};
    use jiff::civil::{Date, DateTime, Time};

    fn checked_memory_handle() -> Result<Connection> {
        let db = Connection::open_in_memory()?;
        db.execute_batch("CREATE TABLE foo (t TEXT, b BLOB)")?;
        Ok(db)
    }

    #[test]
    fn test_date() -> Result<()> {
        let db = checked_memory_handle()?;
        let date = Date::constant(2016, 2, 23);
        db.execute("INSERT INTO foo (t) VALUES (?1)", [date])?;

        let s: String = db.one_column("SELECT t FROM foo")?;
        assert_eq!("2016-02-23", s);
        let t: Date = db.one_column("SELECT t FROM foo")?;
        assert_eq!(date, t);

        db.execute("UPDATE foo set b = date(t)", [])?;
        let t: Date = db.one_column("SELECT b FROM foo")?;
        assert_eq!(date, t);

        let r: Result<Date> = db.one_column("SELECT '2023-02-29'");
        assert!(r.is_err());
        Ok(())
    }

    #[test]
    fn test_time() -> Result<()> {
        let db = checked_memory_handle()?;
        let time = Time::constant(23, 56, 4, 0);
        db.execute("INSERT INTO foo (t) VALUES (?1)", [time])?;

        let s: String = db.one_column("SELECT t FROM foo")?;
        assert_eq!("23:56:04", s);
        let v: Time = db.one_column("SELECT t FROM foo")?;
        assert_eq!(time, v);

        db.execute("UPDATE foo set b = time(t)", [])?;
        let v: Time = db.one_column("SELECT b FROM foo")?;
        assert_eq!(time, v);

        let r: Result<Time> = db.one_column("SELECT '25:22:45'");
        assert!(r.is_err());
        Ok(())
    }

    #[test]
    fn test_date_time() -> Result<()> {
        let db = checked_memory_handle()?;
        let dt = DateTime::constant(2016, 2, 23, 23, 56, 4, 0);

        db.execute("INSERT INTO foo (t) VALUES (?1)", [dt])?;

        let s: String = db.one_column("SELECT t FROM foo")?;
        assert_eq!("2016-02-23T23:56:04", s);
        let v: DateTime = db.one_column("SELECT t FROM foo")?;
        assert_eq!(dt, v);

        db.execute("UPDATE foo set b = datetime(t)", [])?;
        let v: DateTime = db.one_column("SELECT b FROM foo")?;
        assert_eq!(dt, v);

        let r: Result<DateTime> = db.one_column("SELECT '2023-02-29T00:00:00'");
        assert!(r.is_err());
        Ok(())
    }
}
