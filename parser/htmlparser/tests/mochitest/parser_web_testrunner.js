/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Runs html5lib-formatted test cases in the browser. Requires SimpleTest.
 *
 * Define an array named parserDatFiles before loading this script,
 * and it will load each of those dat files into an array, then run
 * the test parser on each and run the tests by assigning the input
 * data to an iframe's url.
 *
 * Your test document should have an element with id "display" and
 * an iframe with id "testframe".
 */

/* import-globals-from ./parser_datreader.js */
/* import-globals-from ./html5_tree_construction_exceptions.js */
/* globals parserDatFiles */

var functionsToRunAsync = [];

window.addEventListener(
  "message",
  function (event) {
    if (event.source == window && event.data == "async-run") {
      event.stopPropagation();
      var fn = functionsToRunAsync.shift();
      fn();
    }
  },
  true
);

function asyncRun(fn) {
  functionsToRunAsync.push(fn);
  window.postMessage("async-run", "*");
}

function writeErrorSummary(input, expected, got, isTodo) {
  if (isTodo) {
    $("display").appendChild(createEl("h2", null, "Unexpected Success:"));
  } else {
    $("display").appendChild(createEl("h2", null, "Unexpected Failure:"));
  }
  $("display").appendChild(createEl("br"));
  $("display").appendChild(createEl("span", null, "Matched: "));
  $("display").appendChild(document.createTextNode("" + (expected == got)));
  var pre = createEl("pre");
  pre.appendChild(document.createTextNode("Input: \n" + input, "\n-\n"));
  pre.appendChild(document.createTextNode("Expected:\n" + expected, "\n-\n"));
  pre.appendChild(document.createTextNode("Output:\n" + got + "\n-\n"));
  $("display").appendChild(pre);
  $("display").appendChild(createEl("hr"));
}

/**
 * Control will bounce back and forth between nextTest() and the
 * event handler returned by makeTestChecker() or the callback returned by
 * makeFragmentTestChecker() until the 'testcases' iterator is spent.
 */
function makeTestChecker(input, expected) {
  return function (e) {
    var domAsString = docToTestOutput(e.target.contentDocument);
    if (html5Exceptions[input]) {
      todo_is(domAsString, expected, "HTML5 expected success.");
      if (domAsString == expected) {
        writeErrorSummary(input, expected, domAsString, true);
      }
    } else {
      is(domAsString, expected, "HTML5 expected success.");
      if (domAsString != expected) {
        writeErrorSummary(input, expected, domAsString, false);
      }
    }
    nextTest(e.target);
  };
}

function makeFragmentTestChecker(input, expected, errors, fragment, testframe) {
  return function () {
    var context;
    if (fragment.startsWith("svg ")) {
      context = document.createElementNS(
        "http://www.w3.org/2000/svg",
        fragment.substring(4)
      );
    } else if (fragment.startsWith("math ")) {
      context = document.createElementNS(
        "http://www.w3.org/1998/Math/MathML",
        fragment.substring(5)
      );
    } else {
      context = document.createElementNS(
        "http://www.w3.org/1999/xhtml",
        fragment
      );
    }
    context.innerHTML = input;
    var domAsString = fragmentToTestOutput(context);
    is(domAsString, expected, "HTML5 expected success. " + new Date());
    if (domAsString != expected) {
      writeErrorSummary(input, expected, domAsString, false);
    }
    nextTest(testframe);
  };
}

var testcases;
function nextTest(testframe) {
  var { done, value } = testcases.next();
  if (done) {
    SimpleTest.finish();
    return;
  }
  var [input, output, errors, fragment] = value;
  if (fragment) {
    asyncRun(
      makeFragmentTestChecker(input, output, errors, fragment, testframe)
    );
  } else {
    testframe.onload = makeTestChecker(input, output, errors);
    testframe.srcdoc = input;
  }
}

var testFileContents = [];
function loadNextTestFile() {
  var datFile = parserDatFiles.shift();
  if (datFile) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function () {
      if (this.readyState == 4) {
        testFileContents.push(this.responseText);
        loadNextTestFile();
      }
    };
    xhr.open("GET", "html5lib_tree_construction/" + datFile);
    xhr.send();
  } else {
    testcases = test_parser(testFileContents);
    nextTest($("testframe"));
  }
}

addLoadEvent(loadNextTestFile);
SimpleTest.waitForExplicitFinish();
