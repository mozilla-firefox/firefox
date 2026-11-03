(hwinference-architecture)=

# HWInference: the on-device hardware-accelerated inference process

`HWInference` is a utility process that runs native, hardware-accelerated
inference libraries (`parakeet.cpp` and `llama.cpp`, backed by `libggml`) outside of
any content process, and outside the main process. Unlike the [Firefox AI
Runtime](inference-architecture) inference process, it does not run
JavaScript: its job is purely computational, receiving some input, running it
against a model file, and producing some output.

This page describes the generic facility: the process itself, how a consumer
connects to it, how models are provisioned, and the security properties that
hold regardless of who the consumer is. It does not cover the specifics of any
one consumer.

The plumbing lives in {searchfox}`toolkit/components/ml/ipc`, and the process
itself is managed by {searchfox}`UtilityProcessManager
<ipc/glue/UtilityProcessManager.cpp>`.
[`SpeechRecognition`](/media/SpeechRecognition), which implements the
on-device recognition side of the Web Speech API, and browser text generation are its current consumers. Speech recognition
is used as the worked example for content-process connections.

## The `HWInference` process

The process is a utility process with its own `SandboxingKind`,
{searchfox}`HW_INFERENCE <ipc/glue/UtilityProcessSandboxing.h>`. Its sandbox
policy resembles that of the GPU process, but it doesn't have access to the
display server, or to things like fonts, or other special system calls or
capabilities related to rendering. It only does computations: it receives some
input (e.g. text, image, audio data) and uses a model file and a library to
perform inference, and produces some output (e.g. timed text fragments,
summary). On macOS it gets a dedicated profile,
{searchfox}`SandboxPolicyHWInference
<security/sandbox/mac/SandboxPolicyHWInference.h>`, rather than the generic
utility one; on Linux, Windows it shares the generic utility policy.

