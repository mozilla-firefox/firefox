// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Manages the pending pings queue and directory.
//!
//! * Keeps track of pending pings, loading any unsent ping from disk on startup;
//! * Exposes [`get_upload_task`](PingUploadManager::get_upload_task) API for
//!   the platform layer to request next upload task;
//! * Exposes
//!   [`process_ping_upload_response`](PingUploadManager::process_ping_upload_response)
//!   API to check the HTTP response from the ping upload and either delete the
//!   corresponding ping from disk or re-enqueue it for sending.

use std::collections::HashMap;
use std::collections::VecDeque;
use std::mem;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::{Arc, RwLock, RwLockWriteGuard};
use std::time::{Duration, Instant};

use chrono::Utc;
use malloc_size_of::MallocSizeOf;
use malloc_size_of_derive::MallocSizeOf;

use crate::error::ErrorKind;
use crate::TimerId;
use crate::{internal_metrics::UploadMetrics, Glean};
pub use directory::process_metadata;
use directory::{PingDirectoryManager, PingPayloadsByDirectory};
use policy::Policy;
use request::create_date_header_value;

use crate::database::StoredSubmittedPingHandler;
pub use directory::{PingMetadata, PingPayload};
pub use request::{HeaderMap, PingRequest};
pub use result::{UploadResult, UploadTaskAction};

mod directory;
mod policy;
mod request;
mod result;

const WAIT_TIME_FOR_PING_PROCESSING: u64 = 1000; // in milliseconds

#[derive(Debug, MallocSizeOf)]
struct RateLimiter {
    /// The instant the current interval has started.
    started: Option<Instant>,
    /// The count for the current interval.
    count: u32,
    /// The duration of each interval.
    interval: Duration,
    /// The maximum count per interval.
    max_count: u32,
}

/// An enum to represent the current state of the RateLimiter.
#[derive(PartialEq)]
enum RateLimiterState {
    /// The RateLimiter has not reached the maximum count and is still incrementing.
    Incrementing,
    /// The RateLimiter has reached the maximum count for the  current interval.
    ///
    /// This variant contains the remaining time (in milliseconds)
    /// until the rate limiter is not throttled anymore.
    Throttled(u64),
}

impl RateLimiter {
    pub fn new(interval: Duration, max_count: u32) -> Self {
        Self {
            started: None,
            count: 0,
            interval,
            max_count,
        }
    }

    fn reset(&mut self) {
        self.started = Some(Instant::now());
        self.count = 0;
    }

    fn elapsed(&self) -> Duration {
        self.started.unwrap().elapsed()
    }

    // The counter should reset if
    //
    // 1. It has never started;
    // 2. It has been started more than the interval time ago;
    // 3. Something goes wrong while trying to calculate the elapsed time since the last reset.
    fn should_reset(&self) -> bool {
        if self.started.is_none() {
            return true;
        }

        // Safe unwrap, we already stated that `self.started` is not `None` above.
        if self.elapsed() > self.interval {
            return true;
        }

        false
    }

    /// Tries to increment the internal counter.
    ///
    /// # Returns
    ///
    /// The current state of the RateLimiter.
    pub fn get_state(&mut self) -> RateLimiterState {
        if self.should_reset() {
            self.reset();
        }

        if self.count == self.max_count {
            // Note that `remining` can't be a negative number because we just called `reset`,
            // which will check if it is and reset if so.
            let remaining = self.interval.as_millis() - self.elapsed().as_millis();
            return RateLimiterState::Throttled(
                remaining
                    .try_into()
                    .unwrap_or(self.interval.as_secs() * 1000),
            );
        }

        self.count += 1;
        RateLimiterState::Incrementing
    }
}

/// An enum representing the possible upload tasks to be performed by an uploader.
///
/// When asking for the next ping request to upload,
/// the requester may receive one out of three possible tasks.
#[derive(PartialEq, Eq, Debug)]
pub enum PingUploadTask {
    /// An upload task
    Upload {
        /// The ping request for upload
        /// See [`PingRequest`](struct.PingRequest.html) for more information.
        request: PingRequest,
    },

    /// A flag signaling that the pending pings directories are not done being processed,
    /// thus the requester should wait and come back later.
    Wait {
        /// The time in milliseconds
        /// the requester should wait before requesting a new task.
        time: u64,
    },

    /// A flag signaling that requester doesn't need to request any more upload tasks at this moment.
    ///
    /// There are three possibilities for this scenario:
    /// * Pending pings queue is empty, no more pings to request;
    /// * Requester has gotten more than MAX_WAIT_ATTEMPTS (3, by default) `PingUploadTask::Wait` responses in a row;
    /// * Requester has reported more than MAX_RECOVERABLE_FAILURES_PER_UPLOADING_WINDOW
    ///   recoverable upload failures on the same uploading window (see below)
    ///   and should stop requesting at this moment.
    ///
    /// An "uploading window" starts when a requester gets a new
    /// `PingUploadTask::Upload(PingRequest)` response and finishes when they
    /// finally get a `PingUploadTask::Done` or `PingUploadTask::Wait` response.
    Done {
        #[doc(hidden)]
        /// Unused field. Required because UniFFI can't handle variants without fields.
        unused: i8,
    },
}

impl PingUploadTask {
    /// Whether the current task is an upload task.
    pub fn is_upload(&self) -> bool {
        matches!(self, PingUploadTask::Upload { .. })
    }

    /// Whether the current task is wait task.
    pub fn is_wait(&self) -> bool {
        matches!(self, PingUploadTask::Wait { .. })
    }

    pub(crate) fn done() -> Self {
        PingUploadTask::Done { unused: 0 }
    }
}

/// Manages the pending pings queue and directory.
#[derive(Debug)]
pub struct PingUploadManager {
    /// A FIFO queue storing a `PingRequest` for each pending ping.
    queue: RwLock<VecDeque<PingRequest>>,
    /// A manager for the pending pings directories.
    directory_manager: PingDirectoryManager,
    /// A flag signaling if we are done processing the pending pings directories.
    processed_pending_pings: Arc<AtomicBool>,
    /// A vector to store the pending pings processed off-thread.
    cached_pings: Arc<RwLock<PingPayloadsByDirectory>>,
    /// The number of upload failures for the current uploading window.
    recoverable_failure_count: AtomicU32,
    /// The number or times in a row a user has received a `PingUploadTask::Wait` response.
    wait_attempt_count: AtomicU32,
    /// A ping counter to help rate limit the ping uploads.
    ///
    /// To keep resource usage in check,
    /// we may want to limit the amount of pings sent in a given interval.
    rate_limiter: Option<RwLock<RateLimiter>>,
    /// The name of the programming language used by the binding creating this instance of PingUploadManager.
    ///
    /// This will be used to build the value User-Agent header for each ping request.
    language_binding_name: String,
    /// Metrics related to ping uploading.
    upload_metrics: UploadMetrics,
    /// Policies for ping storage, uploading and requests.
    policy: Policy,

    in_flight: RwLock<HashMap<String, (TimerId, TimerId)>>,
}

impl MallocSizeOf for PingUploadManager {
    fn size_of(&self, ops: &mut malloc_size_of::MallocSizeOfOps) -> usize {
        let shallow_size = {
            let queue = self.queue.read().unwrap();
            if ops.has_malloc_enclosing_size_of() {
                if let Some(front) = queue.front() {
                    // SAFETY: The front element is a valid interior pointer and thus valid to pass
                    // to an external function.
                    unsafe { ops.malloc_enclosing_size_of(front) }
                } else {
                    // This assumes that no memory is allocated when the VecDeque is empty.
                    0
                }
            } else {
                // If `ops` can't estimate the size of a pointer,
                // we can estimate the allocation size by the size of each element and the
                // allocated capacity.
                queue.capacity() * mem::size_of::<PingRequest>()
            }
        };

        let mut n = shallow_size
            + self.directory_manager.size_of(ops)
            + mem::size_of::<AtomicBool>() // Allocated inside the `self.processed_pending_pings` `Arc`.
            + self.cached_pings.read().unwrap().size_of(ops)
            + self.rate_limiter.as_ref().map(|rl| {
                let lock = rl.read().unwrap();
                (*lock).size_of(ops)
            }).unwrap_or(0)
            + self.language_binding_name.size_of(ops)
            + self.upload_metrics.size_of(ops)
            + self.policy.size_of(ops);

        let in_flight = self.in_flight.read().unwrap();
        n += in_flight.size_of(ops);

        n
    }
}

