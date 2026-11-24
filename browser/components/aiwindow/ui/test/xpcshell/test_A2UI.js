const { A2UI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/A2UI.sys.mjs"
);

describe("A2UI", () => {
  let a2uiData, a2ui;

  describe("when there's no root", () => {
    beforeEach(() => {
      a2uiData = { components: [] };
      a2ui = new A2UI(a2uiData);
    });

    it("returns an empty page definition", () => {
      Assert.deepEqual(a2ui.toUI(), { component: "Page", children: [] });
    });
  });

  describe("when reporting conversions", () => {
    const surface = (...blocks) => ({
      components: [
        {
          id: "root",
          component: "Page",
          header: "hdr",
          children: blocks.map(block => block.id),
        },
        { id: "hdr", component: "Header", title: "Page Title" },
        ...blocks,
      ],
      dataModel: {},
    });
    const statement = { title: "A statement", body: "Its body." };
    const group = { items: [{ text: "An item" }] };

    it("reports nothing for a surface without a root", () => {
      Assert.deepEqual(new A2UI({ components: [] }).conversions(), []);
    });

    it("reports nothing for blocks that render as sent", () => {
      const conversions = new A2UI(
        surface(
          { id: "text", component: "TextBlock", lead: "A lead" },
          {
            id: "highlights",
            component: "Highlights",
            title: "Highlights Title",
            items: [statement, statement],
          },
          {
            id: "column",
            component: "List",
            layout: "column",
            groups: [group, group],
          },
          { id: "row", component: "List", layout: "row", groups: [group] }
        )
      ).conversions();

      Assert.deepEqual(conversions, []);
    });

    it("reports each converted block in render order", () => {
      const conversions = new A2UI(
        surface(
          {
            id: "single",
            component: "Highlights",
            title: "Highlights Title",
            items: [statement],
          },
          { id: "text", component: "TextBlock", lead: "A lead" },
          {
            id: "no-bodies",
            component: "Highlights",
            title: "Highlights Title",
            items: [{ title: "One" }, { title: "Two" }],
          },
          { id: "empty", component: "Highlights", items: [] },
          {
            id: "column",
            component: "List",
            layout: "column",
            groups: [group],
          },
          { id: "no-layout", component: "List", groups: [group] }
        )
      ).conversions();

      Assert.deepEqual(conversions, [
        "Highlights>TextBlock",
        "Highlights>List_row",
        "Highlights>none",
        "List_column>List_row",
        "List>List_row",
      ]);
    });

    it("reports each converted instance of a template", () => {
      const a2uiSurface = surface();
      a2uiSurface.components[0].children = {
        componentId: "template",
        path: "/sections",
      };
      a2uiSurface.components.push({
        id: "template",
        component: "Highlights",
        title: "Highlights Title",
        items: { path: "items" },
      });
      a2uiSurface.dataModel = {
        sections: [
          { items: [statement] },
          { items: [statement, statement] },
          { items: [] },
        ],
      };

      Assert.deepEqual(new A2UI(a2uiSurface).conversions(), [
        "Highlights>TextBlock",
        "Highlights>none",
      ]);
    });

    it("reports the same conversions on each call", () => {
      const instance = new A2UI(
        surface({
          id: "single",
          component: "Highlights",
          title: "Highlights Title",
          items: [statement],
        })
      );

      instance.toUI();
      Assert.deepEqual(instance.conversions(), ["Highlights>TextBlock"]);
      Assert.deepEqual(instance.conversions(), ["Highlights>TextBlock"]);
    });
  });

  describe("when converting a surface to UI more than once", () => {
    beforeEach(() => {
      a2uiData = {
        components: [
          {
            id: "root",
            component: "RootComponent",
            header: "hdr",
            children: ["plan"],
          },
          {
            id: "hdr",
            component: "Header",
            title: { path: "/pageTitle" },
          },
          {
            id: "plan",
            component: "Timeline",
            items: { path: "/timelineItems" },
          },
        ],
        dataModel: {
          pageTitle: "Page Title",
          timelineItems: [
            {
              date_label: "Week 1",
              title: "First task",
            },
          ],
        },
      };
      a2ui = new A2UI(a2uiData);
    });

    it("does not mutate the source or share objects between results", () => {
      const source = structuredClone(a2uiData);
      const first = a2ui.toUI();

      Assert.deepEqual(
        a2uiData,
        source,
        "The source remains unchanged after hydration"
      );

      first.children[0].title = "Modified title";
      first.children[1].items[0].title = "Modified task";

      Assert.deepEqual(
        a2uiData,
        source,
        "Modifying the result does not mutate the source"
      );

      Assert.deepEqual(a2ui.toUI(), {
        component: "RootComponent",
        children: [
          {
            id: "hdr",
            component: "Header",
            title: "Page Title",
          },
          {
            id: "plan",
            component: "Timeline",
            items: [
              {
                date_label: "Week 1",
                title: "First task",
              },
            ],
          },
        ],
      });
    });
  });

  describe("when there's a root with header", () => {
    beforeEach(() => {
      a2uiData = {
        components: [
          {
            id: "root",
            component: "RootComponent",
            header: "hdr",
            children: [],
          },
          {
            id: "hdr",
            component: "Header",
            title: "Page Title",
            subhead: "A subheading",
          },
        ],
      };
      a2ui = new A2UI(a2uiData);
    });

    it("returns a page definition defining the root component name", () => {
      Assert.deepEqual(a2ui.toUI(), {
        component: "RootComponent",
        children: [
          {
            id: "hdr",
            component: "Header",
            title: "Page Title",
            subhead: "A subheading",
          },
        ],
      });
    });

    describe("when the header has data bindings", () => {
      beforeEach(() => {
        a2uiData.components[1].eyebrow = { path: "/headerEyebrow" };
        a2uiData.components[1].title = { path: "/header~1title" };
        a2uiData.components[1].subhead = { path: "/header~0subhead" };
        a2uiData.dataModel = {
          headerEyebrow: "",
          "header/title": "Bound page title",
          "header~subhead": "Bound page subhead",
        };

        a2ui = new A2UI(a2uiData);
      });

      it("resolves the header strings", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              eyebrow: "",
              title: "Bound page title",
              subhead: "Bound page subhead",
            },
          ],
        });
      });
    });

    describe("when the header has sources references", () => {
      beforeEach(() => {
        a2uiData.components[1].references = {
          title: "Some Links",
          items: { path: "/sources" },
        };
        a2uiData.dataModel = {
          sources: [
            { title: "site1 name", href: "https://www.site1.com" },
            { title: "site2 name", href: "https://www.site2.com" },
            { title: "site3 name", href: "https://www.site3.com" },
          ],
        };

        a2ui = new A2UI(a2uiData);
      });

      it("resolves the sources references and adds a sources property", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
              references: {
                title: "Some Links",
                items: [
                  { title: "site1 name", href: "https://www.site1.com" },
                  { title: "site2 name", href: "https://www.site2.com" },
                  { title: "site3 name", href: "https://www.site3.com" },
                ],
              },
            },
          ],
        });
      });
    });

    describe("when root children is a template repeater", () => {
      beforeEach(() => {
        a2uiData.components[0].children = {
          componentId: "timeline-template",
          path: "/timelines",
        };
        a2uiData.components.push({
          id: "timeline-template",
          component: "Timeline",
          title: { path: "/timelineTitle" },
          description: { path: "description" },
          items: { path: "items" },
        });
        a2uiData.dataModel = {
          timelineTitle: "Project timeline",
          timelines: [
            {
              description: "First timeline",
              items: [{ date_label: "Week 1", title: "First task" }],
            },
            {
              description: "Second timeline",
              items: [{ date_label: "Week 2", title: "Second task" }],
            },
          ],
        };

        a2ui = new A2UI(a2uiData);
      });

      it("hydrates one template instance per bound item", () => {
        const template = structuredClone(a2uiData.components[2]);
        const children = a2ui.toUI().children.slice(1);

        Assert.deepEqual(children, [
          {
            id: "timeline-template",
            component: "Timeline",
            title: "Project timeline",
            description: "First timeline",
            items: [{ date_label: "Week 1", title: "First task" }],
          },
          {
            id: "timeline-template",
            component: "Timeline",
            title: "Project timeline",
            description: "Second timeline",
            items: [{ date_label: "Week 2", title: "Second task" }],
          },
        ]);
        Assert.deepEqual(
          a2uiData.components[2],
          template,
          "The component template remains unchanged"
        );
      });
    });

    describe("when a child is a Highlights component", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("ans");
        a2uiData.components.push({
          id: "ans",
          component: "Highlights",
          title: "Highlights Title",
          items: [
            {
              eyebrow: "Verdict",
              title:
                "A cold-climate heat pump works here — paired with the furnace for the coldest nights.",
              body: "Eight of nine sources back dual-fuel for 1940s retrofits.",
              sources: { items: { path: "/sources" } },
            },
            {
              eyebrow: "Bigger lever",
              title: "Air sealing matters more than which model you pick.",
              body: "Envelope work moves the payback further than the equipment on the invoice.",
            },
          ],
        });

        a2uiData.dataModel = {
          sources: [
            { title: "site1 name", href: "https://www.site1.com" },
            { title: "site2 name", href: "https://www.site2.com" },
            { title: "site3 name", href: "https://www.site3.com" },
          ],
        };

        a2ui = new A2UI(a2uiData);
      });

      it("resolves highlights", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "ans",
              component: "Highlights",
              title: "Highlights Title",
              items: [
                {
                  eyebrow: "Verdict",
                  title:
                    "A cold-climate heat pump works here — paired with the furnace for the coldest nights.",
                  body: "Eight of nine sources back dual-fuel for 1940s retrofits.",
                  sources: {
                    items: [
                      { title: "site1 name", href: "https://www.site1.com" },
                      { title: "site2 name", href: "https://www.site2.com" },
                      { title: "site3 name", href: "https://www.site3.com" },
                    ],
                  },
                },
                {
                  eyebrow: "Bigger lever",
                  title: "Air sealing matters more than which model you pick.",
                  body: "Envelope work moves the payback further than the equipment on the invoice.",
                },
              ],
            },
          ],
        });
      });

      it("resolves nested bindings in bound highlight items", () => {
        a2uiData.dataModel.highlightItems = a2uiData.components[2].items;
        a2uiData.components[2].items = { path: "/highlightItems" };
        a2ui = new A2UI(a2uiData);

        const highlightItems = a2ui.toUI().children[1].items;
        Assert.deepEqual(
          highlightItems[0].sources.items,
          a2uiData.dataModel.sources
        );
      });

      it("keeps bound Highlights images when every statement has an image", () => {
        const image = {
          src: "https://example.com/highlight.png",
          alt_text: "Highlight image",
        };
        a2uiData.components[2].items[0].image = image;
        a2uiData.components[2].items[1].image = image;
        a2uiData.dataModel.highlightItems = a2uiData.components[2].items;
        a2uiData.components[2].items = { path: "/highlightItems" };
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(
          a2ui.toUI().children[1].items.map(item => item.image),
          [image, image],
          "every bound Highlights image is preserved"
        );
      });

      it("drops a highlights block with no statements", () => {
        a2uiData.dataModel.highlightItems = [null];
        a2uiData.components[2].items = { path: "/highlightItems" };
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(
          a2ui.toUI().children.map(child => child.id),
          ["hdr"]
        );
      });

      it("turns a single statement into a text block", () => {
        const image = {
          src: "https://example.com/highlight.png",
          alt_text: "Highlight image",
        };
        a2uiData.dataModel.sources.push({ title: "route", href: "route-id" });
        a2uiData.components[2].items = [
          null,
          {
            eyebrow: "Verdict",
            title: "Dual-fuel is the safe pick.",
            body: "Eight of nine sources back dual-fuel.",
            image,
            sources: { items: { path: "/sources" } },
          },
        ];
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(a2ui.toUI().children[1], {
          id: "ans",
          component: "TextBlock",
          lead: "Dual-fuel is the safe pick.",
          image,
          paragraphs: ["Eight of nine sources back dual-fuel."],
          references: [
            { title: "site1 name", href: "https://www.site1.com" },
            { title: "site2 name", href: "https://www.site2.com" },
            { title: "site3 name", href: "https://www.site3.com" },
          ],
        });
      });

      it("turns untitled statements into one text block", () => {
        delete a2uiData.components[2].title;
        a2uiData.components[2].items[1].sources = {
          items: [
            { title: "site1 again", href: "https://www.site1.com" },
            { title: "site4 name", href: "https://www.site4.com" },
          ],
        };
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(a2ui.toUI().children[1], {
          id: "ans",
          component: "TextBlock",
          lead: "",
          paragraphs: [
            "A cold-climate heat pump works here — paired with the furnace for the coldest nights.",
            "Eight of nine sources back dual-fuel for 1940s retrofits.",
            "Air sealing matters more than which model you pick.",
            "Envelope work moves the payback further than the equipment on the invoice.",
          ],
          references: [
            ...a2uiData.dataModel.sources,
            { title: "site4 name", href: "https://www.site4.com" },
          ],
        });
      });

      it("turns statements without bodies into a list", () => {
        for (const item of a2uiData.components[2].items) {
          delete item.body;
        }
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(a2ui.toUI().children[1], {
          id: "ans",
          component: "List",
          title: "Highlights Title",
          groups: [
            {
              items: [
                {
                  text: "A cold-climate heat pump works here — paired with the furnace for the coldest nights.",
                },
                { text: "Air sealing matters more than which model you pick." },
              ],
            },
          ],
          layout: "row",
        });
      });

      it("turns untitled statements without bodies into one text block", () => {
        delete a2uiData.components[2].title;
        for (const item of a2uiData.components[2].items) {
          delete item.body;
        }
        a2ui = new A2UI(a2uiData);

        const block = a2ui.toUI().children[1];
        Assert.equal(block.component, "TextBlock");
        Assert.deepEqual(block.paragraphs, [
          "A cold-climate heat pump works here — paired with the furnace for the coldest nights.",
          "Air sealing matters more than which model you pick.",
        ]);
      });

      it("turns statements without titles into a list", () => {
        for (const item of a2uiData.components[2].items) {
          delete item.title;
        }
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(a2ui.toUI().children[1], {
          id: "ans",
          component: "List",
          title: "Highlights Title",
          groups: [
            {
              items: a2uiData.components[2].items.map(item => ({
                text: item.body,
              })),
            },
          ],
          layout: "row",
        });
      });

      it("lists statements missing a title or a body after the others", () => {
        const { items } = a2uiData.components[2];
        delete items[0].body;
        items.push(
          { eyebrow: "Rebates", body: "Federal and utility rebates stack." },
          { eyebrow: "Sources only" }
        );
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(a2ui.toUI().children[1], {
          id: "ans",
          component: "List",
          title: "Highlights Title",
          groups: [
            {
              heading: "Air sealing matters more than which model you pick.",
              items: [
                {
                  text: "Envelope work moves the payback further than the equipment on the invoice.",
                },
              ],
            },
            {
              items: [
                {
                  text: "A cold-climate heat pump works here — paired with the furnace for the coldest nights.",
                },
                { text: "Federal and utility rebates stack." },
              ],
            },
          ],
          layout: "row",
        });
      });

      it("turns a single statement without a title into a list", () => {
        a2uiData.components[2].items = [
          {
            eyebrow: "Verdict",
            body: "Eight of nine sources back dual-fuel.",
            sources: { items: { path: "/sources" } },
          },
        ];
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(a2ui.toUI().children[1], {
          id: "ans",
          component: "List",
          title: "Highlights Title",
          groups: [
            { items: [{ text: "Eight of nine sources back dual-fuel." }] },
          ],
          layout: "row",
        });
      });

      it("drops a highlights block whose statements have no text", () => {
        a2uiData.components[2].items = [
          { eyebrow: "Verdict" },
          { sources: { items: { path: "/sources" } } },
        ];
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(
          a2ui.toUI().children.map(child => child.id),
          ["hdr"]
        );
      });

      it("takes the text block lead from the first title it finds", () => {
        const leadOf = (highlights, item) => {
          a2uiData.components[2] = {
            id: "ans",
            component: "Highlights",
            ...highlights,
            items: [item],
          };
          return new A2UI(a2uiData).toUI().children[1].lead;
        };

        Assert.equal(
          leadOf(
            { title: "Group" },
            { eyebrow: "Monday", title: "Showers", body: "Rain." }
          ),
          "Showers",
          "The statement title comes first"
        );
        Assert.equal(
          leadOf({}, { eyebrow: "Monday", body: "Rain." }),
          "Monday",
          "Then the eyebrow"
        );
        Assert.equal(leadOf({}, { body: "Rain." }), "", "Else no lead");
      });
    });

    describe("when there is a text block with an image", () => {
      let image;

      beforeEach(() => {
        image = {
          src: "https://example.com/image.png",
          alt_text: "Example image",
        };
        a2uiData.components[0].children.push("note");
        a2uiData.components.push({
          id: "note",
          component: "TextBlock",
          lead: "The lead sentence",
          image,
        });
        a2uiData.dataModel = { image };
      });

      it("preserves an inline image", () => {
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(a2ui.toUI().children[1].image, image);
      });

      it("resolves an absolute image binding", () => {
        a2uiData.components[2].image = { path: "/image" };
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(a2ui.toUI().children[1].image, image);
      });

      it("resolves a relative image binding in a repeated component", () => {
        const secondImage = {
          src: "https://example.com/second.png",
          alt_text: "Second image",
        };
        a2uiData.components[0].children = {
          componentId: "note",
          path: "/entries",
        };
        a2uiData.components[2].image = { path: "image" };
        a2uiData.dataModel = {
          entries: [{ image }, { image: secondImage }],
        };
        a2ui = new A2UI(a2uiData);

        const children = a2ui.toUI().children;
        Assert.deepEqual(children[1].image, image);
        Assert.deepEqual(children[2].image, secondImage);
      });
    });

    describe("when there is a text block with a string lead", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("note");
        a2uiData.components.push({
          id: "note",
          component: "TextBlock",
          lead: "The lead sentence",
        });

        a2ui = new A2UI(a2uiData);
      });

      it("resolves text blocks with leads", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "note",
              component: "TextBlock",
              lead: "The lead sentence",
            },
          ],
        });
      });
    });

    describe("when there is a text block with a lead reference", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("note");
        a2uiData.components.push({
          id: "note",
          component: "TextBlock",
          lead: { path: "/lead" },
        });

        a2uiData.dataModel = {
          lead: "A lead sentence reference",
        };

        a2ui = new A2UI(a2uiData);
      });

      it("resolves text blocks with leads", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "note",
              component: "TextBlock",
              lead: "A lead sentence reference",
            },
          ],
        });
      });
    });

    describe("when there is a text block with a paragraphs array", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("note");
        a2uiData.components.push({
          id: "note",
          component: "TextBlock",
          paragraphs: ["one paragraph", "two paragraph"],
        });

        a2ui = new A2UI(a2uiData);
      });

      it("resolves text blocks with paragraph arrays", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "note",
              component: "TextBlock",
              paragraphs: ["one paragraph", "two paragraph"],
            },
          ],
        });
      });
    });

    describe("when there is a text block with a paragraphs data binding", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("note");
        a2uiData.components.push({
          id: "note",
          component: "TextBlock",
          paragraphs: { path: "/someparagraph" },
        });

        a2uiData.dataModel = {
          someparagraph: ["first p", "second p"],
        };

        a2ui = new A2UI(a2uiData);
      });

      it("resolves text blocks with paragraph arrays", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "note",
              component: "TextBlock",
              paragraphs: ["first p", "second p"],
            },
          ],
        });
      });
    });

    describe("when there is a ranked table", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("quotes");
        a2uiData.components.push({
          id: "quotes",
          component: "RankedTable",
          title: { path: "/table" },
          description: { path: "/tabledesc" },
          columns: [
            { key: "name", label: "Installer", type: "text", role: "title" },
            { key: "source", label: "", type: "text", role: "subtitle" },
            {
              key: "price",
              label: "Quote",
              type: "currency",
              role: "detail",
              goal: "min",
            },
            {
              key: "rating",
              label: "Rating",
              type: "rating",
              role: "detail",
              max: 5,
              goal: "max",
            },
          ],
          rows: { path: "/quotes" },
        });

        a2uiData.dataModel = {
          table: "The table title",
          tabledesc: "Bids pulled from the PDFs in your tabs",
          quotes: [
            {
              name: "Bright Build",
              source: "yelp.com",
              price: 31200,
              rating: 4.6,
            },
            {
              name: "Sterling Mechanical",
              source: "google.com",
              price: 37800,
              rating: 4.8,
            },
            {
              name: "Northwest Comfort",
              source: "angi.com",
              price: 34000,
              rating: 4.2,
            },
          ],
        };

        a2ui = new A2UI(a2uiData);
      });

      it("resolves text blocks with paragraph arrays", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "quotes",
              component: "RankedTable",
              title: "The table title",
              description: "Bids pulled from the PDFs in your tabs",
              columns: [
                {
                  key: "name",
                  label: "Installer",
                  type: "text",
                  role: "title",
                },
                { key: "source", label: "", type: "text", role: "subtitle" },
                {
                  key: "price",
                  label: "Quote",
                  type: "currency",
                  role: "detail",
                  goal: "min",
                },
                {
                  key: "rating",
                  label: "Rating",
                  type: "rating",
                  role: "detail",
                  max: 5,
                  goal: "max",
                },
              ],
              rows: [
                {
                  name: "Bright Build",
                  source: "yelp.com",
                  price: 31200,
                  rating: 4.6,
                },
                {
                  name: "Sterling Mechanical",
                  source: "google.com",
                  price: 37800,
                  rating: 4.8,
                },
                {
                  name: "Northwest Comfort",
                  source: "angi.com",
                  price: 34000,
                  rating: 4.2,
                },
              ],
            },
          ],
        });
      });

      it("keeps bound RankedTable images when every row has an image", () => {
        const image = {
          src: "https://example.com/installer.png",
          alt_text: "Installer image",
        };
        a2uiData.dataModel.quotes[0].image = image;
        a2uiData.dataModel.quotes[1].image = image;
        a2uiData.dataModel.quotes[2].image = image;
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(
          a2ui.toUI().children[1].rows.map(row => row.image),
          [image, image, image],
          "every bound RankedTable image is preserved"
        );
      });

      it("keeps literal rows as is", () => {
        const rows = a2uiData.dataModel.quotes;
        a2uiData.components[2].rows = rows;
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(a2ui.toUI().children[1].rows, rows);
      });
    });

    describe("when there is a cards component with items as a reference", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("picks");
        a2uiData.components.push({
          id: "picks",
          component: "Cards",
          title: { path: "/cardtitle" },
          items: { path: "/carditems" },
        });

        a2uiData.dataModel = {
          cardtitle: "The cards title",
          carditems: [
            {
              eyebrow: "Recommended",
              title: "Cold-climate heat pump + keep the furnace",
              link_title: "Spec at NEEP",
              href: "https://neep.org",
            },
            {
              eyebrow: "Simpler",
              title: "Full electric, no backup",
              link_title: "Sizing guide",
              href: "https://energy.gov",
            },
          ],
        };

        a2ui = new A2UI(a2uiData);
      });

      it("resolves the card's items", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "picks",
              component: "Cards",
              title: "The cards title",
              items: [
                {
                  eyebrow: "Recommended",
                  title: "Cold-climate heat pump + keep the furnace",
                  link_title: "Spec at NEEP",
                  href: "https://neep.org",
                },
                {
                  eyebrow: "Simpler",
                  title: "Full electric, no backup",
                  link_title: "Sizing guide",
                  href: "https://energy.gov",
                },
              ],
            },
          ],
        });
      });
    });

    describe("when there is a cards component with items array and title string", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("picks");
        a2uiData.components.push({
          id: "picks",
          component: "Cards",
          title: "Cards title",
          items: [
            {
              eyebrow: "Recommended",
              title: "Cold-climate heat pump + keep the furnace",
              link_title: "Spec at NEEP",
              href: "https://neep.org",
            },
            {
              eyebrow: "Simpler",
              title: "Full electric, no backup",
              link_title: "Sizing guide",
              href: "https://energy.gov",
            },
          ],
        });

        a2ui = new A2UI(a2uiData);
      });

      it("resolves the card's as is", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "picks",
              component: "Cards",
              title: "Cards title",
              items: [
                {
                  eyebrow: "Recommended",
                  title: "Cold-climate heat pump + keep the furnace",
                  link_title: "Spec at NEEP",
                  href: "https://neep.org",
                },
                {
                  eyebrow: "Simpler",
                  title: "Full electric, no backup",
                  link_title: "Sizing guide",
                  href: "https://energy.gov",
                },
              ],
            },
          ],
        });
      });
    });

    describe("when there is a timeline component with title, description, and items references", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("plan");
        a2uiData.components.push({
          id: "plan",
          component: "Timeline",
          title: { path: "/timelinetitle" },
          description: { path: "/timelinedesc" },
          items: { path: "/timelineitems" },
        });

        a2uiData.dataModel = {
          timelinetitle: "The timeline title",
          timelinedesc: "The timeline description",
          timelineitems: [
            {
              date_label: "Week 1",
              date_eyebrow: "Now",
              title: "Manual J + two quotes",
              description:
                "Size to the measured load, not the old furnace's rating.",
            },
            {
              date_label: "Week 3",
              title: "Permit & order equipment",
              description: "City permit filed; account for lead time.",
            },
            {
              date_label: "Week 6",
              date_eyebrow: "Done",
              title: "Install & inspect",
            },
          ],
        };

        a2ui = new A2UI(a2uiData);
      });

      it("keeps bound Timeline images when every entry has an image", () => {
        const image = {
          src: "https://example.com/event.png",
          alt_text: "Event image",
        };
        a2uiData.dataModel.timelineitems[0].image = image;
        a2uiData.dataModel.timelineitems[1].image = image;
        a2uiData.dataModel.timelineitems[2].image = image;
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(a2ui.toUI().children[1].items[0].image, image);
      });

      it("resolves timeline blocks with items array, and title and descriptions", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "plan",
              component: "Timeline",
              title: "The timeline title",
              description: "The timeline description",
              items: [
                {
                  date_label: "Week 1",
                  date_eyebrow: "Now",
                  title: "Manual J + two quotes",
                  description:
                    "Size to the measured load, not the old furnace's rating.",
                },
                {
                  date_label: "Week 3",
                  title: "Permit & order equipment",
                  description: "City permit filed; account for lead time.",
                },
                {
                  date_label: "Week 6",
                  date_eyebrow: "Done",
                  title: "Install & inspect",
                },
              ],
            },
          ],
        });
      });
    });

    describe("when there is a timeline component with title and description strings and items array", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("plan");
        a2uiData.components.push({
          id: "plan",
          component: "Timeline",
          title: "The timeline title",
          description: "The timeline description",
          items: [
            {
              date_label: "Week 1",
              date_eyebrow: "Now",
              title: "Manual J + two quotes",
              description:
                "Size to the measured load, not the old furnace's rating.",
            },
            {
              date_label: "Week 3",
              title: "Permit & order equipment",
              description: "City permit filed; account for lead time.",
            },
            {
              date_label: "Week 6",
              date_eyebrow: "Done",
              title: "Install & inspect",
            },
          ],
        });

        a2ui = new A2UI(a2uiData);
      });

      it("resolves timeline blocks with items array, and title and descriptions", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "plan",
              component: "Timeline",
              title: "The timeline title",
              description: "The timeline description",
              items: [
                {
                  date_label: "Week 1",
                  date_eyebrow: "Now",
                  title: "Manual J + two quotes",
                  description:
                    "Size to the measured load, not the old furnace's rating.",
                },
                {
                  date_label: "Week 3",
                  title: "Permit & order equipment",
                  description: "City permit filed; account for lead time.",
                },
                {
                  date_label: "Week 6",
                  date_eyebrow: "Done",
                  title: "Install & inspect",
                },
              ],
            },
          ],
        });
      });
    });

    describe("when there is a List component with title and description references and groups data binding", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("todo");
        a2uiData.components.push({
          id: "todo",
          component: "List",
          title: { path: "/listtitle" },
          description: { path: "/listdesc" },
          groups: { path: "/listitems" },
        });

        a2uiData.dataModel = {
          listtitle: "The list title",
          listdesc: "The list description",
          listitems: [
            {
              heading: "This week",
              items: [
                { text: "Get a Manual J load calculation" },
                { text: "Confirm the electric-to-gas price ratio" },
              ],
            },
            {
              heading: "Before signing",
              items: [
                {
                  text: "Ask each installer about their cold-climate model line",
                },
                { text: "Check rebate eligibility" },
              ],
            },
          ],
        };

        a2ui = new A2UI(a2uiData);
      });

      it("resolves the List title, description and groups", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "todo",
              component: "List",
              title: "The list title",
              description: "The list description",
              groups: [
                {
                  heading: "This week",
                  items: [
                    { text: "Get a Manual J load calculation" },
                    { text: "Confirm the electric-to-gas price ratio" },
                  ],
                },
                {
                  heading: "Before signing",
                  items: [
                    {
                      text: "Ask each installer about their cold-climate model line",
                    },
                    { text: "Check rebate eligibility" },
                  ],
                },
              ],
            },
          ],
        });
      });
    });

    describe("when there is a List component with title and description strings and groups array", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("todo");
        a2uiData.components.push({
          id: "todo",
          component: "List",
          title: "The list title",
          description: "The list description",
          groups: [
            {
              heading: "This week",
              items: [
                { text: "Get a Manual J load calculation" },
                { text: "Confirm the electric-to-gas price ratio" },
              ],
            },
            {
              heading: "Before signing",
              items: [
                {
                  text: "Ask each installer about their cold-climate model line",
                },
                { text: "Check rebate eligibility" },
              ],
            },
          ],
        });

        a2ui = new A2UI(a2uiData);
      });

      it("lays out a single group without a description as a row", () => {
        const layoutOf = list => {
          a2uiData.components[2] = {
            ...a2uiData.components[2],
            ...list,
          };
          return new A2UI(a2uiData).toUI().children[1].layout;
        };
        const { groups } = a2uiData.components[2];
        const [group] = groups;

        Assert.equal(
          layoutOf({ description: undefined, groups: [group] }),
          "row",
          "A single group without a description is laid out as a row"
        );
        Assert.equal(
          layoutOf({
            description: undefined,
            groups: [group, { heading: "Empty", items: [] }],
            layout: "column",
          }),
          "row",
          "Empty groups are not counted"
        );
        Assert.equal(
          layoutOf({ description: "A description", layout: "column" }),
          "column",
          "A description keeps the model's layout"
        );
        Assert.equal(
          layoutOf({
            description: undefined,
            groups,
            layout: "column",
          }),
          "column",
          "Several groups keep the model's layout"
        );
      });

      it("resolves the List title, description and groups as is", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "todo",
              component: "List",
              title: "The list title",
              description: "The list description",
              groups: [
                {
                  heading: "This week",
                  items: [
                    { text: "Get a Manual J load calculation" },
                    { text: "Confirm the electric-to-gas price ratio" },
                  ],
                },
                {
                  heading: "Before signing",
                  items: [
                    {
                      text: "Ask each installer about their cold-climate model line",
                    },
                    { text: "Check rebate eligibility" },
                  ],
                },
              ],
            },
          ],
        });
      });
    });

    describe("when there is a SourceLinks component with reference title and items data binding", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("srcs");
        a2uiData.components.push({
          id: "srcs",
          component: "SourceLinks",
          title: { path: "/sourcelinkstitle" },
          items: { path: "/sourcelinksitems" },
        });

        a2uiData.dataModel = {
          sourcelinkstitle: "The sourcelinks title",
          sourcelinksitems: [
            { title: "energy.gov", href: "https://energy.gov" },
            { title: "NEEP", href: "https://neep.org" },
            { title: "r/heatpumps", href: "https://reddit.com/r/heatpumps" },
          ],
        };

        a2ui = new A2UI(a2uiData);
      });

      it("resolves the soucelinks component title and items", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "srcs",
              component: "SourceLinks",
              title: "The sourcelinks title",
              items: [
                { title: "energy.gov", href: "https://energy.gov" },
                { title: "NEEP", href: "https://neep.org" },
                {
                  title: "r/heatpumps",
                  href: "https://reddit.com/r/heatpumps",
                },
              ],
            },
          ],
        });
      });
    });

    describe("when there is a SourceLinks component with title string and items array", () => {
      beforeEach(() => {
        a2uiData.components[0].children.push("srcs");
        a2uiData.components.push({
          id: "srcs",
          component: "SourceLinks",
          title: "The sourcelinks title",
          items: [
            { title: "energy.gov", href: "https://energy.gov" },
            { title: "NEEP", href: "https://neep.org" },
            { title: "r/heatpumps", href: "https://reddit.com/r/heatpumps" },
          ],
        });

        a2ui = new A2UI(a2uiData);
      });

      it("resolves the soucelinks component title and items", () => {
        Assert.deepEqual(a2ui.toUI(), {
          component: "RootComponent",
          children: [
            {
              id: "hdr",
              component: "Header",
              title: "Page Title",
              subhead: "A subheading",
            },
            {
              id: "srcs",
              component: "SourceLinks",
              title: "The sourcelinks title",
              items: [
                { title: "energy.gov", href: "https://energy.gov" },
                { title: "NEEP", href: "https://neep.org" },
                {
                  title: "r/heatpumps",
                  href: "https://reddit.com/r/heatpumps",
                },
              ],
            },
          ],
        });
      });
    });
  });
});

