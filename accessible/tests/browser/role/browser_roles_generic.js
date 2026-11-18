/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from ../../mochitest/role.js */
loadScripts({ name: "role.js", dir: MOCHITESTS_DIR });

/**
 * Test ARIA role map
 */
addAccessibleTask(
  `<span id="aria_alert" role="alert"></span>
  <span id="aria_alert_mixed" role="aLERt"></span>
  <span id="aria_alertdialog" role="alertdialog"></span>
  <span id="aria_alertdialog_mixed" role="aLERTDIALOg"></span>
  <span id="aria_application" role="application"></span>
  <span id="aria_application_mixed" role="aPPLICATIOn"></span>
  <span id="aria_article" role="article"></span>
  <span id="aria_article_mixed" role="aRTICLe"></span>
  <span id="aria_blockquote" role="blockquote"></span>
  <span id="aria_blockquote_mixed" role="bLOCKQUOTe"></span>
  <span id="aria_button" role="button"></span>
  <span id="aria_button_mixed" role="bUTTOn"></span>
  <span id="aria_caption" role="caption"></span>
  <span id="aria_caption_mixed" role="cAPTIOn"></span>
  <span id="aria_checkbox" role="checkbox"></span>
  <span id="aria_checkbox_mixed" role="cHECKBOx"></span>
  <span id="aria_code" role="code"></span>
  <span id="aria_code_mixed" role="cODe"></span>
  <span id="aria_combobox" role="combobox"></span>
  <span id="aria_combobox_mixed" role="cOMBOBOx"></span>
  <span id="aria_comment" role="comment"></span>
  <span id="aria_comment_mixed" role="cOMMENt"></span>
  <span id="aria_deletion" role="deletion"></span>
  <span id="aria_deletion_mixed" role="dELETIOn"></span>
  <span id="aria_dialog" role="dialog"></span>
  <span id="aria_dialog_mixed" role="dIALOg"></span>
  <span id="aria_directory" role="directory"></span>
  <span id="aria_directory_mixed" role="dIRECTORy"></span>
  <span id="aria_document" role="document"></span>
  <span id="aria_document_mixed" role="dOCUMENt"></span>
  <span id="aria_form" role="form"></span>
  <span id="aria_form_mixed" role="fORm"></span>
  <span id="aria_form_with_label" role="form" aria-label="Label"></span>
  <span id="aria_form_with_label_mixed" role="fORm" aria-label="Label"></span>
  <span id="aria_feed" role="feed"></span>
  <span id="aria_feed_mixed" role="fEEd"></span>
  <span id="aria_figure" role="figure"></span>
  <span id="aria_figure_mixed" role="fIGURe"></span>
  <span id="aria_grid" role="grid"></span>
  <span id="aria_grid_mixed" role="gRId"></span>
  <span id="aria_group" role="group"></span>
  <span id="aria_group_mixed" role="gROUp"></span>
  <span id="aria_heading" role="heading"></span>
  <span id="aria_heading_mixed" role="hEADINg"></span>
  <span id="aria_img" role="img"></span>
  <span id="aria_img_mixed" role="iMg"></span>
  <span id="aria_insertion" role="insertion"></span>
  <span id="aria_insertion_mixed" role="iNSERTIOn"></span>
  <span id="aria_link" role="link"></span>
  <span id="aria_link_mixed" role="lINk"></span>
  <span id="aria_list" role="list"></span>
  <span id="aria_list_mixed" role="lISt"></span>
  <span id="aria_listbox" role="listbox"></span>
  <span id="aria_listbox_mixed" role="lISTBOx"></span>
  <span id="aria_log" role="log"></span>
  <span id="aria_log_mixed" role="lOg"></span>
  <span id="aria_mark" role="mark"></span>
  <span id="aria_mark_mixed" role="mARk"></span>
  <span id="aria_marquee" role="marquee"></span>
  <span id="aria_marquee_mixed" role="mARQUEe"></span>
  <span id="aria_math" role="math"></span>
  <span id="aria_math_mixed" role="mATh"></span>
  <span id="aria_menu" role="menu"></span>
  <span id="aria_menu_mixed" role="mENu"></span>
  <span id="aria_menubar" role="menubar"></span>
  <span id="aria_menubar_mixed" role="mENUBAr"></span>
  <span id="aria_meter" role="meter"></span>
  <span id="aria_meter_mixed" role="meTer"></span>
  <span id="aria_note" role="note"></span>
  <span id="aria_note_mixed" role="nOTe"></span>
  <span id="aria_paragraph" role="paragraph"></span>
  <span id="aria_paragraph_mixed" role="pARAGRAPh"></span>
  <span id="aria_presentation" role="presentation" tabindex="0"></span>
  <span id="aria_presentation_mixed" role="pRESENTATIOn" tabindex="0"></span>
  <span id="aria_progressbar" role="progressbar"></span>
  <span id="aria_progressbar_mixed" role="pROGRESSBAr"></span>
  <span id="aria_radio" role="radio"></span>
  <span id="aria_radio_mixed" role="rADIo"></span>
  <span id="aria_radiogroup" role="radiogroup"></span>
  <span id="aria_radiogroup_mixed" role="rADIOGROUp"></span>
  <span id="aria_region_no_name" role="region"></span>
  <span id="aria_region_no_name_mixed" role="rEGIOn"></span>
  <span id="aria_region_has_label" role="region" aria-label="label"></span>
  <span id="aria_region_has_label_mixed" role="rEGIOn" aria-label="label"></span>
  <span id="aria_region_has_labelledby" role="region" aria-labelledby="label"><span id="label" aria-label="label"></span></span>
  <span id="aria_region_has_labelledby_mixed" role="rEGIOn" aria-labelledby="label"><span id="label" aria-label="label"></span></span>
  <span id="aria_region_has_title" role="region" title="title"></span>
  <span id="aria_region_has_title_mixed" role="rEGIOn" title="title"></span>
  <span id="aria_region_empty_name" role="region" aria-label="" title="" aria-labelledby="empty"></span><span id="empty"></span>
  <span id="aria_region_empty_name_mixed" role="rEGIOn" aria-label="" title="" aria-labelledby="empty"></span><span id="empty"></span>
  <table id="aria_region_as_table_with_caption" role="region"><caption>hello</caption></table>
  <table id="aria_region_as_table_with_caption_mixed" role="rEGIOn"><caption>hello</caption></table>
  <table id="aria_region_as_table_with_miscaption" role="region"><caption role="option">hello</caption></table>
  <table id="aria_region_as_table_with_miscaption_mixed" role="rEGIOn"><caption role="option">hello</caption></table>
  <span id="aria_scrollbar" role="scrollbar"></span>
  <span id="aria_scrollbar_mixed" role="sCROLLBAr"></span>
  <span id="aria_searchbox" role="searchbox"></span>
  <span id="aria_searchbox_mixed" role="sEARCHBOx"></span>
  <span id="aria_separator" role="separator"></span>
  <span id="aria_separator_mixed" role="sEPARATOr"></span>
  <span id="aria_slider" role="slider"></span>
  <span id="aria_slider_mixed" role="sLIDEr"></span>
  <span id="aria_spinbutton" role="spinbutton"></span>
  <span id="aria_spinbutton_mixed" role="sPINBUTTOn"></span>
  <span id="aria_status" role="status"></span>
  <span id="aria_status_mixed" role="sTATUs"></span>
  <span id="aria_subscript" role="subscript"></span>
  <span id="aria_subscript_mixed" role="sUBSCRIPt"></span>
  <span id="aria_suggestion" role="suggestion"></span>
  <span id="aria_suggestion_mixed" role="sUGGESTIOn"></span>
  <span id="aria_superscript" role="superscript"></span>
  <span id="aria_superscript_mixed" role="sUPERSCRIPt"></span>
  <span id="aria_switch" role="switch"></span>
  <span id="aria_switch_mixed" role="sWITCh"></span>
  <span id="aria_tablist" role="tablist"></span>
  <span id="aria_tablist_mixed" role="tABLISt"></span>
  <span id="aria_tabpanel" role="tabpanel"></span>
  <span id="aria_tabpanel_mixed" role="tABPANEl"></span>
  <span id="aria_term" role="term"></span>
  <span id="aria_term_mixed" role="tERm"></span>
  <span id="aria_textbox" role="textbox"></span>
  <span id="aria_textbox_mixed" role="tEXTBOx"></span>
  <span id="aria_timer" role="timer"></span>
  <span id="aria_timer_mixed" role="tIMEr"></span>
  <span id="aria_toolbar" role="toolbar"></span>
  <span id="aria_toolbar_mixed" role="tOOLBAr"></span>
  <span id="aria_tooltip" role="tooltip"></span>
  <span id="aria_tooltip_mixed" role="tOOLTIp"></span>
  <span id="aria_tree" role="tree"></span>
  <span id="aria_tree_mixed" role="tREe"></span>
  <span id="aria_treegrid" role="treegrid"></span>
  <span id="aria_treegrid_mixed" role="tREEGRId"></span>`,
  async function testARIARoleMap(browser, accDoc) {
    let getAcc = id => findAccessibleChildByID(accDoc, id);
    testRole(getAcc("aria_alert"), ROLE_ALERT);
    testRole(getAcc("aria_alert_mixed"), ROLE_ALERT);
    testRole(getAcc("aria_alertdialog"), ROLE_DIALOG);
    testRole(getAcc("aria_alertdialog_mixed"), ROLE_DIALOG);
    testRole(getAcc("aria_application"), ROLE_APPLICATION);
    testRole(getAcc("aria_application_mixed"), ROLE_APPLICATION);
    testRole(getAcc("aria_article"), ROLE_ARTICLE);
    testRole(getAcc("aria_article_mixed"), ROLE_ARTICLE);
    testRole(getAcc("aria_blockquote"), ROLE_BLOCKQUOTE);
    testRole(getAcc("aria_blockquote_mixed"), ROLE_BLOCKQUOTE);
    testRole(getAcc("aria_button"), ROLE_PUSHBUTTON);
    testRole(getAcc("aria_button_mixed"), ROLE_PUSHBUTTON);
    testRole(getAcc("aria_caption"), ROLE_CAPTION);
    testRole(getAcc("aria_caption_mixed"), ROLE_CAPTION);
    testRole(getAcc("aria_checkbox"), ROLE_CHECKBUTTON);
    testRole(getAcc("aria_checkbox_mixed"), ROLE_CHECKBUTTON);
    testRole(getAcc("aria_code"), ROLE_CODE);
    testRole(getAcc("aria_code_mixed"), ROLE_CODE);
    testRole(getAcc("aria_combobox"), ROLE_EDITCOMBOBOX);
    testRole(getAcc("aria_combobox_mixed"), ROLE_EDITCOMBOBOX);
    testRole(getAcc("aria_comment"), ROLE_COMMENT);
    testRole(getAcc("aria_comment_mixed"), ROLE_COMMENT);
    testRole(getAcc("aria_deletion"), ROLE_CONTENT_DELETION);
    testRole(getAcc("aria_deletion_mixed"), ROLE_CONTENT_DELETION);
    testRole(getAcc("aria_dialog"), ROLE_DIALOG);
    testRole(getAcc("aria_dialog_mixed"), ROLE_DIALOG);
    testRole(getAcc("aria_directory"), ROLE_LIST);
    testRole(getAcc("aria_directory_mixed"), ROLE_LIST);
    testRole(getAcc("aria_document"), ROLE_NON_NATIVE_DOCUMENT);
    testRole(getAcc("aria_document_mixed"), ROLE_NON_NATIVE_DOCUMENT);
    testRole(getAcc("aria_form"), ROLE_TEXT);
    testRole(getAcc("aria_form_mixed"), ROLE_TEXT);
    testRole(getAcc("aria_form_with_label"), ROLE_FORM_LANDMARK);
    testRole(getAcc("aria_form_with_label_mixed"), ROLE_FORM_LANDMARK);
    testRole(getAcc("aria_feed"), ROLE_GROUPING);
    testRole(getAcc("aria_feed_mixed"), ROLE_GROUPING);
    testRole(getAcc("aria_figure"), ROLE_FIGURE);
    testRole(getAcc("aria_figure_mixed"), ROLE_FIGURE);
    testRole(getAcc("aria_grid"), ROLE_GRID);
    testRole(getAcc("aria_grid_mixed"), ROLE_GRID);
    testRole(getAcc("aria_group"), ROLE_GROUPING);
    testRole(getAcc("aria_group_mixed"), ROLE_GROUPING);
    testRole(getAcc("aria_heading"), ROLE_HEADING);
    testRole(getAcc("aria_heading_mixed"), ROLE_HEADING);
    testRole(getAcc("aria_img"), ROLE_GRAPHIC);
    testRole(getAcc("aria_img_mixed"), ROLE_GRAPHIC);
    testRole(getAcc("aria_insertion"), ROLE_CONTENT_INSERTION);
    testRole(getAcc("aria_insertion_mixed"), ROLE_CONTENT_INSERTION);
    testRole(getAcc("aria_link"), ROLE_LINK);
    testRole(getAcc("aria_link_mixed"), ROLE_LINK);
    testRole(getAcc("aria_list"), ROLE_LIST);
    testRole(getAcc("aria_list_mixed"), ROLE_LIST);
    testRole(getAcc("aria_listbox"), ROLE_LISTBOX);
    testRole(getAcc("aria_listbox_mixed"), ROLE_LISTBOX);
    testRole(getAcc("aria_log"), ROLE_TEXT); // weak role
    testRole(getAcc("aria_log_mixed"), ROLE_TEXT); // weak role
    testRole(getAcc("aria_mark"), ROLE_MARK);
    testRole(getAcc("aria_mark_mixed"), ROLE_MARK);
    testRole(getAcc("aria_marquee"), ROLE_ANIMATION);
    testRole(getAcc("aria_marquee_mixed"), ROLE_ANIMATION);
    testRole(getAcc("aria_math"), ROLE_FLAT_EQUATION);
    testRole(getAcc("aria_math_mixed"), ROLE_FLAT_EQUATION);
    testRole(getAcc("aria_menu"), ROLE_MENUPOPUP);
    testRole(getAcc("aria_menu_mixed"), ROLE_MENUPOPUP);
    testRole(getAcc("aria_menubar"), ROLE_MENUBAR);
    testRole(getAcc("aria_menubar_mixed"), ROLE_MENUBAR);
    testRole(getAcc("aria_meter"), ROLE_METER);
    testRole(getAcc("aria_meter_mixed"), ROLE_METER);
    testRole(getAcc("aria_note"), ROLE_NOTE);
    testRole(getAcc("aria_note_mixed"), ROLE_NOTE);
    testRole(getAcc("aria_paragraph"), ROLE_PARAGRAPH);
    testRole(getAcc("aria_paragraph_mixed"), ROLE_PARAGRAPH);
    testRole(getAcc("aria_presentation"), ROLE_TEXT); // weak role
    testRole(getAcc("aria_presentation_mixed"), ROLE_TEXT); // weak role
    testRole(getAcc("aria_progressbar"), ROLE_PROGRESSBAR);
    testRole(getAcc("aria_progressbar_mixed"), ROLE_PROGRESSBAR);
    testRole(getAcc("aria_radio"), ROLE_RADIOBUTTON);
    testRole(getAcc("aria_radio_mixed"), ROLE_RADIOBUTTON);
    testRole(getAcc("aria_radiogroup"), ROLE_RADIO_GROUP);
    testRole(getAcc("aria_radiogroup_mixed"), ROLE_RADIO_GROUP);
    testRole(getAcc("aria_region_no_name"), ROLE_TEXT);
    testRole(getAcc("aria_region_no_name_mixed"), ROLE_TEXT);
    testRole(getAcc("aria_region_has_label"), ROLE_REGION);
    testRole(getAcc("aria_region_has_label_mixed"), ROLE_REGION);
    testRole(getAcc("aria_region_has_labelledby"), ROLE_REGION);
    testRole(getAcc("aria_region_has_labelledby_mixed"), ROLE_REGION);
    testRole(getAcc("aria_region_has_title"), ROLE_REGION);
    testRole(getAcc("aria_region_has_title_mixed"), ROLE_REGION);
    testRole(getAcc("aria_region_empty_name"), ROLE_TEXT);
    testRole(getAcc("aria_region_empty_name_mixed"), ROLE_TEXT);
    testRole(getAcc("aria_region_as_table_with_caption"), ROLE_REGION);
    testRole(getAcc("aria_region_as_table_with_caption_mixed"), ROLE_REGION);
    testRole(getAcc("aria_region_as_table_with_miscaption"), ROLE_REGION);
    testRole(getAcc("aria_region_as_table_with_miscaption_mixed"), ROLE_REGION);
    testRole(getAcc("aria_scrollbar"), ROLE_SCROLLBAR);
    testRole(getAcc("aria_scrollbar_mixed"), ROLE_SCROLLBAR);
    testRole(getAcc("aria_searchbox"), ROLE_SEARCHBOX);
    testRole(getAcc("aria_searchbox_mixed"), ROLE_SEARCHBOX);
    testRole(getAcc("aria_separator"), ROLE_SEPARATOR);
    testRole(getAcc("aria_separator_mixed"), ROLE_SEPARATOR);
    testRole(getAcc("aria_slider"), ROLE_SLIDER);
    testRole(getAcc("aria_slider_mixed"), ROLE_SLIDER);
    testRole(getAcc("aria_spinbutton"), ROLE_SPINBUTTON);
    testRole(getAcc("aria_spinbutton_mixed"), ROLE_SPINBUTTON);
    testRole(getAcc("aria_status"), ROLE_STATUSBAR);
    testRole(getAcc("aria_status_mixed"), ROLE_STATUSBAR);
    testRole(getAcc("aria_subscript"), ROLE_SUBSCRIPT);
    testRole(getAcc("aria_subscript_mixed"), ROLE_SUBSCRIPT);
    testRole(getAcc("aria_suggestion"), ROLE_SUGGESTION);
    testRole(getAcc("aria_suggestion_mixed"), ROLE_SUGGESTION);
    testRole(getAcc("aria_superscript"), ROLE_SUPERSCRIPT);
    testRole(getAcc("aria_superscript_mixed"), ROLE_SUPERSCRIPT);
    testRole(getAcc("aria_switch"), ROLE_SWITCH);
    testRole(getAcc("aria_switch_mixed"), ROLE_SWITCH);
    testRole(getAcc("aria_tablist"), ROLE_PAGETABLIST);
    testRole(getAcc("aria_tablist_mixed"), ROLE_PAGETABLIST);
    testRole(getAcc("aria_tabpanel"), ROLE_PROPERTYPAGE);
    testRole(getAcc("aria_tabpanel_mixed"), ROLE_PROPERTYPAGE);
    testRole(getAcc("aria_term"), ROLE_TERM);
    testRole(getAcc("aria_term_mixed"), ROLE_TERM);
    testRole(getAcc("aria_textbox"), ROLE_ENTRY);
    testRole(getAcc("aria_textbox_mixed"), ROLE_ENTRY);
    testRole(getAcc("aria_timer"), ROLE_TEXT); // weak role
    testRole(getAcc("aria_timer_mixed"), ROLE_TEXT); // weak role
    testRole(getAcc("aria_toolbar"), ROLE_TOOLBAR);
    testRole(getAcc("aria_toolbar_mixed"), ROLE_TOOLBAR);
    testRole(getAcc("aria_tooltip"), ROLE_TOOLTIP);
    testRole(getAcc("aria_tooltip_mixed"), ROLE_TOOLTIP);
    testRole(getAcc("aria_tree"), ROLE_OUTLINE);
    testRole(getAcc("aria_tree_mixed"), ROLE_OUTLINE);
    testRole(getAcc("aria_treegrid"), ROLE_TREE_TABLE);
    testRole(getAcc("aria_treegrid_mixed"), ROLE_TREE_TABLE);
  },
  { chrome: true, topLevel: true }
);

