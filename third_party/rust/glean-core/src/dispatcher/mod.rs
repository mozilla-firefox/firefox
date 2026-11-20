// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A global dispatcher queue.
//!
//! # Example - Global Dispatch queue
//!
//! The global dispatch queue is pre-configured with a maximum queue size of 100 tasks.
//!
//! ```rust,ignore
//! // Ensure the dispatcher queue is being worked on.
//! dispatcher::flush_init(|_| {});
//!
//! dispatcher::launch(|| {
//!     println!("Executing expensive task");
//!     // Run your expensive task in a separate thread.
//! });
//!
//! dispatcher::launch(|| {
//!     println!("A second task that's executed sequentially, but off the main thread.");
//! });
//! ```

use std::{
    mem,
    ops::Deref,
    sync::{
        atomic::{AtomicBool, AtomicUsize, Ordering},
        Arc,
    },
    thread::JoinHandle,
    time::Duration,
};

use crossbeam_channel::{bounded, unbounded, RecvTimeoutError, SendError, Sender};
use thiserror::Error;

pub use global::*;

pub(crate) mod global;

/// Command received while blocked from further work.
enum Blocked {
    /// Shutdown immediately without processing the queue.
    Shutdown,
    /// Unblock and continue with work as normal.
    Continue,
}

/// The command a worker should execute.
enum Command {
    /// A task is a user-defined function to run.
    Task(Box<dyn FnOnce() + Send>),

    /// Swap the channel
    Swap(Sender<()>),

    /// Signal the worker to finish work and shut down.
    Shutdown,
}

/// The error returned from operations on the dispatcher
#[derive(Error, Debug, PartialEq, Eq)]
pub enum DispatchError {
    /// The worker panicked while running a task
    #[error("The worker panicked while running a task")]
    WorkerPanic,

    /// Maximum queue size reached
    #[error("Maximum queue size reached")]
    QueueFull,

    /// Pre-init buffer was already flushed
    #[error("Pre-init buffer was already flushed")]
    AlreadyFlushed,

    /// Failed to send command to worker thread
    #[error("Failed to send command to worker thread")]
    SendError,

    /// Failed to receive from channel
    #[error("Failed to receive from channel")]
    RecvError(#[from] crossbeam_channel::RecvError),
}

impl<T> From<SendError<T>> for DispatchError {
    fn from(_: SendError<T>) -> Self {
        DispatchError::SendError
    }
}

/// A clonable guard for a dispatch queue.
#[derive(Clone)]
struct DispatchGuard {
    inner: Arc<DispatchGuardInner>,
}

impl Deref for DispatchGuard {
    type Target = DispatchGuardInner;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

struct DispatchGuardInner {
    /// Whether to queue on the preinit buffer or on the unbounded queue
    queue_preinit: AtomicBool,

    /// The number of items that were added to the queue after it filled up.
    overflow_count: AtomicUsize,

    /// The maximum pre-init queue size
    max_queue_size: usize,

    /// Used to unblock the worker thread initially.
    block_sender: Sender<Blocked>,

    /// Sender for the preinit queue.
    preinit_sender: Sender<Command>,

    /// Sender for the unbounded queue.
    sender: Sender<Command>,
}

impl DispatchGuard {
    pub fn launch(&self, task: impl FnOnce() + Send + 'static) -> Result<(), DispatchError> {
        let task = Command::Task(Box::new(task));
        self.send(task)
    }

    pub fn shutdown(&mut self) -> Result<(), DispatchError> {
        // Need to flush in order for the thread to actually process anything,
        // including the shutdown command.
        self.flush_init(|_| {}).ok();
        self.send(Command::Shutdown)
    }

    fn send(&self, task: Command) -> Result<(), DispatchError> {
        if self.queue_preinit.load(Ordering::SeqCst) {
            if self.preinit_sender.len() < self.max_queue_size {
                self.preinit_sender.send(task)?;
                Ok(())
            } else {
                self.overflow_count.fetch_add(1, Ordering::SeqCst);
                // Instead of using a bounded queue, we are handling the bounds
                // checking ourselves. If a bounded queue were full, we would return
                // a QueueFull DispatchError, so we do the same here.
                Err(DispatchError::QueueFull)
            }
        } else {
            self.sender.send(task)?;
            Ok(())
        }
    }

    fn block_on_queue(&self) {
        let (tx, rx) = crossbeam_channel::bounded(0);

        // We explicitly don't use `self.launch` here.
        // We always put this task on the unbounded queue.
        // The pre-init queue might be full before its flushed, in which case this would panic.
        // Blocking on the queue can only work if it is eventually flushed anyway.

        let task = Command::Task(Box::new(move || {
            tx.send(())
                .expect("(worker) Can't send message on single-use channel");
        }));
        self.sender
            .send(task)
            .expect("Failed to launch the blocking task");

        rx.recv()
            .expect("Failed to receive message on single-use channel");
    }

