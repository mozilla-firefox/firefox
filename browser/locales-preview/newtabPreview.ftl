# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Use this file for New Tab features that ship to English locales only (e.g. en-US, en-CA, en-GB).
# These strings are not translated. The English file is packaged for every locale, both in
# omni.ja and in the New Tab train hop XPI, so localized builds show these strings in English.

# Shown in a widget card in place of the widget, until there is a widget to
# show.
newtab-widget-placeholder = Watch this space

newtab-wallpaper-category-title-predecessor = Predecessor

## Solitaire widget

# Context menu item linking to the Yahoo website, the provider of the solitaire game.
newtab-solitaire-menu-powered-by-yahoo = Powered by Yahoo

# Tooltip and screen reader label for the icon-only button that closes the
# expanded Solitaire game and returns the widget to its card.
newtab-solitaire-widget-close-button =
    .title = Close
    .aria-label = Close Solitaire

## Crossword widget

newtab-crossword-widget-title = Daily crossword
# Accessible name for the iframe that contains the crossword puzzle.
newtab-crossword-widget-frame =
    .title = Crossword
newtab-crossword-widget-menu-show-clues = Show Clues
# Shows articles related to today’s crosswords
newtab-crossword-widget-menu-show-articles = Show Articles
newtab-crossword-widget-menu-solve-puzzle = Solve Puzzle
# Context menu item linking to the Particle website, the provider of the crossword.
newtab-crossword-menu-powered-by-particle = Powered by Particle
newtab-crossword-widget-menu-learn-more = Learn More

## Editors’ pick

# Badge shown on a story that was chosen by human editors. The title is shown
# as a tooltip when hovering the badge.
newtab-editors-pick-badge =
    .label = Editors’ pick
    .title = { -brand-product-name } editors recommend this story
