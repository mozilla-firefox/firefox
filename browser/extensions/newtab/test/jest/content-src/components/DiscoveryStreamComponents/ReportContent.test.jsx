import { fireEvent, render } from "@testing-library/react";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import {
  PREF_REPORT_ADS_NEW_REASONS_ENABLED,
  ReportContent,
} from "content-src/components/DiscoveryStreamComponents/ReportContent/ReportContent";
import { combineReducers, createStore } from "redux";
import { Provider } from "react-redux";
import { actionCreators as ac } from "common/Actions.mjs";

const DEFAULT_PROPS = {
  dispatch() {},
  prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "discoverystream.sections.enabled": true,
      "unifiedAds.spocs.enabled": true,
    },
  },
};

const BASE_REPORT = {
  visible: true,
  url: "https://example.com",
  position: 1,
  reporting_url: "https://example.com/report",
};

function testState({ card_type, visible, prefs = {} }) {
  return {
    Prefs: {
      ...DEFAULT_PROPS.prefs,
      values: { ...DEFAULT_PROPS.prefs.values, ...prefs },
    },
    DiscoveryStream: {
      ...INITIAL_STATE.DiscoveryStream,
      report: {
        ...BASE_REPORT,
        card_type,
        visible,
      },
    },
  };
}

// Wrap this around any component that uses useSelector,
// or any mount that uses a child that uses redux.
function WrapWithProvider({ children, state, dispatch }) {
  const store = createStore(combineReducers(reducers), state);
  if (dispatch) {
    store.dispatch = dispatch;
  }
  return <Provider store={store}>{children}</Provider>;
}

// patch dialog element's .showModal()/close() functions to prevent errors in tests
beforeAll(() => {
  if (typeof HTMLDialogElement !== "undefined") {
    HTMLDialogElement.prototype.showModal = function () {
      this.open = true;
    };
    HTMLDialogElement.prototype.close = function () {
      this.open = false;
    };
  }
});

