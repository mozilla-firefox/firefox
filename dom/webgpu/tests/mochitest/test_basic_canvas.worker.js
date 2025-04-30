self.addEventListener("message", async function (event) {
  try {
    const offscreen = event.data.offscreen;
    const context = offscreen.getContext("webgpu");

    const swapChainFormat = navigator.gpu.getPreferredCanvasFormat();
    const adapter = await navigator.gpu.requestAdapter();
    const device = await adapter.requestDevice();

    context.configure({
      device,
      format: swapChainFormat,
      size: { width: 100, height: 100, depth: 1 },
    });

    const texture = context.getCurrentTexture();

    self.postMessage([
      {
        value: texture !== undefined,
        message: "texture !== undefined",
      },
    ]);
  } catch (e) {
    self.postMessage([
      {
        value: false,
        message: "Unhandled exception " + e,
      },
    ]);
  }
});
