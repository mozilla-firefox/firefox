"use strict";

const BASE = "https://example.com/browser/netwerk/test/browser/";

// Shutting down a worker EventSource cancels its channel; the content-process
// channel must still deliver OnStopRequest.
async function checkChannelStops({ stream, terminate }) {
  await BrowserTestUtils.withNewTab(BASE + "dummy.html", async browser => {
    await SpecialPowers.spawn(
      browser,
      [BASE, stream, terminate],
      async (base, stream, terminate) => {
        const sseURL =
          base + "eventsource_worker_close.sjs" + (stream ? "?stream" : "");
        const topics = ["http-on-opening-request", "http-on-stop-request"];
        let opened = false;
        const stopped = Promise.withResolvers();
        const observer = {
          observe(subject, topic) {
            const channel = subject.QueryInterface(Ci.nsIChannel);
            if (channel.URI.spec != sseURL) {
              return;
            }
            if (topic == topics[0]) {
              opened = true;
            } else {
              stopped.resolve(channel.status);
            }
          },
        };
        topics.forEach(t => Services.obs.addObserver(observer, t));

        const worker = new content.Worker(
          base + "eventsource_worker_close_worker.js"
        );
        const message = ContentTaskUtils.waitForEvent(worker, "message");
        worker.postMessage({ url: sseURL, close: !terminate });
        Assert.equal(
          (await message).data,
          "message",
          "EventSource got a message"
        );
        Assert.ok(opened, "saw the EventSource channel open");
        if (terminate) {
          worker.terminate();
        }
        Assert.equal(
          await stopped.promise,
          Cr.NS_ERROR_ABORT,
          "channel stopped"
        );

        topics.forEach(t => Services.obs.removeObserver(observer, t));
        worker.terminate();
      }
    );
  });
}

add_task(async function test_close() {
  await checkChannelStops({ stream: false, terminate: false });
});

add_task(async function test_close_while_streaming() {
  await checkChannelStops({ stream: true, terminate: false });
});

add_task(async function test_terminate_while_streaming() {
  await checkChannelStops({ stream: true, terminate: true });
});
