# Fuzzing

```{toctree}
:glob: true
:hidden: true
:maxdepth: 1
:reversed: true

*
```

This section focuses on explaining the software testing technique called
“Fuzzing” or “Fuzz Testing” and its application to the Mozilla codebase.
The overall goal is to educate developers about the capabilities and
usefulness of fuzzing and also allow them to write their own fuzzing
targets. Note that not all fuzzing tools used at Mozilla are open
source. Some tools are for internal use only because they can easily
find critical security vulnerabilities.

## What is Fuzzing?

Fuzzing (or Fuzz Testing) is a technique to randomly use a program or
parts of it with the goal to uncover bugs. Random usage can have a wide
variety of forms, a few common ones are

- random input data (e.g. file formats, network data, source code, etc.)
- random API usage
- random UI interaction

with the first two being the most practical methods used in the field.
Of course, these methods are not entirely separate, combinations are
possible. Fuzzing is a great way to find quality issues, some of them
being also security issues.

### Random input data

This is probably the most obvious fuzzing method: You have code that
processes data and you provide it with random or mutated data, hoping
that it will uncover bugs in your implementation. Examples are media
formats like JPEG or H.264, but basically anything that involves
processing a “blob” of data can be a valuable target. Countless security
vulnerabilities in a variety of libraries and programs have been found
using this method (the AFLFuzz
[bug-o-rama](http://lcamtuf.coredump.cx/afl/#bugs) gives a good
impression).

Common tools for this task are e.g.
[libFuzzer](https://llvm.org/docs/LibFuzzer.html) and
[AFLFuzz](http://lcamtuf.coredump.cx/afl/), but also specialized
tools with custom logic like
[LangFuzz](https://www.usenix.org/system/files/conference/usenixsecurity12/sec12-final73.pdf)
and [Avalanche](https://github.com/MozillaSecurity/avalanche).

### Random API Usage

Randomly testing APIs is especially helpful with parts of software that
expose a well-defined interface (see also {ref}`Well-defined
behavior and Safety <well-defined-behaviour-and-safety>`). If this interface is additionally exposed to
untrusted parties/content, then this is a strong sign that random API
testing would be worthwhile here, also for security reasons. APIs can be
anything from C++ layer code to APIs offered in the browser.

A good example for a fuzzing target here is the DOM (Document Object
Model) and various other browser APIs. The browser exposes a variety of
different APIs for working with documents, media, communication,
storage, etc. with a growing complexity. Each of these APIs has
potential bugs that can be uncovered with fuzzing. At Mozilla, we
currently use domino (internal tool) for this purpose.

### Random UI Interaction

A third way to test programs and in particular user interfaces is by
directly interacting with the UI in a random way, typically in
combination with other actions the program has to perform. Imagine for
example an automated browser that surfs through the web and randomly
performs actions such as scrolling, zooming and clicking links. The nice
thing about this approach is that you likely find many issues that the
end-user also experiences. However, this approach typically suffers from
bad reproducibility (see also {ref}`Reproducibility <reproducibility>`) and is therefore
often of limited use.

An example for a fuzzing tool using this technique is [Android
Monkey](https://developer.android.com/studio/test/monkey). At
Mozilla however, we currently don’t make much use of this approach.

## Why Fuzzing Helps You

Understanding the value of fuzzing for you as a developer and software
quality in general is important to justify the support this testing
method might need from you. When your component is fuzzed for the first
time there are two common things you will be confronted with:

**Bug reports that don’t seem real bugs or not important:** Fuzzers
find all sorts of bugs in various corners of your component, even
obscure ones. This automatically leads to a larger number of bugs that
either don’t seem to be bugs (see also the {ref}`Well-defined behavior and
safety <well-defined-behaviour-and-safety>` section below) or that don’t seem to be important bugs.

Fixing these bugs is still important for the fuzzers because ignoring them
in fuzzing costs resources (performance, human resources) and might even
prevent the fuzzer from hitting other bugs. For example certain fuzzing tools
like libFuzzer run in-process and have to restart on every crash, involving a
costly re-read of the fuzzing samples.

Also, as some of our code evolves quickly, a corner case might become a
hot code path in a few months.

**New steps to reproduce:** Fuzzing tools are very likely to exercise
your component using different methods than an average end-user. A
common technique is modify existing parts of a program or write entirely
new code to yield a fuzzing "target". This target is specifically
designed to work with the fuzzing tools in use. Reproducing the reported
bugs might require you to learn these new steps to reproduce, including
building/acquiring that target and having the right environment.

Both of these issues might seem like a waste of time in some cases,
however, realizing that both steps are a one-time investment for a
constant stream of valuable bug reports is paramount here. Helping your
security engineers to overcome these issues will ensure that future
regressions in your code can be detected at an earlier stage and in a
form that is more easily actionable. Especially if you are dealing with
regressions in your code already, fuzzing has the potential to make your
job as a developer easier.

One of the best examples at Mozilla is the JavaScript engine. The JS
team has put great quite some effort into getting fuzzing started and
supporting our work. Here’s what Jan de Mooij, a senior platform
engineer for the JavaScript engine, has to say about it:

*“Bugs in the engine can cause mysterious browser crashes and bugs that
are incredibly hard to track down. Fortunately, we don't have to deal
with these time consuming browser issues very often: usually the fuzzers
find a reliable shell test long before the bug makes it into a release.
Fuzzing is invaluable to us and I cannot imagine working on this project
without it.”*

## Levels of Fuzzing in Firefox/Gecko

Applying fuzzing to e.g. Firefox happens at different "levels", similar
to the different types of automated tests we have:

### Full Browser Fuzzing

The most obvious method of testing would be to test the full browser and
doing so is required for certain features like the DOM and other APIs.
The advantage here is that we have all the features of the browser
available and testing happens closely to what we actually ship. The
downside here though is that browser testing is by far the slowest of
all testing methods. In addition, it has the most amount of
non-determinism involved (resulting e.g. in intermittent testcases).
Browser fuzzing at Mozilla is largely done with the [Grizzly
framework](https://blog.mozilla.org/security/2019/07/10/grizzly/)
([meta bug](https://bugzilla.mozilla.org/show_bug.cgi?id=grizzly))
and one of the most successful fuzzers is the Domino tool ([meta
bug](https://bugzilla.mozilla.org/show_bug.cgi?id=domino)).

Summarizing, full browser fuzzing is the right technique to investigate
if your feature really requires it. Consider using other methods (see
below) if your code can be exercised in this way.

### The Fuzzing Interface

**Fuzzing Interface**

The fuzzing interface is glue code living in the Firefox repository in order to make it
easier for developers and security researchers to test C/C++ code with either libFuzzer or afl-fuzz.

This interface offers a gtest (C++ unit test) level component based
fuzzing approach and is suitable for anything that could also be
tested/exercised using a gtest. This method is by far the fastest, but
usually limited to testing isolated components that can be instantiated
on this level. Utilizing this method requires you to write a fuzzing
target similar to writing a gtest. This target will automatically be
usable with libFuzzer and AFLFuzz. We offer a {doc}`comprehensive manual <fuzzing_interface>`
that describes how to write and utilize your own target.

A simple example here is the {searchfox}`SDP parser target <mozilla-central/rev/efdf9bb55789ea782ae3a431bda6be74a87b041e:media/webrtc/signaling/fuzztest/sdp_parser_libfuzz.cpp#30>`,
which tests the SipccSdpParser in our codebase.

### Shell-based Fuzzing

Some of our fuzzing, e.g. JS Engine testing, happens in a separate shell
program. For JS, this is the JS shell also used for most of the JS tests
and development. In theory, xpcshell could also be used for testing but
so far, there has not been a use case for this (most things that can be
reached through xpcshell can also be tested on the gtest level).

Identifying the right level of fuzzing is the first step towards
continuous fuzz testing of your code.

## Code/Process Requirements for Fuzzing

In this section, we are going to discuss how code should be written in
order to yield optimal results with fuzzing.

### Defect Oracles

Fuzzing is only effective if you are able to know when a problem has
been found. Crashes are typically problems if the unit being tested is
safe for fuzzing (see Well-defined behavior and Safety). But there are
many more problems that you would want to find, correctness issues,
corruptions that don’t necessarily crash etc. For this, you need an
*oracle* that tells you something is wrong.

The simplest defect oracle is the assertion (ex: `MOZ_ASSERT`).
Assertions are a very powerful instrument because they can be used to
determine if your program is performing correctly, even if the bug would
not lead to any sort of crash. They can encode arbitrarily complex
information about what is considered correct, information that might
otherwise only exist in the developers’ minds.

External tools like the sanitizers (AddressSanitizer aka ASan,
ThreadSanitizer aka TSan, MemorySanitizer aka MSan and
UndefinedBehaviorSanitizer - UBSan) can also serve as oracles for
sometimes severe issues that would not necessarily crash. Making sure
that these tools can be used on your code is highly useful.

Examples for bugs found with sanitizers are [bug
1419608](https://bugzilla.mozilla.org/show_bug.cgi?id=1419608),
[bug 1580288](https://bugzilla.mozilla.org/show_bug.cgi?id=1580288)
and [bug 922603](https://bugzilla.mozilla.org/show_bug.cgi?id=922603),
but since we started using sanitizers, we have found over 1000 bugs with
these tools.

Another defect oracle can be a reference implementation. Comparing
program behavior (typically output) between two programs or two modes of
the same program that should produce the same outputs can find complex
correctness issues. This method is often called differential testing.

One example where this is regularly used to find issues is the Mozilla
JavaScript engine: Running random programs with and without JIT
compilation enabled finds lots of problems with the JIT implementation.
One example for such a bug is [Bug
1404636](https://bugzilla.mozilla.org/show_bug.cgi?id=1404636).

### Component Decoupling

Being able to test components in isolation can be an advantage for
fuzzing (both for performance and reproducibility). Clear boundaries
between different components and documentation that explains the
contracts usually help with this goal. Sometimes it might be useful to
mock a certain component that the target component is interacting with
and that is much harder if the components are tightly coupled and their
contracts unclear. Of course, this does not mean that one should only
test components in isolation. Sometimes, testing the interaction between
them is even desirable and does not hurt performance at all.

### Avoiding external I/O

External I/O like network or file interactions are bad for performance
and can introduce additional non-determinism. Providing interfaces to
process data directly from memory instead is usually much more helpful.

(well-defined-behaviour-and-safety)=

### Well-defined Behavior and Safety

This requirement mostly ties in where defect oracles ended and is one of
the most important problems seen in the wild nowadays with fuzzing. If a
part of your program’s behavior is unspecified, then this potentially
leads to bad times if the behavior is considered a defect by fuzzing.
For example, if your code has crashes that are not considered bugs, then
your code might be unsuitable for fuzzing. Your component should be
fuzzing safe, meaning that any defect oracle (e.g. assertion or crash)
triggered by the fuzzer is considered a bug. This important aspect is
often neglected. Be aware that any false positives cause both
performance degradation and additional manual work for your fuzzing
team. The Mozilla JS developers for example have implemented this
concept in a “--fuzzing-safe” switch which disables harmful functions.
Sometimes, crashes cannot be avoided for handling certain error
conditions. In such situations, it is important to mark these crashes in
a way the fuzzer can recognize and distinguish them from undesired
crashes. However, keep in mind that crashes in general can be disruptive
to the fuzzing process. Performance is an important aspect of fuzzing
and frequent crashes can severely degrade performance.

(reproducibility)=

### Reproducibility

Being able to reproduce issues found with fuzzing is necessary for
several reasons: First, you as the developer probably want a test that
reproduces the issue so you can debug it better. Our feedback from most
developers is that traces without a reproducible test can help to find a
problem, but it makes the whole process very complicated. Some of these
non-reproducible bugs never get fixed. Second, having a reproducible
test also helps the triage process by allowing an automated bisection to
find the responsible developer. Last but not least, the test can be
added to a test suite, used for automated verification of fixes and even
serve as a basis for more fuzzing.

Adding functionality to the program that improve reproducibility is
therefore a good idea in case non-reproducible issues are found. Some
examples are shown in the next section.

While many problems with reproducibility are specific for the project
you are working on, there is one source of these problems that many
programs have in common: Threading. While some bugs only occur in the
first place due to concurrency, some other bugs would be perfectly
reproducible without threads, but are intermittent and hard to with
threading enabled. If the bug is indeed caused by a data race, then
tools like ThreadSanitizer will help and we are currently working on
making ThreadSanitizer usable on Firefox. For bugs that are not caused
by threading, it sometimes makes sense to be able to disable threading
or limit the amount of worker threads involved.

### Supporting Code

Some possibilities of what support implementations for fuzzing can do
have already been named in the previous sections: Additional defect
oracles and functionality to improve reproducibility and safety. In
fact, many features added specifically for fuzzing fit into one of these
categories. However, there’s room for more: Often, there are ways to
make it easier for fuzzers to exercise complex and hard to reach parts
of your code. For example, if a certain optimization feature is only
turned on under very specific conditions (that are not a requirement for
the optimization), then it makes sense to add a functionality to force
it on. Then, a fuzzer can hit the optimization code much more
frequently, increasing the chance to find issues. Some examples from
Firefox and SpiderMonkey:

- The {searchfox}`FuzzingFunctions <mozilla-central/rev/efdf9bb55789ea782ae3a431bda6be74a87b041e:dom/webidl/FuzzingFunctions.webidl#15>`
  interface in the browser allows fuzzing tools to perform GC/CC, tune various
  settings related to garbage collection or enable features like accessibility
  mode. Being able to force a garbage collection at a specific time helped
  identifying lots of problems in the past.
- The --ion-eager and --baseline-eager flags for the JS shell force JIT
  compilation at various stages, rather than using the builtin
  heuristic to enable it only for hot functions.
- The --no-threads flag disables all threading (if possible) in the JS shell.
  This makes some bugs reproduce deterministically that would otherwise be
  intermittent and harder to find. However, some bugs that only occur with
  threading can’t be found with this option enabled.

Another important feature that must be turned off for fuzzing is
checksums. Many file formats use checksums to validate a file before
processing it. If a checksum feature is still enabled, fuzzers are
likely never going to produce valid files. The same often holds for
cryptographic signatures. Being able to turn off the validation of these
features as part of a fuzzing switch is extremely helpful.

An example for such a checksum can be found in the
{searchfox}`FlacDemuxer <mozilla-central/rev/efdf9bb55789ea782ae3a431bda6be74a87b041e:dom/media/flac/FlacDemuxer.cpp#494>`.

### Test Samples

Some fuzzing strategies make use of existing data that is mutated to
produce the new random data. In fact, mutation-based strategies are
typically superior to others if the original samples are of good quality
because the originals carry a lot of semantics that the fuzzer does not
have to know about or implement. However, success here really stands and
falls with the quality of the samples. If the originals don’t cover
certain parts of the implementation, then the fuzzer will also have to
do more work to get there.

### Fuzz Blockers

Fuzz blockers are issues that prevent fuzzers from being as
effective as possible. Depending on the fuzzer and its scope a fuzz blocker
in one area (or component) can impede performance in other areas and in
some cases block the fuzzer all together. Some examples are:

- Frequent crashes - These can block code paths and waste compute
  resources due to the need to relaunch the fuzzing target and handle
  the results (regardless of whether it is ignored or reported). This can also
  include assertions that are mostly benign in many cases are but easily
  triggered by fuzzers.
- Frequent hangs / timeouts - This includes any issue that slows down
  or blocks execution of the fuzzer or the target.
- Hard to bucket - This includes crashes such as stack overflows or any issue
  that crashes in an inconsistent location. This also includes issues that
  corrupt logs/debugger output or provide a broken/invalid crash report.
- Broken builds - This is fairly straightforward, without up-to-date builds
  fuzzers are unable to run or verify fixes.
- Missing instrumentation - In some cases tools such as ASan are used as
  defect oracles and are required by the fuzzing tools to allow for proper
  automation. In other cases incomplete instrumentation can give a false sense
  of stability or make investigating issues much more time consuming. Although
  this is not necessarily blocking the fuzzers it should be prioritized
  appropriately.

Since these types of crashes harm the overall fuzzing progress, it is important
for them to be addressed in a timely manner. Even if the bug itself might seem
trivial and low priority for the product, it can still have devastating effects
on fuzzing and hence prevent finding other critical issues.

Issues in Bugzilla are marked as fuzz blockers by adding “[fuzzblocker]”
to the “Whiteboard” field. A list of open issues marked as fuzz blockers
can be found on [Bugzilla](https://bugzilla.mozilla.org/buglist.cgi?cmdtype=dorem&remaction=run&namedcmd=fuzzblockers&sharer_id=486634).

### Documentation

It is important for the fuzzing team to know how your software, tests
and designs work. Even obvious tasks, like how a test program is
supposed to be invoked, which options are safe, etc. might be hard to
figure out for the person doing the testing, just as you are reading
this manual right now to find out what is important in fuzzing.

(fuzzing-contact-us)=

### Contact Us

The fuzzing team can be reached at
[fuzzing@mozilla.com](mailto:fuzzing@mozilla.com) or
[on Matrix](https://chat.mozilla.org/#/room/#fuzzing:mozilla.org)
and will be happy to help you with any questions about fuzzing
you might have. We can help you find the right method of fuzzing for
your feature, collaborate on the implementation and provide the
infrastructure to run it and process the results accordingly.
