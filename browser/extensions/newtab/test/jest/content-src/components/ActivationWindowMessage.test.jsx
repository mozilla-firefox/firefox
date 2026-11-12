import { fireEvent, render } from "@testing-library/react";
import { ActivationWindowMessage } from "content-src/components/ActivationWindowMessage/ActivationWindowMessage";

describe("<ActivationWindowMessage>", () => {
  let handleBlock;
  let handleClick;
  let handleDismiss;
  let dispatch;

  beforeEach(() => {
    handleBlock = jest.fn();
    handleClick = jest.fn();
    handleDismiss = jest.fn();
    dispatch = jest.fn();
  });

  function renderMessage(messageData) {
    return render(
      <ActivationWindowMessage
        dispatch={dispatch}
        handleBlock={handleBlock}
        handleClick={handleClick}
        handleDismiss={handleDismiss}
        messageData={messageData}
      />
    );
  }

  it("should render with correct structure for hardcoded strings", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test Heading",
        message: "Test Message",
        imageSrc: "chrome://test/image.png",
        primaryButton: { label: "Primary", action: {} },
        secondaryButton: { label: "Secondary", action: {} },
      },
    });

    expect(
      container.querySelector("aside.activation-window-message")
    ).toBeInTheDocument();

    const img = container.querySelector("img");
    expect(img).toBeInTheDocument();
    expect(img).toHaveAttribute("src", "chrome://test/image.png");

    const heading = container.querySelector("h2");
    expect(heading).toBeInTheDocument();
    expect(heading).toHaveTextContent("Test Heading");
    expect(heading).not.toHaveAttribute("data-l10n-id");

    const message = container.querySelector("p");
    expect(message).toBeInTheDocument();
    expect(message).toHaveTextContent("Test Message");
    expect(message).not.toHaveAttribute("data-l10n-id");
  });

  it("should render heading with Fluent ID", () => {
    const { container } = renderMessage({
      content: {
        heading: { string_id: "test-heading-id" },
        message: "Test Message",
        primaryButton: { label: "Primary", action: {} },
      },
    });

    const heading = container.querySelector("h2");
    expect(heading).toBeInTheDocument();
    expect(heading).toHaveAttribute("data-l10n-id", "test-heading-id");
    expect(heading).toHaveTextContent("");
  });

  it("should render message with Fluent ID", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test Heading",
        message: { string_id: "test-message-id" },
        primaryButton: { label: "Primary", action: {} },
      },
    });

    const message = container.querySelector("p");
    expect(message).toBeInTheDocument();
    expect(message).toHaveAttribute("data-l10n-id", "test-message-id");
    expect(message).toHaveTextContent("");
  });

  it("should render dismiss button", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        primaryButton: { label: "Primary", action: {} },
      },
    });

    const dismissButton = container.querySelector(
      "moz-button[type='icon ghost']"
    );
    expect(dismissButton).toBeInTheDocument();
    expect(dismissButton).toHaveAttribute(
      "iconSrc",
      "chrome://global/skin/icons/close.svg"
    );
    expect(dismissButton).toHaveAttribute(
      "data-l10n-id",
      "newtab-activation-window-message-dismiss-button"
    );
  });

  it("should call handleDismiss and handleBlock when dismiss button is clicked", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        primaryButton: { label: "Primary", action: {} },
      },
    });

    fireEvent.click(container.querySelector("moz-button[type='icon ghost']"));
    expect(handleDismiss).toHaveBeenCalledTimes(1);
    expect(handleBlock).toHaveBeenCalledTimes(1);
  });

  it("should render fallback image if imageSrc not provided", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        primaryButton: { label: "Primary", action: {} },
      },
    });

    const img = container.querySelector("img");
    expect(img).toBeInTheDocument();
    expect(img).toHaveAttribute(
      "src",
      "chrome://newtab/content/data/content/assets/kit-in-circle.svg"
    );
    expect(img).toHaveAttribute("role", "presentation");
  });

  it("should render primary button with plain text label", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        primaryButton: { label: "Click Me", action: {} },
      },
    });

    const primaryButton = container.querySelector("moz-button[type='primary']");
    expect(primaryButton).toBeInTheDocument();
    expect(primaryButton).toHaveTextContent("Click Me");
    expect(primaryButton).not.toHaveAttribute("data-l10n-id");
  });

  it("should render primary button with Fluent ID", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        primaryButton: {
          label: { string_id: "test-primary-button" },
          action: {},
        },
      },
    });

    const primaryButton = container.querySelector("moz-button[type='primary']");
    expect(primaryButton).toBeInTheDocument();
    expect(primaryButton).toHaveAttribute(
      "data-l10n-id",
      "test-primary-button"
    );
    expect(primaryButton).toHaveTextContent("");
  });

  it("should render secondary button with plain text label", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        secondaryButton: { label: "Dismiss", action: { dismiss: true } },
      },
    });

    const secondaryButton = container.querySelector(
      "moz-button[type='default']"
    );
    expect(secondaryButton).toBeInTheDocument();
    expect(secondaryButton).toHaveTextContent("Dismiss");
    expect(secondaryButton).not.toHaveAttribute("data-l10n-id");
  });

  it("should render secondary button with Fluent ID", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        secondaryButton: {
          label: { string_id: "test-secondary-button" },
          action: { dismiss: true },
        },
      },
    });

    const secondaryButton = container.querySelector(
      "moz-button[type='default']"
    );
    expect(secondaryButton).toBeInTheDocument();
    expect(secondaryButton).toHaveAttribute(
      "data-l10n-id",
      "test-secondary-button"
    );
    expect(secondaryButton).toHaveTextContent("");
  });

  it("should not render primary button if not provided", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        secondaryButton: { label: "Dismiss", action: { dismiss: true } },
      },
    });

    expect(
      container.querySelector("moz-button[type='primary']")
    ).not.toBeInTheDocument();
  });

  it("should not render secondary button if not provided", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        primaryButton: { label: "Click", action: {} },
      },
    });

    expect(
      container.querySelector("moz-button[type='default']")
    ).not.toBeInTheDocument();
  });

  it("should not render moz-button-group if no buttons provided", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
      },
    });

    expect(container.querySelector("moz-button-group")).not.toBeInTheDocument();
  });

  it("should apply no-buttons class when no buttons provided", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
      },
    });

    const aside = container.querySelector("aside");
    expect(aside).toHaveClass("activation-window-message", "no-buttons");
  });

  it("should not apply no-buttons class when buttons are provided", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        primaryButton: { label: "Primary", action: {} },
      },
    });

    const aside = container.querySelector("aside");
    expect(aside).toHaveClass("activation-window-message");
    expect(aside).not.toHaveClass("no-buttons");
  });

  it("should render image with role presentation when imageSrc is provided", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        imageSrc: "chrome://test/image.png",
        primaryButton: { label: "Primary", action: {} },
      },
    });

    const img = container.querySelector("img");
    expect(img).toBeInTheDocument();
    expect(img).toHaveAttribute("src", "chrome://test/image.png");
    expect(img).toHaveAttribute("role", "presentation");
  });

  it("should call handleClick when primary button is clicked", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        primaryButton: { label: "Click", action: {} },
      },
    });

    fireEvent.click(container.querySelector("moz-button[type='primary']"));
    expect(handleClick).toHaveBeenCalledTimes(1);
    expect(handleClick).toHaveBeenCalledWith("primary-button");
  });

  it("should dispatch SHOW_PERSONALIZE with its source when the primary action asks for it", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        primaryButton: {
          label: "Customize",
          action: { type: "SHOW_PERSONALIZE" },
        },
      },
    });

    fireEvent.click(container.querySelector("moz-button[type='primary']"));
    expect(dispatch).toHaveBeenCalledWith({
      type: "SHOW_PERSONALIZE",
      data: { source: "activation_window_message" },
    });
  });

  it("should call handleClick when secondary button is clicked", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        secondaryButton: { label: "Dismiss", action: { dismiss: true } },
      },
    });

    fireEvent.click(container.querySelector("moz-button[type='default']"));
    expect(handleClick).toHaveBeenCalledTimes(1);
    expect(handleClick).toHaveBeenCalledWith("secondary-button");
  });

  it("should call handleDismiss and handleBlock when primary button with dismiss action is clicked", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        primaryButton: { label: "Got It", action: { dismiss: true } },
      },
    });

    fireEvent.click(container.querySelector("moz-button[type='primary']"));
    expect(handleClick).toHaveBeenCalledTimes(1);
    expect(handleDismiss).toHaveBeenCalledTimes(1);
    expect(handleBlock).toHaveBeenCalledTimes(1);
  });

  it("should call handleDismiss and handleBlock when secondary button with dismiss action is clicked", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        secondaryButton: { label: "Dismiss", action: { dismiss: true } },
      },
    });

    fireEvent.click(container.querySelector("moz-button[type='default']"));
    expect(handleClick).toHaveBeenCalledTimes(1);
    expect(handleDismiss).toHaveBeenCalledTimes(1);
    expect(handleBlock).toHaveBeenCalledTimes(1);
  });

  it("should not call handleDismiss or handleBlock when button without dismiss action is clicked", () => {
    const { container } = renderMessage({
      content: {
        heading: "Test",
        message: "Test",
        primaryButton: {
          label: "Learn More",
          action: { type: "OPEN_URL", data: {} },
        },
      },
    });

    fireEvent.click(container.querySelector("moz-button[type='primary']"));
    expect(handleClick).toHaveBeenCalledTimes(1);
    expect(handleDismiss).not.toHaveBeenCalled();
    expect(handleBlock).not.toHaveBeenCalled();
  });
});