/**
 * Test ARIA landmarks
 */
addAccessibleTask(
  `  <article id="articlemain" role="main">a main area</article>
  <article id="articlemain_mixed" role="mAIn">a main area</article>
  <article id="articleform" role="form">a form area</article>
  <article id="articleform_mixed" role="fORm">a form area</article>
  <article id="articleform_label" aria-label="form" role="form">a form area</article>
  <article id="articleform_label_mixed" aria-label="form" role="fORm">a form area</article>

  <div id="testArticle" role="article" title="Test article">
    <p>This is a paragraph inside the article.</p>
  </div>

  <div id="testArticle_mixed" role="aRTICLe" title="Test article">
    <p>This is a paragraph inside the article.</p>
  </div>

  <!-- "live" roles -->
  <table role="log" id="log_table">
    <tr><td>Table based log</td></tr>
  </table>
  <table role="LOG" id="log_table_mixed">
    <tr><td>Table based log</td></tr>
  </table>
  <h1 role="marquee" id="marquee_h1">marquee</h1>
  <h1 role="MARQUEE" id="marquee_h1_mixed">marquee</h1>
  <div role="timer" id="timer_div">timer</div>
  <div role="TIMER" id="timer_div_mixed">timer</div>

  <!-- landmarks -->
  <div role="application" id="application">application</div>
  <div role="aPPLICATIOn" id="application_mixed">application</div>
  <div role="form" id="form">form</div>
  <div role="fORm" id="form_mixed">form</div>
  <div role="form" id="form_label" aria-label="form">form</div>
  <div role="fORm" id="form_label_mixed" aria-label="form">form</div>

  <!-- weak landmarks -->
  <div role="banner" id="banner">banner</div>
  <div role="bANNEr" id="banner_mixed">banner</div>
  <div role="complementary" id="complementary">complementary</div>
  <div role="cOMPLEMENTARy" id="complementary_mixed">complementary</div>
  <div role="contentinfo" id="contentinfo">contentinfo</div>
  <div role="cONTENTINFo" id="contentinfo_mixed">contentinfo</div>
  <div role="main" id="main">main</div>
  <div role="mAIN" id="main_mixed">main</div>
  <div role="navigation" id="navigation">navigation</div>
  <div role="nAVIGATIOn" id="navigation_mixed">navigation</div>
  <div role="search" id="search">search</div>
  <div role="sEARCh" id="search_mixed">search</div>

  <!-- landmarks are tables -->
  <table role="application" id="application_table">application table
    <tr><td>hi</td></tr></table>
  <table role="aPPLICATIOn" id="application_table_mixed">application table
    <tr><td>hi</td></tr></table>
  <table role="banner" id="banner_table">banner table
    <tr><td>hi</td></tr></table>
  <table role="bANNEr" id="banner_table_mixed">banner table
    <tr><td>hi</td></tr></table>
  <table role="complementary" id="complementary_table">complementary table
    <tr><td>hi</td></tr></table>
  <table role="cOMPLEMENTARy" id="complementary_table_mixed">complementary table
    <tr><td>hi</td></tr></table>    
  <table role="contentinfo" id="contentinfo_table">contentinfo table
    <tr><td>hi</td></tr></table>
  <table role="cONTENTINFo" id="contentinfo_table_mixed">contentinfo table
    <tr><td>hi</td></tr></table>    
  <table role="main" id="main_table">main table
    <tr><td>hi</td></tr></table>
  <table role="mAIn" id="main_table_mixed">main table
    <tr><td>hi</td></tr></table>    
  <table role="navigation" id="navigation_table">navigation table
    <tr><td>hi</td></tr></table>
  <table role="nAVIGATIOn" id="navigation_table_mixed">navigation table
    <tr><td>hi</td></tr></table>    
  <table role="search" id="search_table">search table
    <tr><td>hi</td></tr></table>
  <table role="sEARCh" id="search_table_mixed">search table
    <tr><td>hi</td></tr></table>
`,
  async function testLandMarks(browser, accDoc) {
    let getAcc = id => findAccessibleChildByID(accDoc, id);
    // Note:
    // The phrase "weak foo" here means that there is no good foo-to-gecko
    // role mapping. Similarly "strong foo" means there is a good foo-to-
    // gecko role mapping.

    testRole(getAcc("articlemain"), ROLE_LANDMARK);
    testRole(getAcc("articlemain_mixed"), ROLE_LANDMARK);
    testRole(getAcc("articleform"), ROLE_ARTICLE);
    testRole(getAcc("articleform_mixed"), ROLE_ARTICLE);
    testRole(getAcc("articleform_label"), ROLE_FORM_LANDMARK);
    testRole(getAcc("articleform_label_mixed"), ROLE_FORM_LANDMARK);

    // Test article exposed as article
    testRole(getAcc("testArticle"), ROLE_ARTICLE);
    testRole(getAcc("testArticle_mixed"), ROLE_ARTICLE);

    // weak roles that are forms of "live regions"
    testRole(getAcc("log_table"), ROLE_TABLE);
    testRole(getAcc("log_table_mixed"), ROLE_TABLE);
    testRole(getAcc("timer_div"), ROLE_SECTION);
    testRole(getAcc("timer_div_mixed"), ROLE_SECTION);

    // other roles that are forms of "live regions"
    testRole(getAcc("marquee_h1"), ROLE_ANIMATION);
    testRole(getAcc("marquee_h1_mixed"), ROLE_ANIMATION);

    // strong landmark
    testRole(getAcc("application"), ROLE_APPLICATION);
    testRole(getAcc("application_mixed"), ROLE_APPLICATION);
    testRole(getAcc("form"), ROLE_SECTION);
    testRole(getAcc("form_mixed"), ROLE_SECTION);
    testRole(getAcc("form_label"), ROLE_FORM_LANDMARK);
    testRole(getAcc("form_label_mixed"), ROLE_FORM_LANDMARK);
    testRole(getAcc("application_table"), ROLE_APPLICATION);
    testRole(getAcc("application_table_mixed"), ROLE_APPLICATION);

    function testLandmark(landmarkId) {
      testRole(getAcc(landmarkId), ROLE_LANDMARK);
      testRole(getAcc(`${landmarkId}_mixed`), ROLE_LANDMARK);

      let landmarkTable = getAcc(`${landmarkId}_table`);
      testRole(landmarkTable, ROLE_LANDMARK);
      ok(
        !!landmarkTable.QueryInterface(nsIAccessibleTable),
        "landmarked table should have nsIAccessibleTable"
      );

      let landmarkTableMixed = getAcc(`${landmarkId}_table_mixed`);
      testRole(landmarkTableMixed, ROLE_LANDMARK);
      ok(
        !!landmarkTableMixed.QueryInterface(nsIAccessibleTable),
        "landmarked table should have nsIAccessibleTable"
      );
      is(landmarkTableMixed.getCellAt(0, 0).firstChild.name, "hi", "no cell");
    }

    testLandmark("banner");
    testLandmark("complementary");
    testLandmark("contentinfo");
    testLandmark("main");
    testLandmark("navigation");
    testLandmark("search");
  },
  { chrome: true, topLevel: true }
);

