/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_Promise_h
#define js_Promise_h

#include "mozilla/Attributes.h"

#include "jstypes.h"

#include "js/RootingAPI.h"
#include "js/TypeDecls.h"
#include "js/UniquePtr.h"

namespace JS {

class JS_PUBLIC_API AutoDebuggerJobQueueInterruption;

/**
 * Abstract base class for an ECMAScript Job Queue:
 * https://www.ecma-international.org/ecma-262/9.0/index.html#sec-jobs-and-job-queues
 *
 * SpiderMonkey doesn't schedule Promise resolution jobs itself; instead, the
 * embedding can provide an instance of this class SpiderMonkey can use to do
 * that scheduling.
 *
 * The JavaScript shell includes a simple implementation adequate for running
 * tests. Browsers need to augment job handling to meet their own additional
 * requirements, so they can provide their own implementation.
 */
class JS_PUBLIC_API JobQueue {
 public:
  virtual ~JobQueue() = default;

  /**
   * Ask the embedding for the host defined data.
   *
   * This is the step 5 in
   * https://html.spec.whatwg.org/multipage/webappapis.html#hostmakejobcallback
   *
   * SpiderMonkey doesn't itself have a notion of host defined data as defined
   * by the HTML spec, so we need the embedding to provide this. See
   * dom/script/ScriptSettings.h for details.
   *
   * If the embedding has the host defined data, this method should return the
   * host defined data via the `incumbentGlobal` and
   * `optionalHostDefinedData` out parameter and return `true`. If the
   * embedding doesn't need the host defined data, this method should set the
   * `incumbentGlobal` and `optionalHostDefinedData` out parameters to
   * `nullptr` and return `true`. If any error happens while generating the host
   * defined data, this method should set a pending exception to `cx` and return
   * `false`.
   *
   * Currently optionalHostDefinedData is only processed if there is an
   * associated incumbent global, and an assertion will fire if there's
   * a optionalHostDefinedData without an incumbent global, in debug builds.
   *
   * incumbentGlobal and optionalHostDefinedData should be same compartment
   * as cx, and will be wrapped as necessary by the JS engine.
   */
  virtual bool getHostDefinedData(
      JSContext* cx, JS::MutableHandle<JSObject*> incumbentGlobal,
      JS::MutableHandle<JSObject*> optionalHostDefinedData) const = 0;

  /**
   * If the embedding has a host-defined global, return it. This is used when
   * we are able to optimize out the optional host defined data, as the
   * embedding may still require this when running jobs.
   *
   * In Gecko, this is used for dealing with the incumbent global.
   */
  virtual bool getHostDefinedGlobal(
      JSContext* cx, JS::MutableHandle<JSObject*> data) const = 0;

  /**
   * Run all jobs in the queue. Running one job may enqueue others; continue to
   * run jobs until the queue is empty.
   *
   * Calling this method at the wrong time can break the web. The HTML spec
   * indicates exactly when the job queue should be drained (in HTML jargon,
   * when it should "perform a microtask checkpoint"), and doing so at other
   * times can incompatibly change the semantics of programs that use promises
   * or other microtask-based features.
   *
   * This method is called only via AutoDebuggerJobQueueInterruption, used by
   * the Debugger API implementation to ensure that the debuggee's job queue is
   * protected from the debugger's own activity. See the comments on
   * AutoDebuggerJobQueueInterruption.
   */
  virtual void runJobs(JSContext* cx) = 0;

  /**
   * Returns true if the job queue stops draining, which results in `empty()`
   * being false after `runJobs()`.
   */
  virtual bool isDrainingStopped() const = 0;

  /*
   * When enqueueing a job, should the job be enqueued in the debug queue
   * rather than the regular queue?
   */
  virtual bool useDebugQueue(JS::Handle<JSObject*> global) const {
    return false;
  }

  /**
   * Trace hook for non-GCThing microtask values.
   *
   * This hook is called during GC tracing for microtask queue values that are
   * not GC-things. It allows the embedding to provide custom tracing for
   * values stored in the microtask queue, particularly for Private values
   * that represent embedding-specific task representations.
   *
   * If the value is not a GC thing, this method will be called to allow the
   * embedding to trace any GC-reachable data associated with the value.
   *
   * The default implementation does nothing.
   */
  virtual void traceNonGCThingMicroTask(JSTracer* trc, JS::Value* valuePtr) {}

