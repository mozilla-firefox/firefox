/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global require, module */

const { logTest } = require("./utils/profiling");

const EAGERNESS_LEVELS = ["immediate", "eager", "moderate", "conservative"];
const SOURCES = ["document", "list", "none"];

async function moveToInstantly(commands, selector) {
  const element = await commands.element.getByCss(selector);
  const actions = commands.action.getActions();
  await actions.move({ origin: element, duration: 0 }).perform();
  await actions.clear();
}

// Drive the click so each eagerness level gets a fair chance to fire its
// prefetch before navigation. `dwellMs` is the settle/hold window: sized to
// clear the target page's server stall, or to land inside it for the
// in-flight subtest.
async function navigateWithPrefetch(
  commands,
  eagerness,
  selector,
  dwellMs,
  log
) {
  switch (eagerness) {
    case "immediate":
      // Prefetch starts at parse time; just let it finish, then click.
      await commands.wait.byTime(dwellMs);
      await commands.mouse.singleClick.bySelector(selector);
      break;
    case "eager":
    case "moderate":
      // Both fire on hover (eager sooner); sustain the hover, then click.
      // Zero-duration move: Marionette interpolates the default 100 ms move
      // in steps and only enters the link at the end, while chromedriver
      // jumps straight to it, which would skew when the hover timer starts.
      await moveToInstantly(commands, selector);
      log("hover done");
      await commands.wait.byTime(dwellMs);
      log("click issued");
      await commands.mouse.singleClick.bySelector(selector);
      break;
    case "conservative":
      // Fires on pointerdown: press, hold so the prefetch can complete, then
      // release on the link to navigate.
      await commands.mouse.clickAndHold.bySelector(selector);
      await commands.wait.byTime(dwellMs);
      await commands.mouse.clickAndHold.releaseAtSelector(selector);
      break;
    default:
      throw new Error(
        `speculation-rules-prefetch: unknown eagerness "${eagerness}"`
      );
  }
}

module.exports = logTest(
  "speculation rules prefetch",
  async function (context, commands) {
    const serverUrl = context.options.browsertime.server_url;
    if (!serverUrl) {
      throw new Error(
        "speculation-rules-prefetch: missing --browsertime.server_url; " +
          "is support_class=speculation_rules.py wired in?"
      );
    }

    const buttonId = context.options.browsertime.button_id || "btn-a";
    const eagerness = context.options.browsertime.eagerness || "moderate";
    if (!EAGERNESS_LEVELS.includes(eagerness)) {
      throw new Error(
        `speculation-rules-prefetch: unsupported eagerness "${eagerness}"; ` +
          `expected one of ${EAGERNESS_LEVELS.join(", ")}`
      );
    }
    const source = context.options.browsertime.source || "document";
    if (!SOURCES.includes(source)) {
      throw new Error(
        `speculation-rules-prefetch: unsupported source "${source}"; ` +
          `expected one of ${SOURCES.join(", ")}`
      );
    }
    const body = context.options.browsertime.body || "";
    // Covers each level's trigger latency plus the 500 ms backend stall.
    const dwellMs = Number(context.options.browsertime.dwell_ms ?? 1000);

    context.log.info(
      `speculation-rules-prefetch: button=${buttonId}, source=${source}, ` +
        `eagerness=${eagerness}, body=${body}, dwell_ms=${dwellMs}`
    );

    await commands.navigate(
      `${serverUrl}/landing.html?eagerness=${eagerness}&source=${source}&body=${body}`
    );
    await commands.wait.byTime(250);

    await commands.measure.start();
    // Wall-clock stamps, so they line up with the demo server's request log.
    const stamp = what =>
      context.log.info(
        `speculation-rules-prefetch: ${what} at ` +
          `${Math.round(performance.timeOrigin + performance.now())}`
      );
    stamp("trigger");
    await navigateWithPrefetch(
      commands,
      eagerness,
      `#${buttonId}`,
      dwellMs,
      stamp
    );
    stamp("clicked");
    await commands.wait.byTime(2500);
    await commands.measure.stop();

    const navInfo = await commands.js.run(`
      const nav = performance.getEntriesByType('navigation')[0];
      const fcp = performance.getEntriesByName('first-contentful-paint')[0];
      return nav ? {
        fcp: fcp ? fcp.startTime : null,
        deliveryType: nav.deliveryType || "",
        type: nav.type,
        duration: nav.duration,
        responseStart: nav.responseStart,
        domContentLoadedEventEnd: nav.domContentLoadedEventEnd,
        loadEventEnd: nav.loadEventEnd,
        href: location.href,
      } : null;
    `);
    context.log.info(
      `speculation-rules-prefetch: navigation entry = ${JSON.stringify(navInfo)}`
    );

    if (!navInfo) {
      throw new Error(
        "speculation-rules-prefetch: no navigation entry found on target page"
      );
    }

    await commands.measure.addObject({
      custom_data: {
        delivery_type: navInfo.deliveryType,
        navigation_duration: navInfo.duration,
        response_start: navInfo.responseStart,
        fcp: navInfo.fcp,
      },
    });

    return true;
  }
);
