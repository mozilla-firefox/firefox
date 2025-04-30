self.addEventListener("message", async function (event) {
  try {
    const adapter = await navigator.gpu.requestAdapter();
    const device = await adapter.requestDevice();

    const swapChainFormat = "rgba8unorm";
    const bundleEncoder = device.createRenderBundleEncoder({
      colorFormats: [swapChainFormat],
    });
    const bundle = bundleEncoder.finish({});

    const texture = device.createTexture({
      size: { width: 100, height: 100, depth: 1 },
      format: swapChainFormat,
      usage: GPUTextureUsage.RENDER_ATTACHMENT,
    });
    const view = texture.createView();

    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
      colorAttachments: [
        {
          view,
          clearValue: { r: 0, g: 0, b: 0, a: 0 },
          loadOp: "clear",
          storeOp: "store",
        },
      ],
    });
    pass.executeBundles([bundle]);
    pass.end();
    const command_buffer = encoder.finish();

    device.queue.submit([command_buffer]);
    self.postMessage([
      {
        value: command_buffer !== undefined,
        message: "command_buffer !== undefined",
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
