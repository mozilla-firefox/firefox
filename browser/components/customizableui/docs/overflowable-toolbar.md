# OverflowableToolbar

The `OverflowableToolbar` class is used for toolbars that, upon shrinking
in width, can overflow their contents into an "overflow panel". In practice,
this is really only the browser navigation toolbar - though in times past, it
was possible to create multiple overflowable toolbars.

CustomizableUI creates the instance when it registers the toolbar and stores
it on the toolbar node as its `overflowable` property, so
`document.getElementById("nav-bar").overflowable.show()` opens the navigation
toolbar's overflow panel.

```{js:autoclass} OverflowableToolbar
:members:
:private-members:
```
