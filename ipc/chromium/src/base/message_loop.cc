// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop.h"

#include <algorithm>

#include "base/logging.h"
#include "base/message_pump_default.h"
#include "base/string_util.h"
#include "base/thread_local.h"
#include "mozilla/Atomics.h"
#include "mozilla/MaybeLeakRefPtr.h"
#include "mozilla/Mutex.h"
#include "mozilla/ProfilerRunnable.h"
#include "mozilla/TargetShutdownTaskSet.h"
#include "nsIEventTarget.h"
#include "nsITargetShutdownTask.h"
#include "nsThreadUtils.h"

#if defined(XP_DARWIN)
#  include "base/message_pump_mac.h"
#  include "base/message_pump_kqueue.h"
#endif
#if defined(XP_UNIX) && !defined(XP_DARWIN)
#  include "base/message_pump_libevent.h"
#endif
#if defined(XP_LINUX) || defined(__DragonFly__) || defined(XP_FREEBSD) || \
    defined(XP_NETBSD) || defined(XP_OPENBSD)
#  if defined(MOZ_WIDGET_GTK)
#    include "base/message_pump_glib.h"
#  endif
#endif
#ifdef ANDROID
#  include "base/message_pump_android.h"
#endif
#include "nsISerialEventTarget.h"

#include "mozilla/ipc/MessagePump.h"
#include "nsThreadUtils.h"

using base::Time;
using base::TimeDelta;
using base::TimeTicks;

using mozilla::Runnable;

static base::ThreadLocalPointer<MessageLoop>& get_tls_ptr() {
  static base::ThreadLocalPointer<MessageLoop> tls_ptr;
  return tls_ptr;
}

//------------------------------------------------------------------------------

#if defined(XP_WIN)

#  ifdef HAVE_SEH_EXCEPTIONS
// Upon a SEH exception in this thread, it restores the original unhandled
// exception filter.
static int SEHFilter(LPTOP_LEVEL_EXCEPTION_FILTER old_filter) {
  ::SetUnhandledExceptionFilter(old_filter);
  return EXCEPTION_CONTINUE_SEARCH;
}
#  endif

// Retrieves a pointer to the current unhandled exception filter. There
// is no standalone getter method.
static LPTOP_LEVEL_EXCEPTION_FILTER GetTopSEHFilter() {
  LPTOP_LEVEL_EXCEPTION_FILTER top_filter = NULL;
  top_filter = ::SetUnhandledExceptionFilter(0);
  ::SetUnhandledExceptionFilter(top_filter);
  return top_filter;
}

#endif  // defined(XP_WIN)

//------------------------------------------------------------------------------

class MessageLoop::EventTarget final : public nsISerialEventTarget,
                                       public nsITargetShutdownTask {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIEVENTTARGET_FULL

  void TargetShutdown() override {
    TargetShutdownTaskSet::TasksArray shutdownTasks;
    {
      mozilla::MutexAutoLock lock(mMutex);
      if (mShutdownTasksRun) {
        return;
      }
      shutdownTasks = mShutdownTasks.Extract();
    }
    for (const auto& task : shutdownTasks) {
      task->TargetShutdown();
    }
  }

  explicit EventTarget(MessageLoop* aLoop)
      : mMutex("MessageLoop::EventTarget"), mLoop(aLoop) {
    mThread = PR_GetCurrentThread();
  }

  // Called by MessageLoop in its destructor, when it will never process another
  // event. This disables queueing of further runnables.
  //
  // FIXME: At some point, we should consider aligning MessageLoop and nsThread,
  // such that dispatches during shutdown are processed instead of being racily
  // dropped or rejected.
  void BeginDestroy() {
    {
      mozilla::MutexAutoLock lock(mMutex);
      mLoop = nullptr;
    }

    TargetShutdown();
  }

  // Called by MessageLoop in its destructor, when no further code will run on
  // the thread.
  void EndDestroy() { mThread = nullptr; }

 private:
  ~EventTarget() = default;

  mozilla::Mutex mMutex;
  bool mShutdownTasksRun MOZ_GUARDED_BY(mMutex) = false;
  TargetShutdownTaskSet mShutdownTasks MOZ_GUARDED_BY(mMutex);
  MessageLoop* mLoop MOZ_GUARDED_BY(mMutex);
};

NS_IMPL_ISUPPORTS(MessageLoop::EventTarget, nsIEventTarget,
                  nsISerialEventTarget)