It is generally only started when needed, and closed quickly when not needed
anymore, but lifetime is in the hands of the *consumer* of the `HWInference`
system, see [Process lifetime](#process-lifetime).

It delegates all model management tasks to the
{searchfox}`ModelHub <toolkit/components/ml/content/ModelHub.sys.mjs>`,
which it calls via IPC to the parent. This includes model availability checks
and download (`ModelHub` handles caching). It can also acquire a handle to a
model file using a `FileDescriptor` passed via IPC, without copy, important
because model files can be quite big. This also allows `mmap`ing some models
(notably mixture-of-experts models), for significant memory footprint gains.

Since it doesn't run JavaScript, it will eventually be possible to tighten the
sandbox further on macOS by making it a different executable, relinquishing
the capability to mark pages as executable for JITing code.

`llama.cpp` runs text generation here when `browser.ml.llama.hwInference` is
enabled. `ONNX Runtime` (for non-LLM inference) is expected to follow.

## Two processes, two kinds of users

A `SandboxingKind` does not imply a single process. `mProcesses` in
{searchfox}`ipc/glue/UtilityProcessManager.cpp` is a flat list: `LaunchProcess`
still hands out the kind's shared process, which lives until `CleanShutdown`,
and `LaunchIndependentProcess` spawns one whose lifetime is exactly that of the
`UtilityProcessKeepAlive` it hands back. `HW_INFERENCE` has two live processes
of the second sort, one per class of consumer, each with its own
`HWInferenceParent` on the main-process side and both under the same sandbox
policy.

They are separate so that a process a content process can reach never shares an
address space with browser data. Content is the riskier IPC peer, and the
process serving it parses what it sends; the browser's process holds page text
and prompts from every origin its features touch. The two also have independent
lifetimes and restart budgets.

{searchfox}`HWInferenceProcess <toolkit/components/ml/ipc/HWInferenceProcess.h>`
is what shares one of them between the consumers of one class: it launches the
process on the first `Acquire`, hands every consumer the same keep-alive, and
tracks that keep-alive weakly, so the consumers alone decide how long the process
lives. It also owns the `HWInferenceParent` bound to that process.
Both `HWInferenceProcess::Content` and `HWInferenceProcess::Browser` stop
relaunching after `browser.ml.hwinference.max_restarts` unexpected deaths in a
row. Their counters are independent; a clean shutdown resets the counter.

Content-driven inference reaches its process through `PContent`, see below. A
browser consumer calls `HWInferenceProcess::Browser().Acquire()`, sends on
`Actor()` once its `WhenReady` resolves, and drops its keep-alive when done.
`TextGenerationParent::ActorDestroy` delays its release by
`browser.ml.hwinference.browser_idle_shutdown_grace_ms`, allowing a subsequent
generator to reuse the process. This grace belongs to text generation, not to
all users of the process. A dead process's keep-alive is harmless to drop; the
next acquire launches a fresh process if its restart budget permits.

Either way, task endpoints reach the process through a `Start*` member of that
`HWInferenceProcess`'s actor, which waits for it to be bound before sending.
`TextGenerationParent::Create` does this for a text generator: it acquires the
process, binds the parent side of a `PTextGeneration` and hands the child side
over with the model file.

Isolating consumers further, per origin, per feature, is a matter of giving each
class its own `HWInferenceProcess`.

The content and browser instances do not share task channels or model state:

```{mermaid}
flowchart LR
  CP[Content processes] --> SR[SpeechRecognitionParent]
  subgraph ContentHW[Content HWInference]
    SR
  end
  subgraph Main[Main process]
    API[TextGenerator] --> TP[TextGenerationParent]
    CHP[Content HWInferenceParent]
    BHP[Browser HWInferenceParent]
  end
  subgraph BrowserHW[Browser HWInference]
    TC[TextGenerationChild] --> LB[LlamaBackend]
  end
  TP --> TC
  SR -. Model requests .-> CHP
  BHP -. Creates generator .-> TC
```

### Text generation threads

`TextGenerationChild` binds on the utility main thread. IPC handlers move each
request to the generation thread, which owns the conversation history. Formatting
reads the committed history and the incoming messages; successful results move
the incoming messages into history before replying. Errors leave history unchanged.
Cancelled and zero-token results retain their input messages, as other successful
results do. `Clear` and history destruction run on the same worker queue.
There is no per-generation history snapshot. Backend input conversion and prompt
formatting still traverse the conversation on the worker.

Each generator owns a `TextGenerator` thread for model loading, prompt formatting,
prefill, decoding, and backend destruction. llama.cpp's internal thread pools
are driven from that thread. Replies and output deltas dispatch back to the
actor's event target. Cancellation and shutdown cross the boundary through
atomic flags. Future engines sharing this process must likewise keep engine
compute and blocking model operations off the utility main thread.

## Process lifetime

Users of an `HWInference` process decide how long it lives.

`HWInferenceProcess::Acquire` hands out the `UtilityProcessKeepAlive` of the
running or launching process (main thread only), the same one to every caller;
when the last reference to it goes away the process is shut down, rather than
lingering until browser shutdown like other Utility processes.

- **Content-process consumers** go through {searchfox}`PContent
  <dom/ipc/PContent.ipdl>`: `AcquireHWInferenceProcess` acquires the content
  process' keep-alive -- whether or not the process then starts -- and
  `ReleaseHWInferenceConnection` drops it. `ContentParent` holds a single
  keep-alive for as long as its content process has a connection outstanding,
  and drops it in its own `ActorDestroy`, so a crashed content process cannot
  pin the utility process forever.
- **Parent-process consumers** call `Acquire` directly, with no IPC involved,
  send on `HWInferenceProcess::Actor` once its `WhenReady` resolves, and drop
  the keep-alive when their own lifetime policy allows.

A keep-alive holds the process it was acquired on rather than its
`SandboxingKind`, so one that outlives that process — it crashed, or the browser
is shutting down — cannot shut down the process that replaced it. A process that
dies, or never comes up, needs nothing from its consumers: the next `Acquire`
launches a fresh one, with a fresh actor, and whoever waited on the old actor's
`WhenReady` is told.

`UtilityProcessManager` has no policy of its own: it shuts the process down the
moment the last keep-alive on it goes away. Other policies belong in the user of
the process. An example is `SpeechRecognition`: the Web API has numerous async
static methods, and it would be wasteful to shutdown the process every time one
of those static methods finish, when another one is about to be called.

## Connecting from a content process

Content consumers first send `PContent::AcquireHWInferenceProcess`. The main
process counts outstanding connections in `ContentParent` and acquires the
content instance's keep-alive. `PContent::ReleaseHWInferenceConnection` drops
that keep-alive when the connection count reaches zero. Content-process death
also drops it.

For speech recognition, content creates a `PSpeechRecognition` endpoint pair
and sends the parent endpoint through `PContent::CreateSpeechRecognition`.
`ContentParent::RecvCreateSpeechRecognition` requires an outstanding connection
and supplies its trusted content-process identity to
`HWInferenceParent::StartContentSpeechRecognition`. Once the utility actor is
ready, `PHWInference::NewContentSpeechRecognition` delivers the endpoint and
identity. `HWInferenceChild` binds a `SpeechRecognitionParent` on the utility
main thread.

```{mermaid}
sequenceDiagram
  participant C as Content process
  participant CP as ContentParent
  participant HWP as HWInferenceParent
  participant HWC as HWInferenceChild
  participant SRP as SpeechRecognitionParent
  C->>CP: AcquireHWInferenceProcess()
  Note over CP: Hold content HWInference keep-alive
  C->>CP: CreateSpeechRecognition(parentEndpoint)
  CP->>HWP: StartContentSpeechRecognition(endpoint, contentId)
  Note over HWP: Wait for utility actor readiness
  HWP->>HWC: NewContentSpeechRecognition(endpoint, contentId)
  HWC->>SRP: Bind endpoint on utility main thread
  C->>SRP: Direct speech IPC
  C->>CP: ReleaseHWInferenceConnection()
```

The main process brokers each task endpoint, but subsequent audio and timed
text flow directly between content and the utility process. Model provisioning
and consent continue to route through the main process. Failed startup drops
the endpoint, allowing the content-side actor to report failure.

### Task protocols

`Endpoint::Bind()` selects the thread on which an actor's `Recv` methods run.
Each task protocol is a separate top-level connection, so its two sides choose
their event targets independently. Speech recognition uses `SpeechIPC` in
content; the utility side receives on the main thread and dispatches inference
to its `Parakeet` thread. Text generation uses the thread split described above.

## Model provisioning: task resolvers and `ModelHub`

Each model-provisioning {searchfox}`PHWInference <toolkit/components/ml/ipc/PHWInference.ipdl>`
request carries a `(task, id)` pair. Two things happen with it, both in the
parent process, in `HWInferenceParent`.

**Resolution of a model:** `task` selects an {searchfox}`nsIMLModelResolver
<toolkit/components/ml/nsIMLModelResolver.idl>`, looked up as the XPCOM
component `@mozilla.org/ml/model-resolver;1?task=<task>`. Its `resolve()` maps
`id` to the `engine`/`model`/`revision`/`filename` of a `ModelHub` artifact,
out of static in-tree data compiled into the binary. An unknown `task` or `id`
fails the request before any `ModelHub` call.
For example, {searchfox}`SpeechModelResolver
<dom/media/webspeech/recognition/SpeechModelResolver.cpp>` resolves the ids
declared in {searchfox}`models.yaml
<dom/media/webspeech/recognition/models.yaml>`.

**Model download gating:** ML models can be pretty big, and so user consent (or
arbitrary asynchronous code) can be inserted prior to a download with
`authorizeDownload()`. It gets the resolved model (and e.g., its size, but other
metadata can be added) and the `WindowGlobalParent` responsible for the request
(0 denotes a parent-process user). If the model is already present locally, this
is resolved immediately. For example, in `SpeechRecognition`, a doorhanger on
that window's tab is displayed the first time a specific language is requested

End to end, with speech recognition as example, originating from a Content
process:

```{mermaid}
sequenceDiagram
  autonumber

  box Content Process
    participant SR as SpeechRecognition
  end

  box HWInference
    participant SRP as SpeechRecognitionParent
  end

  box Main Process
    participant HWP as HWInferenceParent
    participant Res as SpeechModelResolver
    participant MH as nsIMLModelHub (ModelHub)
  end

  SR->>SRP: install(["fr"], innerWindowId)
  Note over SRP: language -> id (dom::SpeechModelFor)
  SRP->>HWP: InstallModel(task, id, innerWindowId,<br/>contentId)
  Note over SRP,HWP: contentId is supplied by the utility, never sent by content.<br/>A parent-process caller passes 0 for both ids.
  HWP->>Res: resolve(id)
  Res-->>HWP: engine/model/revision/filename
  Note over HWP: window must be owned by contentId<br/>(see Security, below)<br/>progressToken created here, to tell<br/>concurrent installs apart
  HWP->>Res: authorizeDownload(model, revision, filename,<br/>window, progressToken, callback)
  Note over Res: already cached, or the user hit Allow<br/>on the model-download doorhanger
  Res-->>HWP: callback->Resolve(allow)
  alt allowed
    HWP->>MH: DownloadModel(engine, task, model, revision, files,<br/>progressToken, progressCallback, completionCallback)
    MH--)HWP: progress callback(s)
    MH-->>HWP: success/fail
  else denied
    Note over HWP: nothing downloaded
  end
  HWP-->>SRP: true/false
  SRP-->>SR: Promise resolves(installed)
```

### The testing mock

Under `browser.ml.modelHub.testing`, `HWInferenceParent` answers from an
in-memory set of "installed" models instead of calling `ModelHub`, so install
and availability agree on what has been "downloaded". Resolution and
`authorizeDownload()` still run.

This is useful e.g. for WPT, for which it is harder to run custom code serving
model in CI.

This isn't needed for Mochitests, who can pull arbitrarily large model files in
there tasks, and run a custom python server to mimick ModelHub repository. This
also means end-to-end testing is possible.

## Security

The consent decision and the download both live entirely in the trusted parent
(main) process, so a compromised content process has no path to install or read
an arbitrary model file, nor to trigger a download without the user's consent.

- Content-facing protocols (e.g. {searchfox}`PSpeechRecognition
  <dom/media/webspeech/recognition/PSpeechRecognition.ipdl>`) never mention
  `model`/`revision`/`filename`. They only carry task-specific, abstract
  identifiers — for `SpeechRecognition`, BCP-47 language tags.
- Turning those into a model id (`dom::SpeechModelFor` for speech
  recognition) reads only a table generated at build time and compiled into the
  binary; it is not loaded from anything runtime-writable or
  attacker-writable. That mapping happens wherever the task's actor runs, for
  `SpeechRecognition`, in the utility process, never in content. Model selection
  can depend on e.g. checking if hardware acceleration is available, and so is
  best done in the `HWInference` process.
- `HWInferenceParent`, on the main-process side, resolves the id back to the
  `ModelHub` slug by calling the task's `nsIMLModelResolver`, which reads the
  very same compiled-in table.

So the only attacker-influenced input anywhere on this path is a task-specific
abstract identifier, matched against a static compiled-in table, and that id is
the only thing that crosses IPC.

### Consent to a model download cannot be faked by content

A model download requires the task resolver's authorization, decided **and
enforced in the parent (main) process**, never in content or in the utility
process:

- Content can only *ask*. It sends its request for a model along with the
  **inner window id** of its requesting document (not a `BrowsingContext` id)
  to the task's utility-process actor. It never sees, and cannot tweak, a
  consent answer.
- The task's utility-process actor maps the request to a model id and relays
  `PHWInference::InstallModel` to the main process, attaching the **trusted**
  `ContentParentId` of the content process that owns the connection — never a
  value content supplies. `ContentParent` attaches it when brokering the
  speech-recognition endpoint through `StartContentSpeechRecognition`.
- `HWInferenceParent::RecvInstallModel` (main process) resolves the id via the
  task's `nsIMLModelResolver`, then resolves the content-supplied inner window
  id to a `WindowGlobalParent` (`WindowGlobalParent::GetByInnerWindowId`) and
  denies the request unless that window's `ContentParentId()` matches the
  trusted `contentId`. A compromised content process cannot name a
  window it does not own to anchor the prompt on another tab or act for another
  origin. A parent-process request has no such check to make: the caller is
  parent-process code, so `contentId`/`innerWindowId` are `0`.
- Only then is the download authorized, by the same `nsIMLModelResolver`.
  Speech recognition's policy is to check whether the model is already cached
  (`nsIMLModelHub`), allowing with no prompt if so, and otherwise to show a
  doorhanger; another task can authorize immediately, show a custom prompt, or
  apply any other policy. Only on a real Allow does `HWInferenceParent` start
  the download via `nsIMLModelHub::DownloadModel`.

## Logging and tests

`MOZ_LOG=HWInference:5` traces the whole facility: connection setup,
`RecvInstallModel`/`RecvIsModelInstalled` and the rest of the model path, and
actor lifetime, in every process involved. `ModelHub:4` can also be useful.

The process lifetime and restart policy are covered by `HWInferenceProcessTest`
and `BrowserHWInferenceProcessTest` in
{searchfox}`toolkit/components/ml/tests/gtest/TestHWInferenceProcess.cpp`.
`TextGenerationTest` covers model loading, generation, cancellation, failure,
and the idle grace with the utility sandbox enabled. Browser tests in
{searchfox}`toolkit/components/ml/tests/browser` cover the WebIDL and MLEngine
surfaces, concurrent generators, profiler markers, and telemetry.

The content path, model provisioning and consent are exercised end to end by
the speech recognition tests, see [its documentation](/media/SpeechRecognition).
