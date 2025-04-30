"use strict";

let seenRequests = [];

const server = createHttpServer({ hosts: ["example.com", "example.org"] });

function populateResults(request) {
  seenRequests.push({
    path: request.path + (request.queryString ? "?" + request.queryString : ""),
    referrer: request.hasHeader("Referer")
      ? request.getHeader("Referer")
      : null,
  });
}

server.registerPathHandler("/dummy", (request, response) => {
  populateResults(request);

  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/html", false);
  response.write(
    '<!DOCTYPE html><html><img id="img_from_page" src="http://example.org/img.png"></img></html>'
  );
});

// Small red image.
const IMG_BYTES = atob(
  "iVBORw0KGgoAAAANSUhEUgAAAAUAAAAFCAYAAACNbyblAAAAHElEQVQI12" +
    "P4//8/w38GIAXDIBKE0DHxgljNBAAO9TXL0Y4OHwAAAABJRU5ErkJggg=="
);

server.registerPathHandler("/img.png", (request, response) => {
  populateResults(request);

  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "image/png");
  response.write(IMG_BYTES);
});

server.registerPathHandler("/img_from_style.png", (request, response) => {
  populateResults(request);

  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "image/png");
  response.write(IMG_BYTES);
});

server.registerPathHandler("/img_from_script.png", (request, response) => {
  populateResults(request);

  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "image/png");
  response.write(IMG_BYTES);
});

add_task(
  {
    pref_set: [["privacy.antitracking.isolateContentScriptResources", false]],
  },
  async function test_contentscript_antitracking_off() {
    await runTest(false);
  }
);

add_task(
  {
    pref_set: [["privacy.antitracking.isolateContentScriptResources", true]],
  },
  async function test_contentscript_antitracking_on() {
    await runTest(true);
  }
);

async function runTest(pref) {
  // Cleanup the previous resource data.
  seenRequests.splice(0, seenRequests.length);

  let extensionData = {
    manifest: {
      host_permissions: ["http://example.com/"],

      content_scripts: [
        {
          matches: ["http://example.com/dummy"],
          run_at: "document_end",
          js: ["contentscript.js"],
        },
        {
          matches: ["http://example.com/dummy"],
          run_at: "document_start",
          css: ["content.css"],
        },
      ],
    },

    files: {
      "contentscript.js": async () => {
        await document.getElementById("img_from_page").decode();

        let img = document.createElement("img");
        img.src = "http://example.org/img_from_script.png";
        document.body.appendChild(img);
        await img.decode();

        img = document.createElement("img");
        img.referrerPolicy = "unsafe-url";
        img.src = "http://example.org/img_from_script.png?withattr";
        document.body.appendChild(img);
        await img.decode();

        const meta = document.createElement("meta");
        meta.httpEquiv = "referrer-policy";
        meta.content = "unsafe-url";
        document.body.appendChild(meta);

        img = document.createElement("img");
        img.src = "http://example.org/img_from_script.png?withmeta";
        document.body.appendChild(img);
        await img.decode();

        browser.test.sendMessage("images_loaded");
      },
      "content.css": `
        body {
          background-image: url("http://example.com/img_from_style.png");
        }`,
    },
  };

  const extension = ExtensionTestUtils.loadExtension(extensionData);
  await extension.startup();

  const contentPage = await ExtensionTestUtils.loadContentPage(
    "http://example.com/dummy"
  );

  await extension.awaitMessage("images_loaded");

  await contentPage.close();
  await extension.unload();

  Assert.equal(
    seenRequests.length,
    6,
    "All the requests are correctly processed"
  );

  // Request order is not guaranteed. Let's sort them by path.
  seenRequests.sort((a, b) => {
    if (a.path < b.path) {
      return -1;
    }
    if (a.path > b.path) {
      return 1;
    }
    return 0;
  });

  const expectedReferrer = pref ? null : "http://example.com/";

  Assert.equal(seenRequests[0].path, "/dummy", "Dummy request received");

  Assert.deepEqual(
    seenRequests[1],
    { path: "/img.png", referrer: "http://example.com/" },
    "Image request received"
  );

  Assert.deepEqual(
    seenRequests[2],
    { path: "/img_from_script.png", referrer: expectedReferrer },
    "Image request from content-script received"
  );

  Assert.deepEqual(
    seenRequests[3],
    { path: "/img_from_script.png?withattr", referrer: expectedReferrer },
    "Image request from content-script received"
  );

  // Although unsafe-url is specified, the request is considered cross-origin, so
  // the referrer is trimmed to the origin.
  Assert.deepEqual(
    seenRequests[4],
    {
      path: "/img_from_script.png?withmeta",
      referrer: expectedReferrer,
    },
    "Image request from content-script with header received"
  );

  Assert.deepEqual(
    seenRequests[5],
    { path: "/img_from_style.png", referrer: null },
    "Image request from CSS received"
  );
}
