const PAGE_CONTENT = `
<!doctype html>
<select id="select"></select>`;

// Builds the select subtree from script so that the parser cannot reshape it,
// then checks that the dropdown lists exactly the items as computed by
// HTMLSelectElement::GetListItems:
//
// <select>
//   <div>                       wrapper, transparent
//     <option>One
//     <div><option>Two</div>     nested wrapper, transparent
//     <hr><hr>                   collapsed into one separator
//     <optgroup label=Group>
//       <div><option>Three</div> wrapper inside an optgroup
//       <optgroup label=Nested>  a grouped optgroup groups nothing
//         <option>Four
//     <optgroup label=Ignored>   a first child legend labels the group
//       <legend>Leg<img alt=end>hidden</legend>
//       <option>Seven<img alt=!>hidden</option>
//     <datalist><option>Five     not in the option list
//     <select><option>Six        not in the option list
function buildSelect() {
  const doc = content.document;
  const select = doc.getElementById("select");
  const el = (name, parent, text) => {
    const node = doc.createElement(name);
    if (text != null) {
      node.textContent = text;
    }
    parent.appendChild(node);
    return node;
  };

  const wrapper = el("div", select);
  el("option", wrapper, "One");
  el("option", el("div", wrapper), "Two");
  el("hr", wrapper);
  el("hr", wrapper);

  const group = el("optgroup", wrapper);
  group.label = "Group";
  el("option", el("div", group), "Three");
  const nested = el("optgroup", group);
  nested.label = "Nested";
  el("option", nested, "Four");

  const legendGroup = el("optgroup", wrapper);
  legendGroup.label = "Ignored";
  const legend = el("legend", legendGroup, "Leg");
  el("img", legend, "hidden").alt = "end";
  const seven = el("option", legendGroup, "Seven");
  el("img", seven, "hidden").alt = "!";

  el("option", el("datalist", wrapper), "Five");
  el("option", el("select", wrapper), "Six");
}

async function testOptionList(customizableSelect) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["test.wait300msAfterTabSwitch", true],
      ["dom.select.customizable_select.enabled", customizableSelect],
    ],
  });

  const pageUrl = "data:text/html," + encodeURIComponent(PAGE_CONTENT);
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, pageUrl);
  await SpecialPowers.spawn(tab.linkedBrowser, [], buildSelect);

  const selectPopup = await openSelectPopup("click");
  const items = Array.from(selectPopup.children).map(child => [
    child.localName,
    child.getAttribute("label"),
  ]);

  Assert.deepEqual(
    items,
    [
      ["menuitem", "One"],
      ["menuitem", "Two"],
      ["menuseparator", null],
      ["menucaption", "Group"],
      ["menuitem", "Three"],
      ["menucaption", "Legend"],
      ["menuitem", "Seven!"],
    ],
    `Dropdown lists the option list, and only the option list (customizable select: ${customizableSelect})`
  );

  await hideSelectPopup();
  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
}

add_task(async function test_option_list_customizable_select_disabled() {
  await testOptionList(false);
});

add_task(async function test_option_list_customizable_select_enabled() {
  await testOptionList(true);
});
