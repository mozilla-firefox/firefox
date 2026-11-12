import { fireEvent, render } from "@testing-library/react";
import { Provider } from "react-redux";
import { createStore, combineReducers } from "redux";
import { actionTypes as at } from "common/Actions.mjs";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { PromoCard } from "content-src/components/DiscoveryStreamComponents/PromoCard/PromoCard";

const renderPromoCard = (state = INITIAL_STATE) => {
  const store = createStore(combineReducers(reducers), state);
  jest.spyOn(store, "dispatch");
  const { container } = render(
    <Provider store={store}>
      <PromoCard />
    </Provider>
  );

  return { container, dispatch: store.dispatch };
};

describe("<PromoCard>", () => {
  it("should render the wallpaper promo content", () => {
    const { container } = renderPromoCard();

    expect(container.querySelector(".promo-card-wrapper")).toBeInTheDocument();
    expect(container.querySelector(".img-wrapper img")).toHaveAttribute(
      "src",
      "chrome://newtab/content/data/content/assets/firefox-mascot-prop-paintbucket-rgb.svg"
    );
    expect(container.querySelector(".promo-card-title")).toHaveAttribute(
      "data-l10n-id",
      "newtab-promo-card-title-addons"
    );
    expect(container.querySelector(".promo-card-body")).toHaveAttribute(
      "data-l10n-id",
      "newtab-promo-card-body-addons"
    );
    expect(container.querySelector(".promo-card-cta")).toHaveAttribute(
      "data-l10n-id",
      "newtab-promo-card-cta-addons"
    );
  });

  it("should open the personalize panel from the CTA", () => {
    const { container, dispatch } = renderPromoCard();

    fireEvent.click(container.querySelector(".promo-card-cta"));

    expect(dispatch).toHaveBeenCalledTimes(2);
    expect(dispatch.mock.calls[0][0]).toMatchObject({
      type: at.PROMO_CARD_CLICK,
    });
    expect(dispatch.mock.calls[1][0]).toMatchObject({
      type: at.SHOW_PERSONALIZE,
      data: { source: "promo_card" },
    });
  });
});
