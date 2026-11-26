class TimingAttackProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.port.onmessage = () => {
      const times = [performance.now(), performance.now()];
      let now;
      do {
        now = performance.now();
        times.push(now);
      } while (now === times[0]);
      this.port.postMessage(times);
    };
  }

  process() {
    return false;
  }
}

registerProcessor('timing-attack-processor', TimingAttackProcessor);