impl PingUploadManager {
    /// Creates a new PingUploadManager.
    ///
    /// # Arguments
    ///
    /// * `data_path` - Path to the pending pings directory.
    /// * `language_binding_name` - The name of the language binding calling this managers instance.
    ///
    /// # Panics
    ///
    /// Will panic if unable to spawn a new thread.
    pub fn new<P: Into<PathBuf>>(data_path: P, language_binding_name: &str) -> Self {
        Self {
            queue: RwLock::new(VecDeque::new()),
            directory_manager: PingDirectoryManager::new(data_path),
            processed_pending_pings: Arc::new(AtomicBool::new(false)),
            cached_pings: Arc::new(RwLock::new(PingPayloadsByDirectory::default())),
            recoverable_failure_count: AtomicU32::new(0),
            wait_attempt_count: AtomicU32::new(0),
            rate_limiter: None,
            language_binding_name: language_binding_name.into(),
            upload_metrics: UploadMetrics::new(),
            policy: Policy::default(),
            in_flight: RwLock::new(HashMap::default()),
        }
    }

    /// Spawns a new thread and processes the pending pings directories,
    /// filling up the queue with whatever pings are in there.
    ///
    /// # Returns
    ///
    /// The `JoinHandle` to the spawned thread
    pub fn scan_pending_pings_directories(
        &self,
        trigger_upload: bool,
    ) -> std::thread::JoinHandle<()> {
        let local_manager = self.directory_manager.clone();
        let local_cached_pings = self.cached_pings.clone();
        let local_flag = self.processed_pending_pings.clone();
        crate::thread::spawn("glean.ping_directory_manager.process_dir", move || {
            {
                // Be sure to drop local_cached_pings lock before triggering upload.
                let mut local_cached_pings = local_cached_pings
                    .write()
                    .expect("Can't write to pending pings cache.");
                local_cached_pings.extend(local_manager.process_dirs());
                local_flag.store(true, Ordering::SeqCst);
            }
            if trigger_upload {
                crate::dispatcher::launch(|| {
                    if let Some(state) = crate::maybe_global_state().and_then(|s| s.lock().ok()) {
                        if let Err(e) = state.callbacks.trigger_upload() {
                            log::error!(
                                "Triggering upload after pending ping scan failed. Error: {}",
                                e
                            );
                        }
                    }
                });
            }
        })
        .expect("Unable to spawn thread to process pings directories.")
    }

    /// Creates a new upload manager with no limitations, for tests.
    #[cfg(test)]
    pub fn no_policy<P: Into<PathBuf>>(data_path: P) -> Self {
        let mut upload_manager = Self::new(data_path, "Test");

        // Disable all policies for tests, if necessary individuals tests can re-enable them.
        upload_manager.policy.set_max_recoverable_failures(None);
        upload_manager.policy.set_max_wait_attempts(None);
        upload_manager.policy.set_max_ping_body_size(None);
        upload_manager
            .policy
            .set_max_pending_pings_directory_size(None);
        upload_manager.policy.set_max_pending_pings_count(None);

        // When building for tests, always scan the pending pings directories and do it sync.
        upload_manager
            .scan_pending_pings_directories(false)
            .join()
            .unwrap();

        upload_manager
    }

    fn processed_pending_pings(&self) -> bool {
        self.processed_pending_pings.load(Ordering::SeqCst)
    }

    fn recoverable_failure_count(&self) -> u32 {
        self.recoverable_failure_count.load(Ordering::SeqCst)
    }

    fn wait_attempt_count(&self) -> u32 {
        self.wait_attempt_count.load(Ordering::SeqCst)
    }

    /// Attempts to build a ping request from a ping file payload.
    ///
    /// Returns the `PingRequest` or `None` if unable to build,
    /// in which case it will delete the ping file and record an error.
    fn build_ping_request(&self, glean: &Glean, ping: PingPayload) -> Option<PingRequest> {
        let PingPayload {
            document_id,
            upload_path: path,
            json_body: body,
            headers,
            body_has_info_sections,
            ping_name,
            uploader_capabilities,
        } = ping;
        let mut request = PingRequest::builder(
            &self.language_binding_name,
            self.policy.max_ping_body_size(),
        )
        .document_id(&document_id)
        .path(path)
        .body(body)
        .body_has_info_sections(body_has_info_sections)
        .ping_name(ping_name)
        .uploader_capabilities(uploader_capabilities);

        if let Some(headers) = headers {
            request = request.headers(headers);
        }

        match request.build() {
            Ok(request) => Some(request),
            Err(e) => {
                log::warn!("Error trying to build ping request: {}", e);
                self.directory_manager.delete_file(&document_id);

                // Record the error.
                // Currently the only possible error is PingBodyOverflow.
                if let ErrorKind::PingBodyOverflow(s) = e.kind() {
                    self.upload_metrics
                        .discarded_exceeding_pings_size
                        .accumulate_sync(glean, *s as i64 / 1024);
                }

                None
            }
        }
    }

    /// Enqueue a ping for upload.
    pub fn enqueue_ping(&self, glean: &Glean, ping: PingPayload) {
        let mut queue = self
            .queue
            .write()
            .expect("Can't write to pending pings queue.");

        let PingPayload {
            ref document_id,
            upload_path: ref path,
            ..
        } = ping;
        // Checks if a ping with this `document_id` is already enqueued.
        if queue
            .iter()
            .any(|request| request.document_id.as_str() == document_id)
        {
            log::warn!(
                "Attempted to enqueue a duplicate ping {} at {}.",
                document_id,
                path
            );
            return;
        }

        {
            let in_flight = self.in_flight.read().unwrap();
            if in_flight.contains_key(document_id) {
                log::warn!(
                    "Attempted to enqueue an in-flight ping {} at {}.",
                    document_id,
                    path
                );
                self.upload_metrics
                    .in_flight_pings_dropped
                    .add_sync(glean, 0);
                return;
            }
        }

        log::trace!("Enqueuing ping {} at {}", document_id, path);
        if let Some(request) = self.build_ping_request(glean, ping) {
            queue.push_back(request)
        }
    }

