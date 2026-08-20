# Firefox AI Runtime

This component is an experimental machine learning local inference runtime based on
[Transformers.js](https://huggingface.co/docs/transformers.js/index) and
the [ONNX runtime](https://onnxruntime.ai/). You can use the component to leverage
the inference runtime in the context of the browser. To try out some inference tasks,
you can refer to the
[1000+ models](https://huggingface.co/models?library=transformers.js)
that are available in the Hugging Face Hub that are compatible with this runtime.

To enable it, flip the `browser.ml.enable` preference to `true` in `about:config`
then visit **about:inference** (Nightly only) or add the following snippet of code
into your (privileged) Javascript code in Firefox or in the browser console:

```javascript
const { createEngine } = ChromeUtils.importESModule("chrome://global/content/ml/EngineProcess.sys.mjs");
const engine = await createEngine({taskName: "summarization"});
const request = { args:  ["This is the text to summarize"]};
const res = await engine.run(request);
console.log(res[0]["summary_text"]);
```

In case of problem, go to `about:logging` find the `Machine Learning` preset
in the dropdown, start logging, reproduce you issue, upload or save the profile,
and open a bug with the link or profile file in [Core :: Machine Learning](https://bugzilla.mozilla.org/enter_bug.cgi?product=Core&component=Machine%20Learning)

Learn more about the platform:

```{toctree}
:maxdepth: 1

architecture
HWInference
api
notifications
models
perf
extensions
```
