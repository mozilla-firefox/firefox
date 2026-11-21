# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

about-inference-title = Inference Manager
about-inference-header = ✨ Inference Manager
about-inference-description = This page gathers all information about local inference
about-inference-ml-disabled =
  .message = To use this page, enable local inference.
about-inference-ml-disabled-enable = Enable
about-inference-simd-supported = SIMD supported
about-inference-onnx-runtime-file = ONNX Runtime file
about-inference-downloads-description = You can delete models here to free up disk space. They will be downloaded again on first inference.
about-inference-delete-button = 🗑️
  .title = Delete model
about-inference-processes-title = Running Inference Processes
about-inference-models-title = Models downloaded
about-inference-yes = Yes
about-inference-no = No
about-inference-total = Total
about-inference-no-processes = No inference processes are running.
about-inference-memory = Memory
about-inference-pid = PID
about-inference-file = File
about-inference-size = Size
about-inference-learn-more = Learn more about the platform here.
about-inference-category-local-inference = Local Inference
about-inference-category-models-storage = Models Storage
about-inference-last-used = Last Used
about-inference-last-updated = Last Updated
about-inference-category-benchmark = Benchmark
about-inference-benchmark-title = Running Benchmark
about-inference-benchmark-description = Performs a full benchmark on the local inference platform. This will take a while.
about-inference-benchmark-button =
  .value = Run Benchmark
about-inference-prompt-title = Confirm
about-inference-prompt-message = Are you sure you want to delete this model?

about-inference-tab-inference-pad = Inference pad
about-inference-tab-text-generation = Text generation
about-inference-tab-devices = HWInference devices

## HWInference devices panel

about-inference-devices = HWInference devices
about-inference-devices-refresh = List devices
about-inference-devices-col-id = Id
about-inference-devices-col-description = Description
about-inference-devices-col-kind = Kind
about-inference-devices-col-backends = Backends

## Text generation panel

about-inference-text-generation = Text generation
about-inference-text-generation-hub = Model hub:
about-inference-text-generation-hub-default = Configured default
about-inference-text-generation-model = Model:
about-inference-text-generation-revision = Revision:
about-inference-text-generation-device = Device:
about-inference-text-generation-threads = Threads:
about-inference-text-generation-flash-attn = Flash attention:
about-inference-text-generation-kv-cache-dtype = KV cache type:
about-inference-text-generation-engines = Engines:
about-inference-text-generation-runs = Runs per engine:
about-inference-text-generation-max-tokens = Max tokens:
about-inference-text-generation-prompt = Prompt:
about-inference-text-generation-run = Generate
about-inference-text-generation-output = Output
about-inference-text-generation-device-cpu = CPU
about-inference-text-generation-device-gpu = GPU, CPU fallback
about-inference-text-generation-col-engine = Engine
about-inference-text-generation-col-run = Run
about-inference-text-generation-col-metric = Metric
about-inference-text-generation-col-min = Min
about-inference-text-generation-col-median = Median
about-inference-text-generation-col-max = Max
about-inference-text-generation-col-init = Engine init (ms)
about-inference-text-generation-col-total = Total (ms)
about-inference-text-generation-col-first-chunk = First chunk (ms)
about-inference-text-generation-col-prompt-tokens = Prompt tokens
about-inference-text-generation-col-prefill = Prefill (ms)
about-inference-text-generation-col-output-tokens = Output tokens
about-inference-text-generation-col-decode = Decode (ms)
about-inference-text-generation-col-tokens-per-second = Tokens/s

## Memories on-device experiment

about-inference-memories-title = { -smart-window-brand-name } Memories On-Device
about-inference-memories-description = Run generate, merge and filter with sample data. Your browsing history and saved memories are not used.
about-inference-memories-run-button =
    .label = Run
about-inference-memories-cancel-button =
    .label = Cancel
about-inference-memories-cleanup-button =
    .label = Clean up
    .title = Remove downloaded model and clear results
about-inference-memories-idle = Ready
about-inference-memories-downloading = Downloading model…
about-inference-memories-loading = Loading model…
about-inference-memories-running-generate = Generating memories…
about-inference-memories-running-merge = Merging memories…
about-inference-memories-running-filter = Filtering memories…
# Variables:
#   $minutes (Number) - Estimated minutes left in the download, rounded up.
#   $speed (Number) - Average download speed in mebibytes per second.
about-inference-memories-time-left =
    { $minutes ->
        [one] About a minute left at { NUMBER($speed, maximumFractionDigits: 0) } MiB/s
       *[other] About { $minutes } minutes left at { NUMBER($speed, maximumFractionDigits: 0) } MiB/s
    }
about-inference-memories-cancelling = Cancelling…
about-inference-memories-cleaning = Removing the downloaded model…
about-inference-memories-done = Complete
about-inference-memories-cancelled = Cancelled
about-inference-memories-timed-out = Five-minute limit reached
about-inference-memories-failed = Failed
about-inference-memories-setup = Setup
about-inference-memories-model = Model
about-inference-memories-accelerator = Accelerator
# Variables:
#   $name (String) - Name of the device that loaded the model.
#   $kind (String) - Kind of device, like GPU or CPU.
about-inference-memories-accelerator-value = { $name } ({ $kind })
about-inference-memories-processor = Processor
# Variables:
#   $name (String) - Processor name.
#   $cores (Number) - Number of processor cores.
about-inference-memories-processor-value =
    { $cores ->
        [0] { $name }
        [one] { $name }, { $cores } core
       *[other] { $name }, { $cores } cores
    }
about-inference-memories-system-memory = System memory
about-inference-memories-summary = Summary
about-inference-memories-download = Download
# Variables:
#   $size (String) - Size of the model file, like 2.6 GB.
about-inference-memories-cached = { $size }, cached
# Variables:
#   $size (String) - Size of the model file, like 2.6 GB.
#   $percent (Number) - Percentage downloaded so far.
about-inference-memories-download-progress = { $size } ({ $percent }%)
# Variables:
#   $size (String) - Size of the model file, like 2.6 GB.
#   $duration (String) - Download duration, like 4m 6s.
#   $speed (Number) - Download speed in mebibytes per second.
about-inference-memories-downloaded = { $size } in { $duration }, { NUMBER($speed, maximumFractionDigits: 0) } MiB/s
about-inference-memories-load = Load time
about-inference-memories-memory = Memory
about-inference-memories-prefill-speed = Prefill speed
about-inference-memories-decode-speed = Decode speed
# Variables:
#   $rate (Number) - Tokens processed or generated per second.
about-inference-memories-token-rate = { NUMBER($rate, maximumFractionDigits: 0) } tokens/s
about-inference-memories-results = Completions on-device
about-inference-memories-generate = Generate
about-inference-memories-merge = Merge
about-inference-memories-filter = Filter
about-inference-memories-col-task = Task
about-inference-memories-col-time = Inference time
# Variables:
#   $seconds (Number) - Duration in seconds.
about-inference-memories-duration = { NUMBER($seconds, minimumFractionDigits: 1, maximumFractionDigits: 1) } s
