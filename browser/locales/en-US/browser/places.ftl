# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

places-open =
  .label = Open
  .accesskey = O
places-open-in-tab =
  .label = Open in New Tab
  .accesskey = w
places-open-in-container-tab =
  .label = Open in New Container Tab
  .accesskey = i
places-open-all-bookmarks =
  .label = Open All Bookmarks
  .accesskey = O
places-open-all-in-tabs =
  .label = Open All in Tabs
  .accesskey = O
places-open-in-window =
  .label = Open in New Window
  .accesskey = N
places-open-in-private-window =
  .label = Open in New Private Window
  .accesskey = P

places-empty-bookmarks-folder =
  .label = (Empty)

places-add-bookmark =
  .label = Add Bookmark…
  .accesskey = B
places-add-folder-contextmenu =
  .label = Add Folder…
  .accesskey = F
places-add-folder =
  .label = Add Folder…
  .accesskey = o
places-add-separator =
  .label = Add Separator
  .accesskey = S

places-view =
  .label = View
  .accesskey = w
places-by-date =
  .label = By Date
  .accesskey = D
places-by-site =
  .label = By Site
  .accesskey = S
places-by-most-visited =
  .label = By Most Visited
  .accesskey = V
places-by-last-visited =
  .label = By Last Visited
  .accesskey = L
places-by-day-and-site =
  .label = By Date and Site
  .accesskey = t

places-history-search =
  .placeholder = Search history
places-history =
  .aria-label = History
places-bookmarks-search =
  .placeholder = Search bookmarks

places-forget-domain-data =
  .label = Forget About This Site…
  .accesskey = F
places-sortby-name =
  .label = Sort By Name
  .accesskey = r
# places-edit-bookmark and places-edit-generic will show one or the other and can have the same access key.
places-edit-bookmark =
  .label = Edit Bookmark…
  .accesskey = E
places-edit-generic =
  .label = Edit…
  .accesskey = E
places-edit-folder2 =
  .label = Edit Folder…
  .accesskey = E
# Variables
#   $count (number) - Number of folders to delete
places-delete-folder =
  .label =
      { $count ->
          [1] Delete Folder
         *[other] Delete Folders
      }
  .accesskey = D
# Variables:
#   $count (number) - The number of pages selected for removal.
places-delete-page =
  .label =
      { $count ->
          [1] Delete Page
         *[other] Delete Pages
      }
  .accesskey = D

# Managed bookmarks are created by enterprise policy and cannot be changed by the user.
managed-bookmarks =
  .label = Managed bookmarks
# This label is used when a managed bookmarks folder doesn't have a name.
managed-bookmarks-subfolder =
  .label = Subfolder

# This label is used for the "Other Bookmarks" folder that appears in the bookmarks toolbar.
other-bookmarks-folder =
  .label = Other Bookmarks

places-show-in-folder =
  .label = Show in Folder
  .accesskey = F

# Variables:
# $count (number) - The number of elements being selected for removal.
places-delete-bookmark =
  .label =
      { $count ->
          [1] Delete Bookmark
         *[other] Delete Bookmarks
      }
  .accesskey = D

# Variables:
#   $count (number) - The number of bookmarks being added.
places-create-bookmark =
  .label =
      { $count ->
          [1] Bookmark Page…
         *[other] Bookmark Pages…
      }
  .accesskey = B

places-untag-bookmark =
  .label = Remove Tag
  .accesskey = R

places-manage-bookmarks =
  .label = Manage Bookmarks
  .accesskey = M

places-library3 =
  .title = Library

places-organize-button =
  .label = Organize
  .tooltiptext = Organize your bookmarks
  .accesskey = O

places-organize-button-mac =
  .label = Organize
  .tooltiptext = Organize your bookmarks

places-file-close =
  .label = Close
  .accesskey = C

places-cmd-close =
  .key = w

places-view-button =
  .label = Views
  .tooltiptext = Change your view
  .accesskey = V

places-view-button-mac =
  .label = Views
  .tooltiptext = Change your view

places-view-menu-columns =
  .label = Show Columns
  .accesskey = C

places-view-menu-sort =
  .label = Sort
  .accesskey = S

places-view-sort-unsorted =
  .label = Unsorted
  .accesskey = U

places-view-sort-ascending =
  .label = A > Z Sort Order
  .accesskey = A

places-view-sort-descending =
  .label = Z > A Sort Order
  .accesskey = Z

places-maintenance-button =
  .label = Import and Backup
  .tooltiptext = Import and backup your bookmarks
  .accesskey = I

places-maintenance-button-mac =
  .label = Import and Backup
  .tooltiptext = Import and backup your bookmarks

places-cmd-backup =
  .label = Backup…
  .accesskey = B

places-cmd-restore =
  .label = Restore
  .accesskey = R

places-cmd-restore-from-file =
  .label = Choose File…
  .accesskey = C

places-import-bookmarks-from-html =
  .label = Import Bookmarks from HTML…
  .accesskey = I

places-export-bookmarks-to-html =
  .label = Export Bookmarks to HTML…
  .accesskey = E

places-import-other-browser =
  .label = Import Data from Another Browser…
  .accesskey = A

places-view-sort-col-name =
  .label = Name

places-view-sort-col-tags =
  .label = Tags

places-view-sort-col-url =
  .label = Location

places-view-sort-col-most-recent-visit =
  .label = Most Recent Visit

places-view-sort-col-visit-count =
  .label = Visit Count

places-view-sort-col-date-added =
  .label = Added

places-view-sort-col-last-modified =
  .label = Last Modified

places-view-sortby-name =
  .label = Sort by Name
  .accesskey = N
places-view-sortby-url =
  .label = Sort by Location
  .accesskey = L
places-view-sortby-date =
  .label = Sort by Most Recent Visit
  .accesskey = V
places-view-sortby-visit-count =
  .label = Sort by Visit Count
  .accesskey = C
places-view-sortby-date-added =
  .label = Sort by Added
  .accesskey = e
places-view-sortby-last-modified =
  .label = Sort by Last Modified
  .accesskey = M
places-view-sortby-tags =
  .label = Sort by Tags
  .accesskey = T

places-cmd-find-key =
  .key = f

places-back-button =
  .tooltiptext = Go back

places-forward-button =
  .tooltiptext = Go forward

places-details-pane-select-an-item-description = Select an item to view and edit its properties

places-details-pane-no-items =
  .value = No items
# Variables:
#   $count (Number): number of items
places-details-pane-items-count =
  .value =
      { $count ->
          [one] One item
         *[other] { $count } items
      }

## Strings used as a placeholder in the Library search field. For example,
## "Search History" stands for "Search through the browser's history".

places-search-bookmarks =
    .placeholder = Search Bookmarks
places-search-history =
    .placeholder = Search History
places-search-downloads =
    .placeholder = Search Downloads

##

places-locked-prompt = The bookmarks and history system will not be functional because one of { -brand-short-name }’s files is in use by another application. Some security software can cause this problem.