    /// Enqueues pings that might have been cached.
    ///
    /// The size of the PENDING_PINGS_DIRECTORY directory will be calculated
    /// (by accumulating each ping's size in that directory)
    /// and in case we exceed the quota, defined by the `quota` arg,
    /// outstanding pings get deleted and are not enqueued.
    ///
    /// The size of the DELETION_REQUEST_PINGS_DIRECTORY will not be calculated
    /// and no deletion-request pings will be deleted. Deletion request pings
    /// are not very common and usually don't contain any data,
    /// we don't expect that directory to ever reach quota.
    /// Most importantly, we don't want to ever delete deletion-request pings.
    ///
    /// # Arguments
    ///
    /// * `glean` - The Glean object holding the database.
    fn enqueue_cached_pings(&self, glean: &Glean) {
        let mut cached_pings = self
            .cached_pings
            .write()
            .expect("Can't write to pending pings cache.");

        if cached_pings.len() > 0 {
            let mut pending_pings_directory_size: u64 = 0;
            let mut pending_pings_count = 0;
            let mut deleting = false;
            let mut delete_reason: Option<&'static str> = None;

            let total = cached_pings.pending_pings.len() as u64;
            self.upload_metrics
                .pending_pings
                .add_sync(glean, total.try_into().unwrap_or(0));

            if total > self.policy.max_pending_pings_count() {
                log::warn!(
                    "More than {} pending pings in the directory, will delete {} old pings.",
                    self.policy.max_pending_pings_count(),
                    total - self.policy.max_pending_pings_count()
                );
            }

            // The pending pings vector is sorted by date in ascending order (oldest -> newest).
            // We need to calculate the size of the pending pings directory
            // and delete the **oldest** pings in case quota is reached.
            // Thus, we reverse the order of the pending pings vector,
            // so that we iterate in descending order (newest -> oldest).
            cached_pings.pending_pings.reverse();
            cached_pings.pending_pings.retain(|(file_size, PingPayload {document_id, ..})| {
                pending_pings_count += 1;
                pending_pings_directory_size += file_size;

                // We don't want to spam the log for every ping over the quota.
                // Size is checked first; if both limits are exceeded simultaneously,
                // size_quota takes precedence as the recorded reason.
                if !deleting && pending_pings_directory_size > self.policy.max_pending_pings_directory_size() {
                    log::warn!(
                        "Pending pings directory has reached the size quota of {} bytes, outstanding pings will be deleted.",
                        self.policy.max_pending_pings_directory_size()
                    );
                    deleting = true;
                    delete_reason = Some("size_quota");
                }

                // Once we reach the number of allowed pings we start deleting,
                // no matter what size.
                // We already log this before the loop.
                if !deleting && pending_pings_count > self.policy.max_pending_pings_count() {
                    deleting = true;
                    delete_reason = Some("count_quota");
                }

                if deleting && self.directory_manager.delete_file(document_id) {
                    self.upload_metrics
                        .deleted_pings_after_quota_hit
                        .add_sync(glean, 1);
                    if let Some(reason) = delete_reason {
                        self.upload_metrics
                            .pending_pings_deleted
                            .get(reason)
                            .add_sync(glean, 1);
                    }
                    return false;
                }

                true
            });
            // After calculating the size of the pending pings directory,
            // we record the calculated number and reverse the pings array back for enqueueing.
            cached_pings.pending_pings.reverse();
            self.upload_metrics
                .pending_pings_directory_size
                .accumulate_sync(glean, pending_pings_directory_size as i64 / 1024);

            // Enqueue the remaining pending pings and
            // enqueue all deletion-request pings.
            cached_pings
                .deletion_request_pings
                .drain(..)
                .for_each(|(_, ping)| self.enqueue_ping(glean, ping));
            cached_pings
                .pending_pings
                .drain(..)
                .for_each(|(_, ping)| self.enqueue_ping(glean, ping));
        }
    }

    /// Adds rate limiting capability to this upload manager.
    ///
    /// The rate limiter will limit the amount of calls to `get_upload_task` per interval.
    ///
    /// Setting this will restart count and timer in case there was a previous rate limiter set
    /// (e.g. if we have reached the current limit and call this function, we start counting again
    /// and the caller is allowed to asks for tasks).
    ///
    /// # Arguments
    ///
    /// * `interval` - the amount of seconds in each rate limiting window.
    /// * `max_tasks` - the maximum amount of task requests allowed per interval.
    pub fn set_rate_limiter(&mut self, interval: u64, max_tasks: u32) {
        self.rate_limiter = Some(RwLock::new(RateLimiter::new(
            Duration::from_secs(interval),
            max_tasks,
        )));
    }

    pub(crate) fn set_max_pending_pings_count(&mut self, n: u64) {
        self.policy.set_max_pending_pings_count(Some(n));
    }

    pub(crate) fn set_max_pending_pings_directory_size(&mut self, n: u64) {
        self.policy.set_max_pending_pings_directory_size(Some(n));
    }

    /// Reads a ping file, creates a `PingRequest` and adds it to the queue.
    ///
    /// Duplicate requests won't be added.
    ///
    /// # Arguments
    ///
    /// * `glean` - The Glean object holding the database.
    /// * `document_id` - The UUID of the ping in question.
    pub fn enqueue_ping_from_file(&self, glean: &Glean, document_id: &str) {
        if let Some(ping) = self.directory_manager.process_file(document_id) {
            self.enqueue_ping(glean, ping);
        }
    }

    /// Clears the pending pings queue, leaves the deletion-request pings.
    pub fn clear_ping_queue(&self) -> RwLockWriteGuard<'_, VecDeque<PingRequest>> {
        log::trace!("Clearing ping queue");
        let mut queue = self
            .queue
            .write()
            .expect("Can't write to pending pings queue.");

