(hwinference-architecture)=

# HWInference: the on-device hardware-accelerated inference process

`HWInference` is a utility process that runs native, hardware-accelerated
inference libraries (currently `parakeet.cpp`, backed by `libggml`) outside of
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
on-device recognition side of the Web Speech API, is the only consumer today,
and is used as the worked example throughout.

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

`ONNX Runtime` (for non-LLM type inference) and `llama.cpp` (for LLM-type
inference on text) are eventually expected to also run inside `HWInference`, to
be able to use hardware acceleration for tasks unrelated to speech recognition.

## One process, many users

There is a single `HWInference` process, keyed like every other utility process
by its `SandboxingKind` alone (see `GetProcess`/`LaunchProcess` in
{searchfox}`ipc/glue/UtilityProcessManager.cpp`), with one
`HWInferenceParent` on the main-process side,
`HWInferenceParent::GetSingleton()`.

Content-driven inference reaches it through
`UtilityProcessManager::StartContentHWInferenceManager`. A privileged,
parent-process-triggered consumer — future "browser AI" features — launches the
same process, with `UtilityProcessManager::LaunchProcessWithKeepAlive`.

What such a consumer does need is a manager protocol of its own alongside
{searchfox}`PHWInferenceManager
<toolkit/components/ml/ipc/PHWInferenceManager.ipdl>`, which is
content-specific: today the only way into the process from outside it is the
content path described below.

Isolating consumers from each other in separate processes — chrome-driven from
content-driven, per origin, per feature — is a matter of keying
`UtilityProcessManager` by more than the `SandboxingKind`, so that a single kind
can have several live processes.

The topology this produces: **every** content process shares the one
`HWInference` process, getting its own `HWInferenceManagerParent`
there, which gives its task actors their identity. Solid arrows
are task traffic, going straight between content and the utility process;
dotted ones are model provisioning, which always goes through the main
process.

```{mermaid}
%%{init: {"flowchart": {"htmlLabels": false}}}%%
flowchart LR
  subgraph CP1[Content Process A]
    SR1[SpeechRecognition]
  end
  subgraph CP2[Content Process B]
    SR2[SpeechRecognition]
  end
  subgraph HWC["HWInference"]
    direction TB
    HMP1[Manager for A] --> SRP1[SpeechRecognitionParent]
    HMP2[Manager for B] --> SRP2[SpeechRecognitionParent]
  end
  subgraph MP[Main Process]
    HWP["HWInferenceParent"]
  end

  SR1 --> HMP1
  SR2 --> HMP2
  SRP1 -.-> HWP
  SRP2 -.-> HWP
```

## Process lifetime

Users of the `HWInference` process decide how long it lives.

`UtilityProcessManager::LaunchProcessWithKeepAlive` hands out a
`UtilityProcessKeepAlive` on the process (main thread only), a single one shared
by every caller; when the last reference to it goes away the process is shut down
with `CleanShutdown`, rather than lingering until browser shutdown like other
Utility processes.

- **Content-process consumers** go through {searchfox}`PContent
  <dom/ipc/PContent.ipdl>`: `RequestHWInferenceConnection` acquires a keep-alive
  for the requesting content process -- whether or not the process then starts
  -- and `ReleaseHWInferenceConnection` drops it. `HWInferenceManagerChild`
  sends exactly one release per request, from `ActorDestroy`. `ContentParent`
  holds a single keep-alive for as long as its content process has a connection
  outstanding, and drops it in its own `ActorDestroy`, so a crashed content
  process cannot pin the utility process forever.
- **Parent-process consumers** call `LaunchProcessWithKeepAlive` directly, with
  no IPC involved, and bind their actor to the process it hands back with
  `UtilityProcessKeepAlive::StartUtility`.

A keep-alive holds the process it was acquired on rather than its
`SandboxingKind`, so one that outlives that process — it crashed, or the browser
is shutting down — cannot shut down the process that replaced it.

`UtilityProcessManager` has no policy of its own: it shuts the process down the
moment the last keep-alive on it goes away. Other policies can be implemented,
they belong in the user of the process. An example is `SpeechRecognition`: the
Web API has numerous async static methods, and it would be wasteful to shutdown
the process every time one of those static methods finish, when another one is
about to be called.

## Connecting from a content process

A content process gets a direct channel to the utility process the first time
one is needed, and reuses it: `PHWInferenceManager` is a process-wide singleton,
shared by every `HWInference` consumer in that content process.

- `HWInferenceManagerChild::AcquireConnection()` returns an
  `HWInferenceConnectionGuard`, and establishes the connection if it is not up
  yet. Establishing it creates a `PHWInferenceManager` endpoint pair, binds the
  child-side endpoint locally as `HWInferenceManagerChild` right away, and calls
  `ContentChild::SendRequestHWInferenceConnection` with the parent-side
  endpoint. The connection works right away, no need to wait.
- The connection is owned by its guards: it is closed once the last one is
  dropped, and each consumer decides how long to hold one, so no consumer can
  tear the channel out from under another.
- `ContentParent::RecvRequestHWInferenceConnection`, in the main process,
  acquires a keep-alive for that content process and brokers the endpoint via
  `UtilityProcessManager::StartContentHWInferenceManager`, which starts (or
  reuses) the `HWInference` process and hands the endpoint over via
  `PHWInference::NewContentHWInferenceManager`.