NS_IMETHODIMP_(bool)
MessageLoop::EventTarget::IsOnCurrentThreadInfallible() {
  // This method is only going to be called if `mThread` is null, which
  // only happens when the thread has exited the event loop.  Therefore, when
  // we are called, we can never be on this thread.
  return false;
}

NS_IMETHODIMP
MessageLoop::EventTarget::IsOnCurrentThread(bool* aResult) {
  *aResult = IsOnCurrentThread();
  return NS_OK;
}

NS_IMETHODIMP
MessageLoop::EventTarget::DispatchFromScript(nsIRunnable* aEvent,
                                             DispatchFlags aFlags) {
  return Dispatch(do_AddRef(aEvent), aFlags);
}

NS_IMETHODIMP
MessageLoop::EventTarget::Dispatch(already_AddRefed<nsIRunnable> aEvent,
                                   DispatchFlags aFlags) {
  mozilla::MaybeLeakRefPtr<nsIRunnable> event(std::move(aEvent),
                                              aFlags & NS_DISPATCH_FALLIBLE);

  mozilla::MutexAutoLock lock(mMutex);
  if (!mLoop) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  mLoop->PostTask(event.forget());
  return NS_OK;
}

NS_IMETHODIMP
MessageLoop::EventTarget::DelayedDispatch(already_AddRefed<nsIRunnable> aEvent,
                                          uint32_t aDelayMs) {
  mozilla::MutexAutoLock lock(mMutex);
  if (!mLoop || mShutdownTasksRun) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  mLoop->PostDelayedTask(std::move(aEvent), aDelayMs);
  return NS_OK;
}

NS_IMETHODIMP
MessageLoop::EventTarget::RegisterShutdownTask(nsITargetShutdownTask* aTask) {
  mozilla::MutexAutoLock lock(mMutex);
  if (!mLoop || mShutdownTasksRun) {
    return NS_ERROR_UNEXPECTED;
  }
  mShutdownTasks.AddTask(aTask);
  return NS_OK;
}

NS_IMETHODIMP
MessageLoop::EventTarget::UnregisterShutdownTask(nsITargetShutdownTask* aTask) {
  mozilla::MutexAutoLock lock(mMutex);
  if (!mLoop) {
    return NS_ERROR_UNEXPECTED;
  }
  return mShutdownTasks.RemoveTask(aTask);
}

nsIEventTarget::FeatureFlags MessageLoop::EventTarget::GetFeatures() {
  // MessageLoop::EventTarget does not SUPPORTS_SHUTDOWN_TASK_DISPATCH.
  return FeatureFlags::SUPPORTS_SHUTDOWN_TASKS;
}

//------------------------------------------------------------------------------

// static
MessageLoop* MessageLoop::current() { return get_tls_ptr().Get(); }

// static
void MessageLoop::set_current(MessageLoop* loop) { get_tls_ptr().Set(loop); }

static mozilla::Atomic<int32_t> message_loop_id_seq(0);

MessageLoop::MessageLoop(Type type, nsISerialEventTarget* aEventTarget)
    : type_(type),
      id_(++message_loop_id_seq),
      nestable_tasks_allowed_(true),
      exception_restoration_(false),
      incoming_queue_lock_("MessageLoop Incoming Queue Lock"),
      state_(nullptr),
      run_depth_base_(1),
      shutting_down_(false),
#ifdef XP_WIN
      os_modal_loop_(false),
