//! Ensure `loadable_extension.rs` works.

use rusqlite::{Connection, Result};
use std::env::consts::{DLL_PREFIX, DLL_SUFFIX};

fn main() -> Result<()> {
    let db = Connection::open_in_memory()?;

    unsafe {
        db.load_extension_enable()?;
        db.load_extension(
            format!("target/debug/examples/{DLL_PREFIX}loadable_extension{DLL_SUFFIX}"),
            None,
        )?;
        db.load_extension_disable()?;
    }

    let str = db.query_row("SELECT rusqlite_test_function()", [], |row| {
        row.get::<_, String>(0)
    })?;
    assert_eq!(&str, "Rusqlite extension loaded correctly!");
    Ok(())
}
