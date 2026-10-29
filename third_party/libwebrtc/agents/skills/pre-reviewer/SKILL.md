---
name: pre-reviewer
description: Automated pre-review tool to inspect C++ style, concurrency safety, test quality, and CL descriptions before submitting code for human review.
---

<!-- go/cmark -->

<!--* freshness: {owner: 'tommi' reviewed: '2026-06-12'} *-->

# Pre-Reviewer Skill

This skill acts as a pre-review confidence check tool for WebRTC C++
contributions. Use this skill to evaluate code changes for common C++
anti-patterns, concurrency bugs, test hygiene issues, and CL description
completeness.

______________________________________________________________________

## 1. C++ Style and Modernization Guardrails

### Lambda Expressions

- **No Capture Renaming:** Do not use captures with initializers to introduce
  new names. Capture variables with their original names.
  - *Correct:* `[on_certificate_ready = std::move(on_certificate_ready)]`
  - *Incorrect:* `[callback = std::move(on_certificate_ready)]`
  - *Reference:*
    [Google C++ Style Guide - Lambda expressions](https://google.github.io/styleguide/cppguide.html#Lambda_expressions)
- **Prefer Implicit Captures for Inline/Local Lambdas:** If the lambda cannot
  escape the current scope (e.g. executed inside `BlockingCall`), prefer
  auto-capture (`[&]`). Do not explicitly list captures as it reduces
  readability.
  - *Reference:*
    [C++ Readability Advice - Lambda capture clauses](http://go/c-readability-advice#explicit-vs-implicit-lambda-capture-clauses)

### Pointer and Value Checks

- **Explicit `nullptr` Comparisons:** Always compare pointer types to `nullptr`
  explicitly. Avoid implicit boolean conversion in conditions.
  - *Correct:* `if (tq_ != nullptr)` / `RTC_DCHECK(transport == nullptr);`
  - *Incorrect:* `if (tq_)` / `RTC_DCHECK(!transport);`
  - *Reference:*
    [Abseil Tip #141: Test Container Emptiness with Boolean Functions](https://abseil.io/tips/141)

### Structs vs. Classes

- **Classes for API Invariants:** Use a class if the object has invariants, has
  wide visibility, or is expected to evolve. Use a struct only for passive data.
  - *Reference:*
    [Google C++ Style Guide - Structs vs. Classes](https://google.github.io/styleguide/cppguide.html#Structs_vs._Classes)
- **Encapsulation:** Make classes' data members private unless they are
  constants.
  - *Reference:*
    [Google C++ Style Guide - Access Control](https://google.github.io/styleguide/cppguide.html#Access_Control)

### Virtual Functions

- **No Default Arguments on Virtuals:** Banned on virtual functions because
  default arguments are bound statically at compile time, which leads to
  unexpected behavior when overriding.
  - *Reference:*
    [Google C++ Style Guide - Default Arguments](https://google.github.io/styleguide/cppguide.html#Default_Arguments)

### Type Deduction (`auto`)

- **Avoid Excessive `auto`:** Explicitly spell out types unless the type is
  overly verbose (e.g. templates) or using `auto` directly improves safety. Do
  not use it merely to avoid writing the type.
  - *Reference:*
    [Google C++ Style Guide - Type deduction](https://google.github.io/styleguide/cppguide.html#Type_deduction)

### Operator Style

- **Pre-increment/decrement:** Prefer the prefix form (`++i`, `--i`) unless
  postfix semantics are required.
  - *Reference:*
    [Google C++ Style Guide - Preincrement and Predecrement](https://google.github.io/styleguide/cppguide.html#Preincrement_and_Predecrement)

### Smart Pointer Initialization

- **Assignment Syntax:** Use assignment syntax when initializing directly with
  smart pointers.
  - *Correct:* `std::unique_ptr<Foo> foo = std::make_unique<Foo>();`
  - *Incorrect:* `std::unique_ptr<Foo> foo(std::make_unique<Foo>());`
  - *Reference:*
    [Abseil Tip #88: Use Assignment Syntax for Smart Pointer Initialization](https://abseil.io/tips/88)

### Modern C++ Patterns (C++20 & Standard Library)

- **Constinit:** Use `constinit` directly instead of `ABSL_CONST_INIT`.
  - *Reference:*
    [cppreference - constinit](https://en.cppreference.com/w/cpp/language/constinit.html)
- **std::exchange:** Use `std::exchange` to simplify reset-and-return logic.
  - *Reference:*
    [cppreference - std::exchange](https://en.cppreference.com/w/cpp/utility/exchange.html)
- **Avoid std::make_pair / std::make_tuple:** Prefer brace initialization or
  class template argument deduction (CTAD) like `std::pair{a, b}` instead of
  `std::make_pair(a, b)`.
- **absl::flat_hash_map:** Prefer as the default map type unless ordering is
  required.
  - *Reference:*
    [Abseil Container Guide](https://abseil.io/docs/cpp/guides/container)

### Type Casting and Conversions

- **Prefer Braced Initialization for Non-Narrowing Conversions:** Prefer braced
  initialization (`Type{value}`) over `static_cast<Type>(value)` when converting
  constants or values where narrowing is not intended.
  - *Correct:* `if (temporal_index >= int{kMaxTemporalStreams})`
  - *Incorrect:* `if (temporal_index >= static_cast<int>(kMaxTemporalStreams))`
  - *Reference:*
    [Google C++ Style Guide - Casting](https://google.github.io/styleguide/cppguide.html#Casting)

### Move Semantics

- **Move First, Dereference After (for std::optional):** For `std::optional`, it
  is recommended to move first and dereference after (e.g. `*std::move(opt)`).
  Note that this is generally not recommended for `std::unique_ptr`.
  - *Reference:*
    [Abseil Tip #181: Move First, Dereference After](https://abseil.io/tips/181)
  - *Reference:*
    [Abseil Tip #224: Avoid value() accessors](https://abseil.io/tips/224)
- **Avoid Redundant Return moves:** Do not use `std::move` on return statements
  when it prevents copy elision (NRVO/RVO). Moving a member variable out of a
  class when returning can still be useful.
- **Avoid Brittle Move Loops:** Avoid moving a member variable into an argument
  of a method only to move it back inside the method. This looks like a "use
  after move" and makes code brittle.
  - *Example:* Avoid `RecreateEncoder(std::move(config_))` which internally
    updates `config_` back. Prefer separating it into a parameterless reset
    method or a config updater.

______________________________________________________________________

## 2. Concurrency, Threading, and Destructors

- **Prevent Reentrancy in Destructors:** When executing callbacks during
  destruction, move or swap the callback list/queue into a local variable first,
  then execute. This prevents callbacks from accessing a partially destroyed
  class.
- **No Sequence Checkers in Destructors:** Remove or omit sequence/thread checks
  (`RTC_DCHECK_RUN_ON`) inside destructors. In C++, it is assumed that no other
  threads can call into an object while it is being destroyed.
- **Atomic Safety:** Do not use relaxed memory order atomics when safety
  invariants depend on the ordering of operations not protected by the same
  lock.
  - *Reference:*
    [Abseil - Atomic Danger](https://abseil.io/docs/cpp/atomic_danger)

______________________________________________________________________

## 3. Testing Best Practices

- **Omit GMock Default Cardinality:** Do not add `.Times(1)` to `EXPECT_CALL` as
  it is the default.
- **Omit Wildcard GMock Parameters:** Omit parameter lists in `EXPECT_CALL` when
  all parameters are wildcards (applicable to non-overloaded methods).
  - *Reference:*
    [GoogleTest Mocking Reference - EXPECT_CALL](https://google.github.io/googletest/reference/mocking.html#EXPECT_CALL)
- **Alias Testing Symbols with `using`:** In test `.cc` files, add a
  using-declaration such as `using ::testing::IsTrue;` near the top of the file,
  next to any existing ones, for each gMock matcher or other `::testing` symbol.
  Do not spell out the `::testing::` prefix at each call site and do not use
  `using namespace ::testing;`.
  - *Correct:* `WaitUntil([&] { return done; }, IsTrue())`
  - *Incorrect:* `WaitUntil([&] { return done; }, ::testing::IsTrue())`
  - *Reference:*
    [GoogleTest Cookbook](https://google.github.io/googletest/gmock_cook_book.html)
  - *Reference:*
    [Google C++ Style Guide - Namespaces](https://google.github.io/styleguide/cppguide.html#Namespaces)
- **Prefer Container Matchers:** Use `EXPECT_THAT` with matchers such as
  `SizeIs()`, `IsEmpty()` and `ElementsAre()` instead of checking the result of
  `size()` or `empty()`. On failure, the matchers also print the contents of the
  container. For a container held in a `std::optional`, wrap the matcher in
  `Optional()`, e.g. `EXPECT_THAT(values, Optional(SizeIs(2)));`.
  - *Correct:* `EXPECT_THAT(values, SizeIs(2));` /
    `EXPECT_THAT(values, IsEmpty());`
  - *Incorrect:* `EXPECT_EQ(values.size(), 2u);` /
    `EXPECT_TRUE(values.empty());`
  - *Reference:*
    [GoogleTest Matchers Reference - Container Matchers](https://google.github.io/googletest/reference/matchers.html#container-matchers)
- **Avoid Unrelated Assertions:** Do not assert expectations on calls that are
  not the focus of the test scenario. Use `ON_CALL` instead of `EXPECT_CALL` for
  setting default behavior without setting expectations.
  - *Reference:*
    [GoogleTest Cookbook - Use ON_CALL](https://google.github.io/googletest/gmock_cook_book.html#UseOnCall)
- **Descriptive Test Names:** Include the test scenario and expected outcome in
  test names.
  - *Reference:*
    [Testing on the Toilet: Writing Descriptive Test Names](https://testing.googleblog.com/2014/10/testing-on-toilet-writing-descriptive.html)
- **Do Not Crash in Tests:** Use `FAIL() << "message";` instead of triggering a
  crash when verifying preconditions or expectations.
  - *Reference:*
    [GoogleTest Reference: Success/Failure Assertions](https://google.github.io/googletest/reference/assertions.html#success-failure)
- **Avoid Double Negations:** Use positive assertions for better readability.
  - *Reference:*
    [Testing Blog: Improve Readability with Positive Assertions](https://testing.googleblog.com/2023/10/improve-readability-with-positive.html)

______________________________________________________________________

## 4. Code Review and CL Process

- **Meaningful CL Descriptions:** The CL description must explain **why** the
  change is made, not just *what* changed. List major goals, link issue tickets,
  and clarify the bigger architectural plan.
  - *Reference:*
    [Google Engineering Practices: CL Descriptions](https://google.github.io/eng-practices/review/developer/cl-descriptions.html)
- **Keep CLs Small:** Split large changes into multiple CLs. Introduce interface
  definitions in one CL, and migrate callers in subsequent CLs.
  - *Reference:*
    [Google Engineering Practices: Small CLs](https://google.github.io/eng-practices/review/developer/small-cls.html)
- **Documentation Freshness:** When adding new `.md` files, always include
  freshness metadata to track ownership.
  - *Reference:*
    [WebRTC Documentation Guidelines](https://webrtc.googlesource.com/src/+/refs/heads/main/g3doc/how_to_write_documentation.md)

______________________________________________________________________

## 5. Inclusive Language Guidelines

- **Use Inclusive Terminology:** Avoid gendered pronouns and non-inclusive terms
  in code, comments, and documentation. Use precise, neutral technical
  alternatives.
  - *Reference:*
    [Inclusive Chromium Code](https://chromium.googlesource.com/chromium/src/+/HEAD/styleguide/inclusive_code.md)
  - *Reference:*
    [Google Developer Documentation Style Guide - Word List](https://developers.google.com/style/word-list)
