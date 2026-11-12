import { fireEvent, render } from "@testing-library/react";
import { actionTypes as at } from "common/Actions.mjs";
import { PersonalizedCard } from "content-src/components/DiscoveryStreamComponents/PersonalizedCard/PersonalizedCard";

function renderCard(dispatch = jest.fn()) {
  return render(
    <PersonalizedCard
      dispatch={dispatch}
      handleDismiss={jest.fn()}
      handleClick={jest.fn()}
      handleBlock={jest.fn()}
      messageData={{
        content: {
          cardTitle: "",
          cardMessage: "",
          ctaText: "",
          linkUrl: "",
          linkText: "",
        },
      }}
    />
  );
}

describe("<PersonalizedCard>", () => {
  it("should render", () => {
    const { container } = renderCard();
    expect(
      container.querySelector(".personalized-card-wrapper")
    ).toBeInTheDocument();
  });

  it("should dispatch SHOW_PERSONALIZE with its source when the toggle is clicked", () => {
    const dispatch = jest.fn();
    const { container } = renderCard(dispatch);
    fireEvent.click(container.querySelector(".personalized-card-button"));
    expect(dispatch).toHaveBeenCalledWith({
      type: at.SHOW_PERSONALIZE,
      data: { source: "personalized_card" },
    });
  });
});
