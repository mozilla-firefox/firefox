# Gecko Speech Recognition implementation architecture

This document summarizes the architecture, call flows, threading model, object
lifetimes, and known gaps for the new SpeechRecognition implementation, that is
able to perform recognition locally, without relying on an external service.

For now, it is intended to help reviewing the implementation, and will be turned
into an architecture overview document prior to landing.

## Architecture

Here's an high level outline of the components of this system, :

Content process:

- `SpeechRecognition` (main thread): implementation of the "recognition" side of
  the [Web Speech
  API](https://webaudio.github.io/web-speech-api/#speechreco-section)
- `SpeechRecognitionBackend` (main thread, real-time audio thread, IPC thread):
  audio ingestion and processing, audibility detection, IPC communication with
  the utility process (sending audio, receiving transcript).
- `SpeechRecognitionChild`: content-side endpoint of each top-level
  `PSpeechRecognition` actor, bound on the dedicated `SpeechIPC` thread.

Utility Process (HWInference)
- `SpeechRecognitionParent`: utility-side endpoint of the top-level
  `PSpeechRecognition` actor. `HWInferenceChild` constructs it with the trusted
  `ContentParentId` supplied by `ContentParent`, so install requests are
  attributed to their true requester.
  `IsModelAvailable`/`IsModelInstalled` run on whichever thread dispatches the
  actor's IPC messages (no dedicated thread for those). `InstallModels` creates
  a progress token and relays the request to the main process over
  `PHWInference` (see Security, below), attaching the trusted `ContentParentId`;
  it does not decide consent itself. Only an actual recognition session (after `Init()`
  succeeds) spawns a dedicated `Parakeet` thread that receives audio, runs
  speech recognition, and sends transcription results back to the content
  process.
- `parakeet.cpp` (mudler/parakeet.cpp) streaming C-API library (via
  `LlamaRuntimeLinker`, loading it dynamically from `libmozinference`), backed
  by `libggml`, optionally using GPU acceleration, for now only macOS.
  `SpeechRecognitionParent` implements the actual speech recognition using the
  library's cache-aware streaming API.
- `HWInferenceChild`: utility→main process bridge for speech recognition model
  availability/install requests, and to get a file descriptor passed down from
  the main process.

Main Process:

- `ContentParent::RecvAcquireHWInferenceProcess` and
  `RecvReleaseHWInferenceConnection`: hold the HWInference utility process
  alive while speech recognition needs it.
- `ContentParent::RecvCreateSpeechRecognition`: receives a
  `PSpeechRecognitionParent` endpoint created in content and asks
  `UtilityProcessManager` to forward it, with the trusted `ContentParentId`,
  to the utility process.
- `UtilityProcessManager`: launches/binds the HWInference utility process and
  sends each speech-recognition parent endpoint over `PHWInference`.
- `HWInferenceParent` (main-process side of `PHWInference`): receives calls for
  model availability checks, calls to get a file descriptor down to the
  HWInference process directly via IPC, and `InstallModel` requests relayed
  from the Utility process. Every one of these carries only a task + model id;
  before doing anything else, `HWInferenceParent` resolves that id to concrete
  `model`/`revision`/`filename` coordinates via the `nsIMLModelResolver`
  registered for the task (contract id
  `@mozilla.org/ml/model-resolver;1?task=<task>`) — for speech recognition,
  `SpeechModelResolver` (`SpeechModelResolver.{h,cpp}`), which just expands the
  id against the same compiled-in model table `SpeechModelFor` reads
  (see Security, below). Before downloading, it asks that same component to
  authorize the download (`nsIMLModelResolver::authorizeDownload`, passing the
  resolved coordinates); `SpeechModelResolver` owns the consent decision and
  shows the doorhanger through
  `SpeechModelDownloadPermissionRequest.{h,cpp}` (see Security, below). Uses
  `nsIMLModelHub` to call into `ModelHub` for both the download and the
  local-cache check.
- `nsIMLModelHub`: thin XPCOM component that wraps `ModelHub`, allowing its use
  from native code.

## Security

The model-provisioning threat model — why a compromised content process cannot
name an arbitrary model artifact, and why it cannot fake consent to a download
— is generic to every `HWInference` consumer and is documented in
[Security](/toolkit/components/ml/HWInference), using speech
recognition as its worked example. Speech recognition contributes
`dom::SpeechModelFor` (a language to an opaque id, in the Utility
process, from a table generated from `models.yaml` at build time),
`SpeechModelResolver` (the id back to `ModelHub` coordinates, in the main
process, from that same table, and the consent decision for a download).

The speech-specific access control layered on top is described below.

Speech recognition shares the single `HWInference` process with every other
consumer of the facility; giving chrome-driven and content-driven inference an OS
process each is future work, see [One process, many
users](/toolkit/components/ml/HWInference).

## Access control and gating

Three independent mechanisms gate access to on-device speech recognition.
They are easy to conflate, so this is the one place that lists them all.

| Mechanism | What it controls | Who sets it | Checked in |
|---|---|---|---|
| Permissions Policy `on-device-speech-recognition` | Per-frame: cross-origin iframes are blocked unless delegated | The embedding page (`allow="on-device-speech-recognition"` / `Permissions-Policy` header); `self` by default | `available()`, `install()`, `start()` |
| AI Controls | Per-profile: the user disabling on-device AI globally or per-feature | The user, in `Settings > Firefox AI`; backed by `browser.ai.control.speechRecognition` (falls back to `browser.ai.control.default` when `"default"`) | `available()`, `install()`, `start()` |
| Model-download permission doorhanger | Per install transaction (site + requested languages): whether this download may proceed | The user, once per transaction; not persisted | `install()`, and `start()` when `media.webspeech.recognition.install_on_start` is on |

All three are checked before any IPC to the Utility process. A page that
fails one of the first two never touches the model hub or inference backend.

### Permissions Policy: `on-device-speech-recognition`

Registered in `PermissionsPolicyUtils.cpp` with a default value of `self`, like
`camera`/`microphone`. Checked via
`PermissionsPolicyUtils::IsFeatureAllowed(doc, u"on-device-speech-recognition"_ns)`.
`available()` resolves `"unavailable"` when disallowed, matching the spec's
availability algorithm, which never rejects. `install()` rejects with
`NotAllowedError`. `start()` fires `service-not-allowed` when disallowed. The
spec has `start()` treat a language pack as unavailable until `install()`
unmasks it for the caller's storage key. Gecko doesn't track that masking
state, but a document that can't call `install()` can't unmask it itself.

Without transient user activation, `start()` doesn't offer to download a
missing model, and proceeds as if
`media.webspeech.recognition.install_on_start` were off. It consumes the
activation, like `install()`.

### AI Controls

`browser.ai.control.speechRecognition` is a string pref with three states:
`"default"`, `"available"`, `"blocked"`. When it is `"default"`, the global
`browser.ai.control.default` applies instead, so blocking on-device AI
generally also blocks speech recognition without a dedicated setting ever
being touched. `IsBlockedByAIControls()` (`SpeechRecognition.cpp`) resolves
this fallback and is called from all three entry points: `available()`
(resolves `"unavailable"`), `install()` and `start()` (both reject/throw
`NotAllowedError`). Unlike the download doorhanger, this is durable and
profile-wide: once blocked, every call is rejected until the user changes it
in `about:preferences`. Surfaced in the AI Controls settings UI via
`OnDeviceModelManager.mjs`/`aiFeatures.mjs`
(`OnDeviceModelFeatures.SpeechRecognition`).

### Model-download permission doorhanger

Only relevant to `install()`, and to `start()` when
`media.webspeech.recognition.install_on_start` is on. Before downloading model
bytes, Gecko shows a
permission doorhanger (prompt type `"speech-recognition-model-download"` in
`ContentPermissionPrompt.sys.mjs` / `PermissionUI.sys.mjs`) naming the
requesting site and the download size. There is no "remember this decision"
checkbox: it is shown again the next time a download is actually needed, since
it gates the download, not the feature.

The prompt, the consent decision, and the download all run in the **parent
(main) process** (`HWInferenceParent::RecvInstallModel`, authorized by
`SpeechModelResolver`, then a `SpeechModelDownloadPermissionRequest` shown
against the requesting tab's `<browser>` element); content only asks, and the
request passes through the Utility process on the way (see "Consent to a
model download cannot be faked by content" above for why that placement is
what makes consent unspoofable).

Concurrent `install()` calls for the same window and language set are coalesced
in the content process into one `SpeechRecognitionInstallTransaction`, so only
one `InstallModels` reaches the Utility process; all the coalesced promises
settle together.

The prompt is skipped, and installation resolves `true` directly, when the
requested model is already cached (there is nothing to download, so nothing to
consent to). Tests bypass the UI with
`media.webspeech.recognition.model-download.prompt.testing` (then
`media.navigator.permission.disabled` decides allow/deny); see
`test_install_overlap.html`,
`browser_speech_recognition_model_download_prompt.js`.

`start()` never downloads: `SpeechRecognitionParent::RetrieveModel` requires the
model to already be installed and fails the session otherwise. Only `install()`
can trigger a download.

The sequence below shows the consent leg; the download leg continues from the
"Allow" branch (see "Downloading and installing a model").

```{mermaid}
sequenceDiagram
  autonumber

  box Content Process
    participant JS as Script
    participant SR as SpeechRecognition
    participant BE as SpeechRecognitionBackend
    participant SRC as SpeechRecognitionChild
  end

  box Utility Process (HWInference)
    participant SRP as SpeechRecognitionParent
    participant HWC as HWInferenceChild
  end

  box Main Process
    participant HWP as HWInferenceParent
    participant Resolver as SpeechModelResolver
    participant PR as SpeechModelDownloadPermissionRequest
    participant CPP as ContentPermissionPrompt.sys.mjs
    participant User as User
  end

  JS->>SR: SpeechRecognition.install({langs: ["en-US"]})
  Note over SR: Permissions Policy, AI Controls,<br/>transient activation checks
  SR->>BE: ::Install(langs, browsingContext)
  BE->>SRC: SendInstallModels(langs, browsingContextId)
  SRC->>SRP: PSpeechRecognition::InstallModels
  Note over SRP: map langs->id,<br/>create progress token
  SRP->>HWC: InstallModel(task, id, innerWindowId, contentId, token)
  HWC->>HWP: PHWInference::InstallModel
  HWP->>Resolver: Resolve(id) -> model, revision, filename
  Note over HWP: resolve innerWindowId to a WindowGlobalParent,<br/>check it is owned by contentId
  HWP->>Resolver: AuthorizeDownload(model, revision, filename, window, token)
  Note over Resolver: skip prompt if already installed
  Resolver->>PR: new SpeechModelDownloadPermissionRequest(principal, <browser>, sizeMB, token)
  PR->>CPP: nsContentPermissionUtils::AskPermission()
  CPP->>User: Show doorhanger (site, sizeMB)
  User->>CPP: Allow / Not now
  CPP-->>PR: Allow() / Cancel()
  PR-->>Resolver: true / false
  alt Allow
    Resolver-->>HWP: true
    Note over HWP: Download via nsIMLModelHub<br/>(see "Downloading and installing a model")
  else Not now
    Resolver-->>HWP: false
    HWP-->>HWC: false
    HWC-->>SRP: false
    SRP-->>SRC: false
    SRC-->>BE: false
    BE-->>SR: Promise resolves(false)
  end
  SR-->>JS: Promise resolves(bool)
```

## Design choices

### The `HWInference` process

Speech recognition runs its inference in `HWInference`, a utility process that
doesn't run JavaScript, has a GPU-process-like sandbox with no display server
access, and delegates model management to `ModelHub`. It is described in
[HWInference](/toolkit/components/ml/HWInference); what matters here is
that speech recognition receives audio, runs the model, and produces timed text
fragments, and that the model file arrives as a `FileDescriptor` so nothing is
copied and large models can be `mmap`ed.

### `parakeet.cpp`

`parakeet.cpp` (mudler/parakeet.cpp) is a third-party C++ library that performs
cache-aware streaming speech recognition using a Parakeet-family (RNN-T/joint)
model, via a streaming C API (`parakeet_capi.h`). It uses `libggml` underneath
for the actual computations (accelerated or not). It is a good choice because
we already vendor `libggml`, as it is the backend of `llama.cpp`, that we use
for e.g. text summarization.

In this patch set, the Metal backend (macOS) has been vendored. The Vulkan
backend (Windows, Linux, Android) will be worked on in a second stage. The CPU
backend works on all platforms.

#### The speech recognition itself

This is best explained in comments in the code, see
`SpeechRecognitionParent::ProcessAudioStreaming` in `SpeechRecognitionParent.cpp`.
The model loads from a file descriptor (`parakeet_capi_load_fd`), opens a
streaming session for the recognition language (`parakeet_capi_stream_begin_lang`,
given a locale that model knows, see Language→model mapping below, and falling
back to language auto-detection if it rejects it anyway), then is fed audio as
it arrives (`parakeet_capi_stream_feed`). The
model keeps its own encoder/decoder caches across feeds, and finalized words are
drained (`parakeet_capi_stream_drain_words`) and emitted as final results at
streaming latency.

This will have to be tuned and I have made most parameters tweakable using prefs
for this purpose.

#### The models

I have uploaded a few models to our bucket, an english-only model, and a
multilingual model. I expect that more models will be added in the future, both
with different performance characteristics, but also containing different
languages, and with different capabilities, such as token timestamping,
diarisation, punctuation correctness, etc. Consequently, the routing is
currently minimal: english goes to the english model, and the other model takes
whatever of the rest it recognizes (see Language→model mapping below).

Our bucket also contains a Voice Activity Detection (VAD) model (Silero VAD),
that can be used to detect speech activity in audio data, but I haven't wired it
yet.

### Lifetimes, thread model

#### Content process

The audio is produced by a real-time thread. It is best to do as little as
possible on it. Consequently, only downmixing to mono (that is almost free) is
done there, and the audio is immediately enqueued to a wait-free ring buffer.

A dedicated thread (called `SpeechResampler`) polls every 20ms, resamples the
audio to the model's sampling rate (constant at 16kHz), and dispatches a block
of audio to the `HWInference` process once more than 40ms is buffered. It is
started on recognition start, stopped on recognition stop or abort.

A **single** thread per content process handles the IPC from the content process
to the HWInference process. Because the `SpeechRecognition` object has both
static and instance methods, all the IPC calls run on this thread. This thread
uses a stable serial event target for the lifetime of the content process once
created; its `LazyIdleThread` releases the backing OS thread while idle.

There is no shared speech-recognition manager actor. Each static call or active
recognition session gets its own top-level `PSpeechRecognition` endpoint pair.
Content creates the pair, sends the parent endpoint to `ContentParent` over
`PContent`, and binds the child endpoint on the `SpeechIPC` thread.

What decides whether speech recognition needs that connection is
`sIPCActorUsers` (main thread only), counting `IPCActorUserGuard`s. One is
held:

- for the lifetime of every `SpeechRecognition` object, taken in its
  constructor via `AcquireProcessKeepAlive()` and dropped in
  `DisconnectFromOwner()` as well as the destructor, so a torn-down window
  releases it without waiting for GC;
- for the duration of each in-flight "transaction" -- the static
  `available()`/`install()` calls, via `RunWithTransientSession()`;
- for an active recognition session (`Start()` to `Stop()`/`Abort()`), from
  `EnsureIPC()`.

On the zero-to-one transition, content sends `AcquireHWInferenceProcess` over
`PContent`; `ContentParent` retains a `UtilityProcessKeepAlive` for the
HWInference process. This process hold is independent of the per-call
`PSpeechRecognition` actors. Endpoint creation itself is synchronous and does
not wait for the utility process: IPC queues messages until both endpoints are
bound, or rejects them if launch or binding fails.

Reaching zero does not release the process immediately. It arms a
`media.webspeech.recognition.idle_shutdown_grace_ms` (default 5s) timer,
cancelled by the next acquisition, so a `stop()`/`start()` cycle or a burst
of static calls reuses the warm process rather than paying for a relaunch. 0
releases immediately, which is what tests use. Once the timer fires, content
sends `ReleaseHWInferenceConnection` and `ContentParent` drops its keep-alive.
This grace period is a speech recognition policy and lives here, not in
`UtilityProcessManager`: other HWInference consumers may want a different one.

`PSpeechRecognition` actors can be created for two reasons:
- transient instances are created and shortly after closed for
  available/install calls. A number of those instances can be active at once,
  e.g. when available/install calls are spammed.
- long-running instances are created for speech recognition. They are kept alive
  until the user stops the recognition. A single instance can be active at once
  (to be relaxed when we allow concurrent speech recognition, after performance
  testing).

#### HWInference process

`SpeechRecognitionParent` handles most of the recognition process. It has a
dedicated thread, started during speech recognition session init, closed during
speech recognition session shutdown. It essentially loops, dequeues audio,
massages it a little bit and feeds it to `parakeet.cpp`'s streaming API.

Its lifetime is dictated by the content process, and only a single session can
be active at once in Firefox (for now, prior to performance testing, this
matches Chrome).

Parakeet objects have the same lifetime as a recognition session. The
initialization requires IPC and is highly asynchronous, to acquire the model
file, but after the init phrase, everything happens on the dedicated thread,
except appending to the SPSC ring buffer, since the audio comes from IPC.

`ActorDestroy()` must not join the dedicated thread synchronously
(`nsIThread::Shutdown()`): it runs on the main thread from inside an IPC
message dispatch, and `Shutdown()` spins a nested native event loop to wait
for the thread, which can reenter and crash. It uses `AsyncShutdown()`
instead, which only requests shutdown; the dedicated thread's own loop
observes `mShouldContinueProcessing`/`mActorDestroyed` and exits on its own,
freeing the Parakeet objects itself as the last thing it does (freeing them
from `ActorDestroy()` after only *requesting* shutdown would race with the
thread still using them). `InitializeParakeetContext()` also checks
`mActorDestroyed` before doing any work, since it can still be mid-flight
(e.g. delayed behind a model fetch) when the actor is torn down concurrently.

#### Main process

The main process is only used to create the `HWInference` process, to decide
when to tear it back down (see "Process lifetime" in the [HWInference
docs](/toolkit/components/ml/HWInference)), and to interact with
ModelHub.

`browser_speech_recognition_process_lifetime.js` covers the speech side of
that: run a session, close the tab, and the process is gone.

### Threads used

**Content process**

- `Main thread` for the implementation of the DOM api
- `MediaTrackGraph` real-time audio thread produces audio data
- Dedicated `SpeechIPC` thread to use the `PSpeechRecognition` actor
  from a stable thread, both for static calls and instance calls
- Dedicated `SpeechResampler` thread to consume audio data, resample the audio,
  and dispatch the resampled block to the `SpeechIPC` thread, which sends it
  over IPC

**HWInference process**

- Main thread receives commands and audio via IPC, produces audio into a ring buffer
- Dedicated `Parakeet` thread receives command, initialize recognition, consumes
  audio from the ring buffer, performs inference

**Parent process**

- No new threads

## Sequence diagrams

This section shows the flow of events and interactions between the different
components involved in the SpeechRecognition process. It covers a simple
scenario: calling `available()` with a language identifier, calling `install()`
with the same language identifier, then starting recognition from a
`MediaStreamTrack`.

### Checking model availability

This diagram covers shows the sequence of events that happens when calling:

```js
SpeechRecognition.available({langs: ["en-US"], processLocally: true});
```

```{mermaid}
sequenceDiagram
  autonumber

  box Content Process
    participant JS as Script
    participant SR as SpeechRecognition
    participant BE as SpeechRecognitionBackend
    participant CC as ContentChild
    participant SRC as SpeechRecognitionChild
  end

  box Main Process
    participant CP as ContentParent
    participant UPM as UtilityProcessManager
    participant HWP as HWInferenceParent
    participant NSIMLMH as nsIMLModelHub
    participant MH as ModelHub
  end

  box Utility Process (HWInference)
    participant SRP as SpeechRecognitionParent
    participant HWC as HWInferenceChild
  end

  JS->>SR: SpeechRecognition.available({langs: ["en-US"], processLocally: true})
  SR->>BE: SpeechRecognitionBackend::Available(langs)
  BE->>CC: AcquireHWInferenceProcess() (first user only)
  CC->>CP: PContent::AcquireHWInferenceProcess
  CP->>UPM: HWInferenceProcess::Content().Acquire()
  BE->>BE: Create PSpeechRecognition endpoints
  BE->>CC: CreateSpeechRecognition(parent endpoint)
  CC->>CP: PContent::CreateSpeechRecognition
  CP->>HWP: StartContentSpeechRecognition(endpoint, contentId)
  HWP->>HWC: PHWInference::NewContentSpeechRecognition(endpoint, contentId)
  HWC->>SRP: Bind parent endpoint
  BE->>SRC: Bind child endpoint on SpeechIPC
  BE->>SRC: SendIsModelInstalled(langs)
  SRC->>SRP: SendIsModelInstalled
  Note over SRP: map langs->model
  SRP->>HWC: PHWInferenceChild::IsModelInstalled(model,rev,file)
  HWC->>HWP: PHWInferenceChild::SendIsModelInstalled
  HWP->>NSIMLMH: "nsIMLModelHub.isModelInstalled(...)"
  NSIMLMH->>MH: "ModelHub.isModelInstalled(...)" (local cache only)

  MH-->>NSIMLMH: bool
  NSIMLMH-->>HWP: bool
  HWP-->>HWC: bool
  SRP-->>SRC: bool
  SRC-->>BE: bool

  alt installed
    BE->>SRC: `Close()`
    BE-->>SR: Promise resolves: available
  else not installed
    BE->>SRC: SendIsModelAvailable(langs)
    SRC->>SRP: SendIsModelAvailable
    SRP->>HWC: PHWInferenceChild::IsModelAvailable(model,rev,file)
    HWC->>HWP: PHWInferenceChild::SendIsModelAvailable
    HWP->>NSIMLMH: "nsIMLModelHub.isModelAvailable(...)"
    NSIMLMH->>MH: "ModelHub.isModelAvailable(...)" (cache, else network HEAD)

    MH-->>NSIMLMH: bool
    NSIMLMH-->>HWP: bool
    HWP-->>HWC: bool
    SRP-->>SRC: bool
    SRC-->>BE: bool
    BE->>SRC: `Close()`
    alt available
      BE-->>SR: Promise resolves: downloadable
    else not available
      BE-->>SR: Promise resolves: unavailable
    end
  end
  SR-->>JS: Promise resolves
```

### Downloading and installing a model

This is the full, end-to-end flow of what happens when running:

```js
SpeechRecognition.install({langs: ["en-US"]});
```

from the content-process call all the way down to `ModelHub` and back. It
folds in the consent leg from the previous diagram (compressed to the
doorhanger's "Allow" outcome; see that diagram for the "Not now" branch and
the already-installed short-circuit) so this one is self-contained,
rather than picking up mid-flight in the main process.

```{mermaid}
sequenceDiagram
  autonumber

  box Content Process
    participant JS as Script
    participant SR as SpeechRecognition
    participant BE as SpeechRecognitionBackend
    participant SRC as SpeechRecognitionChild
  end

  box Utility Process (HWInference)
    participant SRP as SpeechRecognitionParent
    participant HWC as HWInferenceChild
  end

  box Main Process
    participant HWP as HWInferenceParent
    participant Resolver as SpeechModelResolver
    participant PR as SpeechModelDownloadPermissionRequest
    participant CPP as ContentPermissionPrompt.sys.mjs
    participant User as User
    participant NSIMLMH as nsIMLModelHub
    participant MH as ModelHub
  end

  JS->>SR: SpeechRecognition.install({langs: ["en-US"]})
  Note over SR: Permissions Policy, AI Controls,<br/>transient activation checks
  SR->>BE: ::Install(langs, browsingContext)
  BE->>SRC: SendInstallModels(langs, browsingContextId)
  SRC->>SRP: PSpeechRecognition::InstallModels
  Note over SRP: map langs->id,<br/>create progress token
  SRP->>HWC: InstallModel(task, id, innerWindowId, contentId, token)
  HWC->>HWP: PHWInference::InstallModel
  HWP->>Resolver: Resolve(id) -> model, revision, filename
  Resolver-->>HWP: model, revision, filename
  Note over HWP: resolve innerWindowId to a WindowGlobalParent,<br/>check it is owned by contentId
  HWP->>Resolver: AuthorizeDownload(model, revision, filename, window, token)
  Note over Resolver: skip prompt if already installed<br/>(see doorhanger diagram)
  Resolver->>PR: new SpeechModelDownloadPermissionRequest(principal, <browser>, sizeMB, token)
  PR->>CPP: nsContentPermissionUtils::AskPermission()
  CPP->>User: Show doorhanger (site, sizeMB)
  User->>CPP: Allow
  CPP-->>PR: Allow()
  PR-->>Resolver: true
  Resolver-->>HWP: true
  HWP->>NSIMLMH: downloadModel(...)
  NSIMLMH->>MH: getModelDataAsFile(...)
  activate MH
  MH--)NSIMLMH: progress callback
  NSIMLMH--)HWP: progress callback
  Note over HWP: progress notification drives the prompt's progress UI
  MH--)NSIMLMH: Download complete
  deactivate MH
  NSIMLMH-->>HWP: download success/fail
  HWP-->>HWC: bool
  HWC-->>SRP: bool
  SRP-->>SRC: bool
  SRC-->>BE: bool
  BE-->>SR: bool
  SR-->>JS: Promise resolves(bool)
```

### Starting recognition and processing audio

This is what happens after running `start(...)` on a `SpeechRecognition`
instance that is processing locally, passing it a `MediaStreamTrack`. Again, the
initial process creation isn't repeted and is similar to the first diagram.

There are three loops running in parallel at with different interval, in
different process and with different thread priorities in this diagram:

```{mermaid}
sequenceDiagram
  autonumber

  box Content Process
    participant JS as Script
    participant SR as SpeechRecognition
    participant MTG as MediaTrackGraph
    participant BE as SpeechRecognitionBackend
    participant CC as ContentChild
    participant SRC as SpeechRecognitionChild
  end

  box Utility Process (HWInference)
    participant SRP as SpeechRecognitionParent
    participant HWC as HWInferenceChild
    participant WLIB as parakeet.cpp
  end

  box Main Process
    participant CP as ContentParent
    participant UPM as UtilityProcessManager
    participant HWP as HWInferenceParent
    participant MH as ModelHub
  end

  JS->>SR: "start([track])"
  SR->>SR: "Validate track or getUserMedia"
  SR->>BE: "new Backend, Start()"
  BE->>BE: "Create PSpeechRecognition endpoints"
  BE->>CC: "CreateSpeechRecognition(parent endpoint)"
  CC->>CP: "PContent::CreateSpeechRecognition"
  CP->>HWP: "StartContentSpeechRecognition(endpoint, contentId)"
  HWP->>HWC: "PHWInference::NewContentSpeechRecognition"
  HWC->>SRP: "Bind parent endpoint with trusted contentId"
  BE->>SRC: "Bind child endpoint on SpeechIPC"
  BE->>SRC: "SendInit(lang, phrases)"
  SRC->>SRP: SendInit
  SRP->>HWC: PHWInference::GetModelFile
  HWC->>HWP: RecvGetModelFile
  HWP->>MH: getModelFileAsBlob(...)
  MH-->>HWP: Blob
  HWP-->>HWC: FileDescriptor
  HWC-->>SRP: FileDescriptor
  SRP->>SRP: `FileDescriptor` to `FILE*`
  SRP->>WLIB: `parakeet_capi_load_fd(fileno(FILE*))`
  activate WLIB
  WLIB->>WLIB: `fread`, compile shaders, etc.
  WLIB-->>SRP: ctx
  deactivate WLIB
  SRP->>WLIB: `parakeet_capi_stream_begin_lang(ctx, lang)`
  WLIB-->>SRP: stream
  SRP-->>SRC: Init resolved true
  SRC-->>BE: Init resolved true
  BE->>BE: Start resampling thread
  SRP->>SRP: Start Parakeet thread
  loop Audio capture loop, real-time thread, every ~3 to 20ms
    MTG->>MTG: SpeechTrackListener::NotifyQueuedChanges
    MTG->>BE: SpeechRecognitionBackend::DataCallback
    BE->>BE: Downmix, enqueue frames on real-time thread
  end
  loop Speech resampling loop, SpeechResampler thread, polls every 20ms
    BE->>BE: Resample to 16kHz once >40ms buffered
    BE->>SRC: SendAudioDataViaIPC(16kHz f32)
    SRC->>SRP: SendProcessAudioData(16Khz f32)
    SRP->>SRP: Enqueue
  end

  loop Parakeet streaming loop, dequeues as audio arrives
    SRP->>SRP: Dequeue
    SRP->>WLIB: `parakeet_capi_stream_feed(stream, chunk)`
    WLIB-->>SRP: committed text delta, EOU/EOB bitmask
    SRP->>WLIB: `parakeet_capi_stream_drain_words(stream)`
    WLIB-->>SRP: finalized words + timing/confidence
    SRP-->>SRC: OnRecognitionResult(text, final)
    SRC-->>BE: Result callback
    BE-->>SR: Dispatch result event
    Note over JS: recognized text fragments received by script
    SR-->>JS: SpeechRecognitionResult
  end
```

## Open Issues / Not Quite Done / Limitations

### Spec

The Web Speech specification is still ambiguous in areas that matter for this
implementation, especially availability/install semantics, lifecycle ordering
around `abort()`/`stop()`/restart, and event timing. The implementation follows
the current interoperable behavior where practical and keeps remaining
mismatches localized.

Lifecycle behavior has automated coverage for async end ordering,
start-after-error, abort, and session cleanup. DOM event timestamps (`start`,
`audiostart`, `result`, etc.) are now surfaced: `PSpeechRecognition` sends a
`TimeStamp` alongside results, and `SpeechRecognitionParent` sets it from
`TimeStamp::Now()` at emission time. Per-word/per-token timing remains
engine-internal and is not exposed on the DOM event, since the Web Speech result
has no per-word timing field.

### Testing

Testing is automated through mochitests, gtests, and WPT expectation updates.
Current coverage includes availability/install flows, the download permission
prompt, lifecycle and fuzz/interleaving tests, aborted-session cleanup, result
confidence and event timing, real Parakeet e2e coverage, multilingual
recognition, and follow-up phrase-boost coverage.

Remaining gaps are additional upstream WPT automation and broader real-model
scenario coverage.

Running the mochitests locally (headless, Linux):

```
./mach mochitest --headless dom/media/webspeech/recognition/test/
```

- Headless `AudioContext`s stay suspended without a running audio server:
  PipeWire + pipewire-pulse + wireplumber must be running with
  `XDG_RUNTIME_DIR` set (already the case in a normal desktop session; only
  needs starting manually in a bare CI-like environment).
- Tests tagged `parakeet-asr` in `mochitest.toml` need the real Parakeet model.
  `testing/mochitest/runtests.py` auto-starts `testing/tools/serve_model.py`
  (a local stand-in model hub on port 8766) for the run whenever such a test
  is active; nothing needs to be started by hand for a normal
  `./mach mochitest` invocation on this directory.
- `browser.ml.modelHub.testing` mocks `IsModelAvailable` in
  `HWInferenceParent` and the parent-side download + already-installed check
  (`SpeechModelDownloadPermissionRequest.cpp`), plus model retrieval in
  `SpeechRecognitionParent::RecvInit`, so start()-heavy tests (fuzzing, session
  lifecycle) never need a real model file. It does **not** mock `GetModelFile`
  itself: there's no lightweight stand-in for an actual
  parseable model, so a test wanting to exercise real recognition still needs
  the local model server above.
- `MOZ_LOG=SpeechRecognitionParent:5,SpeechRecognitionBackend:5,SpeechRecognition:5`
  is the fastest way to see the IPC/session lifecycle across all three
  layers when a test misbehaves.

### `"speechstart"`/`"speechend"` events

Content-side callbacks are wired (`SpeechRecognitionChild::RecvOnSpeechChange` →
backend → DOM), but `SpeechRecognitionParent` never calls
`SendOnSpeechChange(...)`. One remaining task is to add some code to use a
minuscule VAD (Voice Activity Detection) model and get timing of speech
start/end.

### Global concurrency limit scope

The system currently only supports a single active session via static
`sActiveSession` across the entire `HWInference` process. Chrome does the same.
We will be able to relax this when we understand better the performance story
when there is no hardware acceleration. Having a bunch of recognition sessions
running concurrently is fine when there is hardware acceleration, granted the
same model is used for all session (or there is otherwise enough memory
available).

### Error handline / propagation

`HandleRecognitionErrorFromBackend` maps only `"concurrent-session"` to
`service-not-allowed`, defaulting others to `network`. This will be expanded
and clarified.

### Language→model mapping

`dom/media/webspeech/recognition/models.yaml` is an ordered list of models, each
declaring the locales it recognizes, spelled the way that model expects them
(for the multilingual model, its prompt dictionary keys). A request picks the
first entry that recognizes it, negotiated with
`LocaleService::NegotiateLanguages`, so `es-MX` is recognized as the model's
generic `es`; there is no catch-all, and a tag no entry recognizes is simply not
supported. `media.webspeech.recognition.model.<language subtag>` restricts the
choice to the model it names. A request with no language at all takes the first
entry and passes no locale, leaving the engine to its model file's own default.
I plan to add more models (much smaller, more specialized with different
variants, etc.) prior to landing.

`SpeechModelFor` in `SpeechRecognitionModelMapping.{h,cpp}` answers both, over
a table generated from the yaml: the model id and the locale, returned together
as a match, or no match when the language is unsupported. The Utility process
calls it for `IsModelAvailable`/`IsModelInstalled`/`InstallModels`, so a language
becomes an id before it crosses to the main process; the main process works only
from the coordinates it receives, using `SpeechModelSizeMB` for the prompt's
download size. A session is started with the negotiated locale rather than the
tag content asked for.

`available()` reports `unavailable` and `install()` resolves false for an
unrecognized language, without launching the HWInference process or downloading
anything, and `start()` fires `service-not-allowed`.

### Phrase boost

Phrase boost is implemented in a follow-up patch. The base architecture passes
`SpeechRecognitionPhraseIPC` entries over `PSpeechRecognition::Init`; the
follow-up wires those hints into `parakeet.cpp` and adds dedicated coverage.

### Hardware acceleration on non-macOS

Still needs to be done, CPU-based inference works well though.