    /// Block on the task queue emptying, with a timeout.
    fn block_on_queue_timeout(&self, timeout: Duration) -> Result<(), RecvTimeoutError> {
        let (tx, rx) = crossbeam_channel::bounded(0);

        // We explicitly don't use `self.launch` here.
        // We always put this task on the unbounded queue.
        // The pre-init queue might be full before its flushed, in which case this would panic.
        // Blocking on the queue can only work if it is eventually flushed anyway.

        let task = Command::Task(Box::new(move || {
            // In case the calling thread times out waiting for this
            // the channel will be dropped.
            // But in case the work continues we don't want to panic.
            _ = tx.send(());
        }));
        self.sender
            .send(task)
            .expect("Failed to launch the blocking task");

        rx.recv_timeout(timeout)
    }

    fn kill(&mut self) -> Result<(), DispatchError> {
        // We immediately stop queueing in the pre-init buffer.
        let old_val = self.queue_preinit.swap(false, Ordering::SeqCst);
        if !old_val {
            return Err(DispatchError::AlreadyFlushed);
        }

        // Unblock the worker thread exactly once.
        self.block_sender.send(Blocked::Shutdown)?;
        Ok(())
    }

    /// Flushes the pre-init buffer, running `final_task` as the last pre-init task.
    ///
    /// This function blocks until tasks queued prior to this call are finished.
    /// Once the initial queue is empty the dispatcher will wait for new tasks to be launched.
    ///
    /// `final_task` is passed the number of tasks that overflowed the pre-init buffer.
    /// It is queued regardless of the maximum queue size, so it runs even if the buffer is full.
    ///
    /// Returns an error if called multiple times.
    fn flush_init(
        &mut self,
        final_task: impl FnOnce(usize) + Send + 'static,
    ) -> Result<usize, DispatchError> {
        // We immediately stop queueing in the pre-init buffer.
        let old_val = self.queue_preinit.swap(false, Ordering::SeqCst);
        if !old_val {
            return Err(DispatchError::AlreadyFlushed);
        }

        // Unblock the worker thread exactly once.
        self.block_sender.send(Blocked::Continue)?;

        // Queue the final task behind all other pre-init tasks.
        // We explicitly don't use `self.launch` here, which would discard it if the buffer is full.
        let overflow_count = self.overflow_count.load(Ordering::SeqCst);
        self.preinit_sender
            .send(Command::Task(Box::new(move || final_task(overflow_count))))
            .map_err(|_| DispatchError::SendError)?;

        // Single-use channel to communicate with the worker thread.
        let (swap_sender, swap_receiver) = bounded(0);

        // Send final command and block until it is sent.
        self.preinit_sender
            .send(Command::Swap(swap_sender))
            .map_err(|_| DispatchError::SendError)?;

        // Now wait for the worker thread to do the swap and inform us.
        // This blocks until all tasks in the preinit buffer have been processed.
        swap_receiver.recv()?;

        // We're not queueing anymore.
        global::QUEUE_TASKS.store(false, Ordering::SeqCst);

        let overflow_count = self.overflow_count.load(Ordering::SeqCst);
        if overflow_count > 0 {
            Ok(overflow_count)
        } else {
            Ok(0)
        }
    }
}

/// A dispatcher.
///
/// Run expensive processing tasks sequentially off the main thread.
/// Tasks are processed in a single separate thread in the order they are submitted.
/// The dispatch queue will enqueue tasks while not flushed, up to the maximum queue size.
/// Processing will start after flushing once, processing already enqueued tasks first, then
/// waiting for further tasks to be enqueued.
pub struct Dispatcher {
    /// Guard used for communication with the worker thread.
    guard: DispatchGuard,