#endif  // XP_WIN
      transient_hang_timeout_(0),
      permanent_hang_timeout_(0),
      next_sequence_num_(0) {
  DCHECK(!current()) << "should only have one message loop per thread";
  get_tls_ptr().Set(this);

  // Must initialize after current() is initialized.
  mEventTarget = new EventTarget(this);

  switch (type_) {
    case TYPE_MOZILLA_PARENT:
      MOZ_RELEASE_ASSERT(!aEventTarget);
      pump_ = new mozilla::ipc::MessagePump(aEventTarget);
      return;
    case TYPE_MOZILLA_CHILD:
      MOZ_RELEASE_ASSERT(!aEventTarget);
      pump_ = new mozilla::ipc::MessagePumpForChildProcess();
      // There is a MessageLoop Run call from XRE_InitChildProcess
      // and another one from MessagePumpForChildProcess. The one
      // from MessagePumpForChildProcess becomes the base, so we need
      // to set run_depth_base_ to 2 or we'll never be able to process
      // Idle tasks.
      run_depth_base_ = 2;
      return;
    case TYPE_MOZILLA_NONMAINTHREAD:
      pump_ = new mozilla::ipc::MessagePumpForNonMainThreads(aEventTarget);
      return;
#if defined(XP_WIN) || defined(XP_DARWIN)
    case TYPE_MOZILLA_NONMAINUITHREAD:
      pump_ = new mozilla::ipc::MessagePumpForNonMainUIThreads(aEventTarget);
      return;
#elif defined(MOZ_WIDGET_ANDROID)
    case TYPE_MOZILLA_ANDROID_UI:
      MOZ_RELEASE_ASSERT(aEventTarget);
      pump_ = new mozilla::ipc::MessagePumpForAndroidUI(aEventTarget);
      return;
#endif  // defined(MOZ_WIDGET_ANDROID)
    default:
      // Create one of Chromium's standard MessageLoop types below.
      break;
  }

#if defined(XP_WIN)
  // TODO(rvargas): Get rid of the OS guards.
  if (type_ == TYPE_DEFAULT) {
    pump_ = new base::MessagePumpDefault();
  } else if (type_ == TYPE_IO) {
    pump_ = new base::MessagePumpForIO();
  } else {
    DCHECK(type_ == TYPE_UI);
    pump_ = new base::MessagePumpForUI();
  }
#else
  if (type_ == TYPE_UI) {
#  if defined(XP_DARWIN)
    pump_ = base::MessagePumpMac::Create();
#  elif defined(XP_LINUX) || defined(__DragonFly__) || defined(XP_FREEBSD) || \
      defined(XP_NETBSD) || defined(XP_OPENBSD)
    pump_ = new base::MessagePumpForUI();
#  endif  // XP_LINUX
  } else if (type_ == TYPE_IO) {
#  if defined(XP_DARWIN)
    pump_ = new base::MessagePumpKqueue();
#  else
    pump_ = new base::MessagePumpLibevent();
#  endif
  } else {
    pump_ = new base::MessagePumpDefault();
  }
#endif

  // We want GetCurrentSerialEventTarget() to return the real nsThread if it
  // will be used to dispatch tasks. However, under all other cases; we'll want
  // it to return this MessageLoop's EventTarget.
  if (nsISerialEventTarget* thread = pump_->GetXPCOMThread()) {
    MOZ_ALWAYS_SUCCEEDS(thread->RegisterShutdownTask(mEventTarget));
  } else {
    mozilla::SerialEventTargetGuard::Set(mEventTarget);
  }
}

MessageLoop::~MessageLoop() {
  DCHECK(this == current());

  mEventTarget->BeginDestroy();

  // Let interested parties have one last shot at accessing this.
  FOR_EACH_OBSERVER(DestructionObserver, destruction_observers_,
                    WillDestroyCurrentMessageLoop());

  DCHECK(!state_);

  // Clean up any unprocessed tasks, but take care: deleting a task could
  // result in the addition of more tasks (e.g., via DeleteSoon).  We set a
  // limit on the number of times we will allow a deleted task to generate more
  // tasks.  Normally, we should only pass through this loop once or twice.  If
  // we end up hitting the loop limit, then it is probably due to one task that
  // is being stubborn.  Inspect the queues to see who is left.
  bool did_work;
  for (int i = 0; i < 100; ++i) {
    DeletePendingTasks();
    ReloadWorkQueue();
    // If we end up with empty queues, then break out of the loop.
    did_work = DeletePendingTasks();
    if (!did_work) break;
  }
  DCHECK(!did_work);

  mEventTarget->EndDestroy();

  // OK, now make it so that no one can find us.
  get_tls_ptr().Set(nullptr);
}

void MessageLoop::AddDestructionObserver(DestructionObserver* obs) {
  DCHECK(this == current());
  destruction_observers_.AddObserver(obs);
}

void MessageLoop::RemoveDestructionObserver(DestructionObserver* obs) {
  DCHECK(this == current());
  destruction_observers_.RemoveObserver(obs);
}

void MessageLoop::Run() {
  AutoRunState save_state(this);
  RunHandler();
}

