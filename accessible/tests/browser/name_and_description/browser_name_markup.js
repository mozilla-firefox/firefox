/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";
requestLongerTimeout(2);

/* import-globals-from ../../mochitest/name.js */
/* import-globals-from ../../mochitest/attributes.js */
loadScripts(
  { name: "name.js", dir: MOCHITESTS_DIR },
  { name: "attributes.js", dir: MOCHITESTS_DIR }
);

/**
 * Rules for name tests that are inspired by
 *   accessible/tests/mochitest/name/markuprules.xul
 *
 * Each element in the list of rules represents a name calculation rule for a
 * particular test case.
 *
 * The rules have the following format:
 *   { attr } - calculated from attribute
 *   { elm } - calculated from another element
 *   { fromsubtree } - calculated from element's subtree
 *
 */
const ARIARule = [{ attr: "aria-labelledby" }, { attr: "aria-label" }];
const HTMLControlHeadRule = [...ARIARule, { elm: "label" }];
const rules = {
  CSSContent: [{ elm: "style" }, { fromsubtree: true }],
  HTMLCell: [...ARIARule, { fromsubtree: true }, { attr: "title" }],
  HTMLControl: [
    ...HTMLControlHeadRule,
    { fromsubtree: true },
    { attr: "title" },
  ],
  HTMLElm: [...ARIARule, { attr: "title" }],
  HTMLImg: [...ARIARule, { attr: "alt" }, { attr: "title" }],
  HTMLImgEmptyAlt: [...ARIARule, { attr: "title" }, { attr: "alt" }],
  HTMLInputButton: [
    ...HTMLControlHeadRule,
    { attr: "value" },
    { attr: "title" },
  ],
  HTMLInputImage: [
    ...HTMLControlHeadRule,
    { attr: "alt" },
    { attr: "value" },
    { attr: "title" },
  ],
  HTMLInputImageNoValidSrc: [
    ...HTMLControlHeadRule,
    { attr: "alt" },
    { attr: "value" },
  ],
  HTMLInputReset: [...HTMLControlHeadRule, { attr: "value" }],
  HTMLInputSubmit: [...HTMLControlHeadRule, { attr: "value" }],
  HTMLLink: [...ARIARule, { fromsubtree: true }, { attr: "title" }],
  HTMLLinkImage: [...ARIARule, { fromsubtree: true }, { attr: "title" }],
  HTMLOption: [
    ...ARIARule,
    { attr: "label" },
    { fromsubtree: true },
    { attr: "title" },
  ],
  HTMLTable: [
    ...ARIARule,
    { elm: "caption" },
    { attr: "summary" },
    { attr: "title" },
  ],
};