    /// Handle to the worker thread, allows to wait for it to finish.
    worker: Option<JoinHandle<()>>,
}

impl Dispatcher {
    /// Creates a new dispatcher with a maximum queue size.
    ///
    /// Launched tasks won't run until [`flush_init`] is called.
    ///
    /// [`flush_init`]: #method.flush_init
    pub fn new(max_queue_size: usize) -> Self {
        let (block_sender, block_receiver) = bounded(1);
        let (preinit_sender, preinit_receiver) = unbounded();
        let (sender, mut unbounded_receiver) = unbounded();

        let queue_preinit = AtomicBool::new(true);
        let overflow_count = AtomicUsize::new(0);

        let worker = crate::thread::spawn("glean.dispatcher", move || {
            match block_receiver.recv() {
                Err(_) => {
                    // The other side was disconnected.
                    // There's nothing the worker thread can do.
                    log::error!("The task producer was disconnected. Worker thread will exit.");
                    return;
                }
                Ok(Blocked::Shutdown) => {
                    // The other side wants us to stop immediately
                    return;
                }
                Ok(Blocked::Continue) => {
                    // Queue is unblocked, processing continues as normal.
                }
            }

            let mut receiver = preinit_receiver;
            loop {
                use Command::*;

                match receiver.recv() {
                    Ok(Shutdown) => {
                        break;
                    }

                    Ok(Task(f)) => {
                        (f)();
                    }

                    Ok(Swap(swap_done)) => {
                        // A swap should only occur exactly once.
                        // This is upheld by `flush_init`, which errors out if the preinit buffer
                        // was already flushed.

                        // We swap the channels we listen on for new tasks.
                        // The next iteration will continue with the unbounded queue.
                        mem::swap(&mut receiver, &mut unbounded_receiver);

                        // The swap command MUST be the last one received on the preinit buffer,
                        // so by the time we run this we know all preinit tasks were processed.
                        // We can notify the other side.
                        swap_done
                            .send(())
                            .expect("The caller of `flush_init` has gone missing");
                    }

                    // Other side was disconnected.
                    Err(_) => {
                        log::error!("The task producer was disconnected. Worker thread will exit.");
                        return;
                    }
                }
            }
        })
        .expect("Failed to spawn Glean's dispatcher thread");

        let inner = Arc::new(DispatchGuardInner {
            queue_preinit,
            overflow_count,
            max_queue_size,
            block_sender,
            preinit_sender,
            sender,
        });
        let guard = DispatchGuard { inner };

        Dispatcher {
            guard,
            worker: Some(worker),
        }
    }

    fn guard(&self) -> DispatchGuard {
        self.guard.clone()
    }

