/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { SmartFormFillModel } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs"
);
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const {
  MODEL_FEATURES,
  getRemoteClient,
  _setRemoteClientForTesting,
  _clearRemoteClientForTesting,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

const CUSTOM_PROMPTS_PREF = "browser.smartwindow.customPrompts";
const PURPOSE = "smart-form-fill";

const VALUE_GENERATION_MODULES = [
  {
    feature: MODEL_FEATURES.SMART_FORM_FILL,
    id: "smart-form-fill--value-generation-system-instructions--generic--v2",
    kind: "module",
    model: "generic",
    module: "value-generation-system-instructions",
    prompts: "You help a user fill out web form fields.",
    version: "2.0",
  },
  {
    feature: MODEL_FEATURES.SMART_FORM_FILL,
    id: "smart-form-fill--value-generation-user-data--generic--v2",
    kind: "module",
    model: "generic",
    module: "value-generation-user-data",
    prompts:
      "Current page title:\n{title}\n\n" +
      "Current page url:\n{url}\n\n" +
      "User's browser locale:\n{locale}\n\n" +
      "Current page text:\n{pageText}\n\n" +
      "Relevant memories about the user:\n{memories}\n\n" +
      "Relevant open tabs:\n{pageContext}\n\n" +
      "Available candidate tokens:\n{candidateTokens}\n\n" +
      "Fields to fill:\n{fields}",
    version: "2.0",
  },
];

async function useValueGenerationPrompts() {
  const records = await getRemoteClient().get();
  const params = records.find(
    record =>
      record.feature === MODEL_FEATURES.SMART_FORM_FILL &&
      record.kind === "params" &&
      record.is_default
  );
  const moduleNames = new Set(
    VALUE_GENERATION_MODULES.map(record => record.module)
  );
  const moduleIds = new Set(VALUE_GENERATION_MODULES.map(record => record.id));

  _setRemoteClientForTesting({
    get: async () => [
      ...records.filter(
        record => record !== params && !moduleIds.has(record.id)
      ),
      {
        ...params,
        modules: [
          ...params.modules.filter(module => !moduleNames.has(module.name)),
          ...VALUE_GENERATION_MODULES.map(record => ({
            name: record.module,
            version: record.version,
          })),
        ],
      },
      ...VALUE_GENERATION_MODULES,
    ],
  });
}

function makeRequest(id) {
  return {
    task: "generate",
    page: {
      title: "Example form",
      url: "https://example.com/form",
    },
    fields: [
      {
        id,
        label: id,
        inputType: "text",
        options: [],
        type: "unknown",
        classificationConfidence: "low",
      },
    ],
    candidates: [],
    context: {
      relevantTabs: [],
    },
  };
}

function makeFields(count) {
  return Array.from({ length: count }, (_, index) => ({
    id: `field-${index}`,
    label: `Field ${index}`,
    inputType: "text",
    options: [],
    type: "unknown",
    classificationConfidence: "low",
  }));
}

function generateValuesForFields(fields) {
  return SmartFormFillModel.generateFormValues({
    task: "generate",
    page: {
      title: "Batch test",
      url: "https://example.com/batch",
    },
    fields,
    candidates: [],
    context: {
      pageText: "",
      relevantTabs: [],
      memories: [],
    },
  });
}

function getRequestedFields(request) {
  const marker = "Fields to fill:\n";
  const content = request.args.at(-1).content;

  return JSON.parse(content.slice(content.lastIndexOf(marker) + marker.length));
}

describe("SmartFormFillModel", () => {
  let mockEngineMan;

  beforeEach(async () => {
    await SpecialPowers.pushPrefEnv({
      clear: [[CUSTOM_PROMPTS_PREF]],
    });
    mockEngineMan = new MockEngineManager();
  });

  afterEach(async () => {
    await mockEngineMan.rejectAllRequests();
    mockEngineMan.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  });

  describe("classifyFields", () => {
    it("builds a field classification request", async () => {
      const title = "T".repeat(101);
      const fields = [
        {
          id: "field-1",
          label: "Email address",
          name: "email",
          inputType: "email",
          options: [],
        },
      ];
      const requestPromise = SmartFormFillModel.classifyFields({
        task: "classify",
        enumVersion: "sff-fieldtypes-1",
        page: {
          title,
          url: "https://example.com/form",
        },
        fields,
      });

      const { request, respond } = await mockEngineMan.captureRequest({
        purpose: PURPOSE,
      });

      Assert.equal(request.args.length, 2);
      Assert.equal(request.args[0].role, "system");
      Assert.stringContains(
        request.args[0].content,
        "You classify each field of a web form into exactly one type"
      );
      Assert.stringContains(
        request.args[0].content,
        "Valid field types (choose exactly one per field"
      );
      Assert.deepEqual(request.args[1], {
        role: "user",
        content:
          `Page title:\n${title.substring(0, 100)}\n\n` +
          "Page url:\nEXAMPLE_COM_FORM_1\n\n" +
          `Fields to classify:\n${JSON.stringify(fields)}`,
      });
      Assert.deepEqual(request.tools, []);

      const responseFormat = request.inferenceParams.response_format;
      Assert.equal(responseFormat.type, "json_schema");
      Assert.equal(
        responseFormat.json_schema.name,
        "SmartFormFillFieldClassification"
      );
      Assert.deepEqual(responseFormat.json_schema.schema.required, ["fields"]);

      respond(JSON.stringify({ fields: [] }));
      await requestPromise;
    });
  });

  describe("findRelevantTabs", () => {
    it("builds a relevant tabs request", async () => {
      const pageTitle = "P".repeat(101);
      const tabTitle = "T".repeat(101);
      const fields = [
        {
          id: "field-1",
          label: "Company",
          inputType: "text",
          options: [],
        },
      ];
      const tabs = [
        {
          id: "tab-1",
          title: tabTitle,
          url: "https://example.com/profile",
        },
      ];
      const shapedTabs = [
        {
          ...tabs[0],
          title: tabTitle.substring(0, 100),
          url: "EXAMPLE_COM_PROFILE_1",
        },
      ];
      const requestPromise = SmartFormFillModel.findRelevantTabs({
        task: "select_tabs",
        page: {
          title: pageTitle,
          url: "https://example.com/jobs/apply",
        },
        maxSelectedTabs: 3,
        tabs,
        fields,
      });

      const { request, respond } = await mockEngineMan.captureRequest({
        purpose: PURPOSE,
      });

      Assert.equal(request.args.length, 2);
      Assert.equal(request.args[0].role, "system");
      Assert.stringContains(
        request.args[0].content,
        "decide which of the user's open browser tabs could help autofill"
      );
      Assert.deepEqual(request.args[1], {
        role: "user",
        content:
          "FORM THE USER IS FILLING\n" +
          `Title: ${pageTitle.substring(0, 100)}\n` +
          "URL: EXAMPLE_COM_JOBS_APPLY_1\n" +
          "Fields it is asking for:\n" +
          `${JSON.stringify(fields)}\n\n` +
          "THE USER'S OTHER OPEN TABS (title and URL only)\n" +
          `${JSON.stringify(shapedTabs)}\n\n` +
          "maxSelectedTabs: 3\n\n" +
          `userLocale: ${Services.locale.appLocaleAsBCP47}\n\n` +
          "Which of these tabs could help fill this form? " +
          "Include every tab that plausibly could.",
      });
      Assert.deepEqual(request.tools, []);

      const responseFormat = request.inferenceParams.response_format;
      Assert.equal(responseFormat.type, "json_schema");
      Assert.equal(
        responseFormat.json_schema.name,
        "SmartFormFillRelevantTabs"
      );
      Assert.deepEqual(responseFormat.json_schema.schema.required, [
        "selectedTabs",
      ]);

      respond(JSON.stringify({ selectedTabs: [] }));
      await requestPromise;
    });
  });

  describe("generateFormValues", () => {
    beforeEach(async () => {
      await useValueGenerationPrompts();
    });

    afterEach(() => {
      _clearRemoteClientForTesting();
    });

    it("builds a value generation request with the expected schema", async () => {
      const pageTitle = "P".repeat(101);
      const tabTitle = "T".repeat(101);
      const candidates = [{ token: "§EMAIL_1§", type: "email" }];
      const fields = [
        {
          id: "field-email",
          label: "Email address",
          inputType: "email",
          options: [],
          type: "email",
          classificationConfidence: "high",
        },
        {
          id: "field-reason",
          label: "Why are you interested?",
          inputType: "textarea",
          options: [],
          type: "contextual",
          classificationConfidence: "high",
        },
      ];
      const relevantTabs = [
        {
          title: tabTitle,
          url: "https://example.com/jobs/role",
          tabContent: "The role focuses on browser engineering.",
        },
      ];
      const shapedRelevantTabs = [
        {
          ...relevantTabs[0],
          title: tabTitle.substring(0, 100),
          url: "§url_token: EXAMPLE_COM_JOBS_ROLE_1§",
        },
      ];
      const requestPromise = SmartFormFillModel.generateFormValues({
        task: "generate",
        page: {
          title: pageTitle,
          url: "https://example.com/jobs/generate",
        },
        fields,
        candidates,
        context: {
          pageText: "Apply for the example role.",
          relevantTabs,
          memories: [],
        },
      });

      const { request, respond } = await mockEngineMan.captureRequest({
        purpose: PURPOSE,
      });

      Assert.equal(request.args.length, 2);
      Assert.deepEqual(request.args[0], {
        role: "system",
        content: "You help a user fill out web form fields.",
      });
      Assert.deepEqual(request.args[1], {
        role: "user",
        content:
          `Current page title:\n${pageTitle.substring(0, 100)}\n\n` +
          "Current page url:\n§url_token: EXAMPLE_COM_JOBS_GENERATE_1§\n\n" +
          `User's browser locale:\n${Services.locale.appLocaleAsBCP47}\n\n` +
          "Current page text:\nApply for the example role.\n\n" +
          "Relevant memories about the user:\n[]\n\n" +
          `Relevant open tabs:\n${JSON.stringify(shapedRelevantTabs)}\n\n` +
          `Available candidate tokens:\n${JSON.stringify(candidates)}\n\n` +
          `Fields to fill:\n${JSON.stringify(fields)}`,
      });
      Assert.deepEqual(request.tools, []);

      const responseFormat = request.inferenceParams.response_format;
      Assert.equal(responseFormat.type, "json_schema");
      Assert.equal(responseFormat.json_schema.name, "SmartFormFillFormValues");
      Assert.deepEqual(responseFormat.json_schema.schema.required, [
        "memories_used",
        "tabs_used",
        "fields",
      ]);
      const fieldSchema =
        responseFormat.json_schema.schema.properties.fields.items;
      Assert.deepEqual(fieldSchema.required, [
        "id",
        "action",
        "confidence",
        "value",
      ]);
      Assert.deepEqual(fieldSchema.properties.action.enum, [
        "fill_from_token",
        "select_option",
        "generate",
        "skip",
      ]);

      respond(
        JSON.stringify({
          memories_used: [],
          tabs_used: [],
          fields: [],
        })
      );
      await requestPromise;
    });

    it("resolves URL tokens the model repeats back", async () => {
      const tabUrl = "https://example.com/profile/contact?ref=form";
      const requestPromise = SmartFormFillModel.generateFormValues({
        task: "generate",
        page: {
          title: "Contact form",
          url: "https://example.com/contact",
        },
        fields: makeFields(2),
        candidates: [],
        context: {
          pageText: "",
          relevantTabs: [
            {
              title: "Profile",
              url: tabUrl,
              tabContent: "The profile page for the user.",
            },
          ],
          memories: [],
        },
      });

      const { request, respond } = await mockEngineMan.captureRequest({
        purpose: PURPOSE,
      });
      const tabToken = "EXAMPLE_COM_PROFILE_CONTACT_1";

      Assert.stringContains(
        request.args.at(-1).content,
        `§url_token: ${tabToken}§`,
        "Context tab URLs should be sent as sentinel URL tokens"
      );

      respond(
        JSON.stringify({
          memories_used: [],
          tabs_used: [`§url_token: ${tabToken}§`],
          fields: [
            {
              id: "field-0",
              action: "generate",
              confidence: "high",
              value: `My page is §url_token: ${tabToken}§`,
            },
            {
              id: "field-1",
              action: "generate",
              confidence: "high",
              value: "§url_token: EXAMPLE_COM_INVENTED_9§",
            },
          ],
        })
      );

      const result = await requestPromise;

      Assert.equal(
        result.fields[0].value,
        `My page is ${tabUrl}`,
        "A URL token in a generated value should expand to the full URL"
      );
      Assert.equal(
        result.fields[1].value,
        "",
        "A URL token the model invented should be dropped from the value"
      );
      Assert.deepEqual(
        result.tabs_used,
        [tabUrl],
        "Tabs the model reports using should expand to their full URLs"
      );
    });

    it("resolves values that are not strings to nothing", async () => {
      const requestPromise = generateValuesForFields(makeFields(2));
      const { respond } = await mockEngineMan.captureRequest({
        purpose: PURPOSE,
      });

      respond(
        JSON.stringify({
          memories_used: [],
          tabs_used: [42, null],
          fields: [
            { id: "field-0", action: "generate", confidence: "high" },
            {
              id: "field-1",
              action: "generate",
              confidence: "high",
              value: 42,
            },
          ],
        })
      );

      const result = await requestPromise;

      Assert.equal(
        result.fields[0].value,
        "",
        "A missing generated value should resolve to the empty string"
      );
      Assert.equal(
        result.fields[1].value,
        "",
        "A generated value that is not a string should resolve to the empty string"
      );
      Assert.deepEqual(
        result.tabs_used,
        [],
        "Reported tabs that are not strings should be dropped"
      );
    });

    it("splits fields into batches and combines their results", async () => {
      const fields = makeFields(21);
      const requestPromise = generateValuesForFields(fields);
      const batchSizes = [];

      for (let index = 0; index < 2; index++) {
        const { request, respond } = await mockEngineMan.captureRequest({
          purpose: PURPOSE,
        });
        const requestedFields = getRequestedFields(request);
        batchSizes.push(requestedFields.length);

        respond(
          JSON.stringify({
            memories_used: [],
            tabs_used: ["tab-1"],
            fields: requestedFields.map(({ id }) => ({
              id,
              action: "generate",
              confidence: "high",
              value: `Value for ${id}`,
            })),
          })
        );
      }

      const result = await requestPromise;

      Assert.deepEqual(
        batchSizes,
        [20, 1],
        "Fields should be split at the batch limit"
      );
      Assert.deepEqual(
        result.fields.map(({ id }) => id),
        fields.map(({ id }) => id),
        "Fields from every batch should be combined in request order"
      );
      Assert.deepEqual(
        result.tabs_used,
        ["tab-1"],
        "Tab IDs reported by multiple batches should be deduplicated"
      );
      Assert.deepEqual(
        result.batches,
        { total: 2, failed: 0 },
        "Batch metadata should report both successful requests"
      );
    });

    it("keeps successful results when another batch fails", async () => {
      const fields = makeFields(21);
      const requestPromise = generateValuesForFields(fields);
      const { request, respond } = await mockEngineMan.captureRequest({
        purpose: PURPOSE,
      });
      const successfulFields = getRequestedFields(request);

      respond(
        JSON.stringify({
          memories_used: [],
          tabs_used: [],
          fields: successfulFields.map(({ id }) => ({
            id,
            action: "generate",
            confidence: "high",
            value: `Value for ${id}`,
          })),
        })
      );

      await mockEngineMan.captureRequest({ purpose: PURPOSE });
      await mockEngineMan.rejectAllRequests();

      const result = await requestPromise;

      Assert.deepEqual(
        result.fields.map(({ id }) => id),
        successfulFields.map(({ id }) => id),
        "Successful batch results should be preserved"
      );
      Assert.deepEqual(
        result.batches,
        { total: 2, failed: 1 },
        "Batch metadata should report the failed request"
      );
    });

    it("limits concurrent value generation requests globally", async () => {
      const requestPromises = ["field-1", "field-2", "field-3"].map(id =>
        SmartFormFillModel.generateFormValues(makeRequest(id))
      );

      const engine = await TestUtils.waitForCondition(
        () => mockEngineMan.engines.get(PURPOSE),
        "Wait for the value generation engine"
      );
      await TestUtils.waitForCondition(
        () => engine.runRequests.size === 2,
        "Wait for two concurrent value generation requests"
      );
      await TestUtils.waitForTick();

      Assert.equal(
        engine.runRequests.size,
        2,
        "The third request remains queued"
      );

      const [firstRequestId] = engine.getNextRequest();
      engine.respond(
        firstRequestId,
        JSON.stringify({
          memories_used: [],
          tabs_used: [],
          fields: [],
        })
      );

      await TestUtils.waitForCondition(
        () => engine.runRequests.size === 2,
        "The queued request starts when a slot becomes available"
      );

      for (const requestId of [...engine.runRequests.keys()]) {
        engine.respond(
          requestId,
          JSON.stringify({
            memories_used: [],
            tabs_used: [],
            fields: [],
          })
        );
      }

      const results = await Promise.all(requestPromises);
      Assert.deepEqual(
        results.map(result => result.batches),
        [
          { total: 1, failed: 0 },
          { total: 1, failed: 0 },
          { total: 1, failed: 0 },
        ]
      );
    });
  });
});
