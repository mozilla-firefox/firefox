"use strict";

add_task(async function test_prefetch_transfer_size() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.speculation_rules.enabled", true],
      ["dom.performance.deliverytype.enabled", true],
    ],
  });

  const { HttpServer } = ChromeUtils.importESModule(
    "resource://testing-common/httpd.sys.mjs"
  );
  const server = new HttpServer();
  server.registerPathHandler("/source", (request, response) => {
    response.setHeader("Content-Type", "text/html");
    response.write("<!DOCTYPE html><title>prefetch source</title>");
  });
  server.registerPathHandler("/target", (request, response) => {
    const failPrefetch =
      request.queryString === "failed" &&
      request.hasHeader("Sec-Purpose") &&
      request.getHeader("Sec-Purpose").includes("prefetch");
    response.setStatusLine(request.httpVersion, failPrefetch ? 503 : 200, "OK");
    response.setHeader("Content-Type", "text/html");
    response.setHeader(
      "Cache-Control",
      request.queryString === "failed" ? "no-store" : "private, max-age=600"
    );
    response.write("<!DOCTYPE html><title>prefetch target</title>");
  });
  server.registerPathHandler("/redirect", (request, response) => {
    response.setStatusLine(request.httpVersion, 302, "Found");
    response.setHeader("Cache-Control", "private, max-age=600");
    response.setHeader("Location", `/target?${request.queryString}`);
  });
  server.start(-1);
  // eslint-disable-next-line sdl/no-insecure-url
  const base = `http://localhost:${server.identity.primaryPort}`;
  registerCleanupFunction(() => new Promise(resolve => server.stop(resolve)));

  for (const { name, cached = false, redirect = false, failed = false } of [
    { name: "network" },
    { name: "cache", cached: true },
    { name: "redirect-cache", cached: true, redirect: true },
    { name: "failed", failed: true },
  ]) {
    const target = `${base}/target?${name}`;
    const prefetchURL = redirect ? `${base}/redirect?${name}` : target;
    await BrowserTestUtils.withNewTab(`${base}/source`, async browser => {
      if (cached) {
        await SpecialPowers.spawn(browser, [target], async url => {
          await (await content.fetch(url)).text();
        });
      }

      const stopped = TestUtils.topicObserved(
        "http-on-stop-request",
        subject => subject.QueryInterface(Ci.nsIHttpChannel).URI.spec === target
      );
      await SpecialPowers.spawn(browser, [prefetchURL], url => {
        const script = content.document.createElement("script");
        script.type = "speculationrules";
        script.textContent = JSON.stringify({
          prefetch: [{ source: "list", urls: [url], eagerness: "immediate" }],
        });
        content.document.head.appendChild(script);
      });
      const [subject] = await stopped;
      Assert.equal(
        subject.QueryInterface(Ci.nsIHttpChannel).transferSize > 0,
        !cached,
        `${name}: the prefetch final response has the expected cache status.`
      );

      const loaded = BrowserTestUtils.browserLoaded(browser, false, target);
      BrowserTestUtils.startLoadingURIString(browser, prefetchURL);
      await loaded;
      const entry = await SpecialPowers.spawn(browser, [], () => {
        const timing = content.performance.getEntriesByType("navigation")[0];
        return {
          transferSize: timing.transferSize,
          encodedBodySize: timing.encodedBodySize,
          deliveryType: timing.deliveryType,
        };
      });
      Assert.greater(
        entry.encodedBodySize,
        0,
        `${name}: the response has a body.`
      );
      Assert.equal(
        entry.transferSize,
        cached ? 0 : entry.encodedBodySize + 300,
        `${name}: transferSize reflects the final response, not redirect bytes.`
      );
      Assert.equal(
        entry.deliveryType,
        failed ? "" : "navigational-prefetch",
        `${name}: the navigation reports whether it used the prefetch.`
      );
    });
  }
});