// Runs the loop in two different SEH modes:
// enable_SEH_restoration_ = false : any unhandled exception goes to the last
// one that calls SetUnhandledExceptionFilter().
// enable_SEH_restoration_ = true : any unhandled exception goes to the filter
// that was existed before the loop was run.
void MessageLoop::RunHandler() {
#if defined(XP_WIN)
  if (exception_restoration_) {
    LPTOP_LEVEL_EXCEPTION_FILTER current_filter = GetTopSEHFilter();
    MOZ_SEH_TRY { RunInternal(); }
    MOZ_SEH_EXCEPT(SEHFilter(current_filter)) {}
    return;
  }
#endif

  RunInternal();
}

//------------------------------------------------------------------------------

void MessageLoop::RunInternal() {
  DCHECK(this == current());
  pump_->Run(this);
}

//------------------------------------------------------------------------------
// Wrapper functions for use in above message loop framework.

bool MessageLoop::ProcessNextDelayedNonNestableTask() {
  if (state_->run_depth > run_depth_base_) return false;

  if (deferred_non_nestable_work_queue_.empty()) return false;

  nsCOMPtr<nsIRunnable> task =
      std::move(deferred_non_nestable_work_queue_.front().task);
  deferred_non_nestable_work_queue_.pop();

  RunTask(task.forget());
  return true;
}

//------------------------------------------------------------------------------

void MessageLoop::Quit() {
  DCHECK(current() == this);
  if (state_) {
    state_->quit_received = true;
  } else {
    NOTREACHED() << "Must be inside Run to call Quit";
  }
}

void MessageLoop::PostTask(already_AddRefed<nsIRunnable> task) {
  PostTask_Helper(std::move(task), 0);
}

void MessageLoop::PostDelayedTask(already_AddRefed<nsIRunnable> task,
                                  int delay_ms) {
  PostTask_Helper(std::move(task), delay_ms);
}

void MessageLoop::PostIdleTask(already_AddRefed<nsIRunnable> task) {
  DCHECK(current() == this);
  MOZ_ASSERT(NS_IsMainThread());

  PendingTask pending_task(std::move(task), false);
  mozilla::LogRunnable::LogDispatch(pending_task.task.get());
  deferred_non_nestable_work_queue_.push(std::move(pending_task));
}

// Possibly called on a background thread!
void MessageLoop::PostTask_Helper(already_AddRefed<nsIRunnable> task,
                                  int delay_ms) {
  if (nsISerialEventTarget* target = pump_->GetXPCOMThread()) {
    nsresult rv;
    if (delay_ms) {
      rv = target->DelayedDispatch(std::move(task), delay_ms);
    } else {
      rv = target->Dispatch(std::move(task), NS_DISPATCH_NORMAL);
    }
    MOZ_ALWAYS_SUCCEEDS(rv);
    return;
  }

  // Tasks should only be queued before or during the Run loop, not after.
  MOZ_ASSERT(!shutting_down_);

  PendingTask pending_task(std::move(task), true);

  if (delay_ms > 0) {
    pending_task.delayed_run_time =
        TimeTicks::Now() + TimeDelta::FromMilliseconds(delay_ms);
  } else {
    DCHECK(delay_ms == 0) << "delay should not be negative";
  }

  // Warning: Don't try to short-circuit, and handle this thread's tasks more
  // directly, as it could starve handling of foreign threads.  Put every task
  // into this queue.

  RefPtr<base::MessagePump> pump;
  {
    mozilla::MutexAutoLock locked(incoming_queue_lock_);
    mozilla::LogRunnable::LogDispatch(pending_task.task.get());
    incoming_queue_.push(std::move(pending_task));
    pump = pump_;
  }
  // Since the incoming_queue_ may contain a task that destroys this message
  // loop, we cannot exit incoming_queue_lock_ until we are done with |this|.
  // We use a stack-based reference to the message pump so that we can call
  // ScheduleWork outside of incoming_queue_lock_.

  pump->ScheduleWork();
}

void MessageLoop::SetNestableTasksAllowed(bool allowed) {
  if (nestable_tasks_allowed_ != allowed) {
    nestable_tasks_allowed_ = allowed;
    if (!nestable_tasks_allowed_) return;
    // Start the native pump if we are not already pumping.
    pump_->ScheduleWorkForNestedLoop();
  }
}

void MessageLoop::ScheduleWork() {
  // Start the native pump if we are not already pumping.
  pump_->ScheduleWork();
}

bool MessageLoop::NestableTasksAllowed() const {
  return nestable_tasks_allowed_;
}

