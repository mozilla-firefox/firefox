/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

extern crate xpcom;

use crossbeam_utils::atomic::AtomicCell;
use moz_task::Task;
use nserror::{nsresult, NS_ERROR_FAILURE};
use nsstring::{nsCString, nsString};
use rkv::backend::{
    BackendEnvironmentBuilder, BackendInfo, RecoveryStrategy, SafeMode, SafeModeDatabase,
    SafeModeEnvironment,
};
use rkv::{OwnedValue, StoreError, StoreOptions, Value};
use std::{
    str,
    sync::{Arc, RwLock},
};
use storage_variant::VariantType;
use xpcom::{
    interfaces::{
        nsIKeyValueDatabaseCallback, nsIKeyValueEnumeratorCallback, nsIKeyValueVariantCallback,
        nsIKeyValueVoidCallback, nsIVariant,
    },
    RefPtr, ThreadBoundRefPtr,
};

use crate::{
    error::KeyValueError, fs::WidePathBuf, owned_value::owned_to_variant, KeyValueDatabase,
    KeyValueEnumerator, KeyValuePairResult,
};

type Manager = rkv::Manager<SafeModeEnvironment>;
type Rkv = rkv::Rkv<SafeModeEnvironment>;
type SingleStore = rkv::SingleStore<SafeModeDatabase>;

/// A macro to generate a done() implementation for a Task.
/// Takes one argument that specifies the type of the Task's callback function:
///   value: a callback function that takes a value
///   void: the callback function doesn't take a value
///
/// The "value" variant calls self.convert() to convert a successful result
/// into the value to pass to the callback function.  So if you generate done()
/// for a callback that takes a value, ensure you also implement convert()!
macro_rules! task_done {
    (value) => {
        fn done(&self) -> Result<(), nsresult> {
            // If TaskRunnable calls Task.done() to return a result on the
            // main thread before TaskRunnable returns on the database thread,
            // then the Task will get dropped on the database thread.
            //
            // But the callback is an nsXPCWrappedJS that isn't safe to release
            // on the database thread.  So we move it out of the Task here to ensure
            // it gets released on the main thread.
            let threadbound = self.callback.swap(None).ok_or(NS_ERROR_FAILURE)?;
            let callback = threadbound.get_ref().ok_or(NS_ERROR_FAILURE)?;

            match self.result.swap(None) {
                Some(Ok(value)) => unsafe { callback.Resolve(self.convert(value)?.coerce()) },
                Some(Err(err)) => unsafe { callback.Reject(&*nsCString::from(err.to_string())) },
                None => unsafe { callback.Reject(&*nsCString::from("unexpected")) },
            }
            .to_result()
        }
    };

    (void) => {
        fn done(&self) -> Result<(), nsresult> {
            // If TaskRunnable calls Task.done() to return a result on the
            // main thread before TaskRunnable returns on the database thread,
            // then the Task will get dropped on the database thread.
            //
            // But the callback is an nsXPCWrappedJS that isn't safe to release
            // on the database thread.  So we move it out of the Task here to ensure
            // it gets released on the main thread.
            let threadbound = self.callback.swap(None).ok_or(NS_ERROR_FAILURE)?;
            let callback = threadbound.get_ref().ok_or(NS_ERROR_FAILURE)?;

            match self.result.swap(None) {
                Some(Ok(())) => unsafe { callback.Resolve() },
                Some(Err(err)) => unsafe { callback.Reject(&*nsCString::from(err.to_string())) },
                None => unsafe { callback.Reject(&*nsCString::from("unexpected")) },
            }
            .to_result()
        }
    };
}

/// A tuple comprising an Arc<RwLock<Rkv>> and a SingleStore, which is
/// the result of GetOrCreateTask.  We declare this type because otherwise
/// Clippy complains "error: very complex type used. Consider factoring
/// parts into `type` definitions" (i.e. clippy::type-complexity) when we
/// declare the type of `GetOrCreateTask::result`.
type RkvStoreTuple = (Arc<RwLock<Rkv>>, SingleStore);

