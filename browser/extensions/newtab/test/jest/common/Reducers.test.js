/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";

const {
  TopSites,
  App,
  Prefs,
  Dialog,
  Sections,
  Pocket,
  DiscoveryStream,
  Search,
  WebNotifications,
  ExternalComponents,
  PictureOfTheDay,
  FramedWidgets,
} = reducers;

describe("Reducers", () => {
  describe("FramedWidgets", () => {
    it("replaces the state on FRAMED_WIDGETS_UPDATE", () => {
      const data = { travel: { url: "https://a.example/" } };
      expect(
        FramedWidgets(undefined, { type: at.FRAMED_WIDGETS_UPDATE, data })
      ).toEqual(data);
    });

    it("marks only the widget given by FRAMED_WIDGET_UNAVAILABLE", () => {
      const state = {
        travel: { url: "https://a.example/" },
        crossword: { url: "https://b.example/" },
      };
      expect(
        FramedWidgets(state, {
          type: at.FRAMED_WIDGET_UNAVAILABLE,
          data: { id: "travel" },
        })
      ).toEqual({
        travel: { unavailable: true },
        crossword: { url: "https://b.example/" },
      });
    });
  });

  describe("App", () => {
    it("should return the initial state", () => {
      const nextState = App(undefined, { type: "FOO" });
      expect(nextState).toBe(INITIAL_STATE.App);
    });
    it("should set initialized to true on INIT", () => {
      const nextState = App(undefined, { type: "INIT" });

      expect(nextState.initialized).toBe(true);
    });
    it("should show the customize panel on SHOW_PERSONALIZE", () => {
      const nextState = App(undefined, { type: at.SHOW_PERSONALIZE });

      expect(nextState.customizeMenuVisible).toBe(true);
      expect(nextState.customizePanelWallpaperCategory).toBeNull();
    });
    it("should store the deep-linked wallpaper category on SHOW_PERSONALIZE", () => {
      const nextState = App(undefined, {
        type: at.SHOW_PERSONALIZE,
        data: { wallpaperCategory: "firefox" },
      });

      expect(nextState.customizePanelWallpaperCategory).toBe("firefox");
    });
    it("should store the open source on SHOW_PERSONALIZE", () => {
      const nextState = App(undefined, {
        type: at.SHOW_PERSONALIZE,
        data: { source: "promo_card" },
      });

      expect(nextState.customizePanelSource).toBe("promo_card");
    });
    it("should store no source when SHOW_PERSONALIZE has none", () => {
      const nextState = App(undefined, { type: at.SHOW_PERSONALIZE });

      expect(nextState.customizePanelSource).toBeNull();
    });
    it("should clear the open source on HIDE_PERSONALIZE", () => {
      const nextState = App(
        { customizeMenuVisible: true, customizePanelSource: "promo_card" },
        { type: at.HIDE_PERSONALIZE }
      );

      expect(nextState.customizePanelSource).toBeNull();
    });
    it("should clear customize panel state on HIDE_PERSONALIZE", () => {
      const nextState = App(
        {
          customizeMenuVisible: true,
          customizePanelWallpaperCategory: "firefox",
        },
        { type: at.HIDE_PERSONALIZE }
      );

      expect(nextState.customizeMenuVisible).toBe(false);
      expect(nextState.customizePanelWallpaperCategory).toBeNull();
    });
  });
  describe("TopSites", () => {
    it("should return the initial state", () => {
      const nextState = TopSites(undefined, { type: "FOO" });
      expect(nextState).toBe(INITIAL_STATE.TopSites);
    });
    it("should add top sites on TOP_SITES_UPDATED", () => {
      const newRows = [{ url: "foo.com" }, { url: "bar.com" }];
      const nextState = TopSites(undefined, {
        type: at.TOP_SITES_UPDATED,
        data: { links: newRows },
      });
      expect(nextState.rows).toBe(newRows);
    });
    it("should not update state for empty action.data on TOP_SITES_UPDATED", () => {
      const nextState = TopSites(undefined, { type: at.TOP_SITES_UPDATED });
      expect(nextState).toBe(INITIAL_STATE.TopSites);
    });
    it("should initialize prefs on TOP_SITES_UPDATED", () => {
      const nextState = TopSites(undefined, {
        type: at.TOP_SITES_UPDATED,
        data: { links: [], pref: "foo" },
      });

      expect(nextState.pref).toBe("foo");
    });
    it("should pass prevState.prefs if not present in TOP_SITES_UPDATED", () => {
      const nextState = TopSites(
        { prefs: "foo" },
        { type: at.TOP_SITES_UPDATED, data: { links: [] } }
      );

      expect(nextState.prefs).toBe("foo");
    });
    it("should set editForm.site to action.data on TOP_SITES_EDIT", () => {
      const data = { index: 7 };
      const nextState = TopSites(undefined, { type: at.TOP_SITES_EDIT, data });
      expect(nextState.editForm.index).toBe(data.index);
    });
    it("should set editForm to null on TOP_SITES_CANCEL_EDIT", () => {
      const nextState = TopSites(undefined, { type: at.TOP_SITES_CANCEL_EDIT });
      expect(nextState.editForm).toBeNull();
    });
    it("should preserve the editForm.index", () => {
      const actionTypes = [
        at.PREVIEW_RESPONSE,
        at.PREVIEW_REQUEST,
        at.PREVIEW_REQUEST_CANCEL,
      ];
      actionTypes.forEach(type => {
        const oldState = { editForm: { index: 0, previewUrl: "foo" } };
        const action = { type, data: { url: "foo" } };
        const nextState = TopSites(oldState, action);
        expect(nextState.editForm.index).toBe(0);
      });
    });
    it("should set previewResponse on PREVIEW_RESPONSE", () => {
      const oldState = { editForm: { previewUrl: "url" } };
      const action = {
        type: at.PREVIEW_RESPONSE,
        data: { preview: "data:123", url: "url" },
      };
      const nextState = TopSites(oldState, action);
      expect(nextState.editForm.previewResponse).toBe("data:123");
    });
    it("should return previous state if action url does not match expected", () => {
      const oldState = { editForm: { previewUrl: "foo" } };
      const action = { type: at.PREVIEW_RESPONSE, data: { url: "bar" } };
      const nextState = TopSites(oldState, action);
      expect(nextState).toBe(oldState);
    });
    it("should return previous state if editForm is not set", () => {
      const actionTypes = [
        at.PREVIEW_RESPONSE,
        at.PREVIEW_REQUEST,
        at.PREVIEW_REQUEST_CANCEL,
      ];
      actionTypes.forEach(type => {
        const oldState = { editForm: null };
        const action = { type, data: { url: "bar" } };
        const nextState = TopSites(oldState, action);
        expect(nextState).toBe(oldState);
      });
    });
    it("should set previewResponse to null on PREVIEW_REQUEST", () => {
      const oldState = { editForm: { previewResponse: "foo" } };
      const action = { type: at.PREVIEW_REQUEST, data: {} };
      const nextState = TopSites(oldState, action);
      expect(nextState.editForm.previewResponse).toBeNull();
    });
    it("should set previewUrl on PREVIEW_REQUEST", () => {
      const oldState = { editForm: {} };
      const action = { type: at.PREVIEW_REQUEST, data: { url: "bar" } };
      const nextState = TopSites(oldState, action);
      expect(nextState.editForm.previewUrl).toBe("bar");
    });
    it("should add screenshots for SCREENSHOT_UPDATED", () => {
      const oldState = { rows: [{ url: "foo.com" }, { url: "bar.com" }] };
      const action = {
        type: at.SCREENSHOT_UPDATED,
        data: { url: "bar.com", screenshot: "data:123" },
      };
      const nextState = TopSites(oldState, action);
      expect(nextState.rows).toEqual([
        { url: "foo.com" },
        { url: "bar.com", screenshot: "data:123" },
      ]);
    });
    it("should not modify rows if nothing matches the url for SCREENSHOT_UPDATED", () => {
      const oldState = { rows: [{ url: "foo.com" }, { url: "bar.com" }] };
      const action = {
        type: at.SCREENSHOT_UPDATED,
        data: { url: "baz.com", screenshot: "data:123" },
      };
      const nextState = TopSites(oldState, action);
      expect(nextState).toEqual(oldState);
    });
    it("should bookmark an item on PLACES_BOOKMARK_ADDED", () => {
      const oldState = { rows: [{ url: "foo.com" }, { url: "bar.com" }] };
      const action = {
        type: at.PLACES_BOOKMARK_ADDED,
        data: {
          url: "bar.com",
          bookmarkGuid: "bookmark123",
          bookmarkTitle: "Title for bar.com",
          dateAdded: 1234567,
        },
      };
      const nextState = TopSites(oldState, action);
      const [, newRow] = nextState.rows;
      // new row has bookmark data
      expect(newRow.url).toBe(action.data.url);
      expect(newRow.bookmarkGuid).toBe(action.data.bookmarkGuid);
      expect(newRow.bookmarkTitle).toBe(action.data.bookmarkTitle);
      expect(newRow.bookmarkDateCreated).toBe(action.data.dateAdded);

      // old row is unchanged
      expect(nextState.rows[0]).toBe(oldState.rows[0]);
    });
    it("should not update state for empty action.data on PLACES_BOOKMARK_ADDED", () => {
      const nextState = TopSites(undefined, { type: at.PLACES_BOOKMARK_ADDED });
      expect(nextState).toBe(INITIAL_STATE.TopSites);
    });
    it("should remove a bookmark on PLACES_BOOKMARKS_REMOVED", () => {
      const oldState = {
        rows: [
          { url: "foo.com" },
          {
            url: "bar.com",
            bookmarkGuid: "bookmark123",
            bookmarkTitle: "Title for bar.com",
            dateAdded: 123456,
          },
        ],
      };
      const action = {
        type: at.PLACES_BOOKMARKS_REMOVED,
        data: { urls: ["bar.com"] },
      };
      const nextState = TopSites(oldState, action);
      const [, newRow] = nextState.rows;
      // new row no longer has bookmark data
      expect(newRow.url).toBe(oldState.rows[1].url);
      expect(newRow.bookmarkGuid).toBeUndefined();
      expect(newRow.bookmarkTitle).toBeUndefined();
      expect(newRow.bookmarkDateCreated).toBeUndefined();

      // old row is unchanged
      expect(nextState.rows[0]).toEqual(oldState.rows[0]);
    });
    it("should not update state for empty action.data on PLACES_BOOKMARKS_REMOVED", () => {
      const nextState = TopSites(undefined, {
        type: at.PLACES_BOOKMARKS_REMOVED,
      });
      expect(nextState).toBe(INITIAL_STATE.TopSites);
    });
    it("should update prefs on TOP_SITES_PREFS_UPDATED", () => {
      const state = TopSites(
        {},
        { type: at.TOP_SITES_PREFS_UPDATED, data: { pref: "foo" } }
      );

      expect(state.pref).toBe("foo");
    });
    it("should not update state for empty action.data on PLACES_LINKS_DELETED", () => {
      const nextState = TopSites(undefined, { type: at.PLACES_LINKS_DELETED });
      expect(nextState).toBe(INITIAL_STATE.TopSites);
    });
    it("should remove the site on PLACES_LINKS_DELETED", () => {
      const oldState = { rows: [{ url: "foo.com" }, { url: "bar.com" }] };
      const deleteAction = {
        type: at.PLACES_LINKS_DELETED,
        data: { urls: ["foo.com"] },
      };
      const nextState = TopSites(oldState, deleteAction);
      expect(nextState.rows).toEqual([{ url: "bar.com" }]);
    });
    it("should set showSearchShortcutsForm to true on TOP_SITES_OPEN_SEARCH_SHORTCUTS_MODAL", () => {
      const data = { index: 7 };
      const nextState = TopSites(undefined, {
        type: at.TOP_SITES_OPEN_SEARCH_SHORTCUTS_MODAL,
        data,
      });
      expect(nextState.showSearchShortcutsForm).toBe(true);
    });
    it("should set showSearchShortcutsForm to false on TOP_SITES_CLOSE_SEARCH_SHORTCUTS_MODAL", () => {
      const nextState = TopSites(undefined, {
        type: at.TOP_SITES_CLOSE_SEARCH_SHORTCUTS_MODAL,
      });
      expect(nextState.showSearchShortcutsForm).toBe(false);
    });
    it("should update searchShortcuts on UPDATE_SEARCH_SHORTCUTS", () => {
      const shortcuts = [
        {
          keyword: "@google",
          shortURL: "google",
          url: "https://google.com",
          searchIdentifier: /^google/,
        },
        {
          keyword: "@baidu",
          shortURL: "baidu",
          url: "https://baidu.com",
          searchIdentifier: /^baidu/,
        },
      ];
      const nextState = TopSites(undefined, {
        type: at.UPDATE_SEARCH_SHORTCUTS,
        data: { searchShortcuts: shortcuts },
      });
      expect(nextState.searchShortcuts).toEqual(shortcuts);
    });
    it("should set sov positions and state", () => {
      const positions = [
        { position: 0, assignedPartner: "amp" },
        { position: 1, assignedPartner: "moz-sales" },
      ];
      const nextState = TopSites(undefined, {
        type: at.SOV_UPDATED,
        data: { ready: true, positions },
      });
      expect(nextState.sov.ready).toBe(true);
      expect(nextState.sov.positions).toBe(positions);
    });
  });
  describe("Prefs", () => {
    function prevState(custom = {}) {
      return Object.assign({}, INITIAL_STATE.Prefs, custom);
    }
    it("should have the correct initial state", () => {
      const state = Prefs(undefined, {});
      expect(state).toEqual(INITIAL_STATE.Prefs);
    });
    describe("PREFS_INITIAL_VALUES", () => {
      it("should return a new object", () => {
        const state = Prefs(undefined, {
          type: at.PREFS_INITIAL_VALUES,
          data: {},
        });
        expect(state).not.toBe(INITIAL_STATE.Prefs);
      });
      it("should set initalized to true", () => {
        const state = Prefs(undefined, {
          type: at.PREFS_INITIAL_VALUES,
          data: {},
        });
        expect(state.initialized).toBe(true);
      });
      it("should set .values", () => {
        const newValues = { foo: 1, bar: 2 };
        const state = Prefs(undefined, {
          type: at.PREFS_INITIAL_VALUES,
          data: newValues,
        });
        expect(state.values).toBe(newValues);
      });
    });
    describe("PREF_CHANGED", () => {
      it("should return a new Prefs object", () => {
        const state = Prefs(undefined, {
          type: at.PREF_CHANGED,
          data: { name: "foo", value: 2 },
        });
        expect(state).not.toBe(INITIAL_STATE.Prefs);
      });
      it("should set the changed pref", () => {
        const state = Prefs(prevState({ foo: 1 }), {
          type: at.PREF_CHANGED,
          data: { name: "foo", value: 2 },
        });
        expect(state.values.foo).toBe(2);
      });
      it("should return a new .pref object instead of mutating", () => {
        const oldState = prevState({ foo: 1 });
        const state = Prefs(oldState, {
          type: at.PREF_CHANGED,
          data: { name: "foo", value: 2 },
        });
        expect(state.values).not.toBe(oldState.values);
      });
    });
    describe("MULTIPLE_PREFS_CHANGED", () => {
      it("should merge multiple values in one pass and keep untouched keys", () => {
        const oldState = { ...INITIAL_STATE.Prefs, values: { foo: 1, bar: 2 } };
        const state = Prefs(oldState, {
          type: at.MULTIPLE_PREFS_CHANGED,
          data: { values: { foo: 3, baz: 4 } },
        });
        expect(state.values.foo).toBe(3);
        expect(state.values.bar).toBe(2);
        expect(state.values.baz).toBe(4);
      });
      it("should return a new .values object instead of mutating", () => {
        const oldState = { ...INITIAL_STATE.Prefs, values: { foo: 1 } };
        const state = Prefs(oldState, {
          type: at.MULTIPLE_PREFS_CHANGED,
          data: { values: { foo: 2 } },
        });
        expect(state.values).not.toBe(oldState.values);
      });
    });
  });
  describe("Dialog", () => {
    it("should return INITIAL_STATE by default", () => {
      expect(Dialog(undefined, { type: "non_existent" })).toBe(
        INITIAL_STATE.Dialog
      );
    });
    it("should toggle visible to true on DIALOG_OPEN", () => {
      const action = { type: at.DIALOG_OPEN };
      const nextState = Dialog(INITIAL_STATE.Dialog, action);
      expect(nextState.visible).toBe(true);
    });
    it("should pass url data on DIALOG_OPEN", () => {
      const action = { type: at.DIALOG_OPEN, data: "some url" };
      const nextState = Dialog(INITIAL_STATE.Dialog, action);
      expect(nextState.data).toBe(action.data);
    });
    it("should toggle visible to false on DIALOG_CANCEL", () => {
      const action = { type: at.DIALOG_CANCEL, data: "some url" };
      const nextState = Dialog(INITIAL_STATE.Dialog, action);
      expect(nextState.visible).toBe(false);
    });
    it("should return inital state on DELETE_HISTORY_URL", () => {
      const action = { type: at.DELETE_HISTORY_URL };
      const nextState = Dialog(INITIAL_STATE.Dialog, action);

      expect(nextState).toEqual(INITIAL_STATE.Dialog);
    });
  });
  describe("Sections", () => {
    let oldState;

    beforeEach(() => {
      oldState = new Array(5).fill(null).map((v, i) => ({
        id: `foo_bar_${i}`,
        title: `Foo Bar ${i}`,
        initialized: false,
        rows: [
          { url: "www.foo.bar", pocket_id: 123 },
          { url: "www.other.url" },
        ],
        order: i,
        type: "history",
      }));
    });

    it("should return INITIAL_STATE by default", () => {
      expect(Sections(undefined, { type: "non_existent" })).toBe(
        INITIAL_STATE.Sections
      );
    });
    it("should remove the correct section on SECTION_DEREGISTER", () => {
      const newState = Sections(oldState, {
        type: at.SECTION_DEREGISTER,
        data: "foo_bar_2",
      });
      expect(newState).toHaveLength(4);
      const expectedNewState = oldState.splice(2, 1) && oldState;
      expect(newState).toEqual(expectedNewState);
    });
    it("should add a section on SECTION_REGISTER if it doesn't already exist", () => {
      const action = {
        type: at.SECTION_REGISTER,
        data: { id: "foo_bar_5", title: "Foo Bar 5" },
      };
      const newState = Sections(oldState, action);
      expect(newState).toHaveLength(6);
      const insertedSection = newState.find(
        section => section.id === "foo_bar_5"
      );
      expect(insertedSection.title).toBe(action.data.title);
    });
    it("should set newSection.rows === [] if no rows are provided on SECTION_REGISTER", () => {
      const action = {
        type: at.SECTION_REGISTER,
        data: { id: "foo_bar_5", title: "Foo Bar 5" },
      };
      const newState = Sections(oldState, action);
      const insertedSection = newState.find(
        section => section.id === "foo_bar_5"
      );
      expect(insertedSection.rows).toEqual([]);
    });
    it("should update a section on SECTION_REGISTER if it already exists", () => {
      const NEW_TITLE = "New Title";
      const action = {
        type: at.SECTION_REGISTER,
        data: { id: "foo_bar_2", title: NEW_TITLE },
      };
      const newState = Sections(oldState, action);
      expect(newState).toHaveLength(5);
      const updatedSection = newState.find(
        section => section.id === "foo_bar_2"
      );
      expect(updatedSection && updatedSection.title === NEW_TITLE).toBeTruthy();
    });
    it("should set initialized to false on SECTION_REGISTER if there are no rows", () => {
      const NEW_TITLE = "New Title";
      const action = {
        type: at.SECTION_REGISTER,
        data: { id: "bloop", title: NEW_TITLE },
      };
      const newState = Sections(oldState, action);
      const updatedSection = newState.find(section => section.id === "bloop");
      expect(updatedSection.initialized).toBe(false);
    });
    it("should set initialized to true on SECTION_REGISTER if there are rows", () => {
      const NEW_TITLE = "New Title";
      const action = {
        type: at.SECTION_REGISTER,
        data: { id: "bloop", title: NEW_TITLE, rows: [{}, {}] },
      };
      const newState = Sections(oldState, action);
      const updatedSection = newState.find(section => section.id === "bloop");
      expect(updatedSection.initialized).toBe(true);
    });
    it("should have no effect on SECTION_UPDATE if the id doesn't exist", () => {
      const action = {
        type: at.SECTION_UPDATE,
        data: { id: "fake_id", data: "fake_data" },
      };
      const newState = Sections(oldState, action);
      expect(newState).toEqual(oldState);
    });
    it("should update the section with the correct data on SECTION_UPDATE", () => {
      const FAKE_DATA = { rows: ["some", "fake", "data"], foo: "bar" };
      const action = {
        type: at.SECTION_UPDATE,
        data: Object.assign(FAKE_DATA, { id: "foo_bar_2" }),
      };
      const newState = Sections(oldState, action);
      const updatedSection = newState.find(
        section => section.id === "foo_bar_2"
      );
      expect(updatedSection).toMatchObject(FAKE_DATA);
    });
    it("should set initialized to true on SECTION_UPDATE if rows is defined on action.data", () => {
      const data = { rows: [], id: "foo_bar_2" };
      const action = { type: at.SECTION_UPDATE, data };
      const newState = Sections(oldState, action);
      const updatedSection = newState.find(
        section => section.id === "foo_bar_2"
      );
      expect(updatedSection.initialized).toBe(true);
    });
    it("should retain pinned cards on SECTION_UPDATE", () => {
      const ROW = { id: "row" };
      let newState = Sections(oldState, {
        type: at.SECTION_UPDATE,
        data: Object.assign({ rows: [ROW] }, { id: "foo_bar_2" }),
      });
      let updatedSection = newState.find(section => section.id === "foo_bar_2");
      expect(updatedSection.rows).toEqual([ROW]);

      const PINNED_ROW = { id: "pinned", pinned: true, guid: "pinned" };
      newState = Sections(newState, {
        type: at.SECTION_UPDATE,
        data: Object.assign({ rows: [PINNED_ROW] }, { id: "foo_bar_2" }),
      });
      updatedSection = newState.find(section => section.id === "foo_bar_2");
      expect(updatedSection.rows).toEqual([PINNED_ROW]);

      // Updating the section again should not duplicate pinned cards
      newState = Sections(newState, {
        type: at.SECTION_UPDATE,
        data: Object.assign({ rows: [PINNED_ROW] }, { id: "foo_bar_2" }),
      });
      updatedSection = newState.find(section => section.id === "foo_bar_2");
      expect(updatedSection.rows).toEqual([PINNED_ROW]);

      // Updating the section should retain pinned card at its index
      newState = Sections(newState, {
        type: at.SECTION_UPDATE,
        data: Object.assign({ rows: [ROW] }, { id: "foo_bar_2" }),
      });
      updatedSection = newState.find(section => section.id === "foo_bar_2");
      expect(updatedSection.rows).toEqual([PINNED_ROW, ROW]);

      // Clearing/Resetting the section should clear pinned cards
      newState = Sections(newState, {
        type: at.SECTION_UPDATE,
        data: Object.assign({ rows: [] }, { id: "foo_bar_2" }),
      });
      updatedSection = newState.find(section => section.id === "foo_bar_2");
      expect(updatedSection.rows).toEqual([]);
    });
    it("should have no effect on SECTION_UPDATE_CARD if the id or url doesn't exist", () => {
      const noIdAction = {
        type: at.SECTION_UPDATE_CARD,
        data: {
          id: "non-existent",
          url: "www.foo.bar",
          options: { title: "New title" },
        },
      };
      const noIdState = Sections(oldState, noIdAction);
      const noUrlAction = {
        type: at.SECTION_UPDATE_CARD,
        data: {
          id: "foo_bar_2",
          url: "www.non-existent.url",
          options: { title: "New title" },
        },
      };
      const noUrlState = Sections(oldState, noUrlAction);
      expect(noIdState).toEqual(oldState);
      expect(noUrlState).toEqual(oldState);
    });
    it("should update the card with the correct data on SECTION_UPDATE_CARD", () => {
      const action = {
        type: at.SECTION_UPDATE_CARD,
        data: {
          id: "foo_bar_2",
          url: "www.other.url",
          options: { title: "Fake new title" },
        },
      };
      const newState = Sections(oldState, action);
      const updatedSection = newState.find(
        section => section.id === "foo_bar_2"
      );
      const updatedCard = updatedSection.rows.find(
        card => card.url === "www.other.url"
      );
      expect(updatedCard.title).toBe("Fake new title");
    });
    it("should only update the cards belonging to the right section on SECTION_UPDATE_CARD", () => {
      const action = {
        type: at.SECTION_UPDATE_CARD,
        data: {
          id: "foo_bar_2",
          url: "www.other.url",
          options: { title: "Fake new title" },
        },
      };
      const newState = Sections(oldState, action);
      newState.forEach((section, i) => {
        if (section.id !== "foo_bar_2") {
          expect(section).toEqual(oldState[i]);
        }
      });
    });
    it("should allow action.data to set .initialized", () => {
      const data = { rows: [], initialized: false, id: "foo_bar_2" };
      const action = { type: at.SECTION_UPDATE, data };
      const newState = Sections(oldState, action);
      const updatedSection = newState.find(
        section => section.id === "foo_bar_2"
      );
      expect(updatedSection.initialized).toBe(false);
    });
    it("should dedupe based on dedupeConfigurations", () => {
      const site = { url: "foo.com" };
      const highlights = { rows: [site], id: "highlights" };
      const topstories = { rows: [site], id: "topstories" };
      const dedupeConfigurations = [
        { id: "topstories", dedupeFrom: ["highlights"] },
      ];
      const action = { data: { dedupeConfigurations }, type: "SECTION_UPDATE" };
      const state = [highlights, topstories];

      const nextState = Sections(state, action);

      expect(nextState.find(s => s.id === "highlights").rows).toHaveLength(1);
      expect(nextState.find(s => s.id === "topstories").rows).toHaveLength(0);
    });
    it("should remove blocked and deleted urls from all rows in all sections", () => {
      const blockAction = {
        type: at.PLACES_LINK_BLOCKED,
        data: { url: "www.foo.bar" },
      };
      const deleteAction = {
        type: at.PLACES_LINKS_DELETED,
        data: { urls: ["www.foo.bar"] },
      };
      const newBlockState = Sections(oldState, blockAction);
      const newDeleteState = Sections(oldState, deleteAction);
      newBlockState.concat(newDeleteState).forEach(section => {
        expect(section.rows).toEqual([{ url: "www.other.url" }]);
      });
    });
    it("should not update state for empty action.data on PLACES_LINK_BLOCKED", () => {
      const nextState = Sections(undefined, { type: at.PLACES_LINK_BLOCKED });
      expect(nextState).toBe(INITIAL_STATE.Sections);
    });
    it("should not update state for empty action.data on PLACES_LINKS_DELETED", () => {
      const nextState = Sections(undefined, { type: at.PLACES_LINKS_DELETED });
      expect(nextState).toBe(INITIAL_STATE.Sections);
    });
    it("should not update state for empty action.data on PLACES_BOOKMARK_ADDED", () => {
      const nextState = Sections(undefined, { type: at.PLACES_BOOKMARK_ADDED });
      expect(nextState).toBe(INITIAL_STATE.Sections);
    });
    it("should bookmark an item when PLACES_BOOKMARK_ADDED is received", () => {
      const action = {
        type: at.PLACES_BOOKMARK_ADDED,
        data: {
          url: "www.foo.bar",
          bookmarkGuid: "bookmark123",
          bookmarkTitle: "Title for bar.com",
          dateAdded: 1234567,
        },
      };
      const nextState = Sections(oldState, action);
      // check a section to ensure the correct url was bookmarked
      const [newRow, oldRow] = nextState[0].rows;

      // new row has bookmark data
      expect(newRow.url).toBe(action.data.url);
      expect(newRow.type).toBe("bookmark");
      expect(newRow.bookmarkGuid).toBe(action.data.bookmarkGuid);
      expect(newRow.bookmarkTitle).toBe(action.data.bookmarkTitle);
      expect(newRow.bookmarkDateCreated).toBe(action.data.dateAdded);

      // old row is unchanged
      expect(oldRow).toBe(oldState[0].rows[1]);
    });
    it("should not update state for empty action.data on PLACES_BOOKMARKS_REMOVED", () => {
      const nextState = Sections(undefined, {
        type: at.PLACES_BOOKMARKS_REMOVED,
      });
      expect(nextState).toBe(INITIAL_STATE.Sections);
    });
    it("should remove the bookmark when PLACES_BOOKMARKS_REMOVED is received", () => {
      const action = {
        type: at.PLACES_BOOKMARKS_REMOVED,
        data: {
          urls: ["www.foo.bar"],
          bookmarkGuid: "bookmark123",
        },
      };
      // add some bookmark data for the first url in rows
      oldState.forEach(item => {
        item.rows[0].bookmarkGuid = "bookmark123";
        item.rows[0].bookmarkTitle = "Title for bar.com";
        item.rows[0].bookmarkDateCreated = 1234567;
        item.rows[0].type = "bookmark";
      });
      const nextState = Sections(oldState, action);
      // check a section to ensure the correct bookmark was removed
      const [newRow, oldRow] = nextState[0].rows;

      // new row isn't a bookmark
      expect(newRow.url).toBe(action.data.urls[0]);
      expect(newRow.type).toBe("history");
      expect(newRow.bookmarkGuid).toBeUndefined();
      expect(newRow.bookmarkTitle).toBeUndefined();
      expect(newRow.bookmarkDateCreated).toBeUndefined();

      // old row is unchanged
      expect(oldRow).toBe(oldState[0].rows[1]);
    });
  });
  describe("Pocket", () => {
    it("should return INITIAL_STATE by default", () => {
      expect(Pocket(undefined, { type: "some_action" })).toBe(
        INITIAL_STATE.Pocket
      );
    });
    it("should set waitingForSpoc on a POCKET_WAITING_FOR_SPOC action", () => {
      const state = Pocket(undefined, {
        type: at.POCKET_WAITING_FOR_SPOC,
        data: false,
      });
      expect(state.waitingForSpoc).toBe(false);
    });
    it("should set pocketCta with correct object on a POCKET_CTA", () => {
      const data = {
        cta_button: "cta button",
        cta_text: "cta text",
        cta_url: "https://cta-url.com",
        use_cta: true,
      };
      const state = Pocket(undefined, { type: at.POCKET_CTA, data });
      expect(state.pocketCta.ctaButton).toBe(data.cta_button);
      expect(state.pocketCta.ctaText).toBe(data.cta_text);
      expect(state.pocketCta.ctaUrl).toBe(data.cta_url);
      expect(state.pocketCta.useCta).toBe(data.use_cta);
    });
  });
  describe("DiscoveryStream", () => {
    it("should return INITIAL_STATE by default", () => {
      expect(DiscoveryStream(undefined, { type: "some_action" })).toBe(
        INITIAL_STATE.DiscoveryStream
      );
    });
    it("should set layout data with DISCOVERY_STREAM_LAYOUT_UPDATE", () => {
      const state = DiscoveryStream(undefined, {
        type: at.DISCOVERY_STREAM_LAYOUT_UPDATE,
        data: { layout: ["test"] },
      });
      expect(state.layout[0]).toBe("test");
    });
    it("should reset layout data with DISCOVERY_STREAM_LAYOUT_RESET", () => {
      const layoutData = { layout: ["test"], lastUpdated: 123 };
      const feedsData = {
        "https://foo.com/feed1": { lastUpdated: 123, data: [1, 2, 3] },
      };
      const spocsData = {
        lastUpdated: 123,
        spocs: [1, 2, 3],
      };
      let state = DiscoveryStream(undefined, {
        type: at.DISCOVERY_STREAM_LAYOUT_UPDATE,
        data: layoutData,
      });
      state = DiscoveryStream(state, {
        type: at.DISCOVERY_STREAM_FEEDS_UPDATE,
        data: feedsData,
      });
      state = DiscoveryStream(state, {
        type: at.DISCOVERY_STREAM_SPOCS_UPDATE,
        data: spocsData,
      });
      state = DiscoveryStream(state, {
        type: at.DISCOVERY_STREAM_LAYOUT_RESET,
      });

      expect(state).toEqual(INITIAL_STATE.DiscoveryStream);
    });
    it("should preserve sectionPersonalization with DISCOVERY_STREAM_LAYOUT_RESET", () => {
      const personalization = {
        sports: { isBlocked: true, isFollowed: false, title: "Sports" },
      };
      let state = DiscoveryStream(undefined, {
        type: at.SECTION_PERSONALIZATION_UPDATE,
        data: personalization,
      });
      state = DiscoveryStream(state, {
        type: at.DISCOVERY_STREAM_LAYOUT_RESET,
      });
      expect(state.sectionPersonalization).toEqual(personalization);
    });
    it("should set config data with DISCOVERY_STREAM_CONFIG_CHANGE", () => {
      const state = DiscoveryStream(undefined, {
        type: at.DISCOVERY_STREAM_CONFIG_CHANGE,
        data: { enabled: true },
      });
      expect(state.config).toEqual({ enabled: true });
    });
    it("should set feeds as loaded with DISCOVERY_STREAM_FEEDS_UPDATE", () => {
      const state = DiscoveryStream(undefined, {
        type: at.DISCOVERY_STREAM_FEEDS_UPDATE,
      });
      expect(state.feeds.loaded).toBe(true);
    });
    it("should set spoc_endpoint with DISCOVERY_STREAM_SPOCS_ENDPOINT", () => {
      const state = DiscoveryStream(undefined, {
        type: at.DISCOVERY_STREAM_SPOCS_ENDPOINT,
        data: { url: "foo.com" },
      });
      expect(state.spocs.spocs_endpoint).toBe("foo.com");
    });
    it("should use initial state with DISCOVERY_STREAM_SPOCS_PLACEMENTS", () => {
      const state = DiscoveryStream(undefined, {
        type: at.DISCOVERY_STREAM_SPOCS_PLACEMENTS,
        data: {},
      });
      expect(state.spocs.placements).toEqual([]);
    });
    it("should set placements with DISCOVERY_STREAM_SPOCS_PLACEMENTS", () => {
      const state = DiscoveryStream(undefined, {
        type: at.DISCOVERY_STREAM_SPOCS_PLACEMENTS,
        data: {
          placements: [1, 2, 3],
        },
      });
      expect(state.spocs.placements).toEqual([1, 2, 3]);
    });
    it("should set spocs with DISCOVERY_STREAM_SPOCS_UPDATE", () => {
      const data = {
        lastUpdated: 123,
        spocs: [1, 2, 3],
        spocsCacheUpdateTime: 10 * 60 * 1000,
        spocsOnDemand: true,
      };
      const state = DiscoveryStream(undefined, {
        type: at.DISCOVERY_STREAM_SPOCS_UPDATE,
        data,
      });
      expect(state.spocs).toEqual({
        spocs_endpoint: "",
        data: data.spocs,
        lastUpdated: data.lastUpdated,
        loaded: true,
        frequency_caps: [],
        blocked: [],
        placements: [],
        cacheUpdateTime: data.spocsCacheUpdateTime,
        onDemand: {
          enabled: data.spocsOnDemand,
          loaded: false,
        },
      });
    });
    it("should default to a single spoc placement", () => {
      const deleteAction = {
        type: at.DISCOVERY_STREAM_LINK_BLOCKED,
        data: { url: "https://foo.com" },
      };
      const oldState = {
        spocs: {
          data: {
            spocs: {
              items: [
                {
                  url: "test-spoc.com",
                },
              ],
            },
          },
          loaded: true,
        },
        feeds: {
          data: {},
          loaded: true,
        },
      };

      const newState = DiscoveryStream(oldState, deleteAction);

      expect(newState.spocs.data.spocs.items).toHaveLength(1);
    });
    it("should handle no data from DISCOVERY_STREAM_SPOCS_UPDATE", () => {
      const data = null;
      const state = DiscoveryStream(undefined, {
        type: at.DISCOVERY_STREAM_SPOCS_UPDATE,
        data,
      });
      expect(state.spocs).toEqual(INITIAL_STATE.DiscoveryStream.spocs);
    });
    it("should add blocked spocs to blocked array with DISCOVERY_STREAM_SPOC_BLOCKED", () => {
      const firstState = DiscoveryStream(undefined, {
        type: at.DISCOVERY_STREAM_SPOC_BLOCKED,
        data: { url: "https://foo.com" },
      });
      const secondState = DiscoveryStream(firstState, {
        type: at.DISCOVERY_STREAM_SPOC_BLOCKED,
        data: { url: "https://bar.com" },
      });
      expect(firstState.spocs.blocked).toEqual(["https://foo.com"]);
      expect(secondState.spocs.blocked).toEqual([
        "https://foo.com",
        "https://bar.com",
      ]);
    });
    it("should not update state for empty action.data on DISCOVERY_STREAM_LINK_BLOCKED", () => {
      const newState = DiscoveryStream(undefined, {
        type: at.DISCOVERY_STREAM_LINK_BLOCKED,
      });
      expect(newState).toBe(INITIAL_STATE.DiscoveryStream);
    });
    it("should not update state if feeds are not loaded", () => {
      const deleteAction = {
        type: at.DISCOVERY_STREAM_LINK_BLOCKED,
        data: { url: "foo.com" },
      };
      const newState = DiscoveryStream(undefined, deleteAction);
      expect(newState).toBe(INITIAL_STATE.DiscoveryStream);
    });
    it("should not update state if spocs and feeds data is undefined", () => {
      const deleteAction = {
        type: at.DISCOVERY_STREAM_LINK_BLOCKED,
        data: { url: "foo.com" },
      };
      const oldState = {
        spocs: {
          data: {},
          loaded: true,
          placements: [{ name: "spocs" }],
        },
        feeds: {
          data: {},
          loaded: true,
        },
      };
      const newState = DiscoveryStream(oldState, deleteAction);
      expect(newState).toEqual(oldState);
    });
    it("should remove the site on DISCOVERY_STREAM_LINK_BLOCKED from spocs if feeds data is empty", () => {
      const deleteAction = {
        type: at.DISCOVERY_STREAM_LINK_BLOCKED,
        data: { url: "https://foo.com" },
      };
      const oldState = {
        spocs: {
          data: {
            spocs: {
              items: [{ url: "https://foo.com" }, { url: "test-spoc.com" }],
            },
          },
          loaded: true,
          placements: [{ name: "spocs" }],
        },
        feeds: {
          data: {},
          loaded: true,
        },
      };
      const newState = DiscoveryStream(oldState, deleteAction);
      expect(newState.spocs.data.spocs.items).toEqual([
        { url: "test-spoc.com" },
      ]);
    });
    it("should remove the site on DISCOVERY_STREAM_LINK_BLOCKED from feeds if spocs data is empty", () => {
      const deleteAction = {
        type: at.DISCOVERY_STREAM_LINK_BLOCKED,
        data: { url: "https://foo.com" },
      };
      const oldState = {
        spocs: {
          data: {},
          loaded: true,
          placements: [{ name: "spocs" }],
        },
        feeds: {
          data: {
            "https://foo.com/feed1": {
              data: {
                recommendations: [
                  { url: "https://foo.com" },
                  { url: "test.com" },
                ],
              },
            },
          },
          loaded: true,
        },
      };
      const newState = DiscoveryStream(oldState, deleteAction);
      expect(
        newState.feeds.data["https://foo.com/feed1"].data.recommendations
      ).toEqual([{ url: "test.com" }]);
    });
    it("should remove the site on DISCOVERY_STREAM_LINK_BLOCKED from both feeds and spocs", () => {
      const oldState = {
        feeds: {
          data: {
            "https://foo.com/feed1": {
              data: {
                recommendations: [
                  { url: "https://foo.com" },
                  { url: "test.com" },
                ],
              },
            },
          },
          loaded: true,
        },
        spocs: {
          data: {
            spocs: {
              items: [{ url: "https://foo.com" }, { url: "test-spoc.com" }],
            },
          },
          loaded: true,
          placements: [{ name: "spocs" }],
        },
      };
      const deleteAction = {
        type: at.DISCOVERY_STREAM_LINK_BLOCKED,
        data: { url: "https://foo.com" },
      };
      const newState = DiscoveryStream(oldState, deleteAction);
      expect(newState.spocs.data.spocs.items).toEqual([
        { url: "test-spoc.com" },
      ]);
      expect(
        newState.feeds.data["https://foo.com/feed1"].data.recommendations
      ).toEqual([{ url: "test.com" }]);
    });
    it("should add boookmark details on PLACES_BOOKMARK_ADDED in both feeds and spocs", () => {
      const oldState = {
        feeds: {
          data: {
            "https://foo.com/feed1": {
              data: {
                recommendations: [
                  { url: "https://foo.com" },
                  { url: "test.com" },
                ],
              },
            },
          },
          loaded: true,
        },
        spocs: {
          data: {
            spocs: {
              items: [{ url: "https://foo.com" }, { url: "test-spoc.com" }],
            },
          },
          loaded: true,
          placements: [{ name: "spocs" }],
        },
      };
      const bookmarkAction = {
        type: at.PLACES_BOOKMARK_ADDED,
        data: {
          url: "https://foo.com",
          bookmarkGuid: "bookmark123",
          bookmarkTitle: "Title for bar.com",
          dateAdded: 1234567,
        },
      };

      const newState = DiscoveryStream(oldState, bookmarkAction);

      expect(newState.spocs.data.spocs.items).toHaveLength(2);
      expect(newState.spocs.data.spocs.items[0].bookmarkGuid).toBe(
        bookmarkAction.data.bookmarkGuid
      );
      expect(newState.spocs.data.spocs.items[0].bookmarkTitle).toBe(
        bookmarkAction.data.bookmarkTitle
      );
      expect(newState.spocs.data.spocs.items[1].bookmarkGuid).toBeUndefined();

      expect(
        newState.feeds.data["https://foo.com/feed1"].data.recommendations
      ).toHaveLength(2);
      expect(
        newState.feeds.data["https://foo.com/feed1"].data.recommendations[0]
          .bookmarkGuid
      ).toBe(bookmarkAction.data.bookmarkGuid);
      expect(
        newState.feeds.data["https://foo.com/feed1"].data.recommendations[0]
          .bookmarkTitle
      ).toBe(bookmarkAction.data.bookmarkTitle);
      expect(
        newState.feeds.data["https://foo.com/feed1"].data.recommendations[1]
          .bookmarkGuid
      ).toBeUndefined();
    });

    it("should remove boookmark details on PLACES_BOOKMARKS_REMOVED in both feeds and spocs", () => {
      const oldState = {
        feeds: {
          data: {
            "https://foo.com/feed1": {
              data: {
                recommendations: [
                  {
                    url: "https://foo.com",
                    bookmarkGuid: "bookmark123",
                    bookmarkTitle: "Title for bar.com",
                  },
                  { url: "test.com" },
                ],
              },
            },
          },
          loaded: true,
        },
        spocs: {
          data: {
            spocs: {
              items: [
                {
                  url: "https://foo.com",
                  bookmarkGuid: "bookmark123",
                  bookmarkTitle: "Title for bar.com",
                },
                { url: "test-spoc.com" },
              ],
            },
          },
          loaded: true,
          placements: [{ name: "spocs" }],
        },
      };
      const action = {
        type: at.PLACES_BOOKMARKS_REMOVED,
        data: {
          urls: ["https://foo.com"],
        },
      };

      const newState = DiscoveryStream(oldState, action);

      expect(newState.spocs.data.spocs.items).toHaveLength(2);
      expect(newState.spocs.data.spocs.items[0].bookmarkGuid).toBeUndefined();
      expect(newState.spocs.data.spocs.items[0].bookmarkTitle).toBeUndefined();

      expect(
        newState.feeds.data["https://foo.com/feed1"].data.recommendations
      ).toHaveLength(2);
      expect(
        newState.feeds.data["https://foo.com/feed1"].data.recommendations[0]
          .bookmarkGuid
      ).toBeUndefined();
      expect(
        newState.feeds.data["https://foo.com/feed1"].data.recommendations[0]
          .bookmarkTitle
      ).toBeUndefined();
    });
  });
  describe("Search", () => {
    it("should return INITIAL_STATE by default", () => {
      expect(Search(undefined, { type: "some_action" })).toBe(
        INITIAL_STATE.Search
      );
    });
    it("should set disable to true on DISABLE_SEARCH", () => {
      const nextState = Search(undefined, { type: "DISABLE_SEARCH" });
      expect(nextState.disable).toBe(true);
    });
    it("should set focus and disable to false on SHOW_SEARCH", () => {
      const nextState = Search(undefined, { type: "SHOW_SEARCH" });
      expect(nextState.disable).toBe(false);
    });
  });
  describe("ExternalComponents", () => {
    it("should return INITIAL_STATE by default", () => {
      const nextState = ExternalComponents(undefined, { type: "some_action" });
      expect(nextState).toBe(INITIAL_STATE.ExternalComponents);
    });
    it("should return initial state with empty components array", () => {
      const nextState = ExternalComponents(undefined, { type: "some_action" });
      expect(nextState.components).toEqual([]);
    });
    it("should update components on REFRESH_EXTERNAL_COMPONENTS", () => {
      const testComponents = [
        {
          type: "SEARCH",
          componentURL: "chrome://test/content/component.mjs",
          tagName: "test-component",
          l10nURLs: [],
        },
      ];
      const nextState = ExternalComponents(undefined, {
        type: at.REFRESH_EXTERNAL_COMPONENTS,
        data: testComponents,
      });
      expect(nextState.components).toEqual(testComponents);
    });
    it("should preserve other state when updating components", () => {
      const testComponents = [
        {
          type: "SEARCH",
          componentURL: "chrome://test/content/component.mjs",
          tagName: "test-component",
          l10nURLs: [],
        },
      ];
      const prevState = { components: [], otherProp: "value" };
      const nextState = ExternalComponents(prevState, {
        type: at.REFRESH_EXTERNAL_COMPONENTS,
        data: testComponents,
      });
      expect(nextState.components).toEqual(testComponents);
      expect(nextState.otherProp).toBe("value");
    });
    it("should replace existing components on REFRESH_EXTERNAL_COMPONENTS", () => {
      const oldComponents = [
        {
          type: "OLD",
          componentURL: "chrome://old/content/component.mjs",
          tagName: "old-component",
          l10nURLs: [],
        },
      ];
      const newComponents = [
        {
          type: "NEW",
          componentURL: "chrome://new/content/component.mjs",
          tagName: "new-component",
          l10nURLs: [],
        },
      ];
      const prevState = { components: oldComponents };
      const nextState = ExternalComponents(prevState, {
        type: at.REFRESH_EXTERNAL_COMPONENTS,
        data: newComponents,
      });
      expect(nextState.components).toEqual(newComponents);
      expect(nextState.components).not.toEqual(oldComponents);
    });
  });
  describe("PictureOfTheDay", () => {
    it("PICTURE_OF_THE_DAY_UPDATE stores the picture fields", () => {
      const next = PictureOfTheDay(INITIAL_STATE.PictureOfTheDay, {
        type: at.PICTURE_OF_THE_DAY_UPDATE,
        data: {
          imageUrl: "https://example.com/x.jpg",
          thumbnailUrl: "https://example.com/thumb.jpg",
          title: "T",
          description: "D",
          publishedDate: "2026-06-30",
          lastUpdated: 123,
        },
      });
      expect(next.imageUrl).toBe("https://example.com/x.jpg");
      expect(next.description).toBe("D");
      expect(next.publishedDate).toBe("2026-06-30");
      expect(next.initialized).toBe(true);
    });

    it("defaults missing fields and returns prevState for other actions", () => {
      const updated = PictureOfTheDay(INITIAL_STATE.PictureOfTheDay, {
        type: at.PICTURE_OF_THE_DAY_UPDATE,
        data: {},
      });
      expect(updated.imageUrl).toBe("");
      expect(updated.description).toBe("");

      const prev = INITIAL_STATE.PictureOfTheDay;
      expect(PictureOfTheDay(prev, { type: "SOME_OTHER_ACTION" })).toBe(prev);
    });
  });

  describe("Stocks", () => {
    it("WIDGETS_STOCKS_UPDATE replaces tickers and sets lastUpdated", () => {
      const action = {
        type: at.WIDGETS_STOCKS_UPDATE,
        data: {
          tickers: [{ ticker: "SPY", name: "SPDR S&P 500 ETF Trust" }],
          lastUpdated: 1700000000000,
        },
      };
      const nextState = reducers.Stocks(undefined, action);
      expect(nextState.tickers).toEqual(action.data.tickers);
      expect(nextState.lastUpdated).toBe(1700000000000);
    });

    it("returns previous state for unrelated actions", () => {
      const prev = { tickers: [{ ticker: "DIA" }], lastUpdated: 1 };
      expect(reducers.Stocks(prev, { type: "SOME_OTHER_ACTION" })).toBe(prev);
    });

    it("WIDGETS_STOCKS_UPDATE stores the error flag", () => {
      const action = {
        type: at.WIDGETS_STOCKS_UPDATE,
        data: { tickers: [], lastUpdated: 1700000000000, error: true },
      };
      const nextState = reducers.Stocks(undefined, action);
      expect(nextState.error).toBe(true);
    });

    it("WIDGETS_STOCKS_UPDATE defaults error to false when omitted", () => {
      const action = {
        type: at.WIDGETS_STOCKS_UPDATE,
        data: { tickers: [], lastUpdated: 1 },
      };
      const nextState = reducers.Stocks(undefined, action);
      expect(nextState.error).toBe(false);
    });
  });

  describe("WebNotifications", () => {
    const notification = {
      id: "n1",
      origin: "https://example.com",
      persistent: true,
      title: "hi",
    };
    it("should return INITIAL_STATE by default", () => {
      const nextState = WebNotifications(undefined, {
        type: "some_action",
      });
      expect(nextState).toBe(INITIAL_STATE.WebNotifications);
    });
    it("should set initialized and clear error on WEB_NOTIFICATIONS_UPDATED", () => {
      const prevState = {
        ...INITIAL_STATE.WebNotifications,
        error: { step: "snapshot", message: "boom" },
      };
      const data = {
        lastUpdated: 12345,
        notifications: { abc: { id: "abc", origin: "https://example.com" } },
        byOrigin: { "https://example.com": ["abc"] },
      };
      const nextState = WebNotifications(prevState, {
        type: at.WEB_NOTIFICATIONS_UPDATED,
        data,
      });
      expect(nextState.initialized).toBe(true);
      expect(nextState.lastUpdated).toBe(12345);
      expect(nextState.notifications).toEqual(data.notifications);
      expect(nextState.byOrigin).toEqual(data.byOrigin);
      expect(nextState.error).toBeNull();
    });
    it("should set error and preserve other fields on WEB_NOTIFICATIONS_ERROR", () => {
      const prevState = {
        ...INITIAL_STATE.WebNotifications,
        initialized: true,
        notifications: { abc: { id: "abc" } },
      };
      const errorData = { step: "snapshot", message: "boom" };
      const nextState = WebNotifications(prevState, {
        type: at.WEB_NOTIFICATIONS_ERROR,
        data: errorData,
      });
      expect(nextState.error).toEqual(errorData);
      expect(nextState.initialized).toBe(true);
      expect(nextState.notifications).toEqual(prevState.notifications);
    });
    it("should merge one notification into the index on WEB_NOTIFICATIONS_ADDED", () => {
      const action = {
        type: at.WEB_NOTIFICATIONS_ADDED,
        data: { notification },
      };
      const nextState = WebNotifications(undefined, action);
      expect(nextState.notifications.n1).toBe(notification);
      expect(nextState.byOrigin["https://example.com"]).toEqual(["n1"]);
    });
    it("should not duplicate an existing id on WEB_NOTIFICATIONS_ADDED", () => {
      const prevState = {
        ...INITIAL_STATE.WebNotifications,
        notifications: { n1: notification },
        byOrigin: { "https://example.com": ["n1"] },
      };
      const nextState = WebNotifications(prevState, {
        type: at.WEB_NOTIFICATIONS_ADDED,
        data: { notification },
      });
      expect(nextState.byOrigin["https://example.com"]).toEqual(["n1"]);
    });
    it("should drop ids and prune empty origins on WEB_NOTIFICATIONS_REMOVED", () => {
      const prevState = {
        ...INITIAL_STATE.WebNotifications,
        notifications: { n1: notification },
        byOrigin: { "https://example.com": ["n1"] },
      };
      const nextState = WebNotifications(prevState, {
        type: at.WEB_NOTIFICATIONS_REMOVED,
        data: { removed: [{ origin: "https://example.com", id: "n1" }] },
      });
      expect(nextState.notifications).toEqual({});
      expect(nextState.byOrigin["https://example.com"]).toBeUndefined();
    });
  });
});
