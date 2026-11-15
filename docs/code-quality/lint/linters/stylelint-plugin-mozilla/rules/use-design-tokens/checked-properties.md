% Generated from config.mjs by
% tools/lint/stylelint/stylelint-plugin-mozilla/scripts/update-docs.mjs.
% Don't edit it by hand; regenerate it with
% `./mach npm run update-docs --prefix tools/lint/stylelint/stylelint-plugin-mozilla`.

# Checked properties

The `use-design-tokens` rule checks every property below. The sections are the
property types in
{searchfox}`config.mjs <tools/lint/stylelint/stylelint-plugin-mozilla/config.mjs>`,
and a property listed under several types accepts a value that any of them
accepts. Where two of a property's types fix the same value, the first type in
its `validTypes` wins.

Every checked property also accepts `inherit`, `initial`, `revert`, `revert-layer` and `unset`.

(use-design-tokens-background-attachment)=

## Background attachment

- **Properties:** `background`
- **Non-token values:** `scroll`, `fixed`, `local`

(use-design-tokens-background-clip)=

## Background clip

- **Properties:** `background`
- **Non-token values:** `border-box`, `padding-box`, `content-box`

(use-design-tokens-background-color)=

## Background color

- **Properties:** `background-color`, `background`
- **Token categories:** `background-color`
- **Other tokens:** `--dragover-tab-group-color-invert`, `--dragover-tab-group-color-pale`, `--dragover-tab-group-color`, `--fc-accent-color`, `--fc-background`, `--fc-button-background-active`, `--fc-button-background-hover`, `--fc-button-background`, `--fc-dismiss-button-background-active`, `--fc-dismiss-button-background-hover`, `--fc-dismiss-button-background`, `--fc-primary-button-background-active`, `--fc-primary-button-background-hover`, `--fc-primary-button-background`, `--fxview-background-color-secondary`, `--fxview-element-background-active`, `--fxview-element-background-hover`, `--fxviewtabrow-element-background-active`, `--fxviewtabrow-element-background-hover`, `--identity-color`, `--identity-tab-color`, `--lwt-accent-color`, `--newtab-background-card`, `--newtab-background-color-secondary`, `--newtab-background-color`, `--newtab-button-active-background`, `--newtab-button-background`, `--newtab-button-focus-background`, `--newtab-button-hover-background`, `--newtab-button-secondary-color`, `--newtab-button-static-active-background`, `--newtab-button-static-background`, `--newtab-button-static-focus-background`, `--newtab-button-static-hover-background`, `--newtab-element-active-color`, `--newtab-element-hover-color`, `--newtab-element-secondary-active-color`, `--newtab-element-secondary-color`, `--newtab-element-secondary-hover-color`, `--newtab-overlay-color`, `--newtab-primary-action-background-pocket`, `--newtab-primary-action-background`, `--newtab-primary-element-active-color`, `--newtab-primary-element-hover-color`, `--newtab-primary-element-hover-pocket-color`, `--newtab-status-error`, `--newtab-text-primary-color`, `--newtab-text-secondary-color`, `--newtab-weather-background-color`, `--sidebar-background-color`, `--sidebar-box-background`, `--tab-group-color-invert`, `--tab-group-color-pale`, `--tab-group-color`, `--tab-group-background-color`, `--tab-group-text-color`, `--tab-group-background-color-hover`, `--tab-icon-fill-loading`, `--tabgroup-swatch-color-invert`, `--tabgroup-swatch-color`, `--toolbar-background-color`, `--toolbarbutton-background-color-active`, `--toolbarbutton-background-color-hover`, `--toolbox-background-color-inactive`, `--toolbox-background-color`, `--urlbar-box-background-color`, `--urlbar-box-background-color-focus`, `--urlbar-box-background-color-hover`, `--urlbar-box-background-color-active`, `--urlbarview-background-color-hover`, `--urlbarview-background-color-selected`, `--urlbarview-result-button-hover-background-color`, `--urlbarview-result-button-selected-background-color`, `--color-accent-primary`, `--color-accent-primary-hover`, `--color-accent-primary-active`, `--color-accent-primary-selected`, `--color-accent-attention`
- **Non-token values:** `transparent`, `currentColor`, `auto`, `normal`, `none`, `white`, `black`
- **Only through a local custom property:** tokens in `color`, `text-color`, `border-color`, `icon-color`; system colors
- **System colors:** a warning, not an error, when they are the whole value or when the tokens that `customSuggestions` maps them to give a valid value in their place
- **Autofix:**

  | Value | Fixed to |
  | ----- | -------- |
  | `#000` | `black` |
  | `#000000` | `black` |
  | `#fff` | `white` |
  | `#ffffff` | `white` |