// The threshold for active resizing.
const RESIZE_RATIO: f32 = 0.85;

/// The threshold (50 MB) to switch the resizing policy from the double size to
/// the constant increment for active resizing.
const INCREMENTAL_RESIZE_THRESHOLD: usize = 52_428_800;

/// The incremental resize step (5 MB)
const INCREMENTAL_RESIZE_STEP: usize = 5_242_880;

/// The RKV disk page size and mask.
const PAGE_SIZE: usize = 4096;
const PAGE_SIZE_MASK: usize = 0b_1111_1111_1111;

/// Round the non-zero size to the multiple of page size greater or equal.
///
/// It does not handle the special cases such as size zero and overflow,
/// because even if that happens (extremely unlikely though), RKV will
/// ignore the new size if it's smaller than the current size.
///
/// E.g:
///     [   1 -  4096] -> 4096,
///     [4097 -  8192] -> 8192,
///     [8193 - 12286] -> 12286,
fn round_to_pagesize(size: usize) -> usize {
    if size & PAGE_SIZE_MASK == 0 {
        size
    } else {
        (size & !PAGE_SIZE_MASK) + PAGE_SIZE
    }
}

/// Kvstore employes two auto resizing strategies: active and passive resizing.
/// They work together to liberate consumers from having to guess the "proper"
/// size of the store upfront. See more detail about this in Bug 1543861.
///
/// Active resizing that is performed at the store startup.
///
/// It either increases the size in double, or by a constant size if its size
/// reaches INCREMENTAL_RESIZE_THRESHOLD.
///
/// Note that on Linux / MAC OSX, the increased size would only take effect if
/// there is a write transaction committed afterwards.
fn active_resize(env: &Rkv) -> Result<(), StoreError> {
    let info = env.info()?;
    let current_size = info.map_size();

    let size = if current_size < INCREMENTAL_RESIZE_THRESHOLD {
        current_size << 1
    } else {
        current_size + INCREMENTAL_RESIZE_STEP
    };

    env.set_map_size(size)?;
    Ok(())
}

/// Passive resizing that is performed when the MAP_FULL error occurs. It
/// increases the store with a `wanted` size.
///
/// Note that the `wanted` size must be rounded to a multiple of page size
/// by using `round_to_pagesize`.
fn passive_resize(env: &Rkv, wanted: usize) -> Result<(), StoreError> {
    let info = env.info()?;
    let current_size = info.map_size();
    env.set_map_size(current_size + wanted)?;
    Ok(())
}

pub struct GetOrCreateWithOptionsTask {
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIKeyValueDatabaseCallback>>>,
    path: nsString,
    name: nsCString,
    strategy: RecoveryStrategy,
    result: AtomicCell<Option<Result<RkvStoreTuple, KeyValueError>>>,
}

impl GetOrCreateWithOptionsTask {
    pub fn new(
        callback: RefPtr<nsIKeyValueDatabaseCallback>,
        path: nsString,
        name: nsCString,
        strategy: RecoveryStrategy,
    ) -> GetOrCreateWithOptionsTask {
        GetOrCreateWithOptionsTask {
            callback: AtomicCell::new(Some(ThreadBoundRefPtr::new(callback))),
            path,
            name,
            strategy,
            result: AtomicCell::default(),
        }
    }

    fn convert(&self, result: RkvStoreTuple) -> Result<RefPtr<KeyValueDatabase>, KeyValueError> {
        KeyValueDatabase::new(result.0, result.1)
    }
}

impl Task for GetOrCreateWithOptionsTask {
    fn run(&self) {
        // We do the work within a closure that returns a Result so we can
        // use the ? operator to simplify the implementation.
        self.result
            .store(Some(|| -> Result<RkvStoreTuple, KeyValueError> {
                let store;
                let mut builder = Rkv::environment_builder::<SafeMode>();
                builder.set_corruption_recovery_strategy(self.strategy);
                let mut manager = Manager::singleton().write()?;
                let path = WidePathBuf::new(&self.path).canonicalize()?;
                let rkv = manager.get_or_create_from_builder(
                    path.as_path(),
                    builder,
                    Rkv::from_builder::<SafeMode>,
                )?;
                {
                    let env = rkv.read()?;
                    let load_ratio = env.load_ratio()?.unwrap_or(0.0);
                    if load_ratio > RESIZE_RATIO {
                        active_resize(&env)?;
                    }
                    store = env.open_single(str::from_utf8(&self.name)?, StoreOptions::create())?;
                }
                Ok((rkv, store))
            }()));
    }

