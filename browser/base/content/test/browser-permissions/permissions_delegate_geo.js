"use strict";

window.addEventListener("DOMContentLoaded", () => {
  document.getElementById("geo").addEventListener("click", () => {
    navigator.geolocation.getCurrentPosition(
      () => parent.postMessage("allow", "*"),
      error => parent.postMessage(error.code == 1 ? "deny" : "allow", "*")
    );
  });
});
