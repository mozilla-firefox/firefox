# Async bridges in Rust codes

* Status: Accepted
* Deciders: Jonathan Almeida, Ben Dean-Kawamura, Segun Famisa, Isabella Hugel, Carson Ramsden.
* Date: October 14, 2026
* Feedback deadline: November 10, 2026.

## Context and Problem Statement

Many Rust APIs are both synchronous and blocking.
`rusqlite` in the most common example since queries execute synchronously.
There are other sources too, for example File IO and long-running computations.
On the consumer side, components are usually called from
the application main thread where blocking is not allowed.
How can we call these functions from the consumer application, without blocking the main thread?

A typical solution for this is an async bridge function,
for example
[withContext(Dispatchers.IO)](https://kotlinlang.org/api/kotlinx.coroutines/kotlinx-coroutines-core/kotlinx.coroutines/with-context.html) in Kotlin
or [spawn_blocking](https://docs.rs/tokio/latest/tokio/task/fn.spawn_blocking.html) in tokio.
These functions schedule the blocking operation in a background thread
then wait for the result asynchronously.
Our current approach is to expose a sync/blocking Rust function via UniFFI,
then wrap that function at the application layer.
However, that approach has several issues.

The main issue with this approach is that it's not clear which functions
need to be wrapped with the async bridge.
Most functions should be wrapped, but not all.  For example:

* Cancel functions shouldn't be wrapped since we want them to run immediately.
* Builder functions shouldn't be wrapped because in makes them less convenient to use.
* Constructors may or may not need wrapping.
  LoginStore does since it eagerly opens the SQLite database, but SuggestStore doesn't since it opens its database lazily.

This ambiguity adds extra work for application teams
and requires cross-team collaboration to ensure the components are used correctly.
This has been a steady source of issues on the iOS.

A second issue is that Desktop needs to be special cased.
JavaScript can't natively schedule work in a background thread,
so the async bridging happens in the C++ glue layer.
This is configured by a TOML file that specifies how each function is wrapped.
Having a separate, desktop-only, system creates extra work
and can make the desktop bindings feel like an afterthought.

Lastly, wrapping the entire operation in an async bridge means that
we're always using a thread for duration of the operation.
This includes times when the component is waiting on a mutex, user input, etc.
If we want to avoid using a thread during those times, we need to rework the system.

## Proposal: Move the async bridges to Rust

UniFFI has had the ability to generate async code for a couple years now
and we've been slowly introducing async code to application-services.
We can use this functionality to implement an async bridge in Rust
and move it out of the application layer.
This section describes the steps needed to do that.

### 1: Define the TaskRunner interface

Define a UniFFI trait interface for running tasks in a task runner:

```rust
#[uniffi::export]
pub trait TaskRunner {
    fn run(&self, task: Arc<dyn Fn()>);
}
```

Notes:
  - This depends on UniFFI support for closures (`Arc<dyn Fn()>`) which doesn't exist yet.
    However, it seems possible to implement and it would be a nice UniFFI feature.
    If we don't want to block on this feature, then we can use trait interface with a single method instead.
  - Task doesn't return a value which simplifies the foreign implementations.
    We can still handle return values in our async bridge function by using `oneshot::Channel`
  - In the future, we could add a `task_class` enum param that would have variants like
    `TaskClass::IoBound` and `TaskClass::CpuBound`.  This would allow application code to schedule
    different types of tasks to different queues.

### 2: Implement TaskRunner for all applications

The Kotlin and Swift implementations are straightforward:

```kotlin
class KotlinTaskRunner : TaskRunner {
    fun run(task: () -> Unit) {
        withContext(Dispatchers.IO) {
            task()
        }
    }
}
```

```swift
class SwiftTaskRunner : TaskRunner {
    let queue = DispatchConcurrentQueue.global(
        label: "rust-components-task-runner",
        qos: .background
    )

    func run(task: () -> ()) {
        queue.async {
            task()
        }
    }
}
```

TaskRunner can't be implemented in JS.
However UniFFI traits can also be implemented in Rust,
which we can leverage for the Desktop implementation:

```rust
pub struct DesktopTaskRunner;

impl TaskRunner for DesktopTaskRunner {
    fn run(&self, task: Arc<dyn Fn()>) {
        xpcom::moz_task::dispatch_background_task(
            "rust-components-task-runner",
            task,
        );
    }
}
```

### 3: Implement spawn_blocking

```rust
pub async fn spawn_blocking<F, T>(task: F) -> T
    where F: FnOnce() -> T
{
    // On startup, we register a single global task runner for the application and store it in
    // `GLOBAL_TASK_RUNNER`
    let task_runner = GLOBAL_TASK_RUNNER.get().expect("GLOBAL_TASK_RUNNER to be set");

    // Run the task in the task runner.
    // Use a `oneshot::Channel` to asynchronously wait for the result.
    let (sender, receiver) = oneshot::channel();
    task_runner.run(Arc::new(move || {
        let task_result = task();
        sender.send(task_result);
    }));
    receiver.await
}
```
### 4: Remove async bridges from the application layer

Before:
```kotlin
override suspend fun add(entry: LoginEntry) =
    withContext(coroutineContext) {
        getStorage().add(entry.toLoginEntry()).toLogin()
    }
```

After:
```kotlin
override suspend fun add(entry: LoginEntry) =
    getStorage().add(entry.toLoginEntry()).toLogin()
```

* The iOS changes will look different, since much of that code is still using GCD and completion
  handlers.  See Appendix A for how this would look.
* Desktop would delete `AsyncWrapped` entries in the TOML file
  since the Rust functions would now be `async` and not need configuration.
  In the short-term, we'd still need to explicitly configure `sync` functions as `Sync`.
  Eventually we can make that the default.

### 5: Add async bridges to the Rust code

Before:
```rust
pub fn add(&self, entry: LoginEntry) -> ApiResult<Login> {
    // code to do the work
}
```

After:
```rust
pub async fn add(&self, entry: LoginEntry) -> ApiResult<Login> {
    spawn_blocking(move || {
        // code to do the work
    }).await
}
```

### Potential future work: consider refactorings to block threads less often

We could consider refactoring the Rust code to block threads less often:

```rust
pub fn add(&self, entry: LoginEntry) -> ApiResult<Login> {
    // Unlock the DB and get the encryption key asynchronously
    let db = self.db_mutex.lock().await;
    let key = self.key_manager.get_key().await;
    spawn_blocking(move || {
        // code to do the work, refactor functions to input `db` and `key`
    })
}
```

The benefit here is that we won't block a thread while waiting for the DB mutex
or getting the local encryption key.
The latter means we could update `get_key` to display the primary password dialog if needed
which would reduce some complexity in the JS code.

### Bikeshedding: consider making `spawn_blocking` more ergonomic

Mark points out that calling spawn_blocking
and passing it a move closure might be off-putting for new Rust developers.
We could mitigate this by defining a `spawn_blocking` macro.
With that macro, the code might feel more natural to Kotlin/Swift/JS developers:

```rust
pub fn add(&self, entry: LoginEntry) -> ApiResult<Login> {
    spawn_blocking! {
        // Rust code here
    }.await
}
```

## The big picture: a new API contract for Rust components

This ADR is part of a larger strategy to clarify and simplify the API contract for Rust components.
We hope to end up with a simple contract: Rust calls never block the current thread.
This simplifies the work for application developers,
since they will be free to call Rust component functions without worrying about threading issues.

Breaking this down a bit more:

| Call type  | Examples | Changes |
|-------|-----|------------|
| Sync/non-blocking | checking against the `filter_adult` list, constructing SuggestStore, builder methods  | stays sync, no changes needed |
| Sync/blocking | Adding a login, finding a credit card entry | changes to async, blocking work happens inside `spawn_blocking` |
| Currently sync, but essentially async  | Fetching a REST endpoint | changes to async by switching to `ViaductClient::send` rather than `send_sync` |

This section is meant to contextualize this decision inside the larger plan,
but the details and timeline are out-of-scope for this ADR.

## Out of scope: Cancellation

A related question that comes up often is how async cancellation should work.
This ADR does not address that issue since it's orthogonal to the async bridge question.
However, this would be a great topic for a future ADR.

## Considered Options

* Keep current behavior
* Move async bridges to Rust

## Decision Outcome: Move async bridges to Rust

### Comparison to other options

Pros:
- Application teams don't need write async bridging code
  and application developers don't need to think about threading so much.
- Rust developers have a clear threading contract to follow.
- Desktop no longer needs to be special-cased.
- Allows for future refactorings to reduce thread usage.

Cons:
- Requires 1-2 extra FFI calls.
- Adds some complexity to the Rust code, which could be intimidating to new Rust devs.


# Appendix A: iOS changes

The firefox-ios code that consumes rust components looks quite different, since it's using 
Grand Central Dispatch (GCD) and completion handlers.  Here's some example code:

```swift
public LoginsStorage {
    // First level of wrapping.  This wraps the Rust method in `DispatchQueue.sync`.
    // It's not clear what the purpose of this is, it seems like it may just be leftover code that
    // isn't needed anymore.
    open func add(login: LoginEntry) throws -> Login {
        return try queue.sync {
            try self.store.add(login: login)
        }
    }
}


public RustLogins {
    // Second level of wrapping.  This wraps the Rust method in `DispatchQueue.async`
    // and returns the result via a `completionHandler`
    public func addLogin(login: LoginEntry, completionHandler: @escaping @Sendable (Result<Login?, Error>) -> Void) {
        queue.async {
            guard self.isOpen else {
                let error = LoginsStoreError.UnexpectedLoginsApiError(reason: "Database is closed")
                completionHandler(.failure(error))
                return
            }

            self.getStoredKey { result in
                self.queue.async {
                    guard self.isOpen else {
                        let error = LoginsStoreError.UnexpectedLoginsApiError(reason: "Database is closed")
                        completionHandler(.failure(error))
                        return
                    }

                    switch result {
                    case .success:
                        do {
                            let login = try self.storage?.add(login: login)
                            completionHandler(.success(login))
                        } catch let err as NSError {
                            completionHandler(.failure(err))
                        }
                    case .failure(let err):
                        completionHandler(.failure(err))
                    }
                }
            }
        }
    }
}
```

The iOS is currently moving the code to async Swift, but this is a long term-project.  The iOS team
recommends that the code continue to use completion handlers for now, since they've had issues with
maintaining low-level async functions that are called by high-level functions using the completion
handler model. However, they are okay with this in a specific instance: async UniFFI generated code
thats wrapped to use the completion module.

This means we would refactor the code to look something like this:


```swift
public LoginsStorage {
    // First level of wrapping.  This wraps the async Rust method to use a completion handler
    // It's not clear what the purpose of this is, it seems like it may just be leftover code that
    // isn't needed anymore.
    open func add(login: LoginEntry, completionHandler: @escaping @Sendable (Result<Login?, Error>) -> Void) {
       Task {
            do {
                let login = try await self.store.add(login: login)
                completionHandler(.success(login))
            } catch {
                completionHandler(.failure(error))
            }
        }
    }
}


public RustLogins {
    // Second level of wrapping.  This is mostly the same, but simplified since
    // LoginsStorage now uses completion handlers
    public func addLogin(login: LoginEntry, completionHandler: @escaping @Sendable (Result<Login?, Error>) -> Void) {
        queue.async {
            guard self.isOpen else {
                let error = LoginsStoreError.UnexpectedLoginsApiError(reason: "Database is closed")
                completionHandler(.failure(error))
                return
            }

            self.getStoredKey { result in
                self.queue.async {
                    guard self.isOpen else {
                        let error = LoginsStoreError.UnexpectedLoginsApiError(reason: "Database is closed")
                        completionHandler(.failure(error))
                        return
                    }

                    switch result {
                    case .success:
                        // This case is the only part that changes
                        self.storage?.add(login: login, completionHandler)
                    case .failure(let err):
                        completionHandler(.failure(err))
                    }
                }
            }
        }
    }
}
```