describe("Discovery Stream <ReportContent>", () => {
  let wrapper;
  let dispatch;

  beforeEach(() => {
    dispatch = jest.fn();

    wrapper = render(
      <WrapWithProvider>
        <ReportContent
          dispatch={dispatch}
          {...DEFAULT_PROPS}
          spocs={{ spocs: { data: {} } }}
        />
      </WrapWithProvider>
    );
  });

  afterEach(() => {
    jest.restoreAllMocks();
  });

  it("should render", () => {
    expect(wrapper.container.firstChild).toBeTruthy();
    expect(
      wrapper.container.querySelector(".report-content-form")
    ).toBeInTheDocument();
  });

  it("should open modal if report.visible is true", () => {
    const state = testState({ visible: true });

    wrapper = render(
      <WrapWithProvider state={state}>
        <ReportContent spocs={{ spocs: { data: {} } }} />
      </WrapWithProvider>
    );
    expect(wrapper.container.querySelector("dialog").open).toBe(true);
  });

  it("should close modal if report.visible is false", () => {
    const state = testState({ visible: false });

    wrapper = render(
      <WrapWithProvider state={state}>
        <ReportContent spocs={{ spocs: { data: {} } }} />
      </WrapWithProvider>
    );

    expect(wrapper.container.querySelector("dialog").open).toBe(false);
  });

  it("should render ad reporting options if card_type is spoc", () => {
    const state = testState({ card_type: "spoc" });

    // in the ReportContent.jsx file, spocs.spocs.data is used to grab spoc data
    wrapper = render(
      <WrapWithProvider state={state}>
        <ReportContent spocs={{ spocs: { data: {} } }} />
      </WrapWithProvider>
    );

    expect(
      wrapper.container.querySelector(".report-ads-options")
    ).toBeInTheDocument();

    // test to make sure content options aren't displayed when report ads is open
    expect(
      wrapper.container.querySelectorAll(".report-content-options").length
    ).toBe(0);
  });

  function renderAdReport(prefs = {}) {
    const state = testState({ card_type: "spoc", visible: true, prefs });
    wrapper = render(
      <WrapWithProvider state={state} dispatch={dispatch}>
        <ReportContent spocs={{ spocs: { data: {} } }} />
      </WrapWithProvider>
    );
  }

  function adReasonValues() {
    return [
      ...wrapper.container.querySelectorAll(".report-ads-options moz-radio"),
    ].map(radio => radio.getAttribute("value"));
  }

  const LEGACY_AD_REASON_VALUES = [
    "not_interested",
    "inappropriate",
    "seen_too_many_times",
  ];
  const NEW_REASONS_ON = {
    [PREF_REPORT_ADS_NEW_REASONS_ENABLED]: true,
    adsNewReportReasonsSupported: true,
  };

  it("should render legacy ad reporting reasons when new reasons pref is off", () => {
    renderAdReport();
    expect(adReasonValues()).toEqual(LEGACY_AD_REASON_VALUES);
  });

  it("should render new ad reporting reasons when new reasons pref is on", () => {
    renderAdReport(NEW_REASONS_ON);
    expect(adReasonValues()).toEqual(["scam", "misleading", "offensive"]);
  });

  it("should render legacy ad reporting reasons when new reasons are not supported", () => {
    renderAdReport({ ...NEW_REASONS_ON, adsNewReportReasonsSupported: false });
    expect(adReasonValues()).toEqual(LEGACY_AD_REASON_VALUES);
  });

  it("should dispatch REPORT_AD_SUBMIT with the selected new reason", () => {
    renderAdReport(NEW_REASONS_ON);

    const scamRadio = wrapper.container.querySelector(
      'moz-radio[value="scam"]'
    );
    // moz-radio isn't defined in jsdom, so set .value directly and dispatch
    // a bubbling change event for the radio group's listener to pick up.
    scamRadio.value = "scam";
    fireEvent(scamRadio, new Event("change", { bubbles: true }));
    fireEvent.click(
      wrapper.container.querySelector("moz-button.submit-report-btn")
    );

    const [firstCall] = dispatch.mock.calls;
    expect(firstCall[0]).toMatchObject(
      ac.AlsoToMain({
        type: "REPORT_AD_SUBMIT",
        data: { report_reason: "scam" },
      })
    );
  });

  it("should render content reporting options if card_type is organic", () => {
    const state = testState({ card_type: "organic" });

    // in the ReportContent.jsx file, spocs.spocs.data is used to grab spoc data
    wrapper = render(
      <WrapWithProvider state={state}>
        <ReportContent spocs={{}} />
      </WrapWithProvider>
    );

    expect(
      wrapper.container.querySelector(".report-content-options")
    ).toBeInTheDocument();

    // test to make sure ad options aren't displayed when report content is open
    expect(
      wrapper.container.querySelectorAll(".report-ads-options").length
    ).toBe(0);
  });

  it("should dispatch REPORT_CLOSE when cancel button is clicked", () => {
    const state = testState({ visible: true });

    wrapper = render(
      <WrapWithProvider state={state} dispatch={dispatch}>
        <ReportContent spocs={{}} />
      </WrapWithProvider>
    );

    // Cancel button implementation
    const cancelButton = wrapper.container.querySelector(
      "moz-button.cancel-report-btn"
    );
    expect(cancelButton).toBeInTheDocument();
    fireEvent.click(cancelButton);

    expect(dispatch).toHaveBeenCalledTimes(1);

    const [call] = dispatch.mock.calls;
    expect(call[0]).toEqual(
      ac.AlsoToMain({
        type: "REPORT_CLOSE",
      })
    );
  });

  it("should dispatch REPORT_CONTENT_SUBMIT, BLOCK_URL, and SHOW_TOAST_MESSAGE when submit button is clicked", () => {
    const state = testState({ visible: true, card_type: "organic" });

    wrapper = render(
      <WrapWithProvider state={state} dispatch={dispatch}>
        <ReportContent spocs={{}} />
      </WrapWithProvider>
    );

    // Submit button implementation
    const submitButton = wrapper.container.querySelector(
      "moz-button.submit-report-btn"
    );
    expect(submitButton).toBeInTheDocument();
    fireEvent.click(submitButton);

    // Assert both action types were dispatched
    expect(dispatch).toHaveBeenCalledTimes(3);

    const [firstCall, secondCall, thirdCall] = dispatch.mock.calls;

    // Using .match instead of .deepEqual because submitting a report passes a lot of data during dispatch. And using .match makes it so we don't have to write out all the data
    expect(firstCall[0]).toMatchObject(
      ac.AlsoToMain({
        type: "REPORT_CONTENT_SUBMIT",
      })
    );

    expect(secondCall[0]).toMatchObject(
      ac.AlsoToMain({
        type: "BLOCK_URL",
      })
    );

    expect(thirdCall[0]).toMatchObject(
      ac.OnlyToOneContent(
        {
          type: "SHOW_TOAST_MESSAGE",
        },
        "ActivityStream:Content"
      )
    );
  });

  it("should clear selected radio button when modal closes", () => {
    // Initial render with modal visible
    const openState = testState({ visible: true });

    wrapper = render(
      <WrapWithProvider state={openState} dispatch={dispatch}>
        <ReportContent spocs={{ spocs: {} }} />
      </WrapWithProvider>
    );

    // Select the first radio button
    const radioGroup = wrapper.container.querySelector("moz-radio-group");
    const firstRadio = radioGroup.querySelector("moz-radio");

    firstRadio.setAttribute("checked", "true");

    // Assert it's checked before modal closes
    expect(firstRadio.hasAttribute("checked")).toBe(true);

    // Simulate closing the modal to clear the radio button
    const closedState = testState({ visible: false });

    wrapper = render(
      <WrapWithProvider state={closedState} dispatch={dispatch}>
        <ReportContent spocs={{ spocs: {} }} />
      </WrapWithProvider>
    );

    const updatedRadioGroup =
      wrapper.container.querySelector("moz-radio-group");
    const updatedFirstRadio = updatedRadioGroup.querySelector("moz-radio");

    // The previously checked radio should now be unchecked
    expect(updatedFirstRadio.hasAttribute("checked")).toBe(false);
  });
});