(use-design-tokens-background-image)=

## Background image

- **Properties:** `background`
- **Non-token values:** `none`
- **Functions:** `url()`, `linear-gradient()`, `radial-gradient()`, `conic-gradient()`, `repeating-linear-gradient()`, `repeating-radial-gradient()`, `repeating-conic-gradient()`, `image-set()`

(use-design-tokens-background-position)=

## Background position

- **Properties:** `background`, `background-position`, `background-position-x`, `background-position-y`
- **Token categories:** `size`, `space`
- **Non-token values:** `0`, `top`, `bottom`, `left`, `right`, `center`
- **Units:** any unit

(use-design-tokens-background-repeat)=

## Background repeat

- **Properties:** `background`
- **Non-token values:** `repeat`, `repeat-x`, `repeat-y`, `no-repeat`, `space`, `round`

(use-design-tokens-background-size)=

## Background size

- **Properties:** `background`, `background-size`
- **Token categories:** `size`, `space`, `icon-size`
- **Non-token values:** `auto`, `cover`, `contain`
- **Functions:** `max()`
- **Units:** any unit

(use-design-tokens-border-color)=

## Border color

- **Properties:** `border`, `border-color`, `border-block`, `border-block-color`, `border-block-end`, `border-block-end-color`, `border-block-start`, `border-block-start-color`, `border-bottom`, `border-bottom-color`, `border-inline`, `border-inline-color`, `border-inline-end`, `border-inline-end-color`, `border-inline-start`, `border-inline-start-color`, `border-left`, `border-left-color`, `border-right`, `border-right-color`, `border-top`, `border-top-color`, `outline`, `outline-color`
- **Token categories:** `border-color`, `border`, `outline-color`, `outline`
- **Other tokens:** `--color-accent-primary`, `--color-accent-primary-hover`, `--color-accent-primary-active`, `--color-accent-primary-selected`, `--color-accent-attention`, `--panel-separator-color`
- **Non-token values:** `transparent`, `currentColor`, `white`, `black`, `auto`, `normal`, `none`, `0`
- **Only through a local custom property:** tokens in `color`, `background-color`, `text-color`; system colors
- **System colors:** a warning, not an error, when they are the whole value or when the tokens that `customSuggestions` maps them to give a valid value in their place
- **Autofix:**

  | Value | Fixed to |
  | ----- | -------- |
  | `#000` | `black` |
  | `#000000` | `black` |
  | `#fff` | `white` |
  | `#ffffff` | `white` |

(use-design-tokens-border-radius)=

## Border radius

- **Properties:** `border-radius`, `border-top-left-radius`, `border-top-right-radius`, `border-bottom-right-radius`, `border-bottom-left-radius`, `border-start-start-radius`, `border-start-end-radius`, `border-end-start-radius`, `border-end-end-radius`
- **Token categories:** `border-radius`
- **Non-token values:** `0`
- **Autofix:**

  | Value | Fixed to |
  | ----- | -------- |
  | `9999px` | `var(--border-radius-circle)` |
  | `2px` | `var(--border-radius-xsmall)` |
  | `4px` | `var(--border-radius-small)` |
  | `8px` | `var(--border-radius-medium)` |
  | `16px` | `var(--border-radius-large)` |
  | `50%` | `var(--border-radius-circle)` |
  | `100%` | `var(--border-radius-circle)` |
  | `1000px` | `var(--border-radius-circle)` |

(use-design-tokens-border-style)=

## Border style

- **Properties:** `border`, `border-block`, `border-block-end`, `border-block-start`, `border-bottom`, `border-inline`, `border-inline-end`, `border-inline-start`, `border-left`, `border-right`, `border-top`, `outline`
- **Other tokens:** `--default-focusring`
- **Non-token values:** `solid`, `dashed`, `dotted`, `double`, `groove`, `ridge`, `inset`, `outset`, `none`, `hidden`

(use-design-tokens-border-width)=

## Border width

- **Properties:** `border`, `border-width`, `border-block`, `border-block-width`, `border-block-end`, `border-block-end-width`, `border-block-start`, `border-block-start-width`, `border-bottom`, `border-bottom-width`, `border-inline`, `border-inline-width`, `border-inline-end`, `border-inline-end-width`, `border-inline-start`, `border-inline-start-width`, `border-left`, `border-left-width`, `border-right`, `border-right-width`, `border-top`, `border-top-width`, `outline`, `outline-width`
- **Token categories:** `border-width`, `outline-width`, `outline`
- **Non-token values:** `0`
- **Units:** any unit

