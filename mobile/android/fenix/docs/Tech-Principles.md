# Tech Principles

## What is this document for and where did it come from?

The following list was collectively chosen by the Firefox Android Team to summarise the main technical principles that we care about. We would like to use these principles to inform our decision-making when writing and evaluating code. It was put together following various discussions at Mozweek 2024 and in subsequent sessions.

## The Principles

### Boy (but works with all genders) scout principle

Leave the code better than when you found it, or create a ticket for the improvement.
Tackle foundational work where need be; focus on a solid base, not just high level changes. If we don’t do this, tech debt builds up very quickly.

### KISS / YAGNI

(Keep it Simple Silly / You Ain’t Gonna Need It)

Don’t build for possible future scenarios that may never happen. Simple code is best.

### Readability over code length

Don’t play Kotlin golf. It’s ok to type a little more to make your intention clear & minimise cognitive load for others.

### Avoid side effects - use immutable state

Redux permits side effects to happen in the Middleware by design, as part of a functional pattern. Elsewhere in the code, side effects make code harder to reason about and more prone to bugs.

### Encapsulation / separation of concerns. Prefer self-contained, easily deletable features to spaghetti

(aka Tell, don’t ask / Good ol’ fashioned information hiding)
The risk of changing code later should be easy to understand.
We favour component based design. All features should be independent, powerful, high quality and reusable.

### Test behaviour, not implementation details. Avoid spies and prefer not to mock where possible

Test clamps should go around the public API of a class, rather than leaning heavily on mocks and spies to build implementation detail in reverse. There is rarely a need to mock data objects.
Good refactoring looks like improving the code but not the behaviour, so the test gives us confidence that the feature still works. Dependency injection (e.g. via a constructor) helps write clean & safe unit tests.

Ref: <https://developer.android.com/topic/architecture/recommendations#testing>

### Document code to add information where required

The linter will prompt us to add KDoc comments - if there is no information to be gained by adding a comment at this level because the function / parameter name is already clear, this lint rule can be suppressed.

### Prefer well-named, self-documenting methods over code comments

Keep naming and documentation clear to avoid over reliance on comments.

### Repeat yourself over creating the wrong abstraction

While it is important to consider reuse where appropriate, DRY is a less important principle than encapsulation. Duplicated code is better than the wrong abstraction which unnecessarily couples parts of the code.

### Prefer composition over inheritance

Open inheritance hierarchies break encapsulation (but sealed classes are ok because they are contained).

### Follow existing patterns where possible

Evolving architectural patterns in the app mean we have legacy code that is not written in a way that we would like to copy. So it is important to copy patterns from parts of the codebase that set a good example. At the moment, this can be achieved via collaboration and pairing, but we aim to improve our best practices documentation.

### Avoid God Objects

Rather than passing around large objects like Android `Context`, take in the smallest footprint of required parameters or consider passing in a lambda. This makes unit testing easier.\
There is another problem with the `Context` example…

### Avoid passing Android lifecycle holders as parameters

Passing a `Context`, `Activity`, `Fragment`, `View` or `NavController` around as a parameter to other business logic is dangerous, because these can all be destroyed at any time by the android OS (e.g. if you rotate the screen).\
This can cause two problems:

- **a memory leak**\
If your class holds a strong reference to an `Activity` which has been destroyed, the jvm will not be able to garbage collect it. So your memory footprint will increase by the size of that activity and all its associated view hierarchy.
- **a crash**\
If you try and call methods on the lifecycle object after it has been destroyed, you will probably get an `IllegalStateException`.\
We have had many of these ([here is an example](https://bugzilla.mozilla.org/show_bug.cgi?id=1971142))

There are various alternative approaches, for example using `lifecycleScope` - [see android docs here](https://developer.android.com/topic/libraries/architecture/coroutines#lifecyclescope).

### Keep UI dumb

Aim is to make code that is predictable and easy to unit test by keeping business logic out of UI code. Composables can have some branching to show different ui based on the state that is passed down. But putting decision-making in Fragments and Activities etc. makes things hard to test, as their lifecycle and state management is largely in the control of the Android platform. Generally, pushing Android to the edges works well for testability.

### Have tests that are fast with good coverage across all branches & form factors

Tests should fail if something breaks for users.

### Profile your code

Good to profile locally because we don’t run profiling automation for every code change (very costly). You can also do a better job profiling something more specific than our automation tests would catch. Having to rely on bisect is much more challenging.

### Avoid lint-level 'nit' comments in PRs - these distract from the big picture. Automate lint rules instead

When reviewing PRs, take time to understand the big picture and focus on asking high level questions. If your feedback is about formatting or style and has no effect on logic, raise it separately as a linter PR rather than creating distracting noise.\
Rely on tooling to prevent bikeshedding.

### Prefer concise, idiomatic, performant code which uses optimal data structures

Kotlin offers many useful idioms, and null safety by default. We shouldn’t circumvent that, e.g. with `!!` where that can cause crashes.

***See also: [Readability over code length](https://mozilla-hub.atlassian.net/wiki/spaces/Mobile/pages/997392386/Tech+Principles#Readability-over-code-length.)***\
Concise code should not come at the expense of readability

### Single Responsibility Principle. Objects should have one purpose

This is an outside-in perspective; it means from the perspective of callers, an object should have one reason to change.

### Consider limiting number of parameters by splitting out functions or grouping params into an object

In Compose, this needs to be weighed against recomposition impact - `Composable`s should take in parameters that are likely to change at the same rate.

***See also: [Repeat yourself over creating the wrong abstraction.](https://mozilla-hub.atlassian.net/wiki/spaces/Mobile/pages/997392386/Tech+Principles#Repeat-yourself-over-creating-the-wrong-abstraction.)***\
For the sake of conciseness, we don’t want to artificially group together parameters that don’t conceptually belong together.

### Collective ownership of code

Separating the code from the author empowers us to critique and improve code when someone else may have written it. Say "we" "us" "our" instead of x person's implementation. If someone is on PTO, we don’t have to wait for them to get back to fix a bug in that area.

### Code should be submitted for review in self-contained, working increments

Keeping pull requests small reduces the cognitive burden on the reviewer, but every changeset (ie every patch in a stack) should deliver working software. This means that code should compile and build and tests should pass.

### Avoid broadly catching and swallowing exceptions

Using the following in coroutines can break structured concurrency by inadvertently swallowing `CancellationException`:

- `catch (Exception) {}`
- `catch (Throwable) {}`
- `Result.runCatching() {}` (this catches `Throwable` under the hood)

In Kotlin, unchecked exceptions are preferred (this is the default).
If you need to catch and handle an exception, it is recommended to catch the specific exception subclass and handle that particular case.

### Clean containers

View implementations (fragments or composables) should not assume what parent container implementation they will be added to. For example, from a fragment don't cast its container `Activity` to `HomeActivity`.

Why?

- This enables us to reuse views in different places
- The code is better encapsulated, which facilitates testing
  - unit tests are easier if you inject dependencies rather than "reaching up"
  - ui tests are easier, e.g. we can add views to a `BlankActivity` to test in isolation