    task_done!(value);
}

pub struct PutTask {
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIKeyValueVoidCallback>>>,
    rkv: Arc<RwLock<Rkv>>,
    store: SingleStore,
    key: nsCString,
    value: OwnedValue,
    result: AtomicCell<Option<Result<(), KeyValueError>>>,
}

impl PutTask {
    pub fn new(
        callback: RefPtr<nsIKeyValueVoidCallback>,
        rkv: Arc<RwLock<Rkv>>,
        store: SingleStore,
        key: nsCString,
        value: OwnedValue,
    ) -> PutTask {
        PutTask {
            callback: AtomicCell::new(Some(ThreadBoundRefPtr::new(callback))),
            rkv,
            store,
            key,
            value,
            result: AtomicCell::default(),
        }
    }
}

impl Task for PutTask {
    fn run(&self) {
        // We do the work within a closure that returns a Result so we can
        // use the ? operator to simplify the implementation.
        self.result.store(Some(|| -> Result<(), KeyValueError> {
            let env = self.rkv.read()?;
            let key = str::from_utf8(&self.key)?;
            let v = Value::from(&self.value);
            let mut resized = false;

            // Use a loop here in case we want to retry from a recoverable
            // error such as `StoreError::MapFull`.
            loop {
                let mut writer = env.write()?;

                match self.store.put(&mut writer, key, &v) {
                    Ok(_) => (),

                    // Only handle the first MapFull error via passive resizing.
                    // Propogate the subsequent MapFull error.
                    Err(StoreError::MapFull) if !resized => {
                        // abort the failed transaction for resizing.
                        writer.abort();

                        // calculate the size of pairs and resize the store accordingly.
                        let pair_size =
                            key.len() + v.serialized_size().map_err(StoreError::from)? as usize;
                        let wanted = round_to_pagesize(pair_size);
                        passive_resize(&env, wanted)?;
                        resized = true;
                        continue;
                    }

                    Err(err) => return Err(KeyValueError::StoreError(err)),
                }

                // Ignore errors caused by simultaneous access.
                // We intend to investigate/revert this in bug 1810212.
                match writer.commit() {
                    Err(StoreError::IoError(e)) if e.kind() == std::io::ErrorKind::NotFound => {
                        // Explicitly ignore errors from simultaneous access.
                    }
                    Err(e) => return Err(From::from(e)),
                    _ => (),
                }
                break;
            }

            Ok(())
        }()));
    }

    task_done!(void);
}

pub struct WriteManyTask {
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIKeyValueVoidCallback>>>,
    rkv: Arc<RwLock<Rkv>>,
    store: SingleStore,
    pairs: Vec<(nsCString, Option<OwnedValue>)>,
    result: AtomicCell<Option<Result<(), KeyValueError>>>,
}

impl WriteManyTask {
    pub fn new(
        callback: RefPtr<nsIKeyValueVoidCallback>,
        rkv: Arc<RwLock<Rkv>>,
        store: SingleStore,
        pairs: Vec<(nsCString, Option<OwnedValue>)>,
    ) -> WriteManyTask {
        WriteManyTask {
            callback: AtomicCell::new(Some(ThreadBoundRefPtr::new(callback))),
            rkv,
            store,
            pairs,
            result: AtomicCell::default(),
        }
    }

    fn calc_pair_size(&self) -> Result<usize, StoreError> {
        let mut total = 0;

        for (key, value) in self.pairs.iter() {
            if let Some(val) = value {
                total += key.len();
                total += Value::from(val)
                    .serialized_size()
                    .map_err(StoreError::from)? as usize;
            }
        }

        Ok(total)
    }
}