(use-design-tokens-box-shadow)=

## Box shadow

- **Properties:** `box-shadow`
- **Token categories:** `box-shadow`
- **Non-token values:** `none`

(use-design-tokens-fill)=

## Fill

- **Properties:** `fill`
- **Token categories:** `icon-color`
- **Other tokens:** `--color-accent-primary`, `--color-accent-primary-hover`, `--color-accent-primary-active`, `--color-accent-primary-selected`, `--color-accent-attention`
- **Non-token values:** `none`, `context-fill`, `context-stroke`, `currentColor`, `transparent`, `white`, `black`
- **Functions:** `url()`
- **Only through a local custom property:** tokens in `color`, `background-color`, `border-color`, `text-color`, `outline`
- **Autofix:**

  | Value | Fixed to |
  | ----- | -------- |
  | `#000` | `black` |
  | `#000000` | `black` |
  | `#fff` | `white` |
  | `#ffffff` | `white` |

(use-design-tokens-flex-basis)=

## Flex basis

- **Properties:** `flex-basis`
- **Token categories:** `size`, `icon-size`
- **Non-token values:** `auto`, `fit-content`, `min-content`, `max-content`
- **Units:** `%`

(use-design-tokens-flex-shorthand)=

## Flex shorthand

- **Properties:** `flex`
- **Token categories:** `size`, `icon-size`
- **Non-token values:** `none`, `0`, `1`, `2`, `3`, `4`, `100`, `1000`, `10000`, `auto`, `fit-content`, `min-content`, `max-content`
- **Units:** `%`

(use-design-tokens-font-size)=

## Font size

- **Properties:** `font-size`
- **Token categories:** `font-size`
- **Non-token values:** `xx-small`, `x-small`, `small`, `medium`, `large`, `x-large`, `xx-large`, `xxx-large`, `smaller`, `larger`, `1em`

(use-design-tokens-font-weight)=

## Font weight

- **Properties:** `font-weight`
- **Token categories:** `font-weight`
- **Non-token values:** `normal`
- **Autofix:**

  | Value | Fixed to |
  | ----- | -------- |
  | `200` | `normal` |
  | `300` | `normal` |
  | `400` | `normal` |
  | `500` | `var(--font-weight-semibold)` |
  | `510` | `var(--font-weight-semibold)` |
  | `600` | `var(--font-weight-semibold)` |
  | `700` | `var(--font-weight-bold)` |
  | `800` | `var(--font-weight-bold)` |
  | `lighter` | `normal` |
  | `bold` | `var(--font-weight-bold)` |
  | `bolder` | `var(--font-weight-bold)` |

(use-design-tokens-opacity)=

## Opacity

- **Properties:** `opacity`
- **Token categories:** `opacity`
- **Non-token values:** `0`, `1`

(use-design-tokens-size)=

## Size

- **Properties:** `width`, `min-width`, `max-width`, `height`, `min-height`, `max-height`, `inline-size`, `min-inline-size`, `max-inline-size`, `block-size`, `min-block-size`, `max-block-size`, `inset`, `inset-block`, `inset-block-end`, `inset-block-start`, `inset-inline`, `inset-inline-end`, `inset-inline-start`, `left`, `right`, `top`, `bottom`
- **Token categories:** `size`, `icon-size`
- **Other tokens:** `--downloads-item-height`, `--menu-panel-width-wide`, `--panel-and-palette-icon-size`
- **Non-token values:** `0`, `1px`, `auto`, `none`, `fit-content`, `min-content`, `max-content`, `stretch`, `-moz-available`
- **Functions:** `max()`
- **Units:** `%`, `ch`, `em`, `vh`, `vw`
- **Autofix:**

  | Value | Fixed to |
  | ----- | -------- |
  | `40px` | `var(--size-image-xsmall)` |
  | `48px` | `var(--size-item-xlarge)` |
  | `56px` | `var(--size-image-medium)` |
  | `64px` | `var(--size-image-large)` |
  | `80px` | `var(--size-image-xlarge)` |
  | `96px` | `var(--size-image-xxlarge)` |
  | `12px` | `var(--size-item-xsmall)` |
  | `16px` | `var(--size-item-small)` |
  | `24px` | `var(--size-item-medium)` |
  | `32px` | `var(--size-item-large)` |
  | `100px` | `var(--size-layout-xsmall)` |
  | `200px` | `var(--size-layout-small)` |
  | `300px` | `var(--size-layout-medium)` |
  | `400px` | `var(--size-layout-large)` |
  | `500px` | `var(--size-layout-xlarge)` |
  | `280px` | `var(--size-sidebar)` |
  | `118px` | `var(--size-sidebar-narrow)` |
  | `20px` | `var(--icon-size-medium)` |
  | `0.75rem` | `var(--size-item-xsmall)` |
  | `1rem` | `var(--size-item-small)` |
  | `1.5rem` | `var(--size-item-medium)` |
  | `2rem` | `var(--size-item-large)` |
  | `3rem` | `var(--size-item-xlarge)` |

