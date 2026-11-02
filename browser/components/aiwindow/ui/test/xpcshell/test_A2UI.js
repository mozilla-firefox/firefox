const { A2UI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/A2UI.mjs"
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
        a2uiData.dataModel.sources.push({ title: "route", href: "route-id" });
        a2uiData.components[2].items = [
          null,
          {
            eyebrow: "Verdict",
            body: "Eight of nine sources back dual-fuel.",
            sources: { items: { path: "/sources" } },
          },
        ];
        a2ui = new A2UI(a2uiData);

        Assert.deepEqual(a2ui.toUI().children[1], {
          id: "ans",
          component: "TextBlock",
          lead: "Highlights Title",
          paragraphs: ["Eight of nine sources back dual-fuel."],
          references: [
            { title: "site1 name", href: "https://www.site1.com" },
            { title: "site2 name", href: "https://www.site2.com" },
            { title: "site3 name", href: "https://www.site3.com" },
          ],
        });
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
          leadOf({ title: "Group" }, { eyebrow: "Monday", title: "Showers" }),
          "Showers",
          "The statement title comes first"
        );
        Assert.equal(
          leadOf({ title: "Group" }, { eyebrow: "Monday", body: "Rain." }),
          "Group",
          "Then the highlights title"
        );
        Assert.equal(
          leadOf({}, { eyebrow: "Monday", body: "Rain." }),
          "Monday",
          "Then the eyebrow"
        );
        Assert.equal(leadOf({}, { body: "Rain." }), "", "Else no lead");
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
