/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use lazy_static::lazy_static;
use rusqlite::{self, limits::Limit, types::ToSql};
use std::iter::Map;
use std::slice::Iter;

/// Returns SQLITE_LIMIT_VARIABLE_NUMBER as read from an in-memory connection and cached.
/// connection and cached. That means this will return the wrong value if it's set to a lower
/// value for a connection using this will return the wrong thing, but doing so is rare enough
/// that we explicitly don't support it (why would you want to lower this at runtime?).
///
/// If you call this and the actual value was set to a negative number or zero (nothing prevents
/// this beyond a warning in the SQLite documentation), we panic. However, it's unlikely you can
/// run useful queries if this happened anyway.
pub fn default_max_variable_number() -> usize {
    lazy_static! {
        static ref MAX_VARIABLE_NUMBER: usize = {
            let conn = rusqlite::Connection::open_in_memory()
                .expect("Failed to initialize in-memory connection (out of memory?)");

            let limit = conn.limit(Limit::SQLITE_LIMIT_VARIABLE_NUMBER).unwrap();
            assert!(
                limit > 0,
                "Illegal value for SQLITE_LIMIT_VARIABLE_NUMBER (must be > 0) {}",
                limit
            );
            limit as usize
        };
    }
    *MAX_VARIABLE_NUMBER
}