        queue.retain(|ping| ping.is_deletion_request());
        log::trace!(
            "{} pings left in the queue (only deletion-request expected)",
            queue.len()
        );
        queue
    }

    fn get_upload_task_internal(&self, glean: &Glean, log_ping: bool) -> PingUploadTask {
        // Helper to decide whether to return PingUploadTask::Wait or PingUploadTask::Done.
        //
        // We want to limit the amount of PingUploadTask::Wait returned in a row,
        // in case we reach MAX_WAIT_ATTEMPTS we want to actually return PingUploadTask::Done.
        let wait_or_done = |time: u64| {
            self.wait_attempt_count.fetch_add(1, Ordering::SeqCst);
            if self.wait_attempt_count() > self.policy.max_wait_attempts() {
                PingUploadTask::done()
            } else {
                PingUploadTask::Wait { time }
            }
        };

        if !self.processed_pending_pings() {
            log::info!(
                "Tried getting an upload task, but processing is ongoing. Will come back later."
            );
            return wait_or_done(WAIT_TIME_FOR_PING_PROCESSING);
        }

        // This is a no-op in case there are no cached pings.
        self.enqueue_cached_pings(glean);

        if self.recoverable_failure_count() >= self.policy.max_recoverable_failures() {
            log::warn!(
                "Reached maximum recoverable failures for the current uploading window. You are done."
            );
            return PingUploadTask::done();
        }

        let mut queue = self
            .queue
            .write()
            .expect("Can't write to pending pings queue.");
        match queue.front() {
            Some(request) => {
                if let Some(rate_limiter) = &self.rate_limiter {
                    let mut rate_limiter = rate_limiter
                        .write()
                        .expect("Can't write to the rate limiter.");
                    if let RateLimiterState::Throttled(remaining) = rate_limiter.get_state() {
                        log::info!(
                            "Tried getting an upload task, but we are throttled at the moment."
                        );
                        return wait_or_done(remaining);
                    }
                }

                log::info!(
                    "New upload task with id {} (path: {})",
                    request.document_id,
                    request.path
                );

                if log_ping {
                    if let Some(body) = request.pretty_body() {
                        chunked_log_info(&request.path, &body);
                    } else {
                        chunked_log_info(&request.path, "<invalid ping payload>");
                    }
                }

                {
                    // Synchronous timer starts.
                    // We're in the uploader thread anyway.
                    // But also: No data is stored on disk.
                    let mut in_flight = self.in_flight.write().unwrap();
                    let success_id = self.upload_metrics.send_success.start_sync();
                    let failure_id = self.upload_metrics.send_failure.start_sync();
                    in_flight.insert(request.document_id.clone(), (success_id, failure_id));
                }

                let mut request = queue.pop_front().unwrap();

                // Adding the `Date` header just before actual upload happens.
                request
                    .headers
                    .insert("Date".to_string(), create_date_header_value(Utc::now()));

                PingUploadTask::Upload { request }
            }
            None => {
                log::info!("No more pings to upload! You are done.");
                PingUploadTask::done()
            }
        }
    }

    /// Gets the next `PingUploadTask`.
    ///
    /// # Arguments
    ///
    /// * `glean` - The Glean object holding the database.
    /// * `log_ping` - Whether to log the ping before returning.
    ///
    /// # Returns
    ///
    /// The next [`PingUploadTask`](enum.PingUploadTask.html).
    pub fn get_upload_task(&self, glean: &Glean, log_ping: bool) -> PingUploadTask {
        let task = self.get_upload_task_internal(glean, log_ping);

        if !task.is_wait() && self.wait_attempt_count() > 0 {
            self.wait_attempt_count.store(0, Ordering::SeqCst);
        }

        if !task.is_upload() && self.recoverable_failure_count() > 0 {
            self.recoverable_failure_count.store(0, Ordering::SeqCst);
        }

        task
    }

    /// Processes the response from an attempt to upload a ping.
    ///
    /// Based on the HTTP status of said response,
    /// the possible outcomes are:
    ///
    /// * **200 - 299 Success**
    ///   Any status on the 2XX range is considered a succesful upload,
    ///   which means the corresponding ping file can be deleted.
    ///   _Known 2XX status:_
    ///   * 200 - OK. Request accepted into the pipeline.
    ///
    /// * **408 Request timeout - Recoverable error**
    ///   The endpoint might be currently timing out. A later request might succeed again.
    /// * **429 Too Many Requests - Recoverable error**
    ///   A client might have sent too many requests and the endpoint is rejecting requests
    ///   for the moment.
    /// * **400 - 499 Unrecoverable error**
    ///   Any other status on the 4XX range means something our client did is not correct.
    ///   It is unlikely that the client is going to recover from this by retrying,
    ///   so in this case the corresponding ping file can also be deleted.
    ///   _Known 4XX status:_
    ///   * 404 - not found - POST/PUT to an unknown namespace
    ///   * 405 - wrong request type (anything other than POST/PUT)
    ///   * 411 - missing content-length header
    ///   * 413 - request body too large Note that if we have badly-behaved clients that
    ///           retry on 4XX, we should send back 202 on body/path too long).
    ///   * 414 - request path too long (See above)
    ///
    /// * **Any other error**
    ///   For any other error, a warning is logged and the ping is re-enqueued.
    ///   _Known other errors:_
    ///   * 500 - internal error
    ///
    /// # Note
    ///
    /// The disk I/O performed by this function is not done off-thread,
    /// as it is expected to be called off-thread by the platform.
    ///
    /// # Arguments
    ///
    /// * `glean` - The Glean object holding the database.
    /// * `document_id` - The UUID of the ping in question.
    /// * `status` - The HTTP status of the response.
    pub fn process_ping_upload_response(
        &self,
        glean: &Glean,
        document_id: &str,
        status: UploadResult,
    ) -> UploadTaskAction {
        use UploadResult::*;

        let stop_time = zeitstempel::now_awake();

        if let Some(label) = status.get_label() {
            let metric = self.upload_metrics.ping_upload_failure.get(label);
            metric.add_sync(glean, 1);
        }

        let send_ids = {
            let mut lock = self.in_flight.write().unwrap();
            lock.remove(document_id)
        };

        if send_ids.is_none() {
            self.upload_metrics.missing_send_ids.add_sync(glean, 1);
        }

        let recoverable = || {
            log::warn!(
                    "Recoverable upload failure while attempting to send ping {}, will retry. Error was {:?}",
                    document_id,
                    status
                );
            if let Some((success_id, failure_id)) = send_ids {
                self.upload_metrics.send_success.cancel_sync(success_id);
                self.upload_metrics
                    .send_failure
                    .set_stop_and_accumulate(glean, failure_id, stop_time);
            }
            self.enqueue_ping_from_file(glean, document_id);
            self.recoverable_failure_count
                .fetch_add(1, Ordering::SeqCst);
        };

        match status {
            HttpStatus { code } if (200..=299).contains(&code) => {
                log::info!("Ping {} successfully sent {}.", document_id, code);
                if let Some((success_id, failure_id)) = send_ids {
                    self.upload_metrics
                        .send_success
                        .set_stop_and_accumulate(glean, success_id, stop_time);
                    self.upload_metrics.send_failure.cancel_sync(failure_id);
                }
                if glean.store_submitted_pings_enabled {
                    glean
                        .storage()
                        .mark_ping_as_uploaded(document_id, Utc::now());
                }
                self.directory_manager.delete_file(document_id);
            }

            RecoverableFailure { .. } => {
                recoverable();
            }

            // Special-casing 408 - Request timeout, and 429 - Too Many Requests
            HttpStatus { code: 408 } | HttpStatus { code: 429 } => {
                recoverable();
            }

            UnrecoverableFailure { .. } | HttpStatus { code: 400..=499 } | Incapable { .. } => {
                log::warn!(
                    "Unrecoverable upload failure while attempting to send ping {}. Error was {:?}",
                    document_id,
                    status
                );
                if let Some((success_id, failure_id)) = send_ids {
                    self.upload_metrics.send_success.cancel_sync(success_id);
                    self.upload_metrics
                        .send_failure
                        .set_stop_and_accumulate(glean, failure_id, stop_time);
                }
                if glean.store_submitted_pings_enabled {
                    glean.storage().mark_ping_as_upload_failed(document_id);
                }
                self.directory_manager.delete_file(document_id);
            }

            // All other HTTP status codes are treated recoverable.
            HttpStatus { .. } => {
                recoverable();
            }

            Done { .. } => {
                log::debug!("Uploader signaled Done. Exiting.");
                if let Some((success_id, failure_id)) = send_ids {
                    self.upload_metrics.send_success.cancel_sync(success_id);
                    self.upload_metrics.send_failure.cancel_sync(failure_id);
                }
                return UploadTaskAction::End;
            }
        };

        UploadTaskAction::Next
    }
}

/// Splits log message into chunks on Android.
#[cfg(target_os = "android")]
pub fn chunked_log_info(path: &str, payload: &str) {
    // Since the logcat ring buffer size is configurable, but it's 'max payload' size is not,
    // we must break apart long pings into chunks no larger than the max payload size of 4076b.
    // We leave some head space for our prefix.
    const MAX_LOG_PAYLOAD_SIZE_BYTES: usize = 4000;

    // If the length of the ping will fit within one logcat payload, then we can
    // short-circuit here and avoid some overhead, otherwise we must split up the
    // message so that we don't truncate it.
    if path.len() + payload.len() <= MAX_LOG_PAYLOAD_SIZE_BYTES {
        log::info!("Glean ping to URL: {}\n{}", path, payload);
        return;
    }

    // Otherwise we break it apart into chunks of smaller size,
    // prefixing it with the path and a counter.
    let mut start = 0;
    let mut end = MAX_LOG_PAYLOAD_SIZE_BYTES;
    let mut chunk_idx = 1;
    // Might be off by 1 on edge cases, but do we really care?
    let total_chunks = payload.len() / MAX_LOG_PAYLOAD_SIZE_BYTES + 1;

    while end < payload.len() {
        // Find char boundary from the end.
        // It's UTF-8, so it is within 4 bytes from here.
        for _ in 0..4 {
            if payload.is_char_boundary(end) {
                break;
            }
            end -= 1;
        }

        log::info!(
            "Glean ping to URL: {} [Part {} of {}]\n{}",
            path,
            chunk_idx,
            total_chunks,
            &payload[start..end]
        );

        // Move on with the string
        start = end;
        end = end + MAX_LOG_PAYLOAD_SIZE_BYTES;
        chunk_idx += 1;
    }

    // Print any suffix left
    if start < payload.len() {
        log::info!(
            "Glean ping to URL: {} [Part {} of {}]\n{}",
            path,
            chunk_idx,
            total_chunks,
            &payload[start..]
        );
    }
}

/// Logs payload in one go (all other OS).
#[cfg(not(target_os = "android"))]
pub fn chunked_log_info(_path: &str, payload: &str) {
    log::info!("{}", payload)
}

#[cfg(test)]
mod test {
    use std::thread;
    use uuid::Uuid;

    use super::*;
    use crate::metrics::PingType;
    use crate::{tests::new_glean, PENDING_PINGS_DIRECTORY};

    const PATH: &str = "/submit/app_id/ping_name/schema_version/doc_id";

    #[test]
    fn doesnt_error_when_there_are_no_pending_pings() {
        let (glean, _t) = new_glean(None);

        // Try and get the next request.
        // Verify request was not returned
        assert_eq!(glean.get_upload_task(), PingUploadTask::done());
    }

    #[test]
    fn returns_ping_request_when_there_is_one() {
        let (glean, dir) = new_glean(None);

        let upload_manager = PingUploadManager::no_policy(dir.path());

        // Enqueue a ping
        upload_manager.enqueue_ping(
            &glean,
            PingPayload {
                document_id: Uuid::new_v4().to_string(),
                upload_path: PATH.into(),
                json_body: "".into(),
                headers: None,
                body_has_info_sections: true,
                ping_name: "ping-name".into(),
                uploader_capabilities: vec![],
            },
        );

        // Try and get the next request.
        // Verify request was returned
        let task = upload_manager.get_upload_task(&glean, false);
        assert!(task.is_upload());
    }

