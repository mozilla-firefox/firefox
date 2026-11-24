let chromeWin = Services.ww.activeWindow;
let contextMenu = chromeWin.document.getElementById("contentAreaContextMenu");
contextMenu.addEventListener(
  "popupshown",
  () => {
    var suggestion = contextMenu.querySelector(".spell-suggestion");
    suggestion.doCommand();
    contextMenu.hidePopup();
    sendAsyncMessage("spellingCorrected");
  },
  { once: true }
);