(use-design-tokens-space)=

## Space

- **Properties:** `border-spacing`, `margin`, `margin-block`, `margin-block-end`, `margin-block-start`, `margin-inline`, `margin-inline-end`, `margin-inline-start`, `margin-top`, `margin-right`, `margin-bottom`, `margin-left`, `padding`, `padding-block`, `padding-block-end`, `padding-block-start`, `padding-inline`, `padding-inline-end`, `padding-inline-start`, `padding-top`, `padding-right`, `padding-bottom`, `padding-left`, `gap`, `grid-gap`, `column-gap`, `row-gap`, `grid-column-gap`, `grid-row-gap`, `inset`, `inset-block`, `inset-block-end`, `inset-block-start`, `inset-inline`, `inset-inline-end`, `inset-inline-start`, `left`, `right`, `top`, `bottom`, `scroll-margin`, `scroll-margin-block`, `scroll-margin-block-end`, `scroll-margin-block-start`, `scroll-margin-bottom`, `scroll-margin-inline`, `scroll-margin-inline-end`, `scroll-margin-inline-start`, `scroll-margin-left`, `scroll-margin-right`, `scroll-margin-top`, `scroll-padding`, `scroll-padding-block`, `scroll-padding-block-end`, `scroll-padding-block-start`, `scroll-padding-bottom`, `scroll-padding-inline`, `scroll-padding-inline-end`, `scroll-padding-inline-start`, `scroll-padding-left`, `scroll-padding-right`, `scroll-padding-top`
- **Token categories:** `space`
- **Non-token values:** `-1px`, `0`, `1px`, `auto`
- **Units:** `ch`, `em`, `lh`
- **Autofix:**

  | Value | Fixed to |
  | ----- | -------- |
  | `2px` | `var(--space-xxsmall)` |
  | `4px` | `var(--space-xsmall)` |
  | `8px` | `var(--space-small)` |
  | `12px` | `var(--space-medium)` |
  | `16px` | `var(--space-large)` |
  | `24px` | `var(--space-xlarge)` |
  | `32px` | `var(--space-xxlarge)` |

(use-design-tokens-stroke)=

## Stroke

- **Properties:** `stroke`
- **Token categories:** `icon-color`
- **Other tokens:** `--color-accent-primary`, `--color-accent-primary-hover`, `--color-accent-primary-active`, `--color-accent-primary-selected`, `--color-accent-attention`
- **Non-token values:** `none`, `context-stroke`, `currentColor`, `transparent`, `white`, `black`
- **Functions:** `url()`
- **Only through a local custom property:** tokens in `color`, `background-color`, `border-color`, `text-color`, `outline`
- **Autofix:**

  | Value | Fixed to |
  | ----- | -------- |
  | `#000` | `black` |
  | `#000000` | `black` |
  | `#fff` | `white` |
  | `#ffffff` | `white` |

(use-design-tokens-text-color)=

## Text color

- **Properties:** `color`
- **Token categories:** `text-color`, `icon-color`
- **Other tokens:** `--color-accent-primary`, `--color-accent-primary-hover`, `--color-accent-primary-active`, `--color-accent-primary-selected`, `--color-accent-attention`, `--toolbarseparator-color`
- **Non-token values:** `currentColor`, `white`, `black`
- **Only through a local custom property:** tokens in `color`, `background-color`, `border-color`; system colors
- **System colors:** a warning, not an error, when they are the whole value or when the tokens that `customSuggestions` maps them to give a valid value in their place
- **Autofix:**

  | Value | Fixed to |
  | ----- | -------- |
  | `#000` | `black` |
  | `#000000` | `black` |
  | `#fff` | `white` |
  | `#ffffff` | `white` |