- The utility process binds it as `HWInferenceManagerParent`
  (`HWInferenceManagerParent::CreateForContent`), the parent side of
  {searchfox}`PHWInferenceManager
  <toolkit/components/ml/ipc/PHWInferenceManager.ipdl>`.

This detour through the main process happens once per content process
(subsequent callers reuse the same `HWInferenceManagerChild`). It is also the
riskier of the two `HWInference` IPC boundaries, since content, unlike a
parent-process consumer, may be compromised. See [Security](#security) for
what a task's actor under this manager can and cannot trust from content. Once
established, task traffic, (e.g. audio and timed text for speech recognition)
flows directly between the content and utility processes, without going through
the main process on every message. Model install and consent still route through
the main process, see [Model
provisioning](#model-provisioning-task-resolvers-and-modelhub) below.

```{mermaid}
sequenceDiagram
  autonumber

  box Content Process
    participant SRB as HWInferenceManagerChild
    participant CC as ContentChild
  end

  box Main Process
    participant CP as ContentParent
    participant UPM as UtilityProcessManager
    participant HWP as HWInferenceParent
  end

  box HWInference
    participant HWC as HWInferenceChild
    participant HMP as HWInferenceManagerParent
  end

  Note over SRB: AcquireConnection():<br/>CreateEndpoints(parentEp, childEp)<br/>for PHWInferenceManager
  Note over SRB: bind childEp locally
  SRB->>CC: SendRequestHWInferenceConnection(parentEp)
  CC->>CP: PContent::RequestHWInferenceConnection(parentEp)
  CP->>UPM: StartContentHWInferenceManager(parentEp, contentId)
  Note over UPM: LaunchProcessWithKeepAlive(HW_INFERENCE), then<br/>keepAlive->StartUtility(HWInferenceParent):<br/>launches the process if not already running
  Note over CP: ++mHWInferenceConnections<br/>keeps the UtilityProcessKeepAlive it got back
  UPM->>HWP: SendNewContentHWInferenceManager(parentEp, contentId)
  HWP->>HWC: PHWInference::NewContentHWInferenceManager(parentEp, contentId)
  HWC->>HMP: CreateForContent(parentEp, contentId) (bind)
  Note over SRB,HMP: direct channel established: task actors<br/>(e.g. PSpeechRecognition) are created<br/>directly over it as soon as childEp is bound,<br/>no further main-process hop
```

### Task protocols

`Endpoint::Bind()` binds an actor to the thread that calls it, and that is the
thread every one of that actor's `Recv` methods then runs on. Each task
protocol is a separate toplevel connection created through the manager, so the
two sides choose their threads independently.

`PHWInferenceManager` itself is bound on the main thread in both processes. The
manager answers a request (e.g. `CreateSpeechRecognition()` for speech
recognition) by creating the endpoint pair, binding the utility-process side on
its own thread, passing it the trusted content id it carries
(`HWInferenceManagerParent::ContentId()`), and resolving with the
content-process endpoint.
`HWInferenceManagerChild::CreateSpeechRecognitionSession()` takes the event
target the caller wants its side bound on, dispatches the bind there, and
resolves its promise there too.

Speech recognition passes its `SpeechIPC` thread, keeping audio off the content
main thread. The utility side is bound on the main thread and dispatches
inference to a `Parakeet` thread of its own, so `RecvProcessAudioData` returns
without blocking on it.

```{mermaid}
sequenceDiagram
  autonumber

  box Content Process
    participant C as Consumer<br/>(main thread)
    participant HMC as HWInferenceManagerChild<br/>(main thread)
    participant SRC as SpeechRecognitionChild<br/>(SpeechIPC thread)
  end

  box HWInference
    participant HMP as HWInferenceManagerParent<br/>(main thread)
    participant SRP as SpeechRecognitionParent<br/>(main thread)
  end

  C->>HMC: CreateSpeechRecognitionSession(SpeechIPC)
  HMC->>HMP: CreateSpeechRecognition()
  Note over HMP: CreateEndpoints(parentEp, childEp)
  HMP->>SRP: parentEp.Bind() here, so SRP is bound<br/>to the HWInference main thread
  HMP-->>HMC: resolve(childEp)
  HMC->>SRC: dispatch to SpeechIPC, childEp.Bind() there,<br/>so SRC is bound to SpeechIPC
  SRC-->>C: promise resolves on SpeechIPC
  SRC->>SRP: PSpeechRecognition, SpeechIPC to HWInference main
```

Creating one costs a round trip; teardown is `Close()`.

## Model provisioning: task resolvers and `ModelHub`

Every {searchfox}`PHWInference <toolkit/components/ml/ipc/PHWInference.ipdl>`
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
  Note over SRP: language -> id (dom::LanguagesToSpeechModelId)
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
- Turning those into a model id (`dom::LanguagesToSpeechModelId` for speech
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
  value content supplies. That id comes from the manager
  (`HWInferenceManagerParent::ContentId()`), which got it from `ContentParent`
  when it brokered the connection.
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

The process and its lifetime rules are covered by gtests in
{searchfox}`ipc/glue/test/gtest/TestUtilityProcess.cpp`:

Gtest exercise this new process: 'TestUtilityProcess.HWInference*'.

The content path, model provisioning and consent are exercised end to end by
the speech recognition tests, see [its
documentation](/media/SpeechRecognition).