/**
 * Test abstract roles.
 * User agents must not map abstract roles to anything but a generic "section" gecko role.
 */
addAccessibleTask(
  `<!-- test abstract base type roles -->
  <div role="composite" id="composite">composite</div>
  <div role="cOMPOSITe" id="composite_mixed">composite</div>
  <div role="landmark" id="landmark">landmark</div>
  <div role="lANDMARk" id="landmark_mixed">landmark</div>
  <div role="roletype" id="roletype">roletype</div>
  <div role="rOLETYPe" id="roletype_mixed">roletype</div>
  <div role="structure" id="structure">structure</div>
  <div role="sTRUCTURe" id="structure_mixed">structure</div>
  <div role="widget" id="widget">widget</div>
  <div role="wIDGEt" id="widget_mixed">widget</div>
  <div role="window" id="window">window</div>
  <div role="wINDOw" id="window_mixed">window</div>
  <!-- test abstract input roles -->
  <div role="input" id="input">input</div>
  <div role="iNPUt" id="input_mixed">input</div>
  <div role="range" id="range">range</div>
  <div role="rANGe" id="range_mixed">range</div>
  <div role="select" id="select">select</div>
  <div role="sELECt" id="select_mixed">select</div>
  <!-- test abstract structure roles -->
  <div role="section" id="section">section</div>
  <div role="sECTIOn" id="section_mixed">section</div>
  <div role="sectionhead" id="sectionhead">sectionhead</div>
  <div role="sECTIONHEAd" id="sectionhead_mixed">sectionhead</div>
  <div role="command" id="command">command</div>
  <div role="cOmManD" id="command_mixed">command</div>
`,
  async function testAbstract(browser, accDoc) {
    let getAcc = id => findAccessibleChildByID(accDoc, id);

    function testAbstractRole(id) {
      testRole(getAcc(id), ROLE_SECTION);
      testRole(getAcc(`${id}_mixed`), ROLE_SECTION);
    }

    testAbstractRole("composite");
    testAbstractRole("landmark");
    testAbstractRole("structure");
    testAbstractRole("widget");
    testAbstractRole("window");
    testAbstractRole("input");
    testAbstractRole("range");
    testAbstractRole("select");
    testAbstractRole("section");
    testAbstractRole("sectionhead");
    testAbstractRole("command");
  },
  { chrome: true, topLevel: true }
);