 protected:
  friend class AutoDebuggerJobQueueInterruption;

  /**
   * A saved job queue, represented however the JobQueue implementation pleases.
   * Use AutoDebuggerJobQueueInterruption rather than trying to construct one of
   * these directly; see documentation there.
   *
   * Destructing an instance of this class should assert that the current queue
   * is empty, and then restore the queue the instance captured.
   */
  class SavedJobQueue {
   public:
    virtual ~SavedJobQueue() = default;
  };

  /**
   * Capture this JobQueue's current job queue as a SavedJobQueue and return it,
   * leaving the JobQueue's job queue empty. Destroying the returned object
   * should assert that this JobQueue's current job queue is empty, and restore
   * the original queue.
   *
   * On OOM, this should call JS_ReportOutOfMemory on the given JSContext,
   * and return a null UniquePtr.
   */
  virtual js::UniquePtr<SavedJobQueue> saveJobQueue(JSContext*) = 0;
};

/**
 * Tell SpiderMonkey to use `queue` to schedule promise reactions.
 *
 * SpiderMonkey does not take ownership of the queue; it is the embedding's
 * responsibility to clean it up after the runtime is destroyed.
 */
extern JS_PUBLIC_API void SetJobQueue(JSContext* cx, JobQueue* queue);

/**
 * [SMDOC] Protecting the debuggee's job/microtask queue from debugger activity.
 *
 * When the JavaScript debugger interrupts the execution of some debuggee code
 * (for a breakpoint, for example), the debuggee's execution must be paused
 * while the developer takes time to look at it. During this interruption, other
 * tabs should remain active and usable. If the debuggee shares a main thread
 * with non-debuggee tabs, that means that the thread will have to process
 * non-debuggee HTML tasks and microtasks as usual, even as the debuggee's are
 * on hold until the debugger lets it continue execution. (Letting debuggee
 * microtasks run during the interruption would mean that, from the debuggee's
 * point of view, their side effects would take place wherever the breakpoint
 * was set - in general, not a place other code should ever run, and a violation
 * of the run-to-completion rule.)
 *
 * This means that, even though the timing and ordering of microtasks is
 * carefully specified by the standard - and important to preserve for
 * compatibility and predictability - debugger use may, correctly, have the
 * effect of reordering microtasks. During the interruption, microtasks enqueued
 * by non-debuggee tabs must run immediately alongside their HTML tasks as
 * usual, whereas any debuggee microtasks that were in the queue when the
 * interruption began must wait for the debuggee to be continued - and thus run
 * after microtasks enqueued after they were.
 *
 * Fortunately, this reordering is visible only at the global level: when
 * implemented correctly, it is not detectable by an individual debuggee. Note
 * that a debuggee should generally be a complete unit of similar-origin related
 * browsing contexts. Since non-debuggee activity falls outside that unit, it
 * should never be visible to the debuggee (except via mechanisms that are
 * already asynchronous, like events), so the debuggee should be unable to
 * detect non-debuggee microtasks running when they normally would not. As long
 * as behavior *visible to the debuggee* is unaffected by the interruption, we
 * have respected the spirit of the rule.
 *
 * Of course, even as we accept the general principle that interrupting the
 * debuggee should have as little detectable effect as possible, we still permit
 * the developer to do things like evaluate expressions at the console that have
 * arbitrary effects on the debuggee's state—effects that could never occur
 * naturally at that point in the program. But since these are explicitly
 * requested by the developer, who presumably knows what they're doing, we
 * support this as best we can. If the developer evaluates an expression in the
 * console that resolves a promise, it seems most natural for the promise's
 * reaction microtasks to run immediately, within the interruption. This is an
 * 'unnatural' time for the microtasks to run, but no more unnatural than the
 * evaluation that triggered them.
 *
 * So the overall behavior we need is as follows:
 *
 * - When the debugger interrupts a debuggee, the debuggee's microtask queue
 *   must be saved.
 *
 * - When debuggee execution resumes, the debuggee's microtask queue must be
 *   restored exactly as it was when the interruption occurred.
 *
 * - Non-debuggee task and microtask execution must take place normally during
 *   the interruption.
 *
 * Since each HTML task begins with an empty microtask queue, and it should not
 * be possible for a task to mix debuggee and non-debuggee code, interrupting a
 * debuggee should always find a microtask queue containing exclusively debuggee
 * microtasks, if any. So saving and restoring the microtask queue should affect
 * only the debuggee, not any non-debuggee content.
 *
 * AutoDebuggerJobQueueInterruption
 * --------------------------------
 *
 * AutoDebuggerJobQueueInterruption is an RAII class, meant for use by the
 * Debugger API implementation, that takes care of saving and restoring the
 * queue.
 *
 * Constructing and initializing an instance of AutoDebuggerJobQueueInterruption
 * sets aside the given JSContext's job queue, leaving the JSContext's queue
 * empty. When the AutoDebuggerJobQueueInterruption instance is destroyed, it
 * asserts that the JSContext's current job queue (holding jobs enqueued while
 * the AutoDebuggerJobQueueInterruption was alive) is empty, and restores the
 * saved queue to the JSContext.
 *
 * Since the Debugger API's behavior is up to us, we can specify that Debugger
 * hooks begin execution with an empty job queue, and that we drain the queue
 * after each hook function has run. This drain will be visible to debugger
 * hooks, and makes hook calls resemble HTML tasks, with their own automatic
 * microtask checkpoint. But, the drain will be invisible to the debuggee, as
 * its queue is preserved across the hook invocation.
 *
 * To protect the debuggee's job queue, Debugger takes care to invoke callback
 * functions only within the scope of an AutoDebuggerJobQueueInterruption
 * instance.
 *
 * Why not let the hook functions themselves take care of this?
 * ------------------------------------------------------------
 *
 * Certainly, we could leave responsibility for saving and restoring the job
 * queue to the Debugger hook functions themselves.
 *
 * In fact, early versions of this change tried making the devtools server save
 * and restore the queue explicitly, but because hooks are set and changed in
 * numerous places, it was hard to be confident that every case had been
 * covered, and it seemed that future changes could easily introduce new holes.
 *
 * Later versions of this change modified the accessor properties on the
 * Debugger objects' prototypes to automatically protect the job queue when
 * calling hooks, but the effect was essentially a monkeypatch applied to an API
 * we defined and control, which doesn't make sense.
 *
 * In the end, since promises have become such a pervasive part of JavaScript
 * programming, almost any imaginable use of Debugger would need to provide some
 * kind of protection for the debuggee's job queue, so it makes sense to simply
 * handle it once, carefully, in the implementation of Debugger itself.
 */
class MOZ_RAII JS_PUBLIC_API AutoDebuggerJobQueueInterruption {
 public:
  explicit AutoDebuggerJobQueueInterruption();
  ~AutoDebuggerJobQueueInterruption();