//------------------------------------------------------------------------------

void MessageLoop::RunTask(already_AddRefed<nsIRunnable> aTask) {
  DCHECK(nestable_tasks_allowed_);
  // Execute the task and assume the worst: It is probably not reentrant.
  nestable_tasks_allowed_ = false;

  nsCOMPtr<nsIRunnable> task = aTask;

  {
    mozilla::LogRunnable::Run log(task.get());
    AUTO_PROFILE_FOLLOWING_RUNNABLE(task);
    task->Run();
    task = nullptr;
  }

  nestable_tasks_allowed_ = true;
}

bool MessageLoop::DeferOrRunPendingTask(PendingTask&& pending_task) {
  if (pending_task.nestable || state_->run_depth <= run_depth_base_) {
    RunTask(pending_task.task.forget());
    // Show that we ran a task (Note: a new one might arrive as a
    // consequence!).
    return true;
  }

  // We couldn't run the task now because we're in a nested message loop
  // and the task isn't nestable.
  mozilla::LogRunnable::LogDispatch(pending_task.task.get());
  deferred_non_nestable_work_queue_.push(std::move(pending_task));
  return false;
}

void MessageLoop::AddToDelayedWorkQueue(const PendingTask& pending_task) {
  // Move to the delayed work queue.  Initialize the sequence number
  // before inserting into the delayed_work_queue_.  The sequence number
  // is used to faciliate FIFO sorting when two tasks have the same
  // delayed_run_time value.
  PendingTask new_pending_task(pending_task);
  new_pending_task.sequence_num = next_sequence_num_++;
  mozilla::LogRunnable::LogDispatch(new_pending_task.task.get());
  delayed_work_queue_.push(std::move(new_pending_task));
}

void MessageLoop::ReloadWorkQueue() {
  // We can improve performance of our loading tasks from incoming_queue_ to
  // work_queue_ by waiting until the last minute (work_queue_ is empty) to
  // load.  That reduces the number of locks-per-task significantly when our
  // queues get large.
  if (!work_queue_.empty())
    return;  // Wait till we *really* need to lock and load.

  // Acquire all we can from the inter-thread queue with one lock acquisition.
  {
    mozilla::MutexAutoLock lock(incoming_queue_lock_);
    if (incoming_queue_.empty()) return;
    std::swap(incoming_queue_, work_queue_);
    DCHECK(incoming_queue_.empty());
  }
}

bool MessageLoop::DeletePendingTasks() {
  MOZ_ASSERT(work_queue_.empty());
  bool did_work = !deferred_non_nestable_work_queue_.empty();
  while (!deferred_non_nestable_work_queue_.empty()) {
    deferred_non_nestable_work_queue_.pop();
  }
  did_work |= !delayed_work_queue_.empty();
  while (!delayed_work_queue_.empty()) {
    delayed_work_queue_.pop();
  }
  return did_work;
}

bool MessageLoop::DoWork() {
  if (!nestable_tasks_allowed_) {
    // Task can't be executed right now.
    return false;
  }

  for (;;) {
    ReloadWorkQueue();
    if (work_queue_.empty()) break;

    // Execute oldest task.
    do {
      PendingTask pending_task = std::move(work_queue_.front());
      work_queue_.pop();
      if (!pending_task.delayed_run_time.is_null()) {
        // NB: Don't move, because we use this later!
        AddToDelayedWorkQueue(pending_task);
        // If we changed the topmost task, then it is time to re-schedule.
        if (delayed_work_queue_.top().task == pending_task.task)
          pump_->ScheduleDelayedWork(pending_task.delayed_run_time);
      } else {
        if (DeferOrRunPendingTask(std::move(pending_task))) return true;
      }
    } while (!work_queue_.empty());
  }

  // Nothing happened.
  return false;
}

bool MessageLoop::DoDelayedWork(TimeTicks* next_delayed_work_time) {
  if (!nestable_tasks_allowed_ || delayed_work_queue_.empty()) {
    *next_delayed_work_time = TimeTicks();
    return false;
  }

  if (delayed_work_queue_.top().delayed_run_time > TimeTicks::Now()) {
    *next_delayed_work_time = delayed_work_queue_.top().delayed_run_time;
    return false;
  }

  PendingTask pending_task = delayed_work_queue_.top();
  delayed_work_queue_.pop();

  if (!delayed_work_queue_.empty())
    *next_delayed_work_time = delayed_work_queue_.top().delayed_run_time;

  return DeferOrRunPendingTask(std::move(pending_task));
}

