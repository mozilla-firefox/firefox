/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Calls a deprecated method in a worker and posts back the deprecation report.
onmessage = async () => {
  const report = await new Promise(resolve => {
    const obs = new ReportingObserver(reports => {
      obs.disconnect();
      resolve(reports[0]);
    });
    obs.observe();
    const request = indexedDB.open("deprecated_fluent", 1);
    request.onupgradeneeded = () => request.result.createObjectStore("store");
    request.onsuccess = () => {
      request.result.transaction("store").objectStore("store").mozGetAll();
    };
  });
  postMessage({ id: report.body.id, message: report.body.message });
};