    #[test]
    fn returns_as_many_ping_requests_as_there_are() {
        let (glean, dir) = new_glean(None);

        let upload_manager = PingUploadManager::no_policy(dir.path());

        // Enqueue a ping multiple times
        let n = 10;
        for _ in 0..n {
            upload_manager.enqueue_ping(
                &glean,
                PingPayload {
                    document_id: Uuid::new_v4().to_string(),
                    upload_path: PATH.into(),
                    json_body: "".into(),
                    headers: None,
                    body_has_info_sections: true,
                    ping_name: "ping-name".into(),
                    uploader_capabilities: vec![],
                },
            );
        }

        // Verify a request is returned for each submitted ping
        for _ in 0..n {
            let task = upload_manager.get_upload_task(&glean, false);
            assert!(task.is_upload());
        }

        // Verify that after all requests are returned, none are left
        assert_eq!(
            upload_manager.get_upload_task(&glean, false),
            PingUploadTask::done()
        );
    }

    #[test]
    fn limits_the_number_of_pings_when_there_is_rate_limiting() {
        let (glean, dir) = new_glean(None);

        let mut upload_manager = PingUploadManager::no_policy(dir.path());

        // Add a rate limiter to the upload mangager with max of 10 pings every 3 seconds.
        let max_pings_per_interval = 10;
        upload_manager.set_rate_limiter(3, 10);

        // Enqueue the max number of pings allowed per uploading window
        for _ in 0..max_pings_per_interval {
            upload_manager.enqueue_ping(
                &glean,
                PingPayload {
                    document_id: Uuid::new_v4().to_string(),
                    upload_path: PATH.into(),
                    json_body: "".into(),
                    headers: None,
                    body_has_info_sections: true,
                    ping_name: "ping-name".into(),
                    uploader_capabilities: vec![],
                },
            );
        }

        // Verify a request is returned for each submitted ping
        for _ in 0..max_pings_per_interval {
            let task = upload_manager.get_upload_task(&glean, false);
            assert!(task.is_upload());
        }

        // Enqueue just one more ping
        upload_manager.enqueue_ping(
            &glean,
            PingPayload {
                document_id: Uuid::new_v4().to_string(),
                upload_path: PATH.into(),
                json_body: "".into(),
                headers: None,
                body_has_info_sections: true,
                ping_name: "ping-name".into(),
                uploader_capabilities: vec![],
            },
        );

        // Verify that we are indeed told to wait because we are at capacity
        match upload_manager.get_upload_task(&glean, false) {
            PingUploadTask::Wait { time } => {
                // Wait for the uploading window to reset
                thread::sleep(Duration::from_millis(time));
            }
            _ => panic!("Expected upload manager to return a wait task!"),
        };

        let task = upload_manager.get_upload_task(&glean, false);
        assert!(task.is_upload());
    }

    #[test]
    fn clearing_the_queue_works_correctly() {
        let (glean, dir) = new_glean(None);

        let upload_manager = PingUploadManager::no_policy(dir.path());

        // Enqueue a ping multiple times
        for _ in 0..10 {
            upload_manager.enqueue_ping(
                &glean,
                PingPayload {
                    document_id: Uuid::new_v4().to_string(),
                    upload_path: PATH.into(),
                    json_body: "".into(),
                    headers: None,
                    body_has_info_sections: true,
                    ping_name: "ping-name".into(),
                    uploader_capabilities: vec![],
                },
            );
        }

        // Clear the queue
        drop(upload_manager.clear_ping_queue());

        // Verify there really isn't any ping in the queue
        assert_eq!(
            upload_manager.get_upload_task(&glean, false),
            PingUploadTask::done()
        );
    }

    #[test]
    fn clearing_the_queue_doesnt_clear_deletion_request_pings() {
        let (mut glean, _t) = new_glean(None);

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        // Submit the ping multiple times
        let n = 10;
        for _ in 0..n {
            ping_type.submit_sync(&glean, None);
        }

        glean
            .internal_pings
            .deletion_request
            .submit_sync(&glean, None);

        // Clear the queue
        drop(glean.upload_manager.clear_ping_queue());

        let upload_task = glean.get_upload_task();
        match upload_task {
            PingUploadTask::Upload { request } => assert!(request.is_deletion_request()),
            _ => panic!("Expected upload manager to return the next request!"),
        }

        // Verify there really isn't any other pings in the queue
        assert_eq!(glean.get_upload_task(), PingUploadTask::done());
    }

    #[test]
    fn fills_up_queue_successfully_from_disk() {
        let (mut glean, dir) = new_glean(None);

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        // Submit the ping multiple times
        let n = 10;
        for _ in 0..n {
            ping_type.submit_sync(&glean, None);
        }

        // Create a new upload manager pointing to the same data_path as the glean instance.
        let upload_manager = PingUploadManager::no_policy(dir.path());

        // Verify the requests were properly enqueued
        for _ in 0..n {
            let task = upload_manager.get_upload_task(&glean, false);
            assert!(task.is_upload());
        }

        // Verify that after all requests are returned, none are left
        assert_eq!(
            upload_manager.get_upload_task(&glean, false),
            PingUploadTask::done()
        );
    }

    #[test]
    fn processes_correctly_success_upload_response() {
        let (mut glean, dir) = new_glean(None);

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        // Submit a ping
        ping_type.submit_sync(&glean, None);

        // Get the pending ping directory path
        let pending_pings_dir = dir.path().join(PENDING_PINGS_DIRECTORY);

        // Get the submitted PingRequest
        match glean.get_upload_task() {
            PingUploadTask::Upload { request } => {
                // Simulate the processing of a sucessfull request
                let document_id = request.document_id;
                glean.process_ping_upload_response(&document_id, UploadResult::http_status(200));
                // Verify file was deleted
                assert!(!pending_pings_dir.join(document_id).exists());
            }
            _ => panic!("Expected upload manager to return the next request!"),
        }

        // Verify that after request is returned, none are left
        assert_eq!(glean.get_upload_task(), PingUploadTask::done());
    }

    #[test]
    fn processes_correctly_client_error_upload_response() {
        let (mut glean, dir) = new_glean(None);

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        // Submit a ping
        ping_type.submit_sync(&glean, None);

        // Get the pending ping directory path
        let pending_pings_dir = dir.path().join(PENDING_PINGS_DIRECTORY);

        // Get the submitted PingRequest
        match glean.get_upload_task() {
            PingUploadTask::Upload { request } => {
                // Simulate the processing of a client error
                let document_id = request.document_id;
                glean.process_ping_upload_response(&document_id, UploadResult::http_status(404));
                // Verify file was deleted
                assert!(!pending_pings_dir.join(document_id).exists());
            }
            _ => panic!("Expected upload manager to return the next request!"),
        }

        // Verify that after request is returned, none are left
        assert_eq!(glean.get_upload_task(), PingUploadTask::done());
    }

    #[test]
    fn processes_correctly_server_error_upload_response() {
        let (mut glean, _t) = new_glean(None);

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        // Submit a ping
        ping_type.submit_sync(&glean, None);

        // Get the submitted PingRequest
        match glean.get_upload_task() {
            PingUploadTask::Upload { request } => {
                // Simulate the processing of a client error
                let document_id = request.document_id;
                glean.process_ping_upload_response(&document_id, UploadResult::http_status(500));
                // Verify this ping was indeed re-enqueued
                match glean.get_upload_task() {
                    PingUploadTask::Upload { request } => {
                        assert_eq!(document_id, request.document_id);
                    }
                    _ => panic!("Expected upload manager to return the next request!"),
                }
            }
            _ => panic!("Expected upload manager to return the next request!"),
        }

        // Verify that after request is returned, none are left
        assert_eq!(glean.get_upload_task(), PingUploadTask::done());
    }