  bool init(JSContext* cx);
  bool initialized() const { return !!saved; }

  /**
   * Drain the job queue. (In HTML terminology, perform a microtask checkpoint.)
   *
   * To make Debugger hook calls more like HTML tasks or ECMAScript jobs,
   * Debugger promises that each hook begins execution with a clean microtask
   * queue, and that a microtask checkpoint (queue drain) takes place after each
   * hook returns, successfully or otherwise.
   *
   * To ensure these debugger-introduced microtask checkpoints serve only the
   * hook's microtasks, and never affect the debuggee's, the Debugger API
   * implementation uses only this method to perform the checkpoints, thereby
   * statically ensuring that an AutoDebuggerJobQueueInterruption is in scope to
   * protect the debuggee.
   *
   * SavedJobQueue implementations are required to assert that the queue is
   * empty before restoring the debuggee's queue. If the Debugger API ever fails
   * to perform a microtask checkpoint after calling a hook, that assertion will
   * fail, catching the mistake.
   */
  void runJobs();

 private:
  JSContext* cx;
  js::UniquePtr<JobQueue::SavedJobQueue> saved;
};

enum class PromiseRejectionHandlingState { Unhandled, Handled };

typedef void (*PromiseRejectionTrackerCallback)(
    JSContext* cx, bool mutedErrors, JS::HandleObject promise,
    JS::PromiseRejectionHandlingState state, void* data);

/**
 * Sets the callback that's invoked whenever a Promise is rejected without
 * a rejection handler, and when a Promise that was previously rejected
 * without a handler gets a handler attached.
 */
extern JS_PUBLIC_API void SetPromiseRejectionTrackerCallback(
    JSContext* cx, PromiseRejectionTrackerCallback callback,
    void* data = nullptr);

/**
 * Inform the runtime that the job queue is empty and the embedding is going to
 * execute its last promise job. The runtime may now choose to skip creating
 * promise jobs for asynchronous execution and instead continue execution
 * synchronously. More specifically, this optimization is used to skip the
 * standard job queuing behavior for `await` operations in async functions.
 *
 * This function may be called before executing the last job in the job queue.
 * When it was called, JobQueueMayNotBeEmpty must be called in order to restore
 * the default job queuing behavior before the embedding enqueues its next job
 * into the job queue.
 */
extern JS_PUBLIC_API void JobQueueIsEmpty(JSContext* cx);

/**
 * Inform the runtime that job queue is no longer empty. The runtime can now no
 * longer skip creating promise jobs for asynchronous execution, because
 * pending jobs in the job queue must be executed first to preserve the FIFO
 * (first in - first out) property of the queue. This effectively undoes
 * JobQueueIsEmpty and re-enables the standard job queuing behavior.
 *
 * This function must be called whenever enqueuing a job to the job queue when
 * JobQueueIsEmpty was called previously.
 */
extern JS_PUBLIC_API void JobQueueMayNotBeEmpty(JSContext* cx);

/**
 * Returns a new instance of the Promise builtin class in the current
 * compartment, with the right slot layout.
 *
 * The `executor` can be a `nullptr`. In that case, the only way to resolve or
 * reject the returned promise is via the `JS::ResolvePromise` and
 * `JS::RejectPromise` JSAPI functions.
 */
extern JS_PUBLIC_API JSObject* NewPromiseObject(JSContext* cx,
                                                JS::HandleObject executor);

/**
 * Returns true if the given object is an unwrapped PromiseObject, false
 * otherwise.
 */
extern JS_PUBLIC_API bool IsPromiseObject(JS::HandleObject obj);

/**
 * Returns the current compartment's original Promise constructor.
 */
extern JS_PUBLIC_API JSObject* GetPromiseConstructor(JSContext* cx);

/**
 * Returns the current compartment's original Promise.prototype.
 */
extern JS_PUBLIC_API JSObject* GetPromisePrototype(JSContext* cx);

// Keep this in sync with the PROMISE_STATE defines in SelfHostingDefines.h.
enum class PromiseState { Pending, Fulfilled, Rejected };

/**
 * Returns the given Promise's state as a JS::PromiseState enum value.
 *
 * Returns JS::PromiseState::Pending if the given object is a wrapper that
 * can't safely be unwrapped.
 */
extern JS_PUBLIC_API PromiseState GetPromiseState(JS::HandleObject promise);

/**
 * Returns the given Promise's process-unique ID.
 */
JS_PUBLIC_API uint64_t GetPromiseID(JS::HandleObject promise);

/**
 * Returns the given Promise's result: either the resolution value for
 * fulfilled promises, or the rejection reason for rejected ones.
 */
extern JS_PUBLIC_API JS::Value GetPromiseResult(JS::HandleObject promise);

/**
 * Returns whether the given promise's rejection is already handled or not.
 *
 * The caller must check the given promise is rejected before checking it's
 * handled or not.
 */
extern JS_PUBLIC_API bool GetPromiseIsHandled(JS::HandleObject promise);

/*
 * Given a settled (i.e. fulfilled or rejected, not pending) promise, sets
 * |promise.[[PromiseIsHandled]]| to true and removes it from the list of
 * unhandled rejected promises.
 */
extern JS_PUBLIC_API bool SetSettledPromiseIsHandled(JSContext* cx,
                                                     JS::HandleObject promise);

/*
 * Given a promise (settled or not), sets |promise.[[PromiseIsHandled]]| to true
 * and removes it from the list of unhandled rejected promises if it's settled.
 */
[[nodiscard]] extern JS_PUBLIC_API bool SetAnyPromiseIsHandled(
    JSContext* cx, JS::HandleObject promise);

/**
 * Returns a js::SavedFrame linked list of the stack that lead to the given
 * Promise's allocation.
 *
 * The promise parameter here should be an unwrapped promise.
 */
extern JS_PUBLIC_API JSObject* GetPromiseAllocationSite(
    JS::HandleObject promise);

/**
 * A more flexible version of the above: Handles the cases where
 * 1. maybePromise is wrapped
 * 2. maybePromise is null
 * 3. maybePromise isn't actually a promise.
 *
 * Returns js::SavedFrame if the passed argument is a
 * wrapped promise (that's not a dead object wrapper) and it
 * has an allocation site.
 */
extern JS_PUBLIC_API JSObject*
MaybeGetPromiseAllocationSiteFromPossiblyWrappedPromise(
    JS::HandleObject maybePromise);

extern JS_PUBLIC_API JSObject* GetPromiseResolutionSite(
    JS::HandleObject promise);

#ifdef DEBUG
extern JS_PUBLIC_API void DumpPromiseAllocationSite(JSContext* cx,
                                                    JS::HandleObject promise);

extern JS_PUBLIC_API void DumpPromiseResolutionSite(JSContext* cx,
                                                    JS::HandleObject promise);
#endif

/**
 * Calls the current compartment's original Promise.resolve on the original
 * Promise constructor, with `resolutionValue` passed as an argument.
 */
extern JS_PUBLIC_API JSObject* CallOriginalPromiseResolve(
    JSContext* cx, JS::HandleValue resolutionValue);

/**
 * Calls the current compartment's original Promise.reject on the original
 * Promise constructor, with `resolutionValue` passed as an argument.
 */
extern JS_PUBLIC_API JSObject* CallOriginalPromiseReject(
    JSContext* cx, JS::HandleValue rejectionValue);

/**
 * Resolves the given Promise with the given `resolutionValue`.
 *
 * Calls the `resolve` function that was passed to the executor function when
 * the Promise was created.
 */
extern JS_PUBLIC_API bool ResolvePromise(JSContext* cx,
                                         JS::HandleObject promiseObj,
                                         JS::HandleValue resolutionValue);

/**
 * Rejects the given `promise` with the given `rejectionValue`.
 *
 * Calls the `reject` function that was passed to the executor function when
 * the Promise was created.
 */
extern JS_PUBLIC_API bool RejectPromise(JSContext* cx,
                                        JS::HandleObject promiseObj,
                                        JS::HandleValue rejectionValue);

/**
 * Resolves the given Promise with `resolutionValue`, but guarantees that the
 * user-code-running portion of resolution does not execute on the caller's
 * stack.
 *
 * This corresponds to the SafePromiseResolve abstract operation from
 * the thenable-curtailment proposal
 * (https://tc39.es/proposal-thenable-curtailment/).
 *
 * On return, `promise`'s resolving functions are no-ops: any subsequent
 * resolve/reject call on the same promise (including a second SafeResolve) is
 * a silent no-op.
 *
 * If `resolutionValue` is not an object, or `Get(resolutionValue, "then")`
 * cannot run user code, resolution happens synchronously, exactly as for
 * `JS::ResolvePromise`; a callable `"then"` is still called from a job.
 *
 * Otherwise, the resolving functions are made no-ops immediately and the actual
 * PerformPromiseResolution steps, including `Get(resolutionValue, "then")`,
 * are deferred to a freshly-enqueued microtask. The promise remains pending
 * until that microtask runs.
 *
 * If provided, `deferred` is set to true only when this call successfully
 * enqueues deferred resolution, and to false otherwise.
 */
extern JS_PUBLIC_API bool SafeResolve(JSContext* cx,
                                      JS::HandleObject promiseObj,
                                      JS::HandleValue resolutionValue,
                                      bool* deferred = nullptr);

/**
 * Create a Promise with the given fulfill/reject handlers, that will be
 * fulfilled/rejected with the value/reason that the promise `promise` is
 * fulfilled/rejected with.
 *
 * This function basically acts like `promise.then(onFulfilled, onRejected)`,
 * except that its behavior is unaffected by changes to `Promise`,
 * `Promise[Symbol.species]`, `Promise.prototype.then`, `promise.constructor`,
 * `promise.then`, and so on.
 *
 * This function throws if `promise` is not a Promise from this or another
 * realm.
 *
 * This function will assert if `onFulfilled` or `onRejected` is non-null and
 * also not IsCallable.
 */
extern JS_PUBLIC_API JSObject* CallOriginalPromiseThen(
    JSContext* cx, JS::HandleObject promise, JS::HandleObject onFulfilled,
    JS::HandleObject onRejected);

/**
 * Unforgeable, optimized version of the JS builtin Promise.prototype.then.
 *
 * Takes a Promise instance and nullable `onFulfilled`/`onRejected` callables to
 * enqueue as reactions for that promise. In contrast to Promise.prototype.then,
 * this doesn't create and return a new Promise instance.
 *
 * Throws a TypeError if `promise` isn't a Promise (or possibly a different
 * error if it's a security wrapper or dead object proxy).
 */
extern JS_PUBLIC_API bool AddPromiseReactions(JSContext* cx,
                                              JS::HandleObject promise,
                                              JS::HandleObject onFulfilled,
                                              JS::HandleObject onRejected);

/**
 * Unforgeable, optimized version of the JS builtin Promise.prototype.then.
 *
 * Takes a Promise instance and nullable `onFulfilled`/`onRejected` callables to
 * enqueue as reactions for that promise. In contrast to Promise.prototype.then,
 * this doesn't create and return a new Promise instance.
 *
 * Throws a TypeError if `promise` isn't a Promise (or possibly a different
 * error if it's a security wrapper or dead object proxy).
 *
 * If `onRejected` is null and `promise` is rejected, this function -- unlike
 * the function above -- will not report an unhandled rejection.
 */
extern JS_PUBLIC_API bool AddPromiseReactionsIgnoringUnhandledRejection(
    JSContext* cx, JS::HandleObject promise, JS::HandleObject onFulfilled,
    JS::HandleObject onRejected);

// This enum specifies whether a promise is expected to keep track of
// information that is useful for embedders to implement user activation
// behavior handling as specified in the HTML spec:
// https://html.spec.whatwg.org/multipage/interaction.html#triggered-by-user-activation
// By default, promises created by SpiderMonkey do not make any attempt to keep
// track of information about whether an activation behavior was being processed
// when the original promise in a promise chain was created.  If the embedder
// sets either of the HadUserInteractionAtCreation or
// DidntHaveUserInteractionAtCreation flags on a promise after creating it,
// SpiderMonkey will propagate that flag to newly created promises when
// processing Promise#then and will make it possible to query this flag off of a
// promise further down the chain later using the
// GetPromiseUserInputEventHandlingState() API.
enum class PromiseUserInputEventHandlingState {
  // Don't keep track of this state (default for all promises)
  DontCare,
  // Keep track of this state, the original promise in the chain was created
  // while an activation behavior was being processed.
  HadUserInteractionAtCreation,
  // Keep track of this state, the original promise in the chain was created
  // while an activation behavior was not being processed.
  DidntHaveUserInteractionAtCreation
};

/**
 * Returns the given Promise's activation behavior state flag per above as a
 * JS::PromiseUserInputEventHandlingState value.  All promises are created with
 * the DontCare state by default.
 *
 * Returns JS::PromiseUserInputEventHandlingState::DontCare if the given object
 * is a wrapper that can't safely be unwrapped.
 */
extern JS_PUBLIC_API PromiseUserInputEventHandlingState
GetPromiseUserInputEventHandlingState(JSObject* promise);

/**
 * Sets the given Promise's activation behavior state flag per above as a
 * JS::PromiseUserInputEventHandlingState value.
 *
 * Returns false if the given object is a wrapper that can't safely be
 * unwrapped.
 */
extern JS_PUBLIC_API bool SetPromiseUserInputEventHandlingState(
    JS::HandleObject promise, JS::PromiseUserInputEventHandlingState state);

/**
 * Unforgeable version of the JS builtin Promise.all.
 *
 * Takes a HandleObjectVector of Promise objects and returns a promise that's
 * resolved with an array of resolution values when all those promises have
 * been resolved, or rejected with the rejection value of the first rejected
 * promise.
 *
 * Asserts that all objects in the `promises` vector are, maybe wrapped,
 * instances of `Promise` or a subclass of `Promise`.
 */
extern JS_PUBLIC_API JSObject* GetWaitForAllPromise(
    JSContext* cx, JS::HandleObjectVector promises);

/**
 * The Dispatchable interface allows the embedding to call SpiderMonkey
 * on a JSContext thread when requested via DispatchToEventLoopCallback.
 */
class JS_PUBLIC_API Dispatchable {
 protected:
  // The registered_ flag indicates that this task is counted as part of
  // numRegistered_ of OffThreadPromiseRuntimeState, which will wait until
  // all registered tasks have been run or destroyed.
  bool registered_ = false;