/**
 * Test child roles dependent on ancestor role presence
 */
addAccessibleTask(
  `<table role="button">
    <tr id="buttontable_row">
      <td id="buttontable_cell">cell</td>
    </tr>
  </table>
  <table role="bUTTOn">
    <tr id="buttontable_row_mixed">
      <td id="buttontable_cell_mixed">cell</td>
    </tr>
  </table>

    <!-- roles transformed by ARIA roles of ancestors -->
  <table role="grid">
    <tr>
      <td id="implicit_gridcell">foo</td>
    </tr>
  </table>
  <table role="gRId">
    <tr>
      <td id="implicit_gridcell_mixed">foo</td>
    </tr>
  </table>

  <!-- child roles dependent on ancestor role presence -->
  <div role="grid">
    <div role="row">
      <span id="aria_columnheader" role="columnheader"></span>
      <span id="aria_columnheader_mixed" role="cOLUMNHEADEr"></span>
      <span id="aria_gridcell" role="gridcell"></span>
      <span id="aria_gridcell_mixed" role="gRIDCELl"></span>
      <span id="aria_rowheader" role="rowheader"></span>
      <span id="aria_rowheader_mixed" role="rOWHEADEr"></span>
    </div>
  </div>
  <div role="list">
    <span id="aria_listitem" role="listitem"></span>
    <span id="aria_listitem_mixed" role="lISTITEm"></span>
  </div>
  <div role="menu">
    <span id="aria_menuitem" role="menuitem"></span>
    <span id="aria_menuitem_mixed" role="mENUITEm"></span>
    <span id="aria_menuitemcheckbox" role="menuitemcheckbox"></span>
    <span id="aria_menuitemcheckbox_mixed" role="mENUITEMCHECKBOx"></span>
    <span id="aria_menuitemradio" role="menuitemradio"></span>
    <span id="aria_menuitemradio_mixed" role="mENUITEMRADIo"></span>
  </div>
  <div role="table">
    <span id="aria_row" role="row"></span>
    <span id="aria_row_mixed" role="rOw"></span>
  </div>
  <div role="tablist">
    <span id="aria_tab" role="tab"></span>
    <span id="aria_tab_mixed" role="tAb"></span>
  </div>
  <div role="tree">
    <span id="aria_treeitem" role="treeitem"></span>
    <span id="aria_treeitem_mixed" role="tREEITEm"></span>
  </div>
  <div role="listbox">
    <span id="listOption" role="option"></span>
  </div>
  <div role="combobox">
    <div role="listbox">
      <span id="comboOption1" role="option"></span>
      <div role="group">
        <span id="comboOption2" role="option"></span>
      </div>
    </div>
  </div>

  <!-- roles transformed by ARIA state attributes -->
  <button aria-pressed="true" id="togglebutton"></button>
  <button aria-pressed="tRUe" id="togglebutton_mixed"></button>

  `,
  async function testAncestorDependent(browser, accDoc) {
    let getAcc = id => findAccessibleChildByID(accDoc, id);

    testRole(getAcc("buttontable_row"), ROLE_TEXT_CONTAINER);
    testRole(getAcc("buttontable_row_mixed"), ROLE_TEXT_CONTAINER);
    testRole(getAcc("buttontable_cell"), ROLE_TEXT_CONTAINER);
    testRole(getAcc("buttontable_cell_mixed"), ROLE_TEXT_CONTAINER);

    testRole(getAcc("implicit_gridcell"), ROLE_GRID_CELL);
    testRole(getAcc("implicit_gridcell_mixed"), ROLE_GRID_CELL);

    testRole(getAcc("aria_columnheader"), ROLE_COLUMNHEADER);
    testRole(getAcc("aria_columnheader_mixed"), ROLE_COLUMNHEADER);
    testRole(getAcc("aria_gridcell"), ROLE_GRID_CELL);
    testRole(getAcc("aria_gridcell_mixed"), ROLE_GRID_CELL);
    testRole(getAcc("aria_rowheader"), ROLE_ROWHEADER);
    testRole(getAcc("aria_rowheader_mixed"), ROLE_ROWHEADER);
    testRole(getAcc("aria_listitem"), ROLE_LISTITEM);
    testRole(getAcc("aria_listitem_mixed"), ROLE_LISTITEM);
    testRole(getAcc("aria_menuitem"), ROLE_MENUITEM);
    testRole(getAcc("aria_menuitem_mixed"), ROLE_MENUITEM);
    testRole(getAcc("aria_menuitemcheckbox"), ROLE_CHECK_MENU_ITEM);
    testRole(getAcc("aria_menuitemcheckbox_mixed"), ROLE_CHECK_MENU_ITEM);
    testRole(getAcc("aria_menuitemradio"), ROLE_RADIO_MENU_ITEM);
    testRole(getAcc("aria_menuitemradio_mixed"), ROLE_RADIO_MENU_ITEM);
    testRole(getAcc("aria_row"), ROLE_ROW);
    testRole(getAcc("aria_row_mixed"), ROLE_ROW);
    testRole(getAcc("aria_tab"), ROLE_PAGETAB);
    testRole(getAcc("aria_tab_mixed"), ROLE_PAGETAB);
    testRole(getAcc("aria_treeitem"), ROLE_OUTLINEITEM);
    testRole(getAcc("aria_treeitem_mixed"), ROLE_OUTLINEITEM);
    testRole(getAcc("listOption"), ROLE_OPTION);
    testRole(getAcc("comboOption1"), ROLE_COMBOBOX_OPTION);
    testRole(getAcc("comboOption2"), ROLE_COMBOBOX_OPTION);

    // roles transformed by ARIA state attributes
    testRole(getAcc("togglebutton"), ROLE_TOGGLE_BUTTON);
    testRole(getAcc("togglebutton_mixed"), ROLE_TOGGLE_BUTTON);
  },
  { chrome: true, topLevel: true }
);

