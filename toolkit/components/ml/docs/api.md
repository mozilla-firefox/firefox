# APIs

## Running the pipeline API

You can use the Transformer.js `pipeline` API directly to perform inference, as long
as the model is in our model hub.

The [Transformers.js documentation](https://huggingface.co/tasks) provides a lot
of examples that you can slightly adapt when running in Firefox.

In the example below, a text summarization task is performed using the `summarization` task:

```javascript
const { createEngine } = ChromeUtils.importESModule("chrome://global/content/ml/EngineProcess.sys.mjs");
const options = {
  taskName: "summarization",
  modelId: "mozilla/text_summarization",
  modelRevision: "main"
};

const engine = await createEngine(options);

const text = 'The tower is 324 metres (1,063 ft) tall, about the same height as an 81-storey building, ' +
'and the tallest structure in Paris. Its base is square, measuring 125 metres (410 ft) on each side. ' +
'During its construction, the Eiffel Tower surpassed the Washington Monument to become the tallest ' +
'man-made structure in the world, a title it held for 41 years until the Chrysler Building in New ' +
'York City was finished in 1930. It was the first structure to reach a height of 300 metres. Due to ' +
'the addition of a broadcasting aerial at the top of the tower in 1957, it is now taller than the ' +
'Chrysler Building by 5.2 metres (17 ft). Excluding transmitters, the Eiffel Tower is the second ' +
'tallest free-standing structure in France after the Millau Viaduct.';

const request = { args:  [text], options: { max_new_tokens: 100 } };
const res = await engine.run(request);
console.log(res[0]["summary_text"]);
```

The code sample above executes the LLM and returns the complete output after the computation is finished.
Alternatively, you can receive the output incrementally by using the asynchronous generator method
`runWithGenerator` provided by the engine.

```javascript
let summaryText = "";
for await (const chunk of engine.runWithGenerator(request)){
   summaryText += chunk.text;
}
```

You can use the browser console or toolbox to run this example.
To enable the browser console, flip the following option in `about:config`: **devtools.chrome.enabled**.
To get access to the full toolbox, set the **devtools.debugger.remote-enabled** option.
We recommend using the toolbox to get access to more tools. You will get a security warning
when starting it, when the toolbox connects to the browser.

When running this code, Firefox will look for models in the Mozilla model hub located at <https://model-hub.mozilla.org>
which contains a curated list of models.

## Available Options

Options passed to the `createEngine` function are verified and converted into a `PipelineOptions` object.

Below are the options available:

- **taskName**: The name of the task the pipeline is configured for.
- **featureId**: The identifier for the feature to be used by the pipeline.
- **engineId**: The identifier for the engine to be used by the pipeline.
- **timeoutMS**: The maximum amount of time in milliseconds the worker will run (-1 to never expire).
- **modelHub**: The model hub to use, can be `huggingface` or `mozilla`. When used, `modelHubRootUrl` and `modelHubUrlTemplate` are ignored.
- **modelHubRootUrl**: The root URL of the model hub where models are hosted.
- **modelHubUrlTemplate**: A template URL for building the full URL for the model.
- **modelId**: The identifier for the specific model to be used by the pipeline.
- **modelRevision**: The revision for the specific model to be used by the pipeline.
- **tokenizerId**: The identifier for the tokenizer associated with the model, used for pre-processing inputs.
- **tokenizerRevision**: The revision for the tokenizer associated with the model, used for pre-processing inputs.
- **processorId**: The identifier for any processor required by the model, used for additional input processing.
- **processorRevision**: The revision for any processor required by the model, used for additional input processing.
- **logLevel**: The log level used in the worker
- **runtimeFilename**: Name of the runtime wasm file.
- **dtype**: quantization level, can be `fp32`, `fp16`, `fp8_e5m2`, `fp8_e4m3`, `q8`, `int8`, `uint8`, `q4`, `bnb4`, `q4f16`. Defaults to `q8`
- **device**: device to use (`wasm`, `gpu`, or `cpu`). Defaults to `wasm`
- **executionPriority**: priority for the engine, can be `HIGH` (critical to Firefox), `NORMAL` (default), or `LOW` (third-party calls).
- **kvCacheDtype**: quantization level for the llama.cpp backend. It only applies to the llama.cpp backend when flash attention is enabled. Otherwise, it is always `f32`.
- **numContext**: Maximum context size. Applies only to the llama.cpp backend.
- **numBatch**: Number of tokens processed in a single forward pass. Applies only to the llama.cpp backend.
- **numUbatch**: Token batch size. Applies only to the llama.cpp backend.
- **flashAttn**: Whether to use flash attention. Applies only to the llama.cpp backend.
- **useMmap**: Whether to use memory mapped for the model. Applies only to the llama.cpp backend.
- **useMlock**: Whether to lock in memory the full model. Applies only to the llama.cpp backend.
- **numThreadsDecoding**: Number of threads used during decoding. Applies only to the llama.cpp backend.
- **modelFile**: The name of model file. Currently, only supported for the llama.cpp backend.
- **backend**: The backend to use, can be `onnx`, `onnx-native`, `llama.cpp`, `openai`, or `static-embeddings`. Defaults to `onnx`.

**taskName** and **modelId** are required, the others are optional and will be filled automatically
using values pulled from Remote Settings when the task id is recognized.

To learn about the different inference tasks, refer to this Hugging Face
documentation: [Tasks](https://huggingface.co/tasks)

**featureId** is used to uniquely identify the feature that will be used by the pipeline
and store corresponding options in Remote Settings -- {ref}`See the ml-inference-options collection <inference-remote-settings>`.

**engineId** is used to manage the lifecycle of the engine. When not provided, it defaults to
`default-engine`. Everytime a new engine is created using `createEngine`, the API will ensure that
there's a single engine with the given id. If the options of the existing engine are not different,
the instance is reused. If they differ, the engine is reinitialized with the new options.
This ensures we don't have too many engines running at once since it takes a lot of resources.
To make sure your engine is not destroyed or reused elsewhere, set that value with a unique id
that matches your component.

When an engine is created, an inference process is created if it's not already there, and
a new worker is launched for that engine. The inference process is unique and shared by all engines.

Some values are also set from the preferences (set in `about:config`):

- **browser.ml.logLevel**: Set to "All" to see all logs, which are useful for debugging.
- **browser.ml.modelHubRootUrl**: Model hub root URL used to download models
- **browser.ml.modelHubUrlTemplate**: Model URL template
- **browser.ml.modelCacheTimeout**: Worker timeout in ms. Default value used for **timeoutMS**
- **browser.ml.modelCacheMaxSize**: Maximum disk size for ML model cache (in GiB)
- **browser.ml.checkForMemory**: Whether to check available system memory before creating an engine. Defaults to true.
- **browser.ml.minimumPhysicalMemory**: Minimum physical memory (in bytes) required to run an engine.
- **browser.ml.overridePipelineOptions**: A JSON string that overrides pipeline options. Intended for developer use only.

## URL allow and deny list

We keep a Remote Settings collection called ``ml-model-allow-deny-list``
([dashboard](https://firefox-ai.github.io/runtime-tools/ml-model-allow-deny-list/),
[searchfox](https://searchfox.org/firefox-main/search?q=%22ml-model-allow-deny-list%22))
that contains URL prefixes
that are allowed or denied.

Each record comes with the following fields:

- urlPrefix: The URL prefix to allow or deny
- filter: Set to `ALLOW` to allow, `DENY` to deny
- description: an optional description

When the API is about to fetch a file, its URL is controlled in the allow/deny list.

Examples of patterns:

- ALL models ALL VERSIONS from the mozilla organization on hugging face : <https://huggingface.co/Mozilla/>
- ALL models ALL VERSIONS from our hub: <https://model-hub.mozilla.org/>
- A specific model ALL VERSIONS <https://huggingface.co/typeform/distilbert-base-uncased-mnli/>
- A specific model and a specific version <https://huggingface.co/Mozilla/distilvit/blob/v0.5.0/>

Each URL is tested and needs to be included in the allowlist and not in the denylist

To bypass this check and allow Firefox to download any file for runnings models,
you need to use the `MOZ_ALLOW_EXTERNAL_ML_HUB` environment variable.

If you want to add a new hub, organization or a specific model, ask us by
[opening a ticket](https://bugzilla.mozilla.org/enter_bug.cgi?product=Core&component=Machine%20Learning).

## Using the Hugging Face model hub

By default, the engine will use the Mozilla model hub. You will need to pass `huggingface` as `modelHub`.

The inference engine will then look for models in the Hugging Face model hub. If the URL is
not allowed (see previous section) and you still want to experiment with the model,
use `MOZ_ALLOW_EXTERNAL_ML_HUB`.

To run against a Hugging Face model, visit [this page](https://huggingface.co/models?library=transformers.js) and select on
the top left corner `tasks`. You can pick a task and then choose a model.

For example, models for the `summarization` tasks compatible with our inference engine are listed [here](https://huggingface.co/models?pipeline_tag=summarization&library=transformers.js&sort=trending).

Let's say you want to pick the `Xenova/distilbart-cnn-6-6` model. All you have to do is use the id when calling our
`createEngine` pipeline:

```javascript
const { createEngine } = ChromeUtils.importESModule("chrome://global/content/ml/EngineProcess.sys.mjs");

const options = {
  taskName: "summarization",
  modelId: "Xenova/distilbart-cnn-6-6",
  modelHub: "huggingface"
};

const engine = await createEngine(options);

const text = 'The tower is 324 metres (1,063 ft) tall, about the same height as an 81-storey building, ' +
'and the tallest structure in Paris. Its base is square, measuring 125 metres (410 ft) on each side. ' +
'During its construction, the Eiffel Tower surpassed the Washington Monument to become the tallest ' +
'man-made structure in the world, a title it held for 41 years until the Chrysler Building in New ' +
'York City was finished in 1930. It was the first structure to reach a height of 300 metres. Due to ' +
'the addition of a broadcasting aerial at the top of the tower in 1957, it is now taller than the ' +
'Chrysler Building by 5.2 metres (17 ft). Excluding transmitters, the Eiffel Tower is the second ' +
'tallest free-standing structure in France after the Millau Viaduct.';

const request = { args:  [text], options: { max_new_tokens: 100 } };
const res = await engine.run(request);
console.log(res[0]["summary_text"]);
```

## Running the internal APIs

Some inference tasks are doing more complex operations within the engine, such as image processing.
For these tasks, you can use the internal APIs to run the inference. Those tasks are prefixed with `moz`.

In the example below, an image is converted to text using the `moz-image-to-text` task.

```javascript
const { createEngine } = ChromeUtils.importESModule("chrome://global/content/ml/EngineProcess.sys.mjs");

// options needed for the task
const options = {taskName: "moz-image-to-text" };

// We create the engine object, using the options
const engine = await createEngine(options);

// Preparing a request
const request = {url: "https://huggingface.co/datasets/mishig/sample_images/resolve/main/football-match.jpg"};

// At this point we are ready to do some inference.
const res = await engine.run(request);
// The result is a string containing the text extracted from the image
console.log(res);
```

The following internal tasks are supported by the machine learning engine:

```{eval-rst}
.. js:autofunction:: imageToText
```
