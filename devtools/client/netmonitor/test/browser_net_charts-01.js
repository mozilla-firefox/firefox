/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Makes sure Pie Charts have the right internal structure.
 */

add_task(async function () {
  const { monitor } = await initNetMonitor(HTTPS_SIMPLE_URL, {
    requestCount: 1,
  });

  info("Starting test... ");

  const { document, windowRequire } = monitor.panelWin;
  const { Chart } = windowRequire("devtools/client/shared/widgets/Chart");

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await navigateTo(HTTPS_SIMPLE_URL);
  await onNetworkEvents;

  const pie = Chart.Pie(document, {
    width: 100,
    height: 100,
    data: [
      {
        size: 1,
        label: "foo",
      },
      {
        size: 2,
        label: "bar",
      },
      {
        size: 3,
        label: "baz",
      },
    ],
  });

  const { node } = pie;
  const slicesContainer = node.querySelectorAll(".pie-chart-slice-container");
  const slices = node.querySelectorAll(".pie-chart-slice");
  const labels = node.querySelectorAll(".pie-chart-label");

  ok(
    node.classList.contains("pie-chart-container") &&
      node.classList.contains("generic-chart-container"),
    "A pie chart container was created successfully."
  );
  is(
    node.getAttribute("aria-label"),
    "Pie chart representing the size of each type of request in proportion to each other",
    "pie chart container has expected aria-label"
  );

  is(
    slicesContainer.length,
    3,
    "There should be 3 pie chart slices container created."
  );

  is(slices.length, 3, "There should be 3 pie chart slices created.");
  ok(
    slices[0]
      .getAttribute("d")
      .match(
        /\s*M 50,50 L 49\.\d+,97\.\d+ A 47\.5,47\.5 0 0 1 49\.\d+,2\.5\d* Z/
      ),
    "The first slice has the correct data."
  );
  ok(
    slices[1]
      .getAttribute("d")
      .match(
        /\s*M 50,50 L 91\.\d+,26\.\d+ A 47\.5,47\.5 0 0 1 49\.\d+,97\.\d+ Z/
      ),
    "The second slice has the correct data."
  );
  ok(
    slices[2]
      .getAttribute("d")
      .match(
        /\s*M 50,50 L 50\.\d+,2\.5\d* A 47\.5,47\.5 0 0 1 91\.\d+,26\.\d+ Z/
      ),
    "The third slice has the correct data."
  );

  is(
    slicesContainer[0].getAttribute("aria-label"),
    "baz: 50%",
    "First slice container has expected aria-label"
  );
  is(
    slicesContainer[1].getAttribute("aria-label"),
    "bar: 33.33%",
    "Second slice container has expected aria-label"
  );
  is(
    slicesContainer[2].getAttribute("aria-label"),
    "foo: 16.67%",
    "Third slice container has expected aria-label"
  );

  ok(
    slices[0].hasAttribute("largest"),
    "The first slice should be the largest one."
  );
  ok(
    slices[2].hasAttribute("smallest"),
    "The third slice should be the smallest one."
  );

  is(
    slices[0].getAttribute("data-statistic-name"),
    "baz",
    "The first slice's name is correct."
  );
  is(
    slices[1].getAttribute("data-statistic-name"),
    "bar",
    "The first slice's name is correct."
  );
  is(
    slices[2].getAttribute("data-statistic-name"),
    "foo",
    "The first slice's name is correct."
  );

  is(labels.length, 3, "There should be 3 pie chart labels created.");
  is(labels[0].textContent, "baz", "The first label's text is correct.");
  is(labels[1].textContent, "bar", "The first label's text is correct.");
  is(labels[2].textContent, "foo", "The first label's text is correct.");
  is(
    labels[0].getAttribute("aria-hidden"),
    "true",
    "The first label has aria-hidden."
  );
  is(
    labels[1].getAttribute("aria-hidden"),
    "true",
    "The first label has aria-hidden."
  );
  is(
    labels[2].getAttribute("aria-hidden"),
    "true",
    "The first label has aria-hidden."
  );

  await teardown(monitor);
});
