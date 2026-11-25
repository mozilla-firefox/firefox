// eslint-disable-next-line no-unused-vars
import * as _aiActionConfirmation from "chrome://browser/content/aiwindow/components/ai-action-confirmation.mjs";
// eslint-disable-next-line no-unused-vars
import * as _aiActionConfirmationTabRow from "chrome://browser/content/aiwindow/components/ai-action-confirmation-tab-row.mjs";
import { html } from "chrome://global/content/vendor/lit.all.mjs";

(async () => {
  await customElements.whenDefined("ai-action-confirmation");
  await customElements.whenDefined("ai-action-confirmation-tab-row");
  document.getElementById("test-action-confirmation").rowTemplate = tab =>
    html`<ai-action-confirmation-tab-row
      .tab=${tab}
    ></ai-action-confirmation-tab-row>`;
})();