impl Task for WriteManyTask {
    fn run(&self) {
        // We do the work within a closure that returns a Result so we can
        // use the ? operator to simplify the implementation.
        self.result.store(Some(|| -> Result<(), KeyValueError> {
            let env = self.rkv.read()?;
            let mut resized = false;

            // Use a loop here in case we want to retry from a recoverable
            // error such as `StoreError::MapFull`.
            'outer: loop {
                let mut writer = env.write()?;

                for (key, value) in self.pairs.iter() {
                    let key = str::from_utf8(key)?;
                    match value {
                        // To put.
                        Some(val) => {
                            match self.store.put(&mut writer, key, &Value::from(val)) {
                                Ok(_) => (),

                                // Only handle the first MapFull error via passive resizing.
                                // Propogate the subsequent MapFull error.
                                Err(StoreError::MapFull) if !resized => {
                                    // Abort the failed transaction for resizing.
                                    writer.abort();

                                    // Calculate the size of pairs and resize accordingly.
                                    let pair_size = self.calc_pair_size()?;
                                    let wanted = round_to_pagesize(pair_size);
                                    passive_resize(&env, wanted)?;
                                    resized = true;
                                    continue 'outer;
                                }

                                Err(err) => return Err(KeyValueError::StoreError(err)),
                            }
                        }
                        // To delete.
                        None => {
                            match self.store.delete(&mut writer, key) {
                                Ok(_) => (),

                                // RKV fails with an error if the key to delete wasn't found,
                                // and Rkv returns that error, but we ignore it, as we expect most
                                // of our consumers to want this behavior.
                                Err(StoreError::KeyValuePairNotFound) => (),

                                Err(err) => return Err(KeyValueError::StoreError(err)),
                            }
                        }
                    }
                }

                // Ignore errors caused by simultaneous access.
                // We intend to investigate/revert this in bug 1810212.
                match writer.commit() {
                    Err(StoreError::IoError(e)) if e.kind() == std::io::ErrorKind::NotFound => {
                        // Explicitly ignore errors from simultaneous access.
                    }
                    Err(e) => return Err(From::from(e)),
                    _ => (),
                }
                break; // 'outer: loop
            }

            Ok(())
        }()));
    }

    task_done!(void);
}

pub struct GetTask {
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIKeyValueVariantCallback>>>,
    rkv: Arc<RwLock<Rkv>>,
    store: SingleStore,
    key: nsCString,
    default_value: Option<OwnedValue>,
    result: AtomicCell<Option<Result<Option<OwnedValue>, KeyValueError>>>,
}

impl GetTask {
    pub fn new(
        callback: RefPtr<nsIKeyValueVariantCallback>,
        rkv: Arc<RwLock<Rkv>>,
        store: SingleStore,
        key: nsCString,
        default_value: Option<OwnedValue>,
    ) -> GetTask {
        GetTask {
            callback: AtomicCell::new(Some(ThreadBoundRefPtr::new(callback))),
            rkv,
            store,
            key,
            default_value,
            result: AtomicCell::default(),
        }
    }

    fn convert(&self, result: Option<OwnedValue>) -> Result<RefPtr<nsIVariant>, KeyValueError> {
        Ok(match result {
            Some(val) => owned_to_variant(val)?,
            None => ().into_variant(),
        })
    }
}

impl Task for GetTask {
    fn run(&self) {
        // We do the work within a closure that returns a Result so we can
        // use the ? operator to simplify the implementation.
        self.result
            .store(Some(|| -> Result<Option<OwnedValue>, KeyValueError> {
                let key = str::from_utf8(&self.key)?;
                let env = self.rkv.read()?;
                let reader = env.read()?;
                let value = self.store.get(&reader, key)?;

                Ok(match value {
                    Some(value) => Some(OwnedValue::from(&value)),
                    None => self.default_value.clone(),
                })
            }()));
    }

    task_done!(value);
}

