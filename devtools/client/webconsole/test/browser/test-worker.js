"use strict";

console.log("initial-message-from-worker", { foo: "bar" }, globalThis);

self.addEventListener("message", function onMessage(event) {
  const { type, message } = event.data;

  // Override Date.prototype.getTime and RegExp.toString to make sure those are not
  // called when logging to the console (see Bug 1892638)
  const date = new Date(2024, 0, 1);
  date.getTime = () => {
    return 42;
  };
  // eslint-disable-next-line no-extend-native
  Date.prototype.getTime = date.getTime;
  const regexp = /foo/m;
  regexp.toString = () => {
    return "24";
  };

  switch (type) {
    case "log":
      console.log(message);
      break;
    case "error":
      throw new Error(message);
    case "log-objects":
      console.log("log-from-worker", message, globalThis);
      console.log(Symbol("logged-symbol-from-worker"));
      console.log(["array-item", 42, { key: "value" }]);
      console.log("sab-from-worker", event.data.sab);

      /* Check if page functions can be called by console previewers */
      console.log("date-from-worker", date);
      console.log("regexp-from-worker", regexp, /not-overloaded/g);
      break;
  }
});
