"use strict";

// Try DOM APIs with a global object that is not a Window. See bug 1942517.
add_task(async function () {
  // DOMParser.parseFromString().
  const untrusted_string = `<div id="untrusted">Hello World!</div>`;
  const html = new DOMParser().parseFromString(
    untrusted_string,
    "text/html"
  ).documentElement;
  let div = html.querySelector("#untrusted");
  Assert.equal(div.outerHTML, untrusted_string);

  // Element's innerHTML, outerHTML, insertAdjacentHTML(), setHTMLUnsafe().
  div.innerHTML = "<p></p>";
  Assert.equal(div.outerHTML, `<div id="untrusted"><p></p></div>`);
  div.insertAdjacentHTML("afterbegin", "<div></div>");
  Assert.equal(div.outerHTML, `<div id="untrusted"><div></div><p></p></div>`);
  div.lastElementChild.outerHTML = "";
  Assert.equal(div.outerHTML, `<div id="untrusted"><div></div></div>`);
  div.setHTMLUnsafe("<span></span>");
  Assert.equal(div.outerHTML, `<div id="untrusted"><span></span></div>`);

  // Element's setAttribute(), setAttributeNS().
  div.setAttribute("onclick", ";");
  div.getAttribute("onclick", ";");
  div.setAttributeNS(null, "onclick", ";;");
  div.getAttribute("onclick", ";;");

  // Document's execCommand(), parseHTMLUnsafe(), write(), writeln().
  let document = html.ownerDocument;
  document.execCommand("insertHTML", false, "<em>Hello World</em>");
  Assert.throws(_ => document.parseHTMLUnsafe("<div></div>"), /not a function/);
  Assert.throws(_ => document.write("a"), /insecure/);
  Assert.throws(_ => document.writeln("b"), /insecure/);

  // HTMLIFrameElement's srcdoc.
  let iframe = document.createElement("iframe");
  iframe.srcdoc = "<span></span>";
  Assert.equal(iframe.getAttribute("srcdoc"), "<span></span>");

  // HTMLScriptElement's text, textContent, innerText, src.
  let script = document.createElement("script");
  script.text = ";";
  Assert.equal(script.innerHTML, ";");
  script.textContent = ";;";
  Assert.equal(script.innerHTML, ";;");
  script.innerText = ";;;";
  Assert.equal(script.innerHTML, ";;;");
  script.src = "about:blank";
  Assert.equal(script.getAttribute("src"), "about:blank");

  // ShadowRoot's innerHTML.
  let d = document.createElement("div");
  let s = d.attachShadow({ mode: "open" });
  s.innerHTML = "<i></i>";
  Assert.equal(s.firstElementChild.tagName, "I");

  // Range's createContextualFragment().
  var range = document.createRange();
  range.selectNode(div);
  var result = range.createContextualFragment("<div>ABC</div>");
  Assert.equal(result.textContent, "ABC");
});

// Try eval/Function APIs with a global object that is neither a Window nor a
// WorkerGlobalScope.
// See https://phabricator.services.mozilla.com/D233507?id=967939#inline-1302281
add_task(async function () {
  // eslint-disable-next-line no-eval
  Assert.equal(eval("1+2"), 3);
  Assert.equal(new Function("a", "b", "return a + b;")(1, 2), 3);
});