describe("A2UI image policies", () => {
  const image = { src: "https://example.com/image.png" };

  function renderComponents(components, dataModel = {}) {
    return new A2UI({
      components: [
        {
          id: "root",
          component: "Page",
          children: components.map(component => component.id),
        },
        ...components,
      ],
      dataModel,
    }).toUI().children;
  }

  it("applies independent and all-or-none image policies across component types", () => {
    const rendered = renderComponents([
      {
        id: "illustrated",
        component: "TextBlock",
        lead: "Paris",
        image,
      },
      {
        id: "plain",
        component: "TextBlock",
        lead: "Rome",
      },
      {
        id: "table",
        component: "RankedTable",
        rows: [{ name: "Paris", image }, { name: "Rome" }],
      },
      {
        id: "timeline",
        component: "Timeline",
        items: [
          { date_label: "Monday", title: "Paris", image },
          { date_label: "Tuesday", title: "Rome" },
        ],
      },
      {
        id: "highlights",
        component: "Highlights",
        title: "City highlights",
        items: [
          { title: "Paris", body: "See the Louvre.", image },
          { title: "Rome", body: "See the Colosseum." },
        ],
      },
    ]);
    const [illustrated, plain, table, timeline, highlights] = rendered;

    Assert.deepEqual(illustrated.image, image);
    Assert.ok(!Object.hasOwn(plain, "image"));
    Assert.ok(table.rows.every(row => !Object.hasOwn(row, "image")));
    Assert.ok(timeline.items.every(item => !Object.hasOwn(item, "image")));
    Assert.ok(highlights.items.every(item => !Object.hasOwn(item, "image")));
  });

  it("hides every RankedTable row image when one row has no image", () => {
    const [table] = renderComponents([
      {
        id: "table",
        component: "RankedTable",
        rows: [{ name: "Paris", image }, { name: "Rome" }],
      },
    ]);

    Assert.ok(table.rows.every(row => !Object.hasOwn(row, "image")));
  });

  it("keeps another table and TextBlock images when a table shares their image data", () => {
    const sharedRow = { name: "Paris", image };
    const dataModel = {
      mixedRows: [sharedRow, { name: "Rome" }],
      completeRows: [sharedRow],
      hero: image,
    };
    const original = structuredClone(dataModel);
    const [mixed, complete, text] = renderComponents(
      [
        { id: "mixed", component: "RankedTable", rows: { path: "/mixedRows" } },
        {
          id: "complete",
          component: "RankedTable",
          rows: { path: "/completeRows" },
        },
        {
          id: "text",
          component: "TextBlock",
          lead: "Paris",
          image: { path: "/hero" },
        },
      ],
      dataModel
    );

    Assert.ok(mixed.rows.every(row => !Object.hasOwn(row, "image")));
    Assert.deepEqual(complete.rows[0].image, image);
    Assert.deepEqual(text.image, image);
    Assert.deepEqual(dataModel, original, "the source data is unchanged");
  });

  it("applies RankedTable image consistency separately to each repeater instance", () => {
    const children = new A2UI({
      components: [
        {
          id: "root",
          component: "Page",
          children: { componentId: "table", path: "/tables" },
        },
        { id: "table", component: "RankedTable", rows: { path: "rows" } },
      ],
      dataModel: {
        tables: [
          { rows: [{ name: "Paris", image }, { name: "Rome" }] },
          {
            rows: [
              { name: "London", image },
              { name: "York", image },
            ],
          },
        ],
      },
    }).toUI().children;

    Assert.ok(children[0].rows.every(row => !Object.hasOwn(row, "image")));
    Assert.deepEqual(children[1].rows[0].image, image);
    Assert.deepEqual(children[1].rows[1].image, image);
  });

  it("hides every Timeline image when one entry has no image", () => {
    const [timeline] = renderComponents([
      {
        id: "timeline",
        component: "Timeline",
        items: [
          { date_label: "Monday", title: "Paris", image },
          { date_label: "Tuesday", title: "Rome" },
        ],
      },
    ]);

    Assert.ok(timeline.items.every(item => !Object.hasOwn(item, "image")));
  });

  it("keeps every Timeline image when all entries have images", () => {
    const items = [
      { date_label: "Monday", title: "Paris", image },
      { date_label: "Tuesday", title: "Rome", image },
    ];
    const [timeline] = renderComponents([
      { id: "timeline", component: "Timeline", items },
    ]);

    Assert.deepEqual(timeline.items, items);
  });

  it("hides every Highlights image when one statement has no image", () => {
    const [highlights] = renderComponents([
      {
        id: "highlights",
        component: "Highlights",
        title: "City highlights",
        items: [
          { title: "Paris", body: "See the Louvre.", image },
          { title: "Rome", body: "See the Colosseum." },
        ],
      },
    ]);

    Assert.ok(highlights.items.every(item => !Object.hasOwn(item, "image")));
  });
});