/// Helper for the case where you have a `&[impl ToSql]` of arbitrary length, but need one
/// of no more than the connection's `MAX_VARIABLE_NUMBER` (rather,
/// `default_max_variable_number()`). This is useful when performing batched updates.
///
/// The `do_chunk` callback is called with a slice of no more than `default_max_variable_number()`
/// items as it's first argument, and the offset from the start as it's second.
///
/// See `each_chunk_mapped` for the case where `T` doesn't implement `ToSql`, but can be
/// converted to something that does.
pub fn each_chunk<'a, T, E, F>(items: &'a [T], do_chunk: F) -> Result<(), E>
where
    T: 'a,
    F: FnMut(&'a [T], usize) -> Result<(), E>,
{
    each_sized_chunk(items, default_max_variable_number(), do_chunk)
}

/// A version of `each_chunk` for the case when the conversion to `to_sql` requires an custom
/// intermediate step. For example, you might want to grab a property off of an array of records
pub fn each_chunk_mapped<'a, T, U, E, Mapper, DoChunk>(
    items: &'a [T],
    to_sql: Mapper,
    do_chunk: DoChunk,
) -> Result<(), E>
where
    T: 'a,
    U: ToSql + 'a,
    Mapper: Fn(&'a T) -> U,
    DoChunk: FnMut(Map<Iter<'a, T>, &'_ Mapper>, usize) -> Result<(), E>,
{
    each_sized_chunk_mapped(items, default_max_variable_number(), to_sql, do_chunk)
}

// Split out for testing. Separate so that we can pass an actual slice
// to the callback if they don't need mapping. We could probably unify
// this with each_sized_chunk_mapped with a lot of type system trickery,
// but one of the benefits to each_chunk over the mapped versions is
// that the declaration is simpler.
pub fn each_sized_chunk<'a, T, E, F>(
    items: &'a [T],
    chunk_size: usize,
    mut do_chunk: F,
) -> Result<(), E>
where
    T: 'a,
    F: FnMut(&'a [T], usize) -> Result<(), E>,
{
    if items.is_empty() {
        return Ok(());
    }
    let mut offset = 0;
    for chunk in items.chunks(chunk_size) {
        do_chunk(chunk, offset)?;
        offset += chunk.len();
    }
    Ok(())
}

/// Utility to help perform batched updates, inserts, queries, etc. This is the low-level version
/// of this utility which is wrapped by `each_chunk` and `each_chunk_mapped`, and it allows you to
/// provide both the mapping function, and the chunk size.
///
/// Note: `mapped` basically just refers to the translating of `T` to some `U` where `U: ToSql`
/// using the `to_sql` function. This is useful for e.g. inserting the IDs of a large list
/// of records.
pub fn each_sized_chunk_mapped<'a, T, U, E, Mapper, DoChunk>(
    items: &'a [T],
    chunk_size: usize,
    to_sql: Mapper,
    mut do_chunk: DoChunk,
) -> Result<(), E>
where
    T: 'a,
    U: ToSql + 'a,
    Mapper: Fn(&'a T) -> U,
    DoChunk: FnMut(Map<Iter<'a, T>, &'_ Mapper>, usize) -> Result<(), E>,
{
    if items.is_empty() {
        return Ok(());
    }
    let mut offset = 0;
    for chunk in items.chunks(chunk_size) {
        let mapped = chunk.iter().map(&to_sql);
        do_chunk(mapped, offset)?;
        offset += chunk.len();
    }
    Ok(())
}

#[cfg(test)]
fn check_chunk<T, C>(items: C, expect: &[T], desc: &str)
where
    C: IntoIterator,
    <C as IntoIterator>::Item: ToSql,
    T: ToSql,
{
    let items = items.into_iter().collect::<Vec<_>>();
    assert_eq!(items.len(), expect.len());
    // Can't quite make the borrowing work out here w/o a loop, oh well.
    for (idx, (got, want)) in items.iter().zip(expect.iter()).enumerate() {
        assert_eq!(
            got.to_sql().unwrap(),
            want.to_sql().unwrap(),
            // ToSqlOutput::Owned(Value::Integer(*num)),
            "{}: Bad value at index {}",
            desc,
            idx
        );
    }
}

#[cfg(test)]
mod test_mapped {
    use super::*;

    #[test]
    fn test_separate() {
        let mut iteration = 0;
        each_sized_chunk_mapped(
            &[1, 2, 3, 4, 5],
            3,
            |item| item as &dyn ToSql,
            |chunk, offset| {
                match offset {
                    0 => {
                        assert_eq!(iteration, 0);
                        check_chunk(chunk, &[1, 2, 3], "first chunk");
                    }
                    3 => {
                        assert_eq!(iteration, 1);
                        check_chunk(chunk, &[4, 5], "second chunk");
                    }
                    n => {
                        panic!("Unexpected offset {}", n);
                    }
                }
                iteration += 1;
                Ok::<(), ()>(())
            },
        )
        .unwrap();
    }

    #[test]
    fn test_leq_chunk_size() {
        for &check_size in &[5, 6] {
            let mut iteration = 0;
            each_sized_chunk_mapped(
                &[1, 2, 3, 4, 5],
                check_size,
                |item| item as &dyn ToSql,
                |chunk, offset| {
                    assert_eq!(iteration, 0);
                    iteration += 1;
                    assert_eq!(offset, 0);
                    check_chunk(chunk, &[1, 2, 3, 4, 5], "only iteration");
                    Ok::<(), ()>(())
                },
            )
            .unwrap();
        }
    }

    #[test]
    fn test_empty_chunk() {
        let items: &[i64] = &[];
        each_sized_chunk_mapped::<_, _, (), _, _>(
            items,
            100,
            |item| item as &dyn ToSql,
            |_, _| {
                panic!("Should never be called");
            },
        )
        .unwrap();
    }

    #[test]
    fn test_error() {
        let mut iteration = 0;
        let e = each_sized_chunk_mapped(
            &[1, 2, 3, 4, 5, 6, 7],
            3,
            |item| item as &dyn ToSql,
            |_, offset| {
                if offset == 0 {
                    assert_eq!(iteration, 0);
                    iteration += 1;
                    Ok(())
                } else if offset == 3 {
                    assert_eq!(iteration, 1);
                    iteration += 1;
                    Err("testing".to_string())
                } else {
                    // Make sure we stopped after the error.
                    panic!("Shouldn't get called with offset of {}", offset);
                }
            },
        )
        .expect_err("Should be an error");
        assert_eq!(e, "testing");
    }
}

#[cfg(test)]
mod test_unmapped {
    use super::*;

    #[test]
    fn test_separate() {
        let mut iteration = 0;
        each_sized_chunk(&[1, 2, 3, 4, 5], 3, |chunk, offset| {
            match offset {
                0 => {
                    assert_eq!(iteration, 0);
                    check_chunk(chunk, &[1, 2, 3], "first chunk");
                }
                3 => {
                    assert_eq!(iteration, 1);
                    check_chunk(chunk, &[4, 5], "second chunk");
                }
                n => {
                    panic!("Unexpected offset {}", n);
                }
            }
            iteration += 1;
            Ok::<(), ()>(())
        })
        .unwrap();
    }

    #[test]
    fn test_leq_chunk_size() {
        for &check_size in &[5, 6] {
            let mut iteration = 0;
            each_sized_chunk(&[1, 2, 3, 4, 5], check_size, |chunk, offset| {
                assert_eq!(iteration, 0);
                iteration += 1;
                assert_eq!(offset, 0);
                check_chunk(chunk, &[1, 2, 3, 4, 5], "only iteration");
                Ok::<(), ()>(())
            })
            .unwrap();
        }
    }

    #[test]
    fn test_empty_chunk() {
        let items: &[i64] = &[];
        each_sized_chunk::<_, (), _>(items, 100, |_, _| {
            panic!("Should never be called");
        })
        .unwrap();
    }

    #[test]
    fn test_error() {
        let mut iteration = 0;
        let e = each_sized_chunk(&[1, 2, 3, 4, 5, 6, 7], 3, |_, offset| {
            if offset == 0 {
                assert_eq!(iteration, 0);
                iteration += 1;
                Ok(())
            } else if offset == 3 {
                assert_eq!(iteration, 1);
                iteration += 1;
                Err("testing".to_string())
            } else {
                // Make sure we stopped after the error.
                panic!("Shouldn't get called with offset of {}", offset);
            }
        })
        .expect_err("Should be an error");
        assert_eq!(e, "testing");
    }
}