    #[test]
    fn processes_correctly_unrecoverable_upload_response() {
        let (mut glean, dir) = new_glean(None);

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        // Submit a ping
        ping_type.submit_sync(&glean, None);

        // Get the pending ping directory path
        let pending_pings_dir = dir.path().join(PENDING_PINGS_DIRECTORY);

        // Get the submitted PingRequest
        match glean.get_upload_task() {
            PingUploadTask::Upload { request } => {
                // Simulate the processing of a client error
                let document_id = request.document_id;
                glean.process_ping_upload_response(
                    &document_id,
                    UploadResult::unrecoverable_failure(),
                );
                // Verify file was deleted
                assert!(!pending_pings_dir.join(document_id).exists());
            }
            _ => panic!("Expected upload manager to return the next request!"),
        }

        // Verify that after request is returned, none are left
        assert_eq!(glean.get_upload_task(), PingUploadTask::done());
    }

    #[test]
    fn new_pings_are_added_while_upload_in_progress() {
        let (glean, dir) = new_glean(None);

        let upload_manager = PingUploadManager::no_policy(dir.path());

        let doc1 = Uuid::new_v4().to_string();
        let path1 = format!("/submit/app_id/test-ping/1/{}", doc1);

        let doc2 = Uuid::new_v4().to_string();
        let path2 = format!("/submit/app_id/test-ping/1/{}", doc2);

        // Enqueue a ping
        upload_manager.enqueue_ping(
            &glean,
            PingPayload {
                document_id: doc1.clone(),
                upload_path: path1,
                json_body: "".into(),
                headers: None,
                body_has_info_sections: true,
                ping_name: "test-ping".into(),
                uploader_capabilities: vec![],
            },
        );

        // Try and get the first request.
        let req = match upload_manager.get_upload_task(&glean, false) {
            PingUploadTask::Upload { request } => request,
            _ => panic!("Expected upload manager to return the next request!"),
        };
        assert_eq!(doc1, req.document_id);

        // Schedule the next one while the first one is "in progress"
        upload_manager.enqueue_ping(
            &glean,
            PingPayload {
                document_id: doc2.clone(),
                upload_path: path2,
                json_body: "".into(),
                headers: None,
                body_has_info_sections: true,
                ping_name: "test-ping".into(),
                uploader_capabilities: vec![],
            },
        );

        // Mark as processed
        upload_manager.process_ping_upload_response(
            &glean,
            &req.document_id,
            UploadResult::http_status(200),
        );

        // Get the second request.
        let req = match upload_manager.get_upload_task(&glean, false) {
            PingUploadTask::Upload { request } => request,
            _ => panic!("Expected upload manager to return the next request!"),
        };
        assert_eq!(doc2, req.document_id);

        // Mark as processed
        upload_manager.process_ping_upload_response(
            &glean,
            &req.document_id,
            UploadResult::http_status(200),
        );

        // ... and then we're done.
        assert_eq!(
            upload_manager.get_upload_task(&glean, false),
            PingUploadTask::done()
        );
    }

    #[test]
    fn adds_debug_view_header_to_requests_when_tag_is_set() {
        let (mut glean, _t) = new_glean(None);

        glean.set_debug_view_tag("valid-tag");

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        // Submit a ping
        ping_type.submit_sync(&glean, None);

        // Get the submitted PingRequest
        match glean.get_upload_task() {
            PingUploadTask::Upload { request } => {
                assert_eq!(request.headers.get("X-Debug-ID").unwrap(), "valid-tag")
            }
            _ => panic!("Expected upload manager to return the next request!"),
        }
    }

    #[test]
    fn duplicates_are_not_enqueued() {
        let (glean, dir) = new_glean(None);

        // Create a new upload manager so that we have access to its functions directly,
        // make it synchronous so we don't have to manually wait for the scanning to finish.
        let upload_manager = PingUploadManager::no_policy(dir.path());

        let doc_id = Uuid::new_v4().to_string();
        let path = format!("/submit/app_id/test-ping/1/{}", doc_id);

        // Try to enqueue a ping with the same doc_id twice
        upload_manager.enqueue_ping(
            &glean,
            PingPayload {
                document_id: doc_id.clone(),
                upload_path: path.clone(),
                json_body: "".into(),
                headers: None,
                body_has_info_sections: true,
                ping_name: "test-ping".into(),
                uploader_capabilities: vec![],
            },
        );
        upload_manager.enqueue_ping(
            &glean,
            PingPayload {
                document_id: doc_id,
                upload_path: path,
                json_body: "".into(),
                headers: None,
                body_has_info_sections: true,
                ping_name: "test-ping".into(),
                uploader_capabilities: vec![],
            },
        );

        // Get a task once
        let task = upload_manager.get_upload_task(&glean, false);
        assert!(task.is_upload());

        // There should be no more queued tasks
        assert_eq!(
            upload_manager.get_upload_task(&glean, false),
            PingUploadTask::done()
        );
    }

    #[test]
    fn maximum_of_recoverable_errors_is_enforced_for_uploading_window() {
        let (mut glean, dir) = new_glean(None);

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        // Submit the ping multiple times
        let n = 5;
        for _ in 0..n {
            ping_type.submit_sync(&glean, None);
        }

        let mut upload_manager = PingUploadManager::no_policy(dir.path());

        // Set a policy for max recoverable failures, this is usually disabled for tests.
        let max_recoverable_failures = 3;
        upload_manager
            .policy
            .set_max_recoverable_failures(Some(max_recoverable_failures));

        // Return the max recoverable error failures in a row
        for _ in 0..max_recoverable_failures {
            match upload_manager.get_upload_task(&glean, false) {
                PingUploadTask::Upload { request } => {
                    upload_manager.process_ping_upload_response(
                        &glean,
                        &request.document_id,
                        UploadResult::recoverable_failure(),
                    );
                }
                _ => panic!("Expected upload manager to return the next request!"),
            }
        }

        // Verify that after returning the max amount of recoverable failures,
        // we are done even though we haven't gotten all the enqueued requests.
        assert_eq!(
            upload_manager.get_upload_task(&glean, false),
            PingUploadTask::done()
        );

        // Verify all requests are returned when we try again.
        for _ in 0..n {
            let task = upload_manager.get_upload_task(&glean, false);
            assert!(task.is_upload());
        }
    }

    #[test]
    fn quota_is_enforced_when_enqueueing_cached_pings() {
        let (mut glean, dir) = new_glean(None);

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        // Submit the ping multiple times
        let n = 10;
        for _ in 0..n {
            ping_type.submit_sync(&glean, None);
        }

        let directory_manager = PingDirectoryManager::new(dir.path());
        let pending_pings = directory_manager.process_dirs().pending_pings;
        // The pending pings array is sorted by date in ascending order,
        // the newest element is the last one.
        let (_, newest_ping) = &pending_pings.last().unwrap();
        let PingPayload {
            document_id: newest_ping_id,
            ..
        } = &newest_ping;

        // Create a new upload manager pointing to the same data_path as the glean instance.
        let mut upload_manager = PingUploadManager::no_policy(dir.path());

        // Set the quota to just a little over the size on an empty ping file.
        // This way we can check that one ping is kept and all others are deleted.
        //
        // From manual testing I figured out an empty ping file is 324bytes,
        // I am setting this a little over just so that minor changes to the ping structure
        // don't immediatelly break this.
        upload_manager
            .policy
            .set_max_pending_pings_directory_size(Some(500));

        // Get a task once
        // One ping should have been enqueued.
        // Make sure it is the newest ping.
        match upload_manager.get_upload_task(&glean, false) {
            PingUploadTask::Upload { request } => assert_eq!(&request.document_id, newest_ping_id),
            _ => panic!("Expected upload manager to return the next request!"),
        }

        // Verify that no other requests were returned,
        // they should all have been deleted because pending pings quota was hit.
        assert_eq!(
            upload_manager.get_upload_task(&glean, false),
            PingUploadTask::done()
        );

        // Verify that the correct number of deleted pings was recorded
        assert_eq!(
            n - 1,
            upload_manager
                .upload_metrics
                .deleted_pings_after_quota_hit
                .get_value(&glean, Some("metrics"))
                .unwrap()
        );
        assert_eq!(
            n,
            upload_manager
                .upload_metrics
                .pending_pings
                .get_value(&glean, Some("metrics"))
                .unwrap()
        );
    }