/**
 * Test misc quirks
 */
addAccessibleTask(
  `<!-- take the first known mappable role -->
  <div role="wiggly:worm abc123 button" id="unknown_roles">worm button</div>
  <div role="wiggly:worm abc123 bUTTOn" id="unknown_roles_mixed">worm button</div>

  <!-- misc roles -->
  <div role="note" id="note">note</div>
  <div role="nOTe" id="note_mixed">note</div>
  <div role="scrollbar" id="scrollbar">scrollbar</div>
  <div role="sCROLLBAr" id="scrollbar_mixed">scrollbar</div>

  <div id="dir" role="directory">
    <div role="listitem">A</div>
    <div role="listitem">B</div>
    <div role="listitem">C</div>
  </div>

  <div id="dir_mixed" role="dIRECTORy">
    <div role="listitem">A</div>
    <div role="listitem">B</div>
    <div role="listitem">C</div>
  </div>`,
  async function testRoleQuirks(browser, accDoc) {
    let getAcc = id => findAccessibleChildByID(accDoc, id);

    // ////////////////////////////////////////////////////////////////////////
    // ignore unknown roles, take first known
    testRole(getAcc("unknown_roles"), ROLE_PUSHBUTTON);
    testRole(getAcc("unknown_roles_mixed"), ROLE_PUSHBUTTON);

    // ////////////////////////////////////////////////////////////////////////
    // misc roles
    testRole(getAcc("note"), ROLE_NOTE);
    testRole(getAcc("note_mixed"), ROLE_NOTE);
    testRole(getAcc("scrollbar"), ROLE_SCROLLBAR);
    testRole(getAcc("scrollbar_mixed"), ROLE_SCROLLBAR);
    testRole(getAcc("dir"), ROLE_LIST);
    testRole(getAcc("dir_mixed"), ROLE_LIST);
  },
  { chrome: true, topLevel: true }
);

