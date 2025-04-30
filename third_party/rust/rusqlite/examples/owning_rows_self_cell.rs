extern crate rusqlite;

use rusqlite::{CachedStatement, Connection, Result, Rows};
use self_cell::{self_cell, MutBorrow};

type RowsRef<'a> = Rows<'a>;

self_cell!(
    struct OwningRows<'conn> {
        owner: MutBorrow<CachedStatement<'conn>>,
        #[covariant]
        dependent: RowsRef,
    }
);

fn main() -> Result<()> {
    let conn = Connection::open_in_memory()?;
    let stmt = conn.prepare_cached("SELECT 1")?;
    let mut or = OwningRows::try_new(MutBorrow::new(stmt), |s| s.borrow_mut().query([]))?;
    or.with_dependent_mut(|_stmt, rows| -> Result<()> {
        while let Some(row) = rows.next()? {
            assert_eq!(Ok(1), row.get(0));
        }
        Ok(())
    })?;
    Ok(())
}
