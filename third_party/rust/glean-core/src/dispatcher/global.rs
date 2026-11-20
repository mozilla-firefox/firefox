// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use once_cell::sync::Lazy;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::RwLock;
use std::thread;
use std::time::Duration;

use super::{DispatchError, DispatchGuard, Dispatcher};
use crossbeam_channel::RecvTimeoutError;

pub const GLOBAL_DISPATCHER_LIMIT: usize = 1000000;

static GLOBAL_DISPATCHER: Lazy<RwLock<Option<Dispatcher>>> =
    Lazy::new(|| RwLock::new(Some(Dispatcher::new(GLOBAL_DISPATCHER_LIMIT))));
pub static TESTING_MODE: AtomicBool = AtomicBool::new(false);
pub static QUEUE_TASKS: AtomicBool = AtomicBool::new(true);

pub fn is_test_mode() -> bool {
    TESTING_MODE.load(Ordering::SeqCst)
}

/// Get a dispatcher for the global queue.
///
/// A dispatcher is cheap to create, so we create one on every access instead of caching it.
/// This avoids troubles for tests where the global dispatcher _can_ change.
fn guard() -> DispatchGuard {
    GLOBAL_DISPATCHER
        .read()
        .unwrap()
        .as_ref()
        .map(|dispatcher| dispatcher.guard())
        .unwrap()
}

/// Launches a new task on the global dispatch queue.
///
/// The new task will be enqueued immediately.
/// If the pre-init queue was already flushed,
/// the background thread will process tasks in the queue (see [`flush_init`]).
///
/// This will not block.
///
/// [`flush_init`]: fn.flush_init.html
pub fn launch(task: impl FnOnce() + Send + 'static) {
    let current_thread = thread::current();
    if let Some("glean.shutdown") = current_thread.name() {
        log::error!("Tried to launch a task from the shutdown thread. That is forbidden.");
    }

    let guard = guard();
    match guard.launch(task) {
        Ok(_) => {}
        Err(DispatchError::QueueFull) => {
            log::info!("Exceeded maximum queue size, discarding task");
            // TODO: Record this as an error.
        }
        Err(_) => {
            log::debug!("Failed to launch a task on the queue. Discarding task.");
        }
    }

    // In test mode wait for the execution, unless we're still queueing tasks.
    let is_queueing = QUEUE_TASKS.load(Ordering::SeqCst);
    let is_test = TESTING_MODE.load(Ordering::SeqCst);
    if !is_queueing && is_test {
        guard.block_on_queue();
    }
}

/// Block until all tasks prior to this call are processed.
pub fn block_on_queue() {
    guard().block_on_queue();
}

/// Block until all tasks prior to this call are processed, with a timeout.
pub fn block_on_queue_timeout(timeout: Duration) -> Result<(), RecvTimeoutError> {
    guard().block_on_queue_timeout(timeout)
}

/// Starts processing queued tasks in the global dispatch queue,
/// running `final_task` as the last pre-init task.
///
/// This function blocks until queued tasks prior to this call are finished.
/// Once the initial queue is empty the dispatcher will wait for new tasks to be launched.
///
/// `final_task` is passed the number of tasks that overflowed the pre-init queue.
/// It runs even if the pre-init queue is full, and before any task launched after this call.
///
/// # Returns
///
/// Returns the total number of items that were added to the queue before being flushed,
/// or an error if the queue couldn't be flushed.
pub fn flush_init(final_task: impl FnOnce(usize) + Send + 'static) -> Result<usize, DispatchError> {
    guard().flush_init(final_task)
}

fn join_dispatcher_thread() -> Result<(), DispatchError> {
    // After we issue the shutdown command, make sure to wait for the
    // worker thread to join.
    let mut lock = GLOBAL_DISPATCHER.write().unwrap();
    let dispatcher = lock.as_mut().expect("Global dispatcher has gone missing");

    if let Some(worker) = dispatcher.worker.take() {
        return worker.join().map_err(|_| DispatchError::WorkerPanic);
    }

    Ok(())
}

/// Kill the blocked dispatcher without processing the queue.
///
/// This will immediately shutdown the worker thread
/// and no other tasks will be processed.
/// This only has an effect when the queue is still blocked.
pub fn kill() -> Result<(), DispatchError> {
    guard().kill()?;
    join_dispatcher_thread()
}

/// Shuts down the dispatch queue.
///
/// This will initiate a shutdown of the worker thread
/// and no new tasks will be processed after this.
pub fn shutdown() -> Result<(), DispatchError> {
    guard().shutdown()?;
    join_dispatcher_thread()
}

/// TEST ONLY FUNCTION.
/// Resets the Glean state and triggers init again.
pub fn reset_dispatcher() {
    // We don't care about shutdown errors, since they will
    // definitely happen if this is run concurrently.
    // We will still replace the global dispatcher.
    let _ = shutdown();

    // New dispatcher = we're queuing again.
    QUEUE_TASKS.store(true, Ordering::SeqCst);

    // Now that the dispatcher is shut down, replace it.
    // For that we
    // 1. Create a new
    // 2. Replace the global one
    // 3. Only then return (and thus release the lock)
    let mut lock = GLOBAL_DISPATCHER.write().unwrap();
    let new_dispatcher = Some(Dispatcher::new(GLOBAL_DISPATCHER_LIMIT));
    *lock = new_dispatcher;
}

#[cfg(test)]
mod test {
    use std::sync::{Arc, Mutex};

    use super::*;

    #[test]
    #[ignore] // We can't reset the queue at the moment, so filling it up breaks other tests.
    fn global_fills_up_in_order_and_works() {
        let _ = env_logger::builder().is_test(true).try_init();

        let result = Arc::new(Mutex::new(vec![]));

        for i in 1..=GLOBAL_DISPATCHER_LIMIT {
            let result = Arc::clone(&result);
            launch(move || {
                result.lock().unwrap().push(i);
            });
        }

        {
            let result = Arc::clone(&result);
            launch(move || {
                result.lock().unwrap().push(150);
            });
        }

        flush_init(|_| {}).unwrap();

        {
            let result = Arc::clone(&result);
            launch(move || {
                result.lock().unwrap().push(200);
            });
        }

        block_on_queue();

        let mut expected = (1..=GLOBAL_DISPATCHER_LIMIT).collect::<Vec<_>>();
        expected.push(200);
        assert_eq!(&*result.lock().unwrap(), &expected);
    }

    #[test]
    #[ignore] // We can't reset the queue at the moment, so flushing it breaks other tests.
    fn global_nested_calls() {
        let _ = env_logger::builder().is_test(true).try_init();

        let result = Arc::new(Mutex::new(vec![]));

        {
            let result = Arc::clone(&result);
            launch(move || {
                result.lock().unwrap().push(1);
            });
        }

        flush_init(|_| {}).unwrap();

        {
            let result = Arc::clone(&result);
            launch(move || {
                result.lock().unwrap().push(21);

                {
                    let result = Arc::clone(&result);
                    launch(move || {
                        result.lock().unwrap().push(3);
                    });
                }

                result.lock().unwrap().push(22);
            });
        }

        block_on_queue();

        let expected = vec![1, 21, 22, 3];
        assert_eq!(&*result.lock().unwrap(), &expected);
    }
}