const markupTests = [
  {
    id: "btn",
    ruleset: "HTMLControl",
    markup: `
    <span id="l1">test2</span>
    <span id="l2">test3</span>
    <label for="btn">test4</label>
    <button id="btn"
            aria-label="test1"
            aria-labelledby="l1 l2"
            title="test5">press me</button>`,
    expected: ["test2 test3", "test1", "test4", "press me", "test5"],
  },
  {
    id: "btn",
    ruleset: "HTMLInputButton",
    markup: `
    <span id="l1">test2</span>
    <span id="l2">test3</span>
    <label for="btn">test4</label>
    <input id="btn"
           type="button"
           aria-label="test1"
           aria-labelledby="l1 l2"
           value="name from value"
           alt="no name from al"
           src="no name from src"
           data="no name from data"
           title="name from title"/>`,
    expected: [
      "test2 test3",
      "test1",
      "test4",
      "name from value",
      "name from title",
    ],
  },
  {
    id: "btn-submit",
    ruleset: "HTMLInputSubmit",
    markup: `
    <span id="l1">test2</span>
    <span id="l2">test3</span>
    <label for="btn-submit">test4</label>
    <input id="btn-submit"
           type="submit"
           aria-label="test1"
           aria-labelledby="l1 l2"
           value="name from value"
           alt="no name from atl"
           src="no name from src"
           data="no name from data"
           title="no name from title"/>`,
    expected: ["test2 test3", "test1", "test4", "name from value"],
  },
  {
    id: "btn-reset",
    ruleset: "HTMLInputReset",
    markup: `
    <span id="l1">test2</span>
    <span id="l2">test3</span>
    <label for="btn-reset">test4</label>
    <input id="btn-reset"
           type="reset"
           aria-label="test1"
           aria-labelledby="l1 l2"
           value="name from value"
           alt="no name from alt"
           src="no name from src"
           data="no name from data"
           title="no name from title"/>`,
    expected: ["test2 test3", "test1", "test4", "name from value"],
  },
  {
    id: "btn-image",
    ruleset: "HTMLInputImage",
    markup: `
    <span id="l1">test2</span>
    <span id="l2">test3</span>
    <label for="btn-image">test4</label>
    <input id="btn-image"
           type="image"
           aria-label="test1"
           aria-labelledby="l1 l2"
           alt="name from alt"
           value="name from value"
           src="http://example.com/a11y/accessible/tests/mochitest/moz.png"
           data="no name from data"
           title="name from title"/>`,
    expected: [
      "test2 test3",
      "test1",
      "test4",
      "name from alt",
      "name from value",
      "name from title",
    ],
  },
  {
    id: "btn-image",
    ruleset: "HTMLInputImageNoValidSrc",
    markup: `
    <span id="l1">test2</span>
    <span id="l2">test3</span>
    <label for="btn-image">test4</label>
    <input id="btn-image"
           type="image"
           aria-label="test1"
           aria-labelledby="l1 l2"
           alt="name from alt"
           value="name from value"
           data="no name from data"
           title="no name from title"/>`,
    expected: [
      "test2 test3",
      "test1",
      "test4",
      "name from alt",
      "name from value",
    ],
  },
  {
    id: "opt",
    ruleset: "HTMLOption",
    markup: `
    <span id="l1">test2</span>
    <span id="l2">test3</span>
    <select>
      <option id="opt"
              aria-label="test1"
              aria-labelledby="l1 l2"
              label="test4"
              title="test5">option1</option>
      <option>option2</option>
    </select>`,
    expected: ["test2 test3", "test1", "test4", "option1", "test5"],
  },
  {
    id: "img",
    ruleset: "HTMLImg",
    markup: `
    <span id="l1">test2</span>
    <span id="l2">test3</span>
    <img id="img"
         aria-label="Logo of Mozilla"
         aria-labelledby="l1 l2"
         alt="Mozilla logo"
         title="This is a logo"
         src="http://example.com/a11y/accessible/tests/mochitest/moz.png"/>`,
    expected: [
      "test2 test3",
      "Logo of Mozilla",
      "Mozilla logo",
      "This is a logo",
    ],
  },
  {
    id: "tc",
    ruleset: "HTMLCell",
    markup: `
    <span id="l1">test2</span>
    <span id="l2">test3</span>
    <label for="tc">test4</label>
    <table>
      <tr>
        <td id="tc"
            aria-label="test1"
            aria-labelledby="l1 l2"
            title="test5">
          <p>This is a paragraph</p>
          <a href="#">This is a link</a>
          <ul>
            <li>This is a list</li>
          </ul>
        </td>
      </tr>
    </table>`,
    expected: [
      "test2 test3",
      "test1",
      "This is a paragraph This is a link \u2022 This is a list",
      "test5",
    ],
  },
  {
    id: "gc",
    ruleset: "HTMLCell",
    markup: `
    <span id="l1">test2</span>
    <span id="l2">test3</span>
    <label for="gc">test4</label>
    <table>
      <tr>
        <td id="gc"
            role="gridcell"
            aria-label="test1"
            aria-labelledby="l1 l2"
            title="This is a paragraph This is a link This is a list">
          <p>This is a paragraph</p>
          <a href="#">This is a link</a>
          <ul>
            <li>Listitem1</li>
            <li>Listitem2</li>
          </ul>
        </td>
      </tr>
    </table>`,
    expected: [
      "test2 test3",
      "test1",
      "This is a paragraph This is a link \u2022 Listitem1 \u2022 Listitem2",
      "This is a paragraph This is a link This is a list",
    ],
  },
  {
    id: "t",
    ruleset: "HTMLTable",
    markup: `
    <span id="l1">lby_tst6_1</span>
    <span id="l2">lby_tst6_2</span>
    <label for="t">label_tst6</label>
    <table id="t"
           aria-label="arialabel_tst6"
           aria-labelledby="l1 l2"
           summary="summary_tst6"
           title="title_tst6">
      <caption>caption_tst6</caption>
      <tr>
        <td>cell1</td>
        <td>cell2</td>
      </tr>
    </table>`,
    expected: [
      "lby_tst6_1 lby_tst6_2",
      "arialabel_tst6",
      "caption_tst6",
      "summary_tst6",
      "title_tst6",
    ],
  },
  {
    id: "btn",
    ruleset: "CSSContent",
    markup: `
    <div role="main">
      <style>
        button::before {
          content: "do not ";
        }
      </style>
      <button id="btn">press me</button>
    </div>`,
    expected: ["do not press me", "press me"],
  },
  {
    // TODO: uncomment when Bug-1256382 is resoved.
    // id: 'li',
    // ruleset: 'CSSContent',
    // markup: `
    //   <style>
    //     ul {
    //       list-style-type: decimal;
    //     }
    //   </style>
    //   <ul id="ul">
    //     <li id="li">Listitem</li>
    //   </ul>`,
    // expected: ['1. Listitem', `${String.fromCharCode(0x2022)} Listitem`]
    // }, {
    id: "a",
    ruleset: "HTMLLink",
    markup: `
    <span id="l1">test2</span>
    <span id="l2">test3</span>
    <a id="a"
       href=""
       aria-label="test1"
       aria-labelledby="l1 l2"
       title="test4">test5</a>`,
    expected: ["test2 test3", "test1", "test5", "test4"],
  },
  {
    id: "a-img",
    ruleset: "HTMLLinkImage",
    markup: `
    <span id="l1">test2</span>
    <span id="l2">test3</span>
    <a id="a-img"
       href=""
       aria-label="test1"
       aria-labelledby="l1 l2"
       title="test4"><img alt="test5"/></a>`,
    expected: ["test2 test3", "test1", "test5", "test4"],
  },
];