    /// Waits for the worker thread to finish and finishes the dispatch queue.
    ///
    /// You need to call `shutdown` to initiate a shutdown of the queue.
    #[cfg(test)]
    fn join(mut self) -> Result<(), DispatchError> {
        if let Some(worker) = self.worker.take() {
            worker.join().map_err(|_| DispatchError::WorkerPanic)?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::sync::atomic::AtomicU8;
    use std::sync::Mutex;
    use std::thread;

    fn enable_test_logging() {
        // When testing we want all logs to go to stdout/stderr by default,
        // without requiring each individual test to activate it.
        let _ = env_logger::builder().is_test(true).try_init();
    }

    #[test]
    fn tasks_run_off_the_main_thread() {
        enable_test_logging();

        let main_thread_id = thread::current().id();
        let thread_canary = Arc::new(AtomicBool::new(false));

        let dispatcher = Dispatcher::new(100);

        // Force the Dispatcher out of the pre-init queue mode.
        dispatcher
            .guard()
            .flush_init(|_| {})
            .expect("Failed to get out of preinit queue mode");

        let canary_clone = thread_canary.clone();
        dispatcher
            .guard()
            .launch(move || {
                assert!(thread::current().id() != main_thread_id);
                // Use the canary bool to make sure this is getting called before
                // the test completes.
                assert!(!canary_clone.load(Ordering::SeqCst));
                canary_clone.store(true, Ordering::SeqCst);
            })
            .expect("Failed to dispatch the test task");

        dispatcher.guard().block_on_queue();
        assert!(thread_canary.load(Ordering::SeqCst));
        assert_eq!(main_thread_id, thread::current().id());
    }

    #[test]
    fn launch_correctly_adds_tasks_to_preinit_queue() {
        enable_test_logging();

        let main_thread_id = thread::current().id();
        let thread_canary = Arc::new(AtomicU8::new(0));

        let dispatcher = Dispatcher::new(100);

        // Add 3 tasks to queue each one increasing thread_canary by 1 to
        // signal that the tasks ran.
        for _ in 0..3 {
            let canary_clone = thread_canary.clone();
            dispatcher
                .guard()
                .launch(move || {
                    // Make sure the task is flushed off-the-main thread.
                    assert!(thread::current().id() != main_thread_id);
                    canary_clone.fetch_add(1, Ordering::SeqCst);
                })
                .expect("Failed to dispatch the test task");
        }

        // Ensure that no task ran.
        assert_eq!(0, thread_canary.load(Ordering::SeqCst));

        // Flush the queue and wait for the tasks to complete.
        dispatcher
            .guard()
            .flush_init(|_| {})
            .expect("Failed to get out of preinit queue mode");
        // Validate that we have the expected canary value.
        assert_eq!(3, thread_canary.load(Ordering::SeqCst));
    }

    #[test]
    fn preinit_tasks_are_processed_after_flush() {
        enable_test_logging();

        let dispatcher = Dispatcher::new(10);

        let result = Arc::new(Mutex::new(vec![]));
        for i in 1..=5 {
            let result = Arc::clone(&result);
            dispatcher
                .guard()
                .launch(move || {
                    result.lock().unwrap().push(i);
                })
                .unwrap();
        }

        result.lock().unwrap().push(0);
        dispatcher.guard().flush_init(|_| {}).unwrap();
        for i in 6..=10 {
            let result = Arc::clone(&result);
            dispatcher
                .guard()
                .launch(move || {
                    result.lock().unwrap().push(i);
                })
                .unwrap();
        }

        dispatcher.guard().block_on_queue();

        // This additionally checks that tasks were executed in order.
        assert_eq!(
            &*result.lock().unwrap(),
            &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        );
    }

    #[test]
    fn tasks_after_shutdown_are_not_processed() {
        enable_test_logging();

        let dispatcher = Dispatcher::new(10);

        let result = Arc::new(Mutex::new(vec![]));

        dispatcher.guard().flush_init(|_| {}).unwrap();

        dispatcher.guard().shutdown().unwrap();
        {
            let result = Arc::clone(&result);
            // This might fail because the shutdown is quick enough,
            // or it might succeed and still send the task.
            // In any case that task should not be executed.
            let _ = dispatcher.guard().launch(move || {
                result.lock().unwrap().push(0);
            });
        }

        dispatcher.join().unwrap();

        assert!(result.lock().unwrap().is_empty());
    }

    #[test]
    fn preinit_buffer_fills_up() {
        enable_test_logging();

        let dispatcher = Dispatcher::new(5);

        let result = Arc::new(Mutex::new(vec![]));

        for i in 1..=5 {
            let result = Arc::clone(&result);
            dispatcher
                .guard()
                .launch(move || {
                    result.lock().unwrap().push(i);
                })
                .unwrap();
        }

        {
            let result = Arc::clone(&result);
            let err = dispatcher.guard().launch(move || {
                result.lock().unwrap().push(10);
            });
            assert_eq!(Err(DispatchError::QueueFull), err);
        }

        dispatcher.guard().flush_init(|_| {}).unwrap();

        {
            let result = Arc::clone(&result);
            dispatcher
                .guard()
                .launch(move || {
                    result.lock().unwrap().push(20);
                })
                .unwrap();
        }

        dispatcher.guard().block_on_queue();

        assert_eq!(&*result.lock().unwrap(), &[1, 2, 3, 4, 5, 20]);
    }

    #[test]
    fn flush_init_runs_final_task_after_full_preinit_buffer() {
        enable_test_logging();

        let dispatcher = Dispatcher::new(5);

        let result = Arc::new(Mutex::new(vec![]));

        for i in 1..=5 {
            let result = Arc::clone(&result);
            dispatcher
                .guard()
                .launch(move || {
                    result.lock().unwrap().push(i);
                })
                .unwrap();
        }

        {
            let result = Arc::clone(&result);
            let err = dispatcher.guard().launch(move || {
                result.lock().unwrap().push(10);
            });
            assert_eq!(Err(DispatchError::QueueFull), err);
        }

        {
            let result = Arc::clone(&result);
            let overflow_count = dispatcher
                .guard()
                .flush_init(move |overflow_count| {
                    assert_eq!(1, overflow_count);
                    result.lock().unwrap().push(100);
                })
                .unwrap();
            assert_eq!(1, overflow_count);
        }

        {
            let result = Arc::clone(&result);
            dispatcher
                .guard()
                .launch(move || {
                    result.lock().unwrap().push(20);
                })
                .unwrap();
        }

        dispatcher.guard().block_on_queue();

        // The final task runs despite the full buffer, after all preinit tasks and before any later ones.
        assert_eq!(&*result.lock().unwrap(), &[1, 2, 3, 4, 5, 100, 20]);
    }

    #[test]
    fn normal_queue_is_unbounded() {
        enable_test_logging();

        // Note: We can't actually test that it's fully unbounded,
        // but we can quickly queue more slow tasks than the pre-init buffer holds
        // and then guarantuee they all run.

        let dispatcher = Dispatcher::new(5);

        let result = Arc::new(Mutex::new(vec![]));

        for i in 1..=5 {
            let result = Arc::clone(&result);
            dispatcher
                .guard()
                .launch(move || {
                    result.lock().unwrap().push(i);
                })
                .unwrap();
        }

        dispatcher.guard().flush_init(|_| {}).unwrap();

        // Queue more than 5 tasks,
        // Each one is slow to process, so we should be faster in queueing
        // them up than they are processed.
        for i in 6..=20 {
            let result = Arc::clone(&result);
            dispatcher
                .guard()
                .launch(move || {
                    thread::sleep(Duration::from_millis(50));
                    result.lock().unwrap().push(i);
                })
                .unwrap();
        }

        dispatcher.guard().shutdown().unwrap();
        dispatcher.join().unwrap();

        let expected = (1..=20).collect::<Vec<_>>();
        assert_eq!(&*result.lock().unwrap(), &expected);
    }
}
