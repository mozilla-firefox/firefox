/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const INDEX_SEARCH = "https://www.bing.com/search";
const PAGE_SIZE = 10;

function safeHttpUrl(href) {
  try {
    let url = new URL(href, INDEX_SEARCH);
    if (url.protocol != "http:" && url.protocol != "https:") {
      return null;
    }
    return url.href;
  } catch {
    return null;
  }
}

function parseRss(xmlText) {
  let doc = new DOMParser().parseFromString(xmlText, "application/xml");
  if (doc.querySelector("parsererror")) {
    return [];
  }
  return [...doc.querySelectorAll("item")]
    .map(item => {
      let title = item.querySelector("title")?.textContent?.trim();
      let url = safeHttpUrl(item.querySelector("link")?.textContent?.trim());
      let snippet = item.querySelector("description")?.textContent?.trim();
      return title && url ? { title, url, snippet: snippet || "" } : null;
    })
    .filter(Boolean);
}

function parseHtml(htmlText) {
  let doc = new DOMParser().parseFromString(htmlText, "text/html");
  return [...doc.querySelectorAll("li.b_algo")]
    .map(item => {
      let link = item.querySelector("h2 a, .b_title a");
      let title = link?.textContent?.trim();
      let url = safeHttpUrl(link?.href);
      let snippet =
        item
          .querySelector("p, .b_caption p, .b_lineclamp")
          ?.textContent?.trim() || "";
      return title && url ? { title, url, snippet } : null;
    })
    .filter(Boolean);
}

async function fetchResults(query, page) {
  let first = (page - 1) * PAGE_SIZE + 1;
  let rssUrl = `${INDEX_SEARCH}?format=rss&q=${encodeURIComponent(query)}`;
  let htmlUrl = `${INDEX_SEARCH}?q=${encodeURIComponent(query)}&first=${first}`;

  if (page == 1) {
    try {
      let rssResponse = await fetch(rssUrl, { credentials: "omit" });
      if (rssResponse.ok) {
        let rssResults = parseRss(await rssResponse.text());
        if (rssResults.length) {
          return rssResults;
        }
      }
    } catch {
      // Fall through to HTML.
    }
  }

  let htmlResponse = await fetch(htmlUrl, { credentials: "omit" });
  if (!htmlResponse.ok) {
    throw new Error("search-failed");
  }
  return parseHtml(await htmlResponse.text());
}

export class AboutFunsearchParent extends JSWindowActorParent {
  receiveMessage(message) {
    if (message.name == "FetchResults") {
      let query = String(message.data?.query || "").trim();
      let page = Math.max(parseInt(message.data?.page, 10) || 1, 1);
      if (!query) {
        return [];
      }
      return fetchResults(query, page);
    }
    return undefined;
  }
}