 public:
  bool registered() const { return registered_; }

  // Destruction of Dispatchables is public in order to be used with
  // UniquePtrs. Their destruction by SpiderMonkey is enforced by
  // ReleaseFailedTask.
  virtual ~Dispatchable() = default;

  // ShuttingDown indicates that SpiderMonkey should abort async tasks to
  // expedite shutdown.
  enum MaybeShuttingDown { NotShuttingDown, ShuttingDown };

  // Called by the embedding after DispatchToEventLoopCallback succeeds.
  // Used to correctly release the unique ptr and call the run task.
  static void Run(JSContext* cx, js::UniquePtr<Dispatchable>&& task,
                  MaybeShuttingDown maybeShuttingDown);

  // Used to correctly release the unique ptr. Relies on the
  // OffThreadRuntimePromiseState to handle cleanup via iteration over the
  // live() set.
  static void ReleaseFailedTask(js::UniquePtr<Dispatchable>&& task);

 protected:
  // Dispatchables are created exclusively by SpiderMonkey.
  Dispatchable() = default;

  // These two methods must be implemented in order to correctly handle
  // success and failure cases for the task.

  // Used to execute the task, run on the owning thread.
  // A subclass should override this method to
  //   1) execute the task as necessary and
  //   2) delete the task.
  virtual void run(JSContext* cx, MaybeShuttingDown maybeShuttingDown) = 0;

