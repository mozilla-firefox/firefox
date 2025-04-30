import React from "react";
import { mount } from "enzyme";
import { Provider } from "react-redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { MessageWrapper } from "content-src/components/MessageWrapper/MessageWrapper";
import { createStore, combineReducers } from "redux";

// Mock child component
const MockChild = props => (
  <div data-is-intersecting={props.isIntersecting}></div>
);

// Wrap this around any component that uses useSelector,
// or any mount that uses a child that uses redux.
function WrapWithProvider({ children, state = INITIAL_STATE }) {
  let store = createStore(combineReducers(reducers), state);
  return <Provider store={store}>{children}</Provider>;
}

describe("MessageWrapper Component", () => {
  let wrapper;
  let sandbox;
  let dispatch;
  let observerStub;

  beforeEach(() => {
    sandbox = sinon.createSandbox();
    dispatch = sandbox.stub();
    observerStub = sandbox
      .stub(window, "IntersectionObserver")
      .callsFake(function (cb) {
        this.observe = sandbox.spy();
        this.unobserve = sandbox.spy();
        this.disconnect = sandbox.spy();
        this.callback = cb;
      });

    let state = {
      ...INITIAL_STATE,
      Messages: {
        isHidden: false,
        messageData: { id: "test-message-id" },
      },
    };

    wrapper = mount(
      <WrapWithProvider state={state}>
        <MessageWrapper
          dispatch={dispatch}
          document={{
            visibilityState: "visible",
          }}
        >
          <MockChild />
        </MessageWrapper>
      </WrapWithProvider>
    );
  });

  afterEach(() => {
    sandbox.restore();
  });

  it("should render", () => {
    assert.ok(wrapper.exists());
    assert.ok(wrapper.find(".message-wrapper").exists());
  });

  it("should not render if `Messages.isHidden` is true and hiddenOverride is false", () => {
    wrapper = mount(
      <WrapWithProvider
        state={{
          ...INITIAL_STATE,
          Messages: {
            isHidden: true,
            messageData: { id: "test-message-id" },
          },
        }}
      >
        <MessageWrapper
          hiddenOverride={false}
          dispatch={dispatch}
          document={{
            visibilityState: "visible",
          }}
        >
          <MockChild />
        </MessageWrapper>
      </WrapWithProvider>
    );

    assert.isFalse(wrapper.find(".message-wrapper").exists());
  });

  it("dispatches MESSAGE_IMPRESSION when intersecting", () => {
    // Manually trigger the intersection observer callback
    const child = wrapper.find(MockChild);
    assert.isFalse(child.prop("isIntersecting"));
    const observerInstance = observerStub.getCall(0).returnValue;
    const observedElement = wrapper.find(".message-wrapper").getDOMNode();
    // Simulate an intersection
    observerInstance.callback([
      { isIntersecting: true, target: observedElement },
    ]);
    // Expect dispatch to have been called once
    assert.calledOnce(dispatch);
  });
});
