// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#[macro_use]
extern crate lazy_static;
extern crate libc;
extern crate rkv;
extern crate tempfile;

use rkv::backend::{
    BackendEnvironmentBuilder, SafeMode, SafeModeDatabase, SafeModeEnvironment,
    SafeModeRoTransaction, SafeModeRwTransaction,
};
use std::fs;
use std::fs::File;
use std::io::Write;
use std::iter;
use std::path::Path;
use std::sync::Arc;
use std::thread;
use tempfile::Builder;

type Rkv = rkv::Rkv<SafeModeEnvironment>;
type SingleStore = rkv::SingleStore<SafeModeDatabase>;

fn panic_with_err(err: rkv::StoreError) {
    println!("Got error: {}", err);
    Result::Err(err).unwrap()
}

#[no_mangle]
pub extern "C" fn fuzz_rkv_db_file(raw_data: *const u8, size: libc::size_t) -> libc::c_int {
    let data = unsafe { std::slice::from_raw_parts(raw_data as *const u8, size as usize) };

    // First 8192 bytes are for the lock file.
    if data.len() < 8192 {
        return 0;
    }
    let (lock, db) = data.split_at(8192);
    let mut lock_file = File::create("data.lock").unwrap();
    lock_file.write_all(lock).unwrap();
    let mut db_file = File::create("data.mdb").unwrap();
    db_file.write_all(db).unwrap();

    let env = Rkv::with_capacity::<SafeMode>(Path::new("."), 2).unwrap();
    let store = env
        .open_single("test", rkv::StoreOptions::create())
        .unwrap();

    let reader = env.read().unwrap();
    store.get(&reader, &[0]).unwrap();

    0
}

#[no_mangle]
pub extern "C" fn fuzz_rkv_db_name(raw_data: *const u8, size: libc::size_t) -> libc::c_int {
    let data = unsafe { std::slice::from_raw_parts(raw_data as *const u8, size as usize) };

    let root = Builder::new().prefix("fuzz_rkv_db_name").tempdir().unwrap();
    fs::create_dir_all(root.path()).unwrap();

    let env = Rkv::new::<SafeMode>(root.path()).unwrap();
    let name = String::from_utf8_lossy(data);
    println!("Checking string: '{:?}'", name);
    // Some strings are invalid database names, and are handled as store errors.
    // Ignore those errors, but not others.
    let store = env
        .open_single(name.as_ref(), rkv::StoreOptions::create())
        .unwrap();
    let reader = env.read().unwrap();
    store.get(&reader, &[0]).unwrap();

    0
}

#[no_mangle]
pub extern "C" fn fuzz_rkv_key_write(raw_data: *const u8, size: libc::size_t) -> libc::c_int {
    let data = unsafe { std::slice::from_raw_parts(raw_data as *const u8, size as usize) };

    let root = Builder::new()
        .prefix("fuzz_rkv_key_write")
        .tempdir()
        .unwrap();
    fs::create_dir_all(root.path()).unwrap();

    let env = Rkv::new::<SafeMode>(root.path()).unwrap();
    let store = env
        .open_single("test", rkv::StoreOptions::create())
        .unwrap();

    let mut writer = env.write().unwrap();
    // Some data are invalid values, and are handled as store errors.
    // Ignore those errors, but not others.
    store
        .put(&mut writer, data, &rkv::Value::Str("val"))
        .unwrap();

    0
}

#[no_mangle]
pub extern "C" fn fuzz_rkv_val_write(raw_data: *const u8, size: libc::size_t) -> libc::c_int {
    let data = unsafe { std::slice::from_raw_parts(raw_data as *const u8, size as usize) };

    let root = Builder::new()
        .prefix("fuzz_rkv_val_write")
        .tempdir()
        .unwrap();
    fs::create_dir_all(root.path()).unwrap();

    let env = Rkv::new::<SafeMode>(root.path()).unwrap();
    let store = env
        .open_single("test", rkv::StoreOptions::create())
        .unwrap();

    let mut writer = env.write().unwrap();
    let string = String::from_utf8_lossy(data);
    let value = rkv::Value::Str(&string);
    store.put(&mut writer, "key", &value).unwrap();

    0
}

lazy_static! {
    static ref STATIC_DATA: Vec<String> = {
        let sizes = vec![4, 16, 128, 512, 1024];
        let mut data = Vec::new();

        for (i, s) in sizes.into_iter().enumerate() {
            let bytes = iter::repeat('a' as u8 + i as u8).take(s).collect();
            data.push(String::from_utf8(bytes).unwrap());
        }

        data
    };
}

