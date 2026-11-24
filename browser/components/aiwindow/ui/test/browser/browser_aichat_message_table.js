/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const TABLE_MARKDOWN = `| Header 1 | Header 2 |
|----------|----------|
| A        | B        |`;

let chatTab;

describe("chat message table rendering", () => {
  beforeEach(async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.enabled", true]],
    });
    chatTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "about:aichatcontent"
    );
  });

  afterEach(async () => {
    BrowserTestUtils.removeTab(chatTab);
    chatTab = null;
    await SpecialPowers.popPrefEnv();
  });

  it("should render valid table markdown as ai-chat-table", async () => {
    await SpecialPowers.spawn(
      chatTab.linkedBrowser,
      [TABLE_MARKDOWN],
      async tableMarkdown => {
        await content.customElements.whenDefined("ai-chat-message");
        await content.customElements.whenDefined("ai-chat-table");

        const message = content.document.createElement("ai-chat-message");
        content.document.body.appendChild(message);

        // `role` reflects to the `data-message-role` attribute (it can't use the
        // `role` attribute, which is the native ARIA role), so drive it via that.
        message.setAttribute("data-message-role", "assistant");
        message.message = tableMarkdown;
        message.setAttribute("message", tableMarkdown);

        await ContentTaskUtils.waitForMutationCondition(
          message.shadowRoot,
          { childList: true, subtree: true },
          () =>
            message.shadowRoot.querySelector(".message-assistant ai-chat-table")
        );

        const tableWrapper = message.shadowRoot.querySelector(
          ".message-assistant ai-chat-table"
        );
        Assert.ok(tableWrapper, "ai-chat-table wrapper should exist");

        const table = tableWrapper.querySelector("table");
        Assert.ok(table, "table element should exist inside ai-chat-table");

        const headerCells = table.querySelectorAll("thead th");
        Assert.equal(
          headerCells.length,
          2,
          "Table should have 2 header columns"
        );
        Assert.equal(headerCells[0].textContent, "Header 1");
        Assert.equal(headerCells[1].textContent, "Header 2");

        const bodyRows = table.querySelectorAll("tbody tr");
        Assert.equal(bodyRows.length, 1, "Table should have 1 body row");

        const bodyCells = bodyRows[0].querySelectorAll("td");
        Assert.equal(bodyCells.length, 2, "Body row should have 2 cells");
        Assert.equal(bodyCells[0].textContent, "A");
        Assert.equal(bodyCells[1].textContent, "B");

        message.remove();
      }
    );
  });

  it("should not render invalid table markdown as a table", async () => {
    await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
      await content.customElements.whenDefined("ai-chat-message");

      const message = content.document.createElement("ai-chat-message");
      content.document.body.appendChild(message);

      // Invalid table markdown missing the separator row.
      const invalidTableMarkdown = `| Header 1 | Header 2 |
| A        | B        |`;

      message.setAttribute("data-message-role", "assistant");
      message.message = invalidTableMarkdown;
      message.setAttribute("message", invalidTableMarkdown);

      await ContentTaskUtils.waitForMutationCondition(
        message.shadowRoot,
        { childList: true, subtree: true },
        () => message.shadowRoot.querySelector(".message-assistant")
      );

      const assistantDiv =
        message.shadowRoot.querySelector(".message-assistant");
      Assert.ok(assistantDiv, "Message content should still render");
      Assert.ok(
        assistantDiv.textContent.includes("Header 1"),
        "Message text should be visible"
      );
      Assert.ok(
        !assistantDiv.querySelector("ai-chat-table"),
        "Invalid table markdown should not create ai-chat-table"
      );
      Assert.ok(
        !assistantDiv.querySelector("table"),
        "Invalid table markdown should not create table element"
      );

      message.remove();
    });
  });

  it("should render copy button", async () => {
    await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
      await content.customElements.whenDefined("ai-chat-table");

      // The copy button shows when the table is inside a message that has an
      // id; the table reads it from its shadow host's data-message-id.
      const host = content.document.createElement("div");
      host.dataset.messageId = "test-message-id";
      const shadow = host.attachShadow({ mode: "open" });
      content.document.body.appendChild(host);

      const table = content.document.createElement("ai-chat-table");
      table.setAttribute("data-line-range", "[0,3]");
      shadow.appendChild(table);
      await table.updateComplete;

      const copyButton = table.shadowRoot.querySelector(".table-copy-button");
      Assert.ok(copyButton, "Copy button should render in ai-chat-table");
    });
  });

  it("should not render copy button when dataLineRange is missing", async () => {
    await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
      await content.customElements.whenDefined("ai-chat-table");

      const host = content.document.createElement("div");
      host.dataset.messageId = "test-message-id";
      const shadow = host.attachShadow({ mode: "open" });
      content.document.body.appendChild(host);

      const table = content.document.createElement("ai-chat-table");
      shadow.appendChild(table);
      await table.updateComplete;

      Assert.ok(
        !table.shadowRoot.querySelector(".table-copy-button"),
        "Copy button should not exist"
      );
    });
  });

  it("should not render copy button when messageId is missing", async () => {
    await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
      await content.customElements.whenDefined("ai-chat-table");

      const table = content.document.createElement("ai-chat-table");
      table.setAttribute("data-line-range", "[0,3]");
      content.document.body.appendChild(table);
      await table.updateComplete;

      Assert.ok(
        !table.shadowRoot.querySelector(".table-copy-button"),
        "Copy button should not exist"
      );
    });
  });

  it("should show scroll indicator when table overflows", async () => {
    await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
      await content.customElements.whenDefined("ai-chat-table");

      const table = content.document.createElement("ai-chat-table");
      table.style.display = "block";
      table.style.width = "100px";

      const innerTable = content.document.createElement("table");
      let headerCells = "";
      for (let i = 0; i < 10; i++) {
        headerCells += `<th style="white-space: nowrap">Long header ${i}</th>`;
      }
      innerTable.innerHTML = `<thead><tr>${headerCells}</tr></thead>`;
      table.appendChild(innerTable);

      content.document.body.appendChild(table);
      await table.updateComplete;

      await ContentTaskUtils.waitForCondition(
        () => table.shadowRoot.querySelector(".table-scroll-indicator"),
        "Scroll indicator should appear for an overflowing table"
      );

      table.remove();
    });
  });

  it("should not show scroll indicator when table fits", async () => {
    await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
      await content.customElements.whenDefined("ai-chat-table");

      const table = content.document.createElement("ai-chat-table");
      table.style.display = "block";
      table.style.width = "600px";

      const innerTable = content.document.createElement("table");
      innerTable.innerHTML = `<thead><tr><th>A</th><th>B</th></tr></thead>`;
      table.appendChild(innerTable);

      content.document.body.appendChild(table);
      await table.updateComplete;

      await new Promise(resolve =>
        content.requestAnimationFrame(() =>
          content.requestAnimationFrame(resolve)
        )
      );

      Assert.ok(
        !table.shadowRoot.querySelector(".table-scroll-indicator"),
        "Scroll indicator should not render when the table fits"
      );

      table.remove();
    });
  });

  it("should copy table markdown to clipboard", async () => {
    const restoreSignIn = skipSignIn();
    const messageWithTable = `Here is a markdown table:\n\n${TABLE_MARKDOWN}\n\nThis was a markdown table.`;
    const mockEngineManager = new MockEngineManager();

    let win;
    try {
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;

      await typeInSmartbar(browser, "show me a table");
      await submitSmartbar(browser);
      const aichatBrowser = await getAichatBrowser(browser);
      await mockEngineManager.respondTo({
        purpose: "chat",
        response: messageWithTable,
      });

      await SimpleTest.promiseClipboardChange(TABLE_MARKDOWN, async () => {
        await SpecialPowers.spawn(aichatBrowser, [], async () => {
          const chatContent = content.document.querySelector("ai-chat-content");
          let copyButton;
          await ContentTaskUtils.waitForCondition(() => {
            const message =
              chatContent.shadowRoot.querySelectorAll("ai-chat-message")[1];
            const table = message?.shadowRoot.querySelector("ai-chat-table");
            copyButton = table?.shadowRoot.querySelector(".table-copy-button");
            return copyButton;
          }, "Wait for the table copy button");
          copyButton.click();
        });
      });
    } finally {
      if (win) {
        await BrowserTestUtils.closeWindow(win);
      }
      await mockEngineManager.rejectAllRequests();
      mockEngineManager.cleanupMocks();
      restoreSignIn();
    }
  });

  it("should copy correct table when message contains multiple tables", async () => {
    const TABLE_MARKDOWN_SECOND = `| Column 1 | Column 2 | Column 3 |
|-------|-------|-------|
| 1     | 2     | 3     |
| 4     | 5     | 6     |`;
    const restoreSignIn = skipSignIn();
    const messageWithTwoTables = `First table:\n\n${TABLE_MARKDOWN}\n\nSecond table:\n\n${TABLE_MARKDOWN_SECOND}`;
    const mockEngineManager = new MockEngineManager();

    let win;
    try {
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;

      await typeInSmartbar(browser, "show me two tables");
      await submitSmartbar(browser);
      const aichatBrowser = await getAichatBrowser(browser);
      await mockEngineManager.respondTo({
        purpose: "chat",
        response: messageWithTwoTables,
      });

      // Copy first table
      await SimpleTest.promiseClipboardChange(TABLE_MARKDOWN, async () => {
        await SpecialPowers.spawn(aichatBrowser, [], async () => {
          const chatContent = content.document.querySelector("ai-chat-content");
          let copyButton;
          await ContentTaskUtils.waitForCondition(() => {
            const message =
              chatContent.shadowRoot.querySelectorAll("ai-chat-message")[1];
            const table =
              message?.shadowRoot.querySelectorAll("ai-chat-table")[0];
            copyButton = table?.shadowRoot.querySelector(".table-copy-button");
            return copyButton;
          }, "Wait for the table copy button");
          copyButton.click();
        });
      });

      // Copy second table
      await SimpleTest.promiseClipboardChange(
        TABLE_MARKDOWN_SECOND,
        async () => {
          await SpecialPowers.spawn(aichatBrowser, [], async () => {
            const chatContent =
              content.document.querySelector("ai-chat-content");
            let copyButton;
            await ContentTaskUtils.waitForMutationCondition(
              chatContent.shadowRoot,
              { childList: true, subtree: true },
              () => {
                const message =
                  chatContent.shadowRoot.querySelectorAll("ai-chat-message")[1];
                const table =
                  message?.shadowRoot.querySelectorAll("ai-chat-table")[1];
                copyButton =
                  table?.shadowRoot.querySelector(".table-copy-button");
                return copyButton;
              }
            );
            copyButton.click();
          });
        }
      );
    } finally {
      if (win) {
        await BrowserTestUtils.closeWindow(win);
      }
      await mockEngineManager.rejectAllRequests();
      mockEngineManager.cleanupMocks();
      restoreSignIn();
    }
  });
});
