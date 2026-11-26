function run_audioworklet_test(isolated) {
  const resolution = isolated ? 5 : 100;
  promise_test(async (t) => {
    assert_equals(self.crossOriginIsolated, isolated,
                  'Document cross-origin isolated value matches');
    const context = new AudioContext();
    t.add_cleanup(() => context.close());
    await context.audioWorklet.addModule(
        'resources/audioworklet-timing-attack-processor.js');
    const node = new AudioWorkletNode(context, 'timing-attack-processor');
    const times = await new Promise((resolve) => {
      node.port.onmessage = (e) => resolve(e.data);
      node.port.postMessage('sample');
    });
    for (let i = 0; i < times.length; i++) {
      for (let j = i + 1; j < times.length; j++) {
        const diff = times[j] - times[i];
        if (diff !== 0) {
          assert_greater_than_equal(diff * 1000, resolution,
                                    'Difference in microseconds.');
        }
      }
    }
  }, `The minimum resolution of performance.now() in AudioWorkletGlobalScope is ${resolution} microseconds when ${isolated ? '' : 'not '}cross-origin isolated`);
}
