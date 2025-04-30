ok(
  SpecialPowers.getBoolPref("dom.webgpu.enabled"),
  "WebGPU pref should be enabled."
);
SimpleTest.waitForExplicitFinish();

const workerWrapperFunc = async function (worker_path, data, transfer) {
  const worker = new Worker(worker_path);

  const results = new Promise((resolve, reject) => {
    worker.addEventListener("message", event => {
      resolve(event.data);
    });
  });

  worker.postMessage(data, transfer);
  for (const result of await results) {
    ok(result.value, result.message);
  }
};

async function runWorkerTest(worker_path, data, transfer) {
  try {
    await workerWrapperFunc(worker_path, data, transfer);
  } catch (e) {
    ok(false, "Unhandled exception " + e);
  }
  SimpleTest.finish();
}