/**
 * Test equations
 */
addAccessibleTask(
  `<p>Image:
    <img id="img_eq" role="math" src="foo" alt="x^2 + y^2 + z^2">
  </p>

  <p>Image:
    <img id="img_eq_mixed" role="mATh" src="foo" alt="x^2 + y^2 + z^2">
  </p>

  <p>Text:
    <span id="txt_eq" role="math" title="x^2 + y^2 + z^2">x<sup>2</sup> +
      y<sup>2</sup> + z<sup>2</sup></span>
  </p>
  <p>Text:
    <span id="txt_eq_mixed" role="mATh" title="x^2 + y^2 + z^2">x<sup>2</sup> +
      y<sup>2</sup> + z<sup>2</sup></span>
  </p>

  `,
  async function testRoleQuirks(browser, accDoc) {
    let getAcc = id => findAccessibleChildByID(accDoc, id);
    // Test equation image
    testRole(getAcc("img_eq"), ROLE_FLAT_EQUATION);
    testRole(getAcc("img_eq_mixed"), ROLE_FLAT_EQUATION);

    // Test textual equation
    testRole(getAcc("txt_eq"), ROLE_FLAT_EQUATION);
    testRole(getAcc("txt_eq_mixed"), ROLE_FLAT_EQUATION);
  },
  { chrome: true, topLevel: true }
);