#[no_mangle]
pub extern "C" fn fuzz_rkv_calls(raw_data: *const u8, size: libc::size_t) -> libc::c_int {
    let data = unsafe { std::slice::from_raw_parts(raw_data as *const u8, size as usize) };
    let mut fuzz = data.iter().copied();

    fn maybe_do<I: Iterator<Item = u8>>(fuzz: &mut I, f: impl FnOnce(&mut I) -> ()) -> Option<()> {
        match fuzz.next().map(|byte| byte % 2) {
            Some(0) => Some(f(fuzz)),
            _ => None,
        }
    }

    fn maybe_abort<I: Iterator<Item = u8>>(
        fuzz: &mut I,
        read: rkv::Reader<SafeModeRoTransaction>,
    ) -> Result<(), rkv::StoreError> {
        match fuzz.next().map(|byte| byte % 2) {
            Some(0) => Ok(read.abort()),
            _ => Ok(()),
        }
    }

    fn maybe_commit<I: Iterator<Item = u8>>(
        fuzz: &mut I,
        write: rkv::Writer<SafeModeRwTransaction>,
    ) -> Result<(), rkv::StoreError> {
        match fuzz.next().map(|byte| byte % 3) {
            Some(0) => write.commit(),
            Some(1) => Ok(write.abort()),
            _ => Ok(()),
        }
    }

    fn get_static_data<'a, I: Iterator<Item = u8>>(fuzz: &mut I) -> Option<&'a String> {
        fuzz.next().map(|byte| {
            let data = &*STATIC_DATA;
            let n = byte as usize;
            data.get(n % data.len()).unwrap()
        })
    }

    fn get_fuzz_data<I: Iterator<Item = u8> + Clone>(
        fuzz: &mut I,
        max_len: usize,
    ) -> Option<Vec<u8>> {
        fuzz.next().map(|byte| {
            let n = byte as usize;
            fuzz.clone().take((n * n) % max_len).collect()
        })
    }

    fn get_any_data<I: Iterator<Item = u8> + Clone>(
        fuzz: &mut I,
        max_len: usize,
    ) -> Option<Vec<u8>> {
        match fuzz.next().map(|byte| byte % 2) {
            Some(0) => get_static_data(fuzz).map(|v| v.clone().into_bytes()),
            Some(1) => get_fuzz_data(fuzz, max_len),
            _ => None,
        }
    }

    fn store_put<I: Iterator<Item = u8> + Clone>(fuzz: &mut I, env: &Rkv, store: &SingleStore) {
        let key = match get_any_data(fuzz, 1024) {
            Some(key) => key,
            None => return,
        };
        let value = match get_any_data(fuzz, usize::MAX) {
            Some(value) => value,
            None => return,
        };

        let mut writer = env.write().unwrap();

        match store.put(&mut writer, key, &rkv::Value::Blob(&value)) {
            Ok(_) => {}
            Err(err) => panic_with_err(err),
        };

        maybe_commit(fuzz, writer).unwrap();
    }

    fn store_get<I: Iterator<Item = u8> + Clone>(fuzz: &mut I, env: &Rkv, store: &SingleStore) {
        let key = match get_any_data(fuzz, 1024) {
            Some(key) => key,
            None => return,
        };

        let mut reader = match env.read() {
            Ok(reader) => reader,
            Err(err) => return panic_with_err(err),
        };

        match store.get(&mut reader, key) {
            Ok(_) => {}
            Err(err) => panic_with_err(err),
        };

        maybe_abort(fuzz, reader).unwrap();
    }

    fn store_delete<I: Iterator<Item = u8> + Clone>(fuzz: &mut I, env: &Rkv, store: &SingleStore) {
        let key = match get_any_data(fuzz, 1024) {
            Some(key) => key,
            None => return,
        };

        let mut writer = env.write().unwrap();

        // Deleting a random key that isn't in the store is expected and not a
        // bug, so don't panic on it.
        match store.delete(&mut writer, key) {
            Ok(_) => {}
            Err(rkv::StoreError::KeyValuePairNotFound) => {}
            Err(err) => panic_with_err(err),
        };

        maybe_commit(fuzz, writer).unwrap();
    }

    fn store_resize<I: Iterator<Item = u8>>(fuzz: &mut I, env: &Rkv) {
        let n = fuzz.next().unwrap_or(1) as usize;
        env.set_map_size(1_048_576 * (n % 100)).unwrap() // 1,048,576 bytes, i.e. 1MiB.
    }

    let root = Builder::new().prefix("fuzz_rkv_calls").tempdir().unwrap();
    fs::create_dir_all(root.path()).unwrap();

    let mut builder: SafeMode = Rkv::environment_builder();
    builder.set_max_dbs(1); // need at least one db

    maybe_do(&mut fuzz, |fuzz| {
        let n = fuzz.next().unwrap_or(126) as u32; // default
        builder.set_max_readers(1 + n);
    });
    maybe_do(&mut fuzz, |fuzz| {
        let n = fuzz.next().unwrap_or(0) as u32;
        builder.set_max_dbs(1 + n);
    });
    maybe_do(&mut fuzz, |fuzz| {
        let n = fuzz.next().unwrap_or(1) as usize;
        builder.set_map_size(1_048_576 * (n % 100)); // 1,048,576 bytes, i.e. 1MiB.
    });

    let env = Rkv::from_builder(root.path(), builder).unwrap();
    let store = env
        .open_single("test", rkv::StoreOptions::create())
        .unwrap();

    let shared_env = Arc::new(env);
    let shared_store = Arc::new(store);

    let use_threads = fuzz.next().map(|byte| byte % 10 == 0).unwrap_or(false);
    let max_threads = if use_threads { 16 } else { 1 };
    let num_threads = fuzz.next().unwrap_or(0) as usize % max_threads + 1;
    let chunk_size = fuzz.len() / num_threads;

    let threads = (0..num_threads).map(|_| {
        let env = shared_env.clone();
        let store = shared_store.clone();

        let chunk: Vec<_> = fuzz.by_ref().take(chunk_size).collect();
        let mut fuzz = chunk.into_iter();

        thread::spawn(move || loop {
            match fuzz.next().map(|byte| byte % 4) {
                Some(0) => store_put(&mut fuzz, &env, &store),
                Some(1) => store_get(&mut fuzz, &env, &store),
                Some(2) => store_delete(&mut fuzz, &env, &store),
                Some(3) => store_resize(&mut fuzz, &env),
                _ => break,
            }
        })
    });

    for handle in threads {
        handle.join().unwrap()
    }

    0
}