/**
 * Test accessible name that is calculated from an attribute, remove the
 * attribute before proceeding to the next name test. If attribute removal
 * results in a reorder or text inserted event - wait for it. If accessible
 * becomes defunct, update its reference using the one that is attached to one
 * of the above events.
 *
 * @param {object} browser      current "tabbrowser" element
 * @param {object} target       { acc, id } structure that contains an
 *                               accessible and its content element
 *                               id.
 * @param {object} rule         current attr rule for name calculation
 * @param {[type]} expected     expected name value
 */
async function testAttrRule(browser, target, rule, expected) {
  let { id, acc } = target;
  let { attr } = rule;

  testName(acc, expected);

  let nameChange = waitForEvent(EVENT_NAME_CHANGE, id);
  await invokeContentTask(browser, [id, attr], (contentId, contentAttr) => {
    content.document.getElementById(contentId).removeAttribute(contentAttr);
  });
  let event = await nameChange;

  // Update accessible just in case it is now defunct.
  target.acc = findAccessibleChildByID(event.accessible, id);
}

/**
 * Test accessible name that is calculated from an element name, remove the
 * element before proceeding to the next name test. If element removal results
 * in a reorder event - wait for it. If accessible becomes defunct, update its
 * reference using the one that is attached to a possible reorder event.
 *
 * @param {object} browser      current "tabbrowser" element
 * @param {object} target       { acc, id } structure that contains an
 *                               accessible and its content element
 *                               id.
 * @param {object} rule         current elm rule for name calculation
 * @param {[type]} expected     expected name value
 */
async function testElmRule(browser, target, rule, expected) {
  let { id, acc } = target;
  let { elm } = rule;

  testName(acc, expected);
  let nameChange = waitForEvent(EVENT_NAME_CHANGE, id);

  await invokeContentTask(browser, [elm], contentElm => {
    content.document.querySelector(`${contentElm}`).remove();
  });
  let event = await nameChange;

  // Update accessible just in case it is now defunct.
  target.acc = findAccessibleChildByID(event.accessible, id);
}

/**
 * Test accessible name that is calculated from its subtree, remove the subtree
 * and wait for a reorder event before proceeding to the next name test. If
 * accessible becomes defunct, update its reference using the one that is
 * attached to a reorder event.
 *
 * @param {object} browser      current "tabbrowser" element
 * @param {object} target       { acc, id } structure that contains an
 *                               accessible and its content element
 *                               id.
 * @param {object} rule         current subtree rule for name calculation
 * @param {[type]} expected     expected name value
 */
async function testSubtreeRule(browser, target, rule, expected) {
  let { id, acc } = target;

  testName(acc, expected);
  let nameChange = waitForEvent(EVENT_NAME_CHANGE, id);

  await invokeContentTask(browser, [id], contentId => {
    let elm = content.document.getElementById(contentId);
    while (elm.firstChild) {
      elm.firstChild.remove();
    }
  });
  let event = await nameChange;

  // Update accessible just in case it is now defunct.
  target.acc = findAccessibleChildByID(event.accessible, id);
}

/**
 * Iterate over a list of rules and test accessible names for each one of the
 * rules.
 *
 * @param {object} browser      current "tabbrowser" element
 * @param {object} target       { acc, id } structure that contains an
 *                               accessible and its content element
 *                               id.
 * @param {Array}  ruleset      A list of rules to test a target with
 * @param {Array}  expected     A list of expected name value for each rule
 */
async function testNameRule(browser, target, ruleset, expected) {
  for (let i = 0; i < ruleset.length; ++i) {
    let rule = ruleset[i];
    let testFn;
    if (rule.attr) {
      testFn = testAttrRule;
    } else if (rule.elm) {
      testFn = testElmRule;
    } else if (rule.fromsubtree) {
      testFn = testSubtreeRule;
    }
    await testFn(browser, target, rule, expected[i]);
  }
}

markupTests.forEach(({ id, ruleset, markup, expected }) =>
  addAccessibleTask(
    markup,
    async function (browser, accDoc) {
      const observer = {
        observe(subject) {
          const event = subject.QueryInterface(nsIAccessibleEvent);
          console.log(eventToString(event));
        },
      };
      Services.obs.addObserver(observer, "accessible-event");
      // Find a target accessible from an accessible subtree.
      let acc = findAccessibleChildByID(accDoc, id);
      let target = { id, acc };
      await testNameRule(browser, target, rules[ruleset], expected);
      Services.obs.removeObserver(observer, "accessible-event");
    },
    { iframe: true, remoteIframe: true }
  )
);

/**
 * Generic test cases ported from mochitest/name/test_general.html
 */