/**
 * Test doc role changes
 */
addAccessibleTask(
  ``,
  async function testRoleQuirks(browser, accDoc) {
    await invokeSetAttribute(browser, "body", "role", "application");
    await untilCacheIs(
      () => accDoc.role,
      ROLE_APPLICATION,
      "Doc role is 'application'"
    );

    await invokeSetAttribute(browser, "body", "role", "dialog");
    await untilCacheIs(() => accDoc.role, ROLE_DIALOG, "Doc role is 'dialog'");

    // Other roles aren't valid on body elements.

    await invokeSetAttribute(browser, "body", "role", "document");
    await untilCacheIs(
      () => accDoc.role,
      ROLE_DOCUMENT,
      "Doc role is 'document'"
    );

    await invokeSetAttribute(browser, "body", "role", "button");
    await untilCacheIs(
      () => accDoc.role,
      ROLE_DOCUMENT,
      "Doc role is 'document'"
    );

    // ... but mixed roles should still function like their regular variants

    await invokeSetAttribute(browser, "body", "role", "ApPLiCaTiOn");
    await untilCacheIs(
      () => accDoc.role,
      ROLE_APPLICATION,
      "Doc role is 'application'"
    );

    await invokeSetAttribute(browser, "body", "role", "dIaLoG");
    await untilCacheIs(() => accDoc.role, ROLE_DIALOG, "Doc role is 'dialog'");
  },
  { chrome: true, topLevel: true }
);