  // Used to transfer the task back to the runtime if the embedding, upon
  // taking ownership of the task, fails to dispatch and run it. This allows
  // the runtime to delete the task during shutdown. This method can be called
  // from any thread.
  // Typically, this will be used with a UniquePtr like so:
  //   auto task = myTask.release();
  //   task->transferToRuntime();
  virtual void transferToRuntime() = 0;
};

/**
 * Callback to dispatch a JS::Dispatchable to a JSContext's thread's event
 * loop.
 *
 * The DispatchToEventLoopCallback set on a particular JSContext must accept
 * JS::Dispatchable instances and arrange for their `run` methods to be called
 * eventually on the JSContext's thread. This is used for cross-thread dispatch,
 * so the callback itself must be safe to call from any thread. It cannot
 * trigger a GC.
 *
 * The DelayedDispatchToEventLoopCallback in addition takes a delay, and it
 * must accept JS::Dispatchable instances and arrange for their `run` methods
 * to be called after the delay on the JSContext's thread.
 * The embeddings must have its own timeout manager to handle the delay.
 * If a timeout manager is not available for given context, it should return
 * false, optionally with a warning message printed.
 *
 * The callback accepts a closure that is the same as the one given by the call
 * to JS::InitAsyncTaskCallbacks.
 *
 * If the callback returns `true`, it must eventually run the given
 * Dispatchable; otherwise, SpiderMonkey may leak memory or hang.
 *
 * The callback may return `false` to indicate that the JSContext's thread is
 * shutting down and is no longer accepting runnables. Shutting down is a
 * one-way transition: once the callback has rejected a runnable, it must reject
 * all subsequently submitted runnables as well.
 */

typedef bool (*DispatchToEventLoopCallback)(
    void* closure, js::UniquePtr<Dispatchable>&& dispatchable);

/**
 * Callback to dispatch a JS::Dispatchable to a JSContext's thread's event
 * loop with a delay.
 *
 * The DelayedDispatchToEventLoopCallback set on a particular JSContext must
 * accept JS::Dispatchable instances and arrange for their `run` methods to be
 * called after the given delay on the JSContext's thread. This is used for
 * cross-thread dispatch, so the callback itself must be safe to call from any
 * thread. It cannot trigger a GC.
 *
 * The embeddings must have its own timeout manager to handle the delay.
 * If a timeout manager is not available for given context, it should return
 * false, optionally with a warning message printed.
 *
 * The callback accepts a closure that is the same as the one given by the call
 * to JS::InitAsyncTaskCallbacks.
 *
 * If the callback returns `true`, it must eventually run the given
 * Dispatchable; otherwise, SpiderMonkey may leak memory or hang.
 *
 * The callback may return `false` to indicate that the JSContext's thread is
 * shutting down and is no longer accepting runnables. Shutting down is a
 * one-way transition: once the callback has rejected a runnable, it must reject
 * all subsequently submitted runnables as well.
 */

typedef bool (*DelayedDispatchToEventLoopCallback)(
    void* closure, js::UniquePtr<Dispatchable>&& dispatchable, uint32_t delay);

/**
 * Callback to notify the embedder that a background task has begun. This can
 * be used to keep the JSContext's thread alive if it's subject to garbage
 * collection (e.g. as web workers are).
 *
 * This callback will be called on the same thread as the JSContext, but it
 * must not perform any garbage collection or re-enter the engine.
 *
 * The passed dispatchable can be used to track the async task. Most async
 * tasks will finish in a bounded amount of time, but some
 * (i.e. Atomics.waitAsync) are not guaranteed to finish. These tasks may be
 * canceled using JS::CancelAsyncTasks.
 *
 * The callback accepts a closure that is the same as the one given by the call
 * to JS::InitAsyncTaskCallbacks.
 */
typedef void (*AsyncTaskStartedCallback)(void* closure,
                                         Dispatchable* dispatchable);

/**
 * Callback to notify the embedder that a background task has finished. This can
 * be used to keep the JSContext's thread alive if it's subject to garbage
 * collection (e.g. as web workers are).
 *
 * This callback will be called on the same thread as the JSContext, but it
 * must not perform any garbage collection or re-enter the engine.
 *
 * The passed dispatchable will be the same as the one given in
 * AsyncTaskStartedCallback. See that callback for more information.
 *
 * The callback accepts a closure that is the same as the one given by the call
 * to JS::InitAsyncTaskCallbacks.
 */
typedef void (*AsyncTaskFinishedCallback)(void* closure,
                                          Dispatchable* dispatchable);

/**
 * Initialize the async task callbacks. Embedders must use this or else call
 * js::UseInternalJobQueues to select a default implementation built into
 * SpiderMonkey. This latter depends on the embedding to call js::RunJobs on
 * the JavaScript thread to process queued Dispatchables at appropriate times.
 *
 * The dispatchCallback and delayedDispatchCallback are mandatory.
 * asyncTaskStartedCallback and asyncTaskFinishedCallback are optional and may
 * be null.
 *
 */
extern JS_PUBLIC_API void InitAsyncTaskCallbacks(
    JSContext* cx, DispatchToEventLoopCallback dispatchCallback,
    DelayedDispatchToEventLoopCallback delayedDispatchCallback,
    AsyncTaskStartedCallback asyncTaskStartedCallback,
    AsyncTaskFinishedCallback asyncTaskFinishedCallback, void* closure);

/**
 * Cancel all cancellable async tasks. Some tasks may not be cancellable, and
 * must be waited on through a call to JS::ShutdownAsyncTasks.
 *
 * This will fire JSAsyncTaskFinished callbacks for cancelled tasks.
 */
extern JS_PUBLIC_API void CancelAsyncTasks(JSContext* cx);

/**
 * Cancel all cancellable async tasks and block until non-cancellable tasks are
 * finished.
 *
 * This will fire JSAsyncTaskFinished callbacks for all tasks, and there should
 * be no outstanding tasks after this returns. All roots held by tasks will be
 * released.
 *
 * Destroying a JSRuntime will implicitly perform this. However, this is not
 * soon enough for cycle collection, which needs to have roots dropped earlier
 * so that the cycle collector can transitively remove roots for a future GC.
 * For these and other cases, the set of pending async tasks can be canceled
 * with this call earlier than JSRuntime destruction.
 */

extern JS_PUBLIC_API void ShutdownAsyncTasks(JSContext* cx);

}  // namespace JS

#endif  // js_Promise_h
