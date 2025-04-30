extern crate rusqlite;

use ouroboros::self_referencing;
use rusqlite::{CachedStatement, Connection, Result, Rows};

#[self_referencing]
struct OwningRows<'conn> {
    stmt: CachedStatement<'conn>,
    #[borrows(mut stmt)]
    #[covariant]
    rows: Rows<'this>,
}

fn main() -> Result<()> {
    let conn = Connection::open_in_memory()?;
    let stmt = conn.prepare_cached("SELECT 1")?;
    let mut or = OwningRowsTryBuilder {
        stmt,
        rows_builder: |s| s.query([]),
    }
    .try_build()?;
    or.with_rows_mut(|rows| -> Result<()> {
        while let Some(row) = rows.next()? {
            assert_eq!(Ok(1), row.get(0));
        }
        Ok(())
    })?;
    Ok(())
}