pub struct HasTask {
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIKeyValueVariantCallback>>>,
    rkv: Arc<RwLock<Rkv>>,
    store: SingleStore,
    key: nsCString,
    result: AtomicCell<Option<Result<bool, KeyValueError>>>,
}

impl HasTask {
    pub fn new(
        callback: RefPtr<nsIKeyValueVariantCallback>,
        rkv: Arc<RwLock<Rkv>>,
        store: SingleStore,
        key: nsCString,
    ) -> HasTask {
        HasTask {
            callback: AtomicCell::new(Some(ThreadBoundRefPtr::new(callback))),
            rkv,
            store,
            key,
            result: AtomicCell::default(),
        }
    }

    fn convert(&self, result: bool) -> Result<RefPtr<nsIVariant>, KeyValueError> {
        Ok(result.into_variant())
    }
}

impl Task for HasTask {
    fn run(&self) {
        // We do the work within a closure that returns a Result so we can
        // use the ? operator to simplify the implementation.
        self.result.store(Some(|| -> Result<bool, KeyValueError> {
            let key = str::from_utf8(&self.key)?;
            let env = self.rkv.read()?;
            let reader = env.read()?;
            let value = self.store.get(&reader, key)?;
            Ok(value.is_some())
        }()));
    }

    task_done!(value);
}

pub struct DeleteTask {
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIKeyValueVoidCallback>>>,
    rkv: Arc<RwLock<Rkv>>,
    store: SingleStore,
    key: nsCString,
    result: AtomicCell<Option<Result<(), KeyValueError>>>,
}

impl DeleteTask {
    pub fn new(
        callback: RefPtr<nsIKeyValueVoidCallback>,
        rkv: Arc<RwLock<Rkv>>,
        store: SingleStore,
        key: nsCString,
    ) -> DeleteTask {
        DeleteTask {
            callback: AtomicCell::new(Some(ThreadBoundRefPtr::new(callback))),
            rkv,
            store,
            key,
            result: AtomicCell::default(),
        }
    }
}

impl Task for DeleteTask {
    fn run(&self) {
        // We do the work within a closure that returns a Result so we can
        // use the ? operator to simplify the implementation.
        self.result.store(Some(|| -> Result<(), KeyValueError> {
            let key = str::from_utf8(&self.key)?;
            let env = self.rkv.read()?;
            let mut writer = env.write()?;

            match self.store.delete(&mut writer, key) {
                Ok(_) => (),

                // RKV fails with an error if the key to delete wasn't found,
                // and Rkv returns that error, but we ignore it, as we expect most
                // of our consumers to want this behavior.
                Err(StoreError::KeyValuePairNotFound) => (),

                Err(err) => return Err(KeyValueError::StoreError(err)),
            }

            // Ignore errors caused by simultaneous access.
            // We intend to investigate/revert this in bug 1810212.
            match writer.commit() {
                Err(StoreError::IoError(e)) if e.kind() == std::io::ErrorKind::NotFound => {
                    // Explicitly ignore errors from simultaneous access.
                }
                Err(e) => return Err(From::from(e)),
                _ => (),
            }

            Ok(())
        }()));
    }

    task_done!(void);
}

pub struct ClearTask {
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIKeyValueVoidCallback>>>,
    rkv: Arc<RwLock<Rkv>>,
    store: SingleStore,
    result: AtomicCell<Option<Result<(), KeyValueError>>>,
}

impl ClearTask {
    pub fn new(
        callback: RefPtr<nsIKeyValueVoidCallback>,
        rkv: Arc<RwLock<Rkv>>,
        store: SingleStore,
    ) -> ClearTask {
        ClearTask {
            callback: AtomicCell::new(Some(ThreadBoundRefPtr::new(callback))),
            rkv,
            store,
            result: AtomicCell::default(),
        }
    }
}

