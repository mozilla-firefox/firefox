# PanelMultiView

Allows a popup panel to host multiple subviews. The main view shown when the
panel is opened may slide out to display a subview, which in turn may lead to
other subviews in a cascade menu pattern.

The \<panel> element should contain a \<panelmultiview> element. Views are
declared using \<panelview> elements that are usually children of the main
\<panelmultiview> element, although they don't need to be, as views can also
be imported into the panel from other panels or popup sets.

The panel should be opened asynchronously using the openPopup static method
on the PanelMultiView object. This will display the view specified using the
mainViewId attribute on the contained \<panelmultiview> element.

Specific subviews can slide in using the showSubView method, and backwards
navigation can be done using the goBack method or through a button in the
subview headers.

The process of displaying the main view or a new subview requires multiple
steps to be completed, hence at any given time the \<panelview> element may
be in different states:

-- Open or closed

All the \<panelview> elements start "closed", meaning that they are not
associated to a \<panelmultiview> element and can be located anywhere in
the document. When the openPopup or showSubView methods are called, the
relevant view becomes "open" and the \<panelview> element may be moved to
ensure it is a descendant of the \<panelmultiview> element.

The "ViewShowing" event is fired at this point, when the view is not
visible yet. The event is allowed to cancel the operation, in which case
the view is closed immediately.

Closing the view does not move the node back to its original position.

In browser windows, the views declared in the appMenu-viewCache template,
including the main view of the app menu and its subviews, stay in the
template's content, outside the document, until they are first shown.
The document's getElementById method doesn't find such a view. The
PanelMultiView.getViewNode static method looks in the document first and
then in the template, so use it to retrieve a view by ID.

-- Visible or invisible

This indicates whether the view is visible in the document from a layout
perspective, regardless of whether it is currently scrolled into view. In
fact, all subviews are already visible before they start sliding in.

Before scrolling into view, a view may become visible but be placed in a
special off-screen area of the document where layout and measurements can
take place asyncronously.

When navigating forward, an open view may become invisible but stay open
after sliding out of view. The last known size of these views is still
taken into account for determining the overall panel size.

When navigating backwards, an open subview will first become invisible and
then will be closed.

-- Active or inactive

This indicates whether the view is fully scrolled into the visible area
and ready to receive mouse and keyboard events. An active view is always
visible, but a visible view may be inactive. For example, during a scroll
transition, both views will be inactive.

When a view becomes active, the ViewShown event is fired synchronously,
and the showSubView and goBack methods can be called for navigation.

For the main view of the panel, the ViewShown event is dispatched during
the "popupshown" event, which means that other "popupshown" handlers may
be called before the view is active. Thus, code that needs to perform
further navigation automatically should either use the ViewShown event or
wait for an event loop tick, like BrowserTestUtils.waitForEvent does.

-- Navigating with the keyboard

An open view may keep state related to keyboard navigation, even if it is
invisible. When a view is closed, keyboard navigation state is cleared.

This diagram shows how \<panelview> nodes move during navigation:

```
In this <panelmultiview>     In other panels    Action
          ┌───┬───┬───┐        ┌───┬───┐
          │(A)│ B │ C │        │ D │ E │          Open panel
          └───┴───┴───┘        └───┴───┘
      ┌───┬───┬───┐            ┌───┬───┐
      │{A}│(C)│ B │            │ D │ E │          Show subview C
      └───┴───┴───┘            └───┴───┘
  ┌───┬───┬───┬───┐            ┌───┐
  │{A}│{C}│(D)│ B │            │ E │              Show subview D
  └───┴───┴───┴───┘            └───┘
    │ ┌───┬───┬───┬───┐        ┌───┐
    │ │{A}│(C)│ D │ B │        │ E │              Go back
    │ └───┴───┴───┴───┘        └───┘
    │   │   │
    │   │   └── Currently visible view
    │   │   │
    └───┴───┴── Open views
```

```{js:autoclass} AssociatedToNode
:members:
:private-members:
```

```{js:autoclass} PanelMultiView
:members:
:private-members:
```

```{js:autoclass} PanelView
:members:
:private-members:
```
