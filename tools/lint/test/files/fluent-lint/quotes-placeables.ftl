# $file (String) - File name.
bad-double-quote-variable = Open "{ $file }"
# $file (String) - File name.
bad-single-quote-variable = Open '{ $file }'
bad-double-quote-term = "{ -brand-short-name }" is ready
bad-single-quote-message = '{ other-message }' is ready
bad-apostrophe-after-term = { -brand-short-name }'s settings
bad-double-quote-attribute =
    .label = Open "{ -brand-short-name }"
# $count (Number) - Number of tabs.
bad-double-quote-select = "{ $count ->
    [one] tab
   *[other] tabs
  }" are open
# $count (Number) - Number of tabs.
bad-double-quote-variant = { $count ->
    [one] Close "{ $count }" tab
   *[other] Close tabs
  }
bad-double-quote-html = "<b>Bold</b>" text
bad-double-quote-multiline =
    Open "{ -brand-short-name }"
    now

# $file (String) - File name.
good-double-quote-variable = Open “{ $file }”
good-single-quote-term = ‘{ -brand-short-name }’ is ready
good-apostrophe-after-term = { -brand-short-name }’s settings
# $url (String) - Link URL.
good-html-attribute-variable = This is a <a href="{ $url }">link</a>
good-html-attribute-term = <a data-l10n-name="link" title="{ -brand-short-name }">Link</a>
good-function-call-quotes = Last modified: { DATETIME($timeChanged, month: "long") }
