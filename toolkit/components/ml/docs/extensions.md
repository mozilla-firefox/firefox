# WebExtensions AI API

:::{note}
The extension developer is responsible to comply with [Mozilla's add-on policies](https://extensionworkshop.com/documentation/publish/add-on-policies/)
as well as regulatory rules when
providing AI features, such as the [EU AI Act](<https://www.europarl.europa.eu/thinktank/en/document/EPRS_BRI(2021)698792>).
:::

The Firefox AI Platform API can be used from web extensions via a trial API we've added in 134. This API
is enabled by default in Nightly. For Beta and Release, toggle the following flags in `about:config`:

- `browser.ml.enable` → true
- `extensions.ml.enabled` → true

WebExtensions that use the `trialML` optional permission will be able to use the API.

The permission is added to your manifest.json file as follows:

```json
{
    "optional_permissions": ["trialML"],
}
```

The WebExtensions inference API wraps the Firefox AI API and comes in four endpoints under
the `browser.trial.ml` namespace:

- **createEngine**: creates an inference engine.
- **runEngine**: runs an inference engine.
- **onProgress**: listener for engine events
- **deleteCachedModels**: delete model(s) files

Below is a full example of using the engine to summarize a content:

```javascript
// 1. Initialize the event listener
browser.trial.ml.onProgress.addListener(progressData => {
  console.log(progressData);
});

// 2. Create the engine, may trigger downloads.
await browser.trial.ml.createEngine({
  modelHub: "huggingface",
  taskName: "summarization",
});

// 3. Call the engine
const text = 'The tower is 324 metres (1,063 ft) tall, about the same height as an 81-storey building, ' +
'and the tallest structure in Paris. Its base is square, measuring 125 metres (410 ft) on each side. ' +
'During its construction, the Eiffel Tower surpassed the Washington Monument to become the tallest ' +
'man-made structure in the world, a title it held for 41 years until the Chrysler Building in New ' +
'York City was finished in 1930. It was the first structure to reach a height of 300 metres. Due to ' +
'the addition of a broadcasting aerial at the top of the tower in 1957, it is now taller than the ' +
'Chrysler Building by 5.2 metres (17 ft). Excluding transmitters, the Eiffel Tower is the second ' +
'tallest free-standing structure in France after the Millau Viaduct.';

const res = await browser.trial.ml.runEngine({
  args: [text],
});

// 4. Get the results.
console.log(res[0]["summary_text"]);

// 5. Delete the downloaded model files
await browser.trial.ml.deleteCachedModels();
```

The `createEngine` call will trigger downloads in case the model files are not already cached in OPFS.
This means that the first call to `createEngine` may last for a while, which need to be taken
into account when building the web extension. Subsequent calls will be much faster.

## Engine arguments

When calling that API, the object you pass to it can contain the following arguments (a subset of the arguments of the platform API):

- **taskName**: The name of the task the pipeline is configured for. MANDATORY
- **modelHub**: The model hub to use, can be huggingface or mozilla. When used, modelHubRootUrl and modelHubUrlTemplate are ignored.
- **modelId**: The identifier for the specific model to be used by the pipeline.
- **modelRevision**: The revision for the specific model to be used by the pipeline.
- **tokenizerId**: The identifier for the tokenizer associated with the model, used for pre-processing inputs.
- **tokenizerRevision**: The revision for the tokenizer associated with the model, used for pre-processing inputs.
- **processorId**: The identifier for any processor required by the model, used for additional input processing.
- **processorRevision**: The revision for any processor required by the model, used for additional input processing.
- **dtype**: quantization level
- **device**: device to use (wasm or gpu)

Besides `taskName`, all other arguments are optional, and the API will pick sane defaults.

Notice that model files can be very large, and it’s recommended to use quantized versions to reduce the size of the downloads.

We also have not activated all tasks for this first version because we have not yet implemented a streaming API for
the inference tasks, making it impractical to run tasks that run on audio, video or large amounts of data.

## Default models

Below is a list of supported tasks and their default models that will be picked if you don't provide
one.

- **text-classification**: Xenova/distilbert-base-uncased-finetuned-sst-2-english
- **token-classification**: Xenova/bert-base-multilingual-cased-ner-hrl
- **question-answering**: Xenova/distilbert-base-cased-distilled-squad
- **fill-mask**: Xenova/bert-base-uncased
- **summarization**: Xenova/distilbart-cnn-6-6
- **translation**: Xenova/t5-small
- **text2text-generation**: Xenova/flan-t5-small
- **text-generation**: onnx-community/gpt2-ONNX
- **zero-shot-classification**: Xenova/distilbert-base-uncased-mnli
- **image-to-text**: Mozilla/distilvit
- **image-classification**: Xenova/vit-base-patch16-224
- **image-segmentation**: Xenova/detr-resnet-50-panoptic
- **zero-shot-image-classification**: Xenova/clip-vit-base-patch32
- **object-detection**: Xenova/detr-resnet-50
- **zero-shot-object-detection**: Xenova/owlvit-base-patch32
- **document-question-answering**: Xenova/donut-base-finetuned-docvqa
- **image-to-image**: Xenova/swin2SR-classical-sr-x2-64
- **depth-estimation**: Xenova/dpt-large
- **feature-extraction**: Xenova/all-MiniLM-L6-v2
- **image-feature-extraction**: Xenova/vit-base-patch16-224-in21k
- **text-to-speech**: Xenova/speecht5_tts

Any model in Hugging Face that is compatible with Transformers.js should work.
You can browse them using [this link](https://huggingface.co/models?library=transformers.js&sort=trending).

Once the engine is created, the `runEngine` API will execute. To know what arguments to pass to args
and options, you can refer to the [Transformers.js documentation](https://huggingface.co/docs/transformers.js/index#tasks).

In practice, `args` is the first argument passed to the Transformers.js pipeline API, and `options` the second.

So the example below:

```javascript
const gen = await pipeline('summarization', 'Xenova/distilbart-cnn-6-6');
const output = await gen(text, {max_new_tokens: 100});
```

Becomes:

```javascript
await browser.trial.ml.createEngine({
  modelHub: "huggingface",
  taskName: "summarization",
  modelId: "Xenova/distilbart-cnn-6-6"
});

const output = await browser.trial.ml.runEngine({
  args: [text],
});
```

## Limitations

This trial API comes with a few limitations.

Beside restricting a few tasks, Firefox will not authorize web extensions to download any model that is not
in our model hub, or in the organizations that are allowed in Hugging Face.

The two blessed organizations in Hugging Face for now are [Mozilla](https://huggingface.co/Mozilla) and [Xenova](https://huggingface.co/Xenova) which provide over a thousand models to play with.

We are planning to add more organizations in the future and provide a process for web extension developers
to ask for their models to be added in our list.

Extensions are also not able to run several engines in parallel to avoid resource conflicts.
This means that if you want to run different tasks, it needs to be done in sequence.
This limitation might be relaxed in the future as well.

Last, but not least, if the device memory resources are getting too low, engine running in an extension might
be deleted and an error will be thrown.

## Examples

We've implemented some examples, look for all repositories starting with `webext-ai` in <https://github.com/firefox-ai>.