    #[test]
    fn number_quota_is_enforced_when_enqueueing_cached_pings() {
        let (mut glean, dir) = new_glean(None);

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        // How many pings we allow at maximum
        let count_quota = 3;
        // The number of pings we fill the pending pings directory with.
        let n = 10;

        // Submit the ping multiple times
        for _ in 0..n {
            ping_type.submit_sync(&glean, None);
        }

        let directory_manager = PingDirectoryManager::new(dir.path());
        let pending_pings = directory_manager.process_dirs().pending_pings;
        // The pending pings array is sorted by date in ascending order,
        // the newest element is the last one.
        let expected_pings = pending_pings
            .iter()
            .rev()
            .take(count_quota)
            .map(|(_, ping)| ping.document_id.clone())
            .collect::<Vec<_>>();

        // Create a new upload manager pointing to the same data_path as the glean instance.
        let mut upload_manager = PingUploadManager::no_policy(dir.path());

        upload_manager
            .policy
            .set_max_pending_pings_count(Some(count_quota as u64));

        // Get a task once
        // One ping should have been enqueued.
        // Make sure it is the newest ping.
        for ping_id in expected_pings.iter().rev() {
            match upload_manager.get_upload_task(&glean, false) {
                PingUploadTask::Upload { request } => assert_eq!(&request.document_id, ping_id),
                _ => panic!("Expected upload manager to return the next request!"),
            }
        }

        // Verify that no other requests were returned,
        // they should all have been deleted because pending pings quota was hit.
        assert_eq!(
            upload_manager.get_upload_task(&glean, false),
            PingUploadTask::done()
        );

        // Verify that the correct number of deleted pings was recorded
        assert_eq!(
            (n - count_quota) as i32,
            upload_manager
                .upload_metrics
                .deleted_pings_after_quota_hit
                .get_value(&glean, Some("metrics"))
                .unwrap()
        );
        assert_eq!(
            n as i32,
            upload_manager
                .upload_metrics
                .pending_pings
                .get_value(&glean, Some("metrics"))
                .unwrap()
        );
    }

    #[test]
    fn size_and_count_quota_work_together_size_first() {
        let (mut glean, dir) = new_glean(None);

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        let expected_number_of_pings = 3;
        // The number of pings we fill the pending pings directory with.
        let n = 10;

        // Submit the ping multiple times
        for _ in 0..n {
            ping_type.submit_sync(&glean, None);
        }

        let directory_manager = PingDirectoryManager::new(dir.path());
        let pending_pings = directory_manager.process_dirs().pending_pings;
        // The pending pings array is sorted by date in ascending order,
        // the newest element is the last one.
        let expected_pings = pending_pings
            .iter()
            .rev()
            .take(expected_number_of_pings)
            .map(|(_, ping)| ping.document_id.clone())
            .collect::<Vec<_>>();

        // Create a new upload manager pointing to the same data_path as the glean instance.
        let mut upload_manager = PingUploadManager::no_policy(dir.path());

        // From manual testing we figured out a basically empty ping file is 399 bytes,
        // so this allows 3 pings with some headroom in case of future changes.
        upload_manager
            .policy
            .set_max_pending_pings_directory_size(Some(1300));
        upload_manager.policy.set_max_pending_pings_count(Some(5));

        // Get a task once
        // One ping should have been enqueued.
        // Make sure it is the newest ping.
        for ping_id in expected_pings.iter().rev() {
            match upload_manager.get_upload_task(&glean, false) {
                PingUploadTask::Upload { request } => assert_eq!(&request.document_id, ping_id),
                _ => panic!("Expected upload manager to return the next request!"),
            }
        }

        // Verify that no other requests were returned,
        // they should all have been deleted because pending pings quota was hit.
        assert_eq!(
            upload_manager.get_upload_task(&glean, false),
            PingUploadTask::done()
        );

        // Verify that the correct number of deleted pings was recorded
        assert_eq!(
            (n - expected_number_of_pings) as i32,
            upload_manager
                .upload_metrics
                .deleted_pings_after_quota_hit
                .get_value(&glean, Some("metrics"))
                .unwrap()
        );
        assert_eq!(
            n as i32,
            upload_manager
                .upload_metrics
                .pending_pings
                .get_value(&glean, Some("metrics"))
                .unwrap()
        );
        // Verify the labeled deletion counter attributes deletions to size_quota
        assert_eq!(
            (n - expected_number_of_pings) as i32,
            upload_manager
                .upload_metrics
                .pending_pings_deleted
                .get("size_quota")
                .get_value(&glean, Some("health"))
                .unwrap()
        );
        assert!(upload_manager
            .upload_metrics
            .pending_pings_deleted
            .get("count_quota")
            .get_value(&glean, Some("health"))
            .is_none());
    }

    #[test]
    fn size_and_count_quota_work_together_count_first() {
        let (mut glean, dir) = new_glean(None);

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        let expected_number_of_pings = 2;
        // The number of pings we fill the pending pings directory with.
        let n = 10;

        // Submit the ping multiple times
        for _ in 0..n {
            ping_type.submit_sync(&glean, None);
        }

        let directory_manager = PingDirectoryManager::new(dir.path());
        let pending_pings = directory_manager.process_dirs().pending_pings;
        // The pending pings array is sorted by date in ascending order,
        // the newest element is the last one.
        let expected_pings = pending_pings
            .iter()
            .rev()
            .take(expected_number_of_pings)
            .map(|(_, ping)| ping.document_id.clone())
            .collect::<Vec<_>>();

        // Create a new upload manager pointing to the same data_path as the glean instance.
        let mut upload_manager = PingUploadManager::no_policy(dir.path());

        // Set a large enough size quota so it never triggers before the count quota does.
        upload_manager
            .policy
            .set_max_pending_pings_directory_size(Some(100_000));
        upload_manager.policy.set_max_pending_pings_count(Some(2));

        // Get a task once
        // One ping should have been enqueued.
        // Make sure it is the newest ping.
        for ping_id in expected_pings.iter().rev() {
            match upload_manager.get_upload_task(&glean, false) {
                PingUploadTask::Upload { request } => assert_eq!(&request.document_id, ping_id),
                _ => panic!("Expected upload manager to return the next request!"),
            }
        }

        // Verify that no other requests were returned,
        // they should all have been deleted because pending pings quota was hit.
        assert_eq!(
            upload_manager.get_upload_task(&glean, false),
            PingUploadTask::done()
        );

        // Verify that the correct number of deleted pings was recorded
        assert_eq!(
            (n - expected_number_of_pings) as i32,
            upload_manager
                .upload_metrics
                .deleted_pings_after_quota_hit
                .get_value(&glean, Some("metrics"))
                .unwrap()
        );
        assert_eq!(
            n as i32,
            upload_manager
                .upload_metrics
                .pending_pings
                .get_value(&glean, Some("metrics"))
                .unwrap()
        );
        // Verify the labeled deletion counter attributes deletions to count_quota
        assert_eq!(
            (n - expected_number_of_pings) as i32,
            upload_manager
                .upload_metrics
                .pending_pings_deleted
                .get("count_quota")
                .get_value(&glean, Some("health"))
                .unwrap()
        );
        assert!(upload_manager
            .upload_metrics
            .pending_pings_deleted
            .get("size_quota")
            .get_value(&glean, Some("health"))
            .is_none());
    }

    #[test]
    fn pending_pings_deleted_is_not_recorded_when_quota_not_hit() {
        let (mut glean, dir) = new_glean(None);

        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        // Submit fewer pings than any quota.
        for _ in 0..3 {
            ping_type.submit_sync(&glean, None);
        }

        let mut upload_manager = PingUploadManager::no_policy(dir.path());
        upload_manager.policy.set_max_pending_pings_count(Some(10));
        upload_manager
            .policy
            .set_max_pending_pings_directory_size(Some(1024 * 1024));

        upload_manager.get_upload_task(&glean, false);

        assert!(upload_manager
            .upload_metrics
            .pending_pings_deleted
            .get("count_quota")
            .get_value(&glean, Some("health"))
            .is_none());
        assert!(upload_manager
            .upload_metrics
            .pending_pings_deleted
            .get("size_quota")
            .get_value(&glean, Some("health"))
            .is_none());
    }

