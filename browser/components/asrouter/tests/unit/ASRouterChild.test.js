/*eslint max-nested-callbacks: ["error", 10]*/
import { ASRouterChild } from "actors/ASRouterChild.sys.mjs";
import { MESSAGE_TYPE_HASH as msg } from "modules/ActorConstants.mjs";

describe("ASRouterChild", () => {
  let asRouterChild = null;
  let sandbox = null;
  beforeEach(() => {
    sandbox = sinon.createSandbox();
    asRouterChild = new ASRouterChild();
    asRouterChild.telemetry = {
      sendTelemetry: sandbox.stub(),
    };
    sandbox.stub(asRouterChild, "sendAsyncMessage");
    sandbox.stub(asRouterChild, "sendQuery").returns(Promise.resolve());
  });
  afterEach(() => {
    sandbox.restore();
    asRouterChild = null;
  });
  describe("asRouterMessage", () => {
    describe("uses sendAsyncMessage for types that don't need an async response", () => {
      [
        msg.DISABLE_PROVIDER,
        msg.ENABLE_PROVIDER,
        msg.EXPIRE_QUERY_CACHE,
        msg.IMPRESSION,
        msg.RESET_PROVIDER_PREF,
        msg.SET_PROVIDER_USER_PREF,
      ].forEach(type => {
        it(`type ${type}`, () => {
          asRouterChild.asRouterMessage({
            type,
            data: {
              something: 1,
            },
          });
          sandbox.assert.calledOnce(asRouterChild.sendAsyncMessage);
          sandbox.assert.calledWith(asRouterChild.sendAsyncMessage, type, {
            something: 1,
          });
        });
      });
    });
    it("uses sendQuery for USER_ACTION so callers can await the result", () => {
      asRouterChild.asRouterMessage({
        type: msg.USER_ACTION,
        data: { something: 1 },
      });
      sandbox.assert.notCalled(asRouterChild.sendAsyncMessage);
      sandbox.assert.calledWith(asRouterChild.sendQuery, msg.USER_ACTION, {
        something: 1,
      });
    });
    // Some legacy privileged extensions still send this legacy NEWTAB_MESSAGE_REQUEST
    // action type. We simply
    it("can accept the legacy NEWTAB_MESSAGE_REQUEST message without throwing", async () => {
      assert.doesNotThrow(async () => {
        let result = await asRouterChild.asRouterMessage({
          type: "NEWTAB_MESSAGE_REQUEST",
          data: {},
        });
        sandbox.assert.deepEqual(result, {});
        sandbox.assert.notCalled(asRouterChild.sendAsyncMessage);
      });
    });
  });
});
