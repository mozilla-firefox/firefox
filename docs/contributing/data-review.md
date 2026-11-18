# Data Review

**Everything that lands in the Firefox repository that adds or expands data
collection must go through the data review process.**

The data review process is in place to ensure we follow Firefox's
{doc}`standards for data collection <data-collection>`.

This will require assessing the sensitivity of the data that is being
collected, and going through the [sensitive data collection
process](https://wiki.mozilla.org/Data_Collection#Step_3:_Sensitive_Data_Collection_Review_Process)
if necessary. All data collection is subject to our [overall data
collection policy](https://wiki.mozilla.org/Data_Collection).

Documentation for the data collection request process and the
expectations we have for people following it [lives on the
wiki](https://wiki.mozilla.org/Data_Collection#Requesting_Data_Collection).
This document describes the technical implementation in Phabricator
using tags.

1. Any change that touches metrics will be automatically flagged with a
   `needs-data-classification` tag by Phabricator, using [this herald
   rule](https://phabricator.services.mozilla.com/H436). If a change
   adds/updates data collection in a way that doesn’t automatically
   trigger this rule, this tag should be added manually (and if
   appropriate, please file a bug to update the herald rule so it
   happens automatically next time).
2. After assessing data sensitivity, the tag can be replaced with either
   `data-classification-low` or `data-classification-high` depending
   on that sensitivity.
3. Adding `data-classification-high` will auto-add the `#data-stewards`
   reviewer group as a blocking reviewer for the change and initiate the
   [sensitive data review process](https://wiki.mozilla.org/Data_Collection#Step_3:_Sensitive_Data_Collection_Review_Process).
4. For patches making mechanical changes that happen to trigger the
   herald rule linked above, but that do not actually add or update any
   data collection, the `data-classification-unnecessary` tag can be used.

Patches with the `needs-data-classification` tag will not be landable in
Lando. The process linked above must be followed in order to land the
change.