addAccessibleTask(
  `
  <!-- aria-label, simple label -->
  <span id="btn_simple_aria_label" role="button" aria-label="I am a button"/>
  <br/>
  <!-- aria-label plus aria-labelledby -->
  <span id="btn_both_aria_labels" role="button" aria-label="I am a button, two"
        aria-labelledby="labelledby_text btn_both_aria_labels"/>
  <br/>

  <!-- aria-labelledby, single relation -->
  <span id="labelledby_text">text</span>
  <button id="btn_labelledby_text"
          aria-labelledby="labelledby_text">1</button>
  <br/>

  <!-- aria-labelledby, multiple relations -->
  <span id="labelledby_text1">text1</span>
  <span id="labelledby_text2">text2</span>
  <button id="btn_labelledby_texts"
          aria-labelledby="labelledby_text1 labelledby_text2">2</button>
  <br/>

  <!-- name from named accessible -->
  <input id="labelledby_namedacc" type="checkbox"
         aria-label="Data" />
  <input id="input_labelledby_namedacc"
          aria-labelledby="labelledby_namedacc" />

  <!-- the name from subtree, mixed content -->
  <span id="labelledby_mixed">no<span>more text</span></span>
  <button id="btn_labelledby_mixed"
          aria-labelledby="labelledby_mixed">3</button>
  <br/>

  <!-- the name from subtree, mixed/hidden content -->
  <span id="labelledby_mixed_hidden_child">
    no<span>more
      <span style="display: none;">hidden</span>
      text2
      <span style="visibility: hidden">hidden2</span>
    </span>
  </span>
  <button id="btn_labelledby_mixed_hidden_child"
          aria-labelledby="labelledby_mixed_hidden_child">3.1</button>
  <br/>

  <!-- the name from subtree, mixed/completely hidden content -->
  <span id="labelledby_mixed_hidden"
        style="display: none;">lala <span>more hidden </span>text</span>
  <button id="btn_labelledby_mixed_hidden"
          aria-labelledby="labelledby_mixed_hidden">3.2</button>
  <br/>

  <!-- the name from subtree, mixed content, block structure -->
  <div id="labelledby_mixed_block"><div>text</div>more text</div></div>
  <button id="btn_labelledby_mixed_block"
          aria-labelledby="labelledby_mixed_block">4</button>
  <br/>

  <!-- the name from subtree, mixed content, table structure -->
  <table><tr>
    <td id="labelledby_mixed_table">text<span>TEXT</span>text</td>
  </tr></table>
  <button id="btn_labelledby_mixed_table"
          aria-labelledby="labelledby_mixed_table">5</button>
  <br/>

  <!-- the name from subtree, child img -->
  <span id="labelledby_mixed_img">text<img alt="image"/></span>
  <button id="btn_labelledby_mixed_img"
          aria-labelledby="labelledby_mixed_img">6</button>
  <br/>

  <!-- the name from subtree, child inputs -->
  <span id="labelledby_mixed_input">
    <input type="button" id="input_button" title="input button"/>
    <input type="submit" id="input_submit"/>
    <input type="reset" id="input_reset"/>
    <input type="image" id="input_image" title="input image"/>
  </span>
  <button id="btn_labelledby_mixed_input"
          aria-labelledby="labelledby_mixed_input">7</button>
  <br/>

  <!-- the name from subtree, child object -->
  <span id="labelledby_mixed_object">
    <object data="about:blank" title="object"></object>
  </span>
  <button id="btn_labelledby_mixed_object"
          aria-labelledby="labelledby_mixed_object">8</button>
  <br/>

  <!-- the name from subtree, child br -->
  <span id="labelledby_mixed_br">text<br/>text</span>
  <button id="btn_labelledby_mixed_br"
          aria-labelledby="labelledby_mixed_br">9</button>
  <br/>

  <!-- the name from subtree, name from label content rather than from its title
    attribute -->
  <label  for="from_label_ignoretitle"
          title="Select your country of origin">Country:</label>
  <select id="from_label_ignoretitle">
    <option>Germany</option>
    <option>Russia</option>
  </select>

  <!-- the name from subtree, name from html:p content rather than from its
    title attribute -->
  <p id="p_ignoretitle"
     title="Select your country of origin">Choose country from.</p>
  <select id="from_p_ignoretitle" aria-labelledby="p_ignoretitle">
    <option>Germany</option>
    <option>Russia</option>
  </select>

  <!-- the name from subtree, name from html:input value rather than from its
    title attribute -->
  <p id="from_input_ignoretitle" aria-labelledby="input_ignoretitle">Country</p>
  <input id="input_ignoretitle"
         value="Custom country"
         title="Input your country of origin"/>

  <!-- name from subtree, surround control by spaces to not jam the text -->
  <label id="insert_spaces_around_control">
    start<input value="value">end
  </label>

  <!-- no name from subtree because it holds whitespaces only -->
  <a id="from_label_ignore_ws_subtree" href="about:mozilla" title="about">&nbsp;&nbsp;  </a>

  <!-- Don't use subtree unless referenced by aria-labelledby. -->
  <div id="alert" role="alert">Error</div>
  <input type="text" id="inputLabelledByAlert" aria-labelledby="alert">

  <!-- label element, label contains control -->
  <label>text<button id="btn_label_inside">10</button>text</label>
  <br/>

  <!-- label element, label and the button are in the same form -->
  <form>
    <label for="btn_label_inform">in form</label>
    <button id="btn_label_inform">11</button>
  </form>

  <!-- label element, label is outside of the form of the button -->
  <label for="btn_label_outform">out form</label>
  <form>
    <button id="btn_label_outform">12</button>
  </form>

  <!-- label element, label and the button are in the same document -->
  <label for="btn_label_indocument">in document</label>
  <button id="btn_label_indocument">13</button>

  <!-- multiple label elements for single button -->
  <label for="btn_label_multi">label1</label>
  <label for="btn_label_multi">label2</label>
  <button id="btn_label_multi">button</button>

  <!-- a label containing more than one controls -->
  <label>
    Enable <input id="ctrl_in_label_1" type="checkbox"> a
    <input id="ctrl_in_label_2" type="button" value="button"> control
  </label>

  <!-- name from children -->
  <span id="btn_children" role="button">14</span>

  <!-- no name from content, ARIA role overrides this rule -->
  <button id="btn_nonamefromcontent" role="img">1</button>

  <!-- name from children, hidden children -->
  <div role="listbox" tabindex="0">
    <div id="lb_opt1_children_hidden" role="option" tabindex="0">
      <span>i am visible</span>
      <span style="display:none">i am hidden</span>
    </div>
  </div>

  <table role="menu">
    <tr role="menuitem" id="tablemenuitem">
      <td>menuitem 1</td>
    </tr>
    <tr role="menuitem">
      <td>menuitem 2</td>
    </tr>
  </table>

  <label id="label_with_acronym">
    <acronym title="O A T F">OATF</acronym>
    <abbr title="World Wide Web">WWW</abbr>
  </label>

  <div id="testArticle" role="article" title="Test article">
    <p>This is a paragraph inside the article.</p>
  </div>

  <h1 id="h1" title="oops">heading</h1>
  <div role="heading" id="aria_heading">aria_heading</div>

  <!-- name from title attribute -->
  <span id="btn_title" role="group" title="title">15</span>

  <!-- A textarea nested in a label with a text child (bug #453371). -->
  <form>
    <label>Story
      <textarea id="textareawithchild" name="name">Foo</textarea>
      is ended.
    </label>
  </form>

  <!-- controls having a value used as part of computed name -->
  <input type="checkbox" id="ctrlvalue_progressbar:input">
  <label for="ctrlvalue_progressbar:input">
    foo <span role="progressbar"
               aria-valuenow="5" aria-valuemin="1"
               aria-valuemax="10">5</span> baz
  </label>

  <input type="checkbox" id="ctrlvalue_scrollbar:input" />
  <label for="ctrlvalue_scrollbar:input">
    foo <span role="scrollbar"
              aria-valuenow="5" aria-valuemin="1"
              aria-valuemax="10">5</span> baz
  </label>

  <input type="checkbox" id="ctrlvalue_slider:input">
  <label for="ctrlvalue_slider:input">
    foo <input role="slider" type="range"
               value="5" min="1" max="10"
               aria-valuenow="5" aria-valuemin="1"
               aria-valuemax="10"> baz
  </label>

  <input type="checkbox" id="ctrlvalue_spinbutton:input">
  <label for="ctrlvalue_spinbutton:input">
    foo <input role="spinbutton" type="number"
               value="5" min="1" max="10"
               aria-valuenow="5" aria-valuemin="1"
               aria-valuemax="10">
    baz
  </label>

  <input type="checkbox" id="ctrlvalue_combobox:input">
  <label for="ctrlvalue_combobox:input">
    foo
    <div role="combobox">
      <div role="textbox"></div>
      <ul role="listbox" style="list-style-type: none;">
        <li role="option">1</li>
        <li role="option" aria-selected="true">5</li>
        <li role="option">3</li>
      </ul>
    </div>
    baz
  </label>

  <input type="checkbox" id="ctrlvalue_meter:input">
  <label for="ctrlvalue_meter:input">
    foo
    <meter>5</meter>
    baz
  </label>

  <!-- a label with a nested control in the start, middle and end -->
  <form>
    <!-- at the start (without and with whitespaces) -->
    <label id="comboinstart"><select id="combo3">
        <option>One</option>
        <option>Two</option>
      </select>
      day(s).
    </label>

    <label id="textboxinstart">
      <input id="textbox1" value="Two">
      days.
    </label>

    <!-- in the middle -->
    <label id="comboinmiddle">
      Subscribe to
      <select id="combo4" name="occupation">
        <option>ATOM</option>
        <option>RSS</option>
      </select>
      feed.
    </label>

    <label id="comboinmiddle2" for="checkbox">Play the
      <select id="combo5">
        <option>Haliluya</option>
        <option>Hurra</option>
      </select>
      sound when new mail arrives
    </label>
    <input id="checkbox" type="checkbox" />

    <label id="comboinmiddle3" for="combo6">Play the
      <select id="combo6">
        <option>Haliluya</option>
        <option>Hurra</option>
      </select>
      sound when new mail arrives
    </label>

    <!-- at the end (without and with whitespaces) -->
    <label id="comboinend">
      This day was
      <select id="combo7" name="occupation">
        <option>sunny</option>
        <option>rainy</option>
      </select></label>

    <label id="textboxinend">
      This day was
      <input id="textbox2" value="sunny">
    </label>
  </form>

  <!-- placeholder  -->
  <input id="ph_password" type="password" value="" placeholder="a placeholder" />
  <input id="ph_text" type="text" placeholder="a placeholder" />
  <textarea id="ph_textarea" cols="5" placeholder="a placeholder"></textarea>

  <!-- placeholder does not win -->
  <input id="ph_text2" type="text" aria-label="a label" placeholder="meh" />
  <textarea id="ph_textarea2" cols="5" aria-labelledby="ph_text2"
            placeholder="meh"></textarea>

  <label for="ph_text3">a label</label>
  <input id="ph_text3" placeholder="meh" />

  <p>Image:
    <img id="img_eq" role="math" src="foo" alt="x^2 + y^2 + z^2">
    <input type="image"  id="input_img_eq" src="foo" alt="x^2 + y^2 + z^2">
  </p>

  <p>Text:
    <span id="txt_eq" role="math" title="x^2 + y^2 + z^2">x<sup>2</sup> +
      y<sup>2</sup> + z<sup>2</sup></span>
  </p>

  <!-- duplicate announcement -->
  <div id="test_note" role="note">subtree</div>

  <!-- No name for tr from its sub tree -->
  <table><tr id="NoNameForTR"><th>a</th><td>b</td></tr></table>
  <table style="display: block;">
    <tr id="NoNameForNonStandardTR" style="display:block;">
      <th>a</th><td>b</td>
    </tr>
  </table>

  <!-- Name from sub tree of tr, because it has a strong ARIA role -->
  <table><tr id="NameForTRBecauseStrongARIA" role="row"><th>a</th><td>b</td></tr></table>

  <!-- No name for tr because of weak (landmark) role -->
  <table><tr id="NoNameForTRBecauseWeakARIA" role="main"><th>a</th><td>b</td></tr></table>

  <!-- Name from subtree of grouping if requested by other object -->
  <div id="grouping" role="group">label</div>
  <button id="requested_name_from_grouping" aria-labelledby="grouping"></button>
  <!-- Name from sub tree of tbody marked as display:block;, which is also a grouping -->
  <div role="list">
    <div id="listitem_containing_block_tbody" role="listitem">
      <table>
        <tbody style="display: block;">
          <tr><td>label</td></tr>
        </tbody>
      </table>
    </div>
  </div>
  <!-- Name from subtree of treeitem containing grouping -->
  <div role="tree">
    <div id="treeitem_containing_grouping" role="treeitem" aria-level="1" aria-expanded="true">root
      <div role="group">
        <div role="treeitem" aria-level="2">sub</div>
      </div>
    </div>
  </div>

  <!-- Name from subtree of grouping labelled by an ancestor. -->
  <div id="grouping_ancestor_label">label
    <div id="grouping_labelledby_ancestor" role="group" aria-labelledby="grouping_ancestor_label">
      This content should not be included in the grouping's label.
    </div>
  </div>

  <!-- Text nodes and inline elements. -->
  <div role="listbox">
    <div id="container_text_inline" role="option">a<strong>b</strong>c</div>
    <!-- Text nodes and block elements. -->
    <div id="container_text_block" role="option">a<p>b</p>c</div>
    <!-- Text nodes and empty block elements. -->
    <div id="container_text_emptyblock" role="option">a<p></p><p></p>b</div>
  </div>

  <!-- aria-labelledby referring to a slot -->
  <div id="shadowHost">
    shadowButtonVisibleText
    <span slot="hiddenSlot">shadowButtonHiddenText</span>
  </div>
  <template id="shadowTemplate">
    <input type="button" id="shadowButtonVisibleText" aria-labelledby="visibleSlot">
    <slot id="visibleSlot"></slot>
    <input type="button" id="shadowButtonHiddenText" aria-labelledby="hiddenSlot">
    <slot id="hiddenSlot" name="hiddenSlot" hidden></slot>
  </template>

  <!-- aria-labelledby referring to a hidden container with script/style -->
  <button id="buttonScriptStyle" aria-labelledby="hiddenScriptStyle"></button>
  <div id="hiddenScriptStyle" hidden>
    <script> 42; </script>
    <style> .noOp {} </style>
    <span>content</span>
  </div>

  <!-- Name from subtree on link including <code>, etc. -->
  <a id="linkWithCodeSupSubInsDel" href="#">
    before
    <code>code</code>
    <sup>sup</sup>
    <sub>sub</sub>
    <ins>ins</ins>
    <del>del</del>
    after
  </a>
`,
  async function testGeneric(browser, docAcc) {
    // aria-label
    function testName_(id, expectedName) {
      const acc = findAccessibleChildByID(docAcc, id);
      testName(acc, expectedName);
    }

    // Simple label provided via ARIA
    testName_("btn_simple_aria_label", "I am a button");

    // aria-label and aria-labelledby, expect aria-labelledby
    testName_("btn_both_aria_labels", "text I am a button, two");

    // ////////////////////////////////////////////////////////////////////////
    // aria-labelledby

    // Single relation. The value of 'aria-labelledby' contains the ID of
    // an element. Gets the name from text node of that element.
    testName_("btn_labelledby_text", "text");

    // Multiple relations. The value of 'aria-labelledby' contains the IDs
    // of elements. Gets the name from text nodes of those elements.
    testName_("btn_labelledby_texts", "text1 text2");

    // ////////////////////////////////////////////////////////////////////////
    // Name from named accessible

    testName_("input_labelledby_namedacc", "Data");

    // ////////////////////////////////////////////////////////////////////////
    // Name from subtree (single relation labelled_by).

    // Gets the name from text nodes contained by nested elements
    testName_("btn_labelledby_mixed", "nomore text");

    // Gets the name from text nodes contained by nested elements, ignores
    // hidden elements (bug 443081).
    testName_("btn_labelledby_mixed_hidden_child", "nomore text2");

    // Gets the name from hidden text nodes contained by nested elements,
    // (label element is hidden entirely), (bug 443081).
    testName_("btn_labelledby_mixed_hidden", "lala more hidden text");

    // Gets the name from text nodes contained by nested elements having block
    // representation (every text node value in the name should be divided by
    // spaces)
    testName_("btn_labelledby_mixed_block", "text more text");

    // Gets the name from text nodes contained by html:td. The text nodes
    // should not be divided by spaces.
    testName_("btn_labelledby_mixed_table", "textTEXTtext");

    // Gets the name from image accessible.
    testName_("btn_labelledby_mixed_img", "text image");

    // Gets the name from input accessibles
    // Note: if input have label elements then the name isn't calculated
    // from them.
    testName_(
      "btn_labelledby_mixed_input",
      "input button Submit Query Reset Submit Query"
    );

    // Gets the name from the title of object element.
    testName_("btn_labelledby_mixed_object", "object");

    // Gets the name from text nodes. Element br adds space between them.
    testName_("btn_labelledby_mixed_br", "text text");

    // Gets the name from label content which allows name from subtree,
    // ignore @title attribute on label
    testName_("from_label_ignoretitle", "Country:");

    // Gets the name from html:p content, which doesn't allow name from
    // subtree, ignore @title attribute on label
    testName_("from_p_ignoretitle", "Choose country from.");

    // Gets the name from html:input value, ignore @title attribute on input
    testName_("from_input_ignoretitle", "Custom country");

    // Insert spaces around the control's value to not jam sibling text nodes
    testName_("insert_spaces_around_control", "start value end");

    // Gets the name from @title, ignore whitespace content
    testName_("from_label_ignore_ws_subtree", "about");

    // role="alert" doesn't get name from subtree...
    testName_("alert", null);
    // but the subtree is used if referenced by aria-labelledby.
    testName_("inputLabelledByAlert", "Error");

    // ////////////////////////////////////////////////////////////////////////
    // label element

    // The label element contains the button. The name of the button is
    // calculated from the content of the label.
    // Note: the name does not contain the content of the button.
    testName_("btn_label_inside", "texttext");

    // The label element and the button are placed in the same form. Gets
    // the name from the label subtree.
    testName_("btn_label_inform", "in form");

    // The label element is placed outside of form where the button is.
    // Take into account the label.
    testName_("btn_label_outform", "out form");

    // The label element and the button are in the same document. Gets the
    // name from the label subtree.
    testName_("btn_label_indocument", "in document");

    // Multiple label elements for single button
    testName_("btn_label_multi", "label1 label2");

    // Multiple controls inside a label element
    testName_("ctrl_in_label_1", "Enable a button control");
    testName_("ctrl_in_label_2", "button");

    // ////////////////////////////////////////////////////////////////////////
    // name from children

    // ARIA role button is presented allowing the name calculation from
    // children.
    testName_("btn_children", "14");

    // html:button, no name from content
    testName_("btn_nonamefromcontent", null);

    // ARIA role option is presented allowing the name calculation from
    // visible children (bug 443081).
    testName_("lb_opt1_children_hidden", "i am visible");

    // Get the name from subtree of menuitem crossing role nothing to get
    // the name from its children.
    testName_("tablemenuitem", "menuitem 1");

    // Get the name from child acronym title attribute rather than from
    // acronym content.
    testName_("label_with_acronym", "O A T F World Wide Web");

    testName_("testArticle", "Test article");

    testName_("h1", "heading");
    testName_("aria_heading", "aria_heading");

    // ////////////////////////////////////////////////////////////////////////
    // title attribute

    // If nothing is left. Let's try title attribute.
    testName_("btn_title", "title");

    // ////////////////////////////////////////////////////////////////////////
    // textarea name

    // textarea's name should not have the value, which initially is specified
    // by a text child.
    testName_("textareawithchild", "Story is ended.");

    // new textarea name should not reflect the value change.
    let valueChanged = waitForEvent(
      EVENT_TEXT_VALUE_CHANGE,
      "textareawithchild"
    );
    await invokeContentTask(browser, [], () => {
      content.document.getElementById("textareawithchild").value = "Bar";
    });
    await valueChanged;

    testName_("textareawithchild", "Story is ended.");

    // ////////////////////////////////////////////////////////////////////////
    // controls having a value used as a part of computed name

    testName_("ctrlvalue_progressbar:input", "foo 5 baz");
    testName_("ctrlvalue_scrollbar:input", "foo 5 baz");
    testName_("ctrlvalue_slider:input", "foo 5 baz");
    testName_("ctrlvalue_spinbutton:input", "foo 5 baz");
    testName_("ctrlvalue_combobox:input", "foo 5 baz");
    testName_("ctrlvalue_meter:input", "foo 5 baz");

    // ///////////////////////////////////////////////////////////////////////
    // label with nested combobox (test for 'f' item of name computation guide)

    testName_("comboinstart", "One day(s).");
    testName_("combo3", "day(s).");

    testName_("textboxinstart", "Two days.");
    testName_("textbox1", "days.");

    testName_("comboinmiddle", "Subscribe to ATOM feed.");
    testName_("combo4", "Subscribe to feed.");

    testName_(
      "comboinmiddle2",
      "Play the Haliluya sound when new mail arrives"
    );
    testName_("combo5", null); // label isn't used as a name for control
    testName_("checkbox", "Play the Haliluya sound when new mail arrives");
    testName_(
      "comboinmiddle3",
      "Play the Haliluya sound when new mail arrives"
    );
    testName_("combo6", "Play the sound when new mail arrives");

    testName_("comboinend", "This day was sunny");
    testName_("combo7", "This day was");

    testName_("textboxinend", "This day was sunny");
    testName_("textbox2", "This day was");

    // placeholder
    testName_("ph_password", "a placeholder");
    testName_("ph_text", "a placeholder");
    testName_("ph_textarea", "a placeholder");
    testName_("ph_text2", "a label");
    testName_("ph_textarea2", "a label");
    testName_("ph_text3", "a label");

    // Test equation image
    testName_("img_eq", "x^2 + y^2 + z^2");
    testName_("input_img_eq", "x^2 + y^2 + z^2");
    testName_("txt_eq", "x^2 + y^2 + z^2");

    // //////////////////////////////////////////////////////////////////////
    // tests for duplicate announcement of content

    testName_("test_note", null);

    // //////////////////////////////////////////////////////////////////////
    // Tests for name from sub tree of tr element.

    // By default, we want no name.
    testName_("NoNameForTR", null);
    testName_("NoNameForNonStandardTR", null);

    // But we want it if the tr has an ARIA role.
    testName_("NameForTRBecauseStrongARIA", "a b");

    // But not if it is a weak (landmark) ARIA role
    testName_("NoNameForTRBecauseWeakARIA", null);

    // Name from sub tree of grouping if requested by other accessible.
    testName_("grouping", null);
    testName_("requested_name_from_grouping", "label");
    testName_("listitem_containing_block_tbody", "label");
    // Groupings shouldn't be included when calculating from the subtree of
    // a treeitem.
    testName_("treeitem_containing_grouping", "root");

    // Name from subtree of grouping labelled by an ancestor.
    testName_("grouping_labelledby_ancestor", "label");

    // Name from subtree of a container containing text nodes and inline
    // elements. There should be no spaces because everything is inline.
    testName_("container_text_inline", "abc");
    // Name from subtree of a container containing text nodes and block
    // elements. There should be a space on both sides of the block.
    testName_("container_text_block", "a b c");
    // Name from subtree of a container containing text nodes and empty
    // block elements. There should be space on either side of the blocks, but
    // not a double space.
    testName_("container_text_emptyblock", "a b");

    let reordered = waitForEvent(EVENT_REORDER, "shadowHost");
    await invokeContentTask(browser, [], () => {
      const shadowHost = content.document.getElementById("shadowHost");
      const shadowRoot = shadowHost.attachShadow({ mode: "open" });
      shadowRoot.append(
        content.document
          .getElementById("shadowTemplate")
          .content.cloneNode(true)
      );
    });
    await reordered;

    const shadowRoot = findAccessibleChildByID(docAcc, "shadowHost");
    testName(shadowRoot.firstChild, "shadowButtonVisibleText");
    testName(shadowRoot.lastChild, "shadowButtonHiddenText");

    // Label from a hidden element containing script and style elements.
    testName_("buttonScriptStyle", "content");

    // Name from subtree on a link containing <code>, etc.
    testName_("linkWithCodeSupSubInsDel", "before code sup sub ins del after");
  },
  { topLevel: true, chrome: true }
);