    #[test]
    fn pending_pings_config_overrides_are_applied() {
        let (_, dir) = new_glean(None);

        let mut upload_manager = PingUploadManager::new(dir.path(), "test");

        let custom_count: u64 = 42;
        let custom_size: u64 = 999_999;
        upload_manager.set_max_pending_pings_count(custom_count);
        upload_manager.set_max_pending_pings_directory_size(custom_size);

        assert_eq!(
            custom_count,
            upload_manager.policy.max_pending_pings_count()
        );
        assert_eq!(
            custom_size,
            upload_manager.policy.max_pending_pings_directory_size()
        );
    }

    #[test]
    fn maximum_wait_attemps_is_enforced() {
        let (glean, dir) = new_glean(None);

        let mut upload_manager = PingUploadManager::no_policy(dir.path());

        // Define a max_wait_attemps policy, this is disabled for tests by default.
        let max_wait_attempts = 3;
        upload_manager
            .policy
            .set_max_wait_attempts(Some(max_wait_attempts));

        // Add a rate limiter to the upload mangager with max of 1 ping 5secs.
        //
        // We arbitrarily set the maximum pings per interval to a very low number,
        // when the rate limiter reaches it's limit get_upload_task returns a PingUploadTask::Wait,
        // which will allow us to test the limitations around returning too many of those in a row.
        let secs_per_interval = 5;
        let max_pings_per_interval = 1;
        upload_manager.set_rate_limiter(secs_per_interval, max_pings_per_interval);

        // Enqueue two pings
        upload_manager.enqueue_ping(
            &glean,
            PingPayload {
                document_id: Uuid::new_v4().to_string(),
                upload_path: PATH.into(),
                json_body: "".into(),
                headers: None,
                body_has_info_sections: true,
                ping_name: "ping-name".into(),
                uploader_capabilities: vec![],
            },
        );
        upload_manager.enqueue_ping(
            &glean,
            PingPayload {
                document_id: Uuid::new_v4().to_string(),
                upload_path: PATH.into(),
                json_body: "".into(),
                headers: None,
                body_has_info_sections: true,
                ping_name: "ping-name".into(),
                uploader_capabilities: vec![],
            },
        );

        // Get the first ping, it should be returned normally.
        match upload_manager.get_upload_task(&glean, false) {
            PingUploadTask::Upload { .. } => {}
            _ => panic!("Expected upload manager to return the next request!"),
        }

        // Try to get the next ping,
        // we should be throttled and thus get a PingUploadTask::Wait.
        // Check that we are indeed allowed to get this response as many times as expected.
        for _ in 0..max_wait_attempts {
            let task = upload_manager.get_upload_task(&glean, false);
            assert!(task.is_wait());
        }

        // Check that after we get PingUploadTask::Wait the allowed number of times,
        // we then get PingUploadTask::Done.
        assert_eq!(
            upload_manager.get_upload_task(&glean, false),
            PingUploadTask::done()
        );

        // Wait for the rate limiter to allow upload tasks again.
        thread::sleep(Duration::from_secs(secs_per_interval));

        // Check that we are allowed again to get pings.
        let task = upload_manager.get_upload_task(&glean, false);
        assert!(task.is_upload());

        // And once we are done we don't need to wait anymore.
        assert_eq!(
            upload_manager.get_upload_task(&glean, false),
            PingUploadTask::done()
        );
    }

    #[test]
    fn wait_task_contains_expected_wait_time_when_pending_pings_dir_not_processed_yet() {
        let (glean, dir) = new_glean(None);
        let upload_manager = PingUploadManager::new(dir.path(), "test");
        match upload_manager.get_upload_task(&glean, false) {
            PingUploadTask::Wait { time } => {
                assert_eq!(time, WAIT_TIME_FOR_PING_PROCESSING);
            }
            _ => panic!("Expected upload manager to return a wait task!"),
        };
    }

    #[test]
    fn cannot_enqueue_ping_while_its_being_processed() {
        let (glean, dir) = new_glean(None);

        let upload_manager = PingUploadManager::no_policy(dir.path());

        // Enqueue a ping and start processing it
        let identifier = &Uuid::new_v4();
        let ping = PingPayload {
            document_id: identifier.to_string(),
            upload_path: PATH.into(),
            json_body: "".into(),
            headers: None,
            body_has_info_sections: true,
            ping_name: "ping-name".into(),
            uploader_capabilities: vec![],
        };
        upload_manager.enqueue_ping(&glean, ping);
        assert!(upload_manager.get_upload_task(&glean, false).is_upload());

        // Attempt to re-enqueue the same ping
        let ping = PingPayload {
            document_id: identifier.to_string(),
            upload_path: PATH.into(),
            json_body: "".into(),
            headers: None,
            body_has_info_sections: true,
            ping_name: "ping-name".into(),
            uploader_capabilities: vec![],
        };
        upload_manager.enqueue_ping(&glean, ping);

        // No new pings should have been enqueued so the upload task is Done.
        assert_eq!(
            upload_manager.get_upload_task(&glean, false),
            PingUploadTask::done()
        );

        // Process the upload response
        upload_manager.process_ping_upload_response(
            &glean,
            &identifier.to_string(),
            UploadResult::http_status(200),
        );
    }

    #[test]
    fn stores_pings_during_submission_and_upload_if_enabled() {
        let (mut glean, _t) = new_glean(None);
        glean.set_store_submitted_pings_enabled(true);

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        // Submit a ping
        ping_type.submit_sync(&glean, None);

        let pings = glean.storage().get_all_submitted_pings();
        assert_eq!(pings.len(), 1);
        let ping = pings.first().unwrap();
        assert!(ping.submitted_date() <= Utc::now());
        assert!(ping.uploaded_date.is_none());

        // Get the submitted PingRequest
        match glean.get_upload_task() {
            PingUploadTask::Upload { request } => {
                // Simulate the processing of a sucessful request
                let document_id = request.document_id;
                glean.process_ping_upload_response(&document_id, UploadResult::http_status(200));
            }
            _ => panic!("Expected upload manager to return the next request!"),
        }

        let pings = glean.storage().get_all_submitted_pings();
        assert_eq!(pings.len(), 1);
        let ping = pings.first().unwrap();
        assert!(ping.submitted_date() <= Utc::now());
        assert!(ping.uploaded_date.is_some());

        // Verify that after request is returned, none are left
        assert_eq!(glean.get_upload_task(), PingUploadTask::done());
    }

    #[test]
    fn stores_pings_during_submission_and_marks_as_upload_failed_when_appropriate() {
        let (mut glean, _t) = new_glean(None);
        glean.set_store_submitted_pings_enabled(true);

        // Register a ping for testing
        let ping_type = PingType::new(
            "test",
            true,
            /* send_if_empty */ true,
            true,
            true,
            true,
            vec![],
            vec![],
            true,
            vec![],
        );
        glean.register_ping_type(&ping_type);

        // Submit a ping
        ping_type.submit_sync(&glean, None);

        let pings = glean.storage().get_all_submitted_pings();
        assert_eq!(pings.len(), 1);
        let ping = pings.first().unwrap();
        assert!(ping.submitted_date() <= Utc::now());
        assert!(ping.uploaded_date.is_none());

        // Get the submitted PingRequest
        match glean.get_upload_task() {
            PingUploadTask::Upload { request } => {
                // Simulate the processing of a sucessful request
                let document_id = request.document_id;
                glean.process_ping_upload_response(&document_id, UploadResult::http_status(400));
            }
            _ => panic!("Expected upload manager to return the next request!"),
        }

        let pings = glean.storage().get_all_submitted_pings();
        assert_eq!(pings.len(), 1);
        let ping = pings.first().unwrap();
        assert!(ping.submitted_date() <= Utc::now());
        assert!(ping.upload_failed.is_some());
        assert!(ping.uploaded_date.is_none());

        // Verify that after request is returned, none are left
        assert_eq!(glean.get_upload_task(), PingUploadTask::done());
    }
}