/**
 * Test re-parented option's role gets recomputed.
 */
addAccessibleTask(
  `<div role="combobox"><div id="listbox" role="listbox"></div></div>
  <div role="option" id="option">Bananas</div>`,
  async function testRelocation(browser, accDoc) {
    is(
      findAccessibleChildByID(accDoc, "option").role,
      ROLE_OPTION,
      "option outside of combobox should have option role"
    );

    let evt = waitForEvent(EVENT_INNER_REORDER, "listbox");
    await invokeSetAttribute(browser, "listbox", "aria-owns", "option");
    await evt;

    is(
      findAccessibleChildByID(accDoc, "option").role,
      ROLE_COMBOBOX_OPTION,
      "option in combobox should have combobox option role"
    );
  },
  { chrome: true, topLevel: true }
);

/**
 * Test that a menuitem outside of a menu still gets a role of menuitem.
 * Strictly speaking, this is an authoring error, but the spec allows browsers
 * to expose the role regardless. Unfortunately, some sites rely on this,
 * including Google Docs, so this test ensures we don't regress those.
 */
addAccessibleTask(
  `
<div role="menubar">
  <div role="menuitem" aria-owns="child"></div>
</div>
<div role="menu">
  <div id="child" role="menuitem">child</div>
</div>
  `,
  async function testOrphanedMenuitem(browser, docAcc) {
    const child = findAccessibleChildByID(docAcc, "child");
    testRole(child, ROLE_MENUITEM);
  },
  { chrome: true, topLevel: true }
);