bool MessageLoop::DoIdleWork() {
  if (ProcessNextDelayedNonNestableTask()) return true;

  if (state_->quit_received) pump_->Quit();

  return false;
}

//------------------------------------------------------------------------------
// MessageLoop::AutoRunState

MessageLoop::AutoRunState::AutoRunState(MessageLoop* loop) : loop_(loop) {
  // Top-level Run should only get called once.
  MOZ_ASSERT(!loop_->shutting_down_);

  // Make the loop reference us.
  previous_state_ = loop_->state_;
  if (previous_state_) {
    run_depth = previous_state_->run_depth + 1;
  } else {
    run_depth = 1;
  }
  loop_->state_ = this;

  // Initialize the other fields:
  quit_received = false;
#if defined(XP_WIN)
  dispatcher = nullptr;
#endif
}

MessageLoop::AutoRunState::~AutoRunState() {
  loop_->state_ = previous_state_;

  // If exiting a top-level Run, then we're shutting down.
  loop_->shutting_down_ = !previous_state_;
}

//------------------------------------------------------------------------------
// MessageLoop::PendingTask

bool MessageLoop::PendingTask::operator<(const PendingTask& other) const {
  // Since the top of a priority queue is defined as the "greatest" element, we
  // need to invert the comparison here.  We want the smaller time to be at the
  // top of the heap.

  if (delayed_run_time < other.delayed_run_time) return false;

  if (delayed_run_time > other.delayed_run_time) return true;

  // If the times happen to match, then we use the sequence number to decide.
  // Compare the difference to support integer roll-over.
  return (sequence_num - other.sequence_num) > 0;
}

//------------------------------------------------------------------------------
// MessageLoop::SerialEventTarget

nsISerialEventTarget* MessageLoop::SerialEventTarget() { return mEventTarget; }

//------------------------------------------------------------------------------
// MessageLoopForUI

#if defined(XP_WIN)

void MessageLoopForUI::Run(Dispatcher* dispatcher) {
  AutoRunState save_state(this);
  state_->dispatcher = dispatcher;
  RunHandler();
}

void MessageLoopForUI::AddObserver(Observer* observer) {
  pump_win()->AddObserver(observer);
}

void MessageLoopForUI::RemoveObserver(Observer* observer) {
  pump_win()->RemoveObserver(observer);
}

void MessageLoopForUI::WillProcessMessage(const MSG& message) {
  pump_win()->WillProcessMessage(message);
}
void MessageLoopForUI::DidProcessMessage(const MSG& message) {
  pump_win()->DidProcessMessage(message);
}
void MessageLoopForUI::PumpOutPendingPaintMessages() {
  pump_ui()->PumpOutPendingPaintMessages();
}

#endif  // defined(XP_WIN)

//------------------------------------------------------------------------------
// MessageLoopForIO

#if defined(XP_WIN)

void MessageLoopForIO::RegisterIOHandler(HANDLE file, IOHandler* handler) {
  pump_io()->RegisterIOHandler(file, handler);
}

bool MessageLoopForIO::WaitForIOCompletion(DWORD timeout, IOHandler* filter) {
  return pump_io()->WaitForIOCompletion(timeout, filter);
}

#elif defined(XP_DARWIN)

bool MessageLoopForIO::WatchFileDescriptor(int fd, bool persistent, Mode mode,
                                           FileDescriptorWatcher* controller,
                                           Watcher* delegate) {
  return pump_kqueue()->WatchFileDescriptor(
      fd, persistent, static_cast<base::MessagePumpKqueue::Mode>(mode),
      controller, delegate);
}

bool MessageLoopForIO::WatchMachReceivePort(mach_port_t port,
                                            MachPortWatchController* controller,
                                            MachPortWatcher* delegate) {
  return pump_kqueue()->WatchMachReceivePort(port, controller, delegate);
}

#else

bool MessageLoopForIO::WatchFileDescriptor(int fd, bool persistent, Mode mode,
                                           FileDescriptorWatcher* controller,
                                           Watcher* delegate) {
  return pump_libevent()->WatchFileDescriptor(
      fd, persistent, static_cast<base::MessagePumpLibevent::Mode>(mode),
      controller, delegate);
}

#endif