impl Task for ClearTask {
    fn run(&self) {
        // We do the work within a closure that returns a Result so we can
        // use the ? operator to simplify the implementation.
        self.result.store(Some(|| -> Result<(), KeyValueError> {
            let env = self.rkv.read()?;
            let mut writer = env.write()?;
            self.store.clear(&mut writer)?;
            // Ignore errors caused by simultaneous access.
            // We intend to investigate/revert this in bug 1810212.
            match writer.commit() {
                Err(StoreError::IoError(e)) if e.kind() == std::io::ErrorKind::NotFound => {
                    // Explicitly ignore errors from simultaneous access.
                }
                Err(e) => return Err(From::from(e)),
                _ => (),
            }

            Ok(())
        }()));
    }

    task_done!(void);
}

pub struct EnumerateTask {
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIKeyValueEnumeratorCallback>>>,
    rkv: Arc<RwLock<Rkv>>,
    store: SingleStore,
    from_key: nsCString,
    to_key: nsCString,
    result: AtomicCell<Option<Result<Vec<KeyValuePairResult>, KeyValueError>>>,
}

impl EnumerateTask {
    pub fn new(
        callback: RefPtr<nsIKeyValueEnumeratorCallback>,
        rkv: Arc<RwLock<Rkv>>,
        store: SingleStore,
        from_key: nsCString,
        to_key: nsCString,
    ) -> EnumerateTask {
        EnumerateTask {
            callback: AtomicCell::new(Some(ThreadBoundRefPtr::new(callback))),
            rkv,
            store,
            from_key,
            to_key,
            result: AtomicCell::default(),
        }
    }

    fn convert(
        &self,
        result: Vec<KeyValuePairResult>,
    ) -> Result<RefPtr<KeyValueEnumerator>, KeyValueError> {
        Ok(KeyValueEnumerator::new(result))
    }
}

impl Task for EnumerateTask {
    fn run(&self) {
        // We do the work within a closure that returns a Result so we can
        // use the ? operator to simplify the implementation.
        self.result.store(Some(
            || -> Result<Vec<KeyValuePairResult>, KeyValueError> {
                let env = self.rkv.read()?;
                let reader = env.read()?;
                let from_key = str::from_utf8(&self.from_key)?;
                let to_key = str::from_utf8(&self.to_key)?;

                let iterator = if from_key.is_empty() {
                    self.store.iter_start(&reader)?
                } else {
                    self.store.iter_from(&reader, &from_key)?
                };

                // Ideally, we'd enumerate pairs lazily, as the consumer calls
                // nsIKeyValueEnumerator.getNext(), which calls our
                // KeyValueEnumerator.get_next() implementation.  But KeyValueEnumerator
                // can't reference the Iter because Rust "cannot #[derive(xpcom)]
                // on a generic type," and the Iter requires a lifetime parameter,
                // which would make KeyValueEnumerator generic.
                //
                // Our fallback approach is to eagerly collect the iterator
                // into a collection that KeyValueEnumerator owns.  Fixing this so we
                // enumerate pairs lazily is bug 1499252.
                let pairs: Vec<KeyValuePairResult> = iterator
                    // Convert the key to a string so we can compare it to the "to" key.
                    // For forward compatibility, we don't fail here if we can't convert
                    // a key to UTF-8.  Instead, we store the Err in the collection
                    // and fail lazily in KeyValueEnumerator.get_next().
                    .map(|result| match result {
                        Ok((key, val)) => Ok((str::from_utf8(key), val)),
                        Err(err) => Err(err),
                    })
                    // Stop iterating once we reach the to_key, if any.
                    .take_while(|result| match result {
                        Ok((key, _val)) => {
                            if to_key.is_empty() {
                                true
                            } else {
                                match *key {
                                    Ok(key) => key < to_key,
                                    Err(_err) => true,
                                }
                            }
                        }
                        Err(_) => true,
                    })
                    // Convert the key/value pair to owned.
                    .map(|result| match result {
                        Ok((key, val)) => match (key, val) {
                            (Ok(key), val) => Ok((key.to_owned(), OwnedValue::from(&val))),
                            (Err(err), _) => Err(err.into()),
                        },
                        Err(err) => Err(KeyValueError::StoreError(err)),
                    })
                    .collect();

                Ok(pairs)
            }(),
        ));
    }

    task_done!(value);
}
