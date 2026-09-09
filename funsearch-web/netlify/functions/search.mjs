const INDEX = "https://www.bing.com/search";
const DDG = "https://html.duckduckgo.com/html/";
const PAGE_SIZE = 10;

function safeHttpUrl(href, base) {
  try {
    const url = new URL(href, base);
    if (url.protocol !== "http:" && url.protocol !== "https:") {
      return null;
    }
    return url.href;
  } catch {
    return null;
  }
}

function parseRss(xmlText) {
  const items = [...xmlText.matchAll(/<item>([\s\S]*?)<\/item>/gi)];
  return items
    .map(match => {
      const block = match[1];
      const title = block.match(/<title>(?:<!\[CDATA\[)?([\s\S]*?)(?:\]\]>)?<\/title>/i)?.[1]
        ?.replace(/<[^>]+>/g, "")
        .trim();
      const link = block.match(/<link>([\s\S]*?)<\/link>/i)?.[1]?.trim();
      const snippet = block
        .match(/<description>(?:<!\[CDATA\[)?([\s\S]*?)(?:\]\]>)?<\/description>/i)?.[1]
        ?.replace(/<[^>]+>/g, "")
        .trim();
      const url = safeHttpUrl(link, INDEX);
      return title && url ? { title, url, snippet: snippet || "" } : null;
    })
    .filter(Boolean);
}

function parseBingHtml(html) {
  const results = [];
  const blocks = html.split(/<li class="b_algo"/i).slice(1);
  for (const block of blocks) {
    const linkMatch = block.match(/<h2[^>]*>\s*<a[^>]*href="([^"]+)"[^>]*>([\s\S]*?)<\/a>/i);
    if (!linkMatch) {
      continue;
    }
    const title = linkMatch[2].replace(/<[^>]+>/g, "").trim();
    const url = safeHttpUrl(linkMatch[1], INDEX);
    const snippet =
      block
        .match(/<p[^>]*>([\s\S]*?)<\/p>/i)?.[1]
        ?.replace(/<[^>]+>/g, "")
        .trim() || "";
    if (title && url) {
      results.push({ title, url, snippet });
    }
  }
  return results;
}

function parseDdgHtml(html) {
  const results = [];
  const links = html.matchAll(
    /<a[^>]*class="[^"]*result__a[^"]*"[^>]*href="([^"]+)"[^>]*>([\s\S]*?)<\/a>/gi
  );
  for (const match of links) {
    const title = match[2].replace(/<[^>]+>/g, "").trim();
    let href = match[1];
    const uddg = href.match(/[?&]uddg=([^&]+)/);
    if (uddg) {
      href = decodeURIComponent(uddg[1]);
    }
    const url = safeHttpUrl(href, DDG);
    if (title && url) {
      results.push({ title, url, snippet: "" });
    }
  }
  return results;
}

async function fetchText(url) {
  const response = await fetch(url, {
    headers: {
      "User-Agent":
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:146.0) Gecko/20100101 Funxplorer/146.0",
      Accept: "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    },
  });
  if (!response.ok) {
    throw new Error("fetch-failed");
  }
  return response.text();
}

export default async req => {
  if (req.method !== "GET") {
    return new Response("Method not allowed", { status: 405 });
  }

  const { searchParams } = new URL(req.url);
  const query = (searchParams.get("q") || "").trim();
  const page = Math.max(parseInt(searchParams.get("page") || "1", 10) || 1, 1);
  if (!query) {
    return Response.json({ query, page, results: [] });
  }

  const first = (page - 1) * PAGE_SIZE + 1;
  let results = [];

  try {
    if (page === 1) {
      const rss = await fetchText(`${INDEX}?format=rss&q=${encodeURIComponent(query)}`);
      results = parseRss(rss);
    }
    if (!results.length) {
      const html = await fetchText(
        `${INDEX}?q=${encodeURIComponent(query)}&first=${first}`
      );
      results = parseBingHtml(html);
    }
  } catch {
    results = [];
  }

  if (!results.length) {
    try {
      const html = await fetchText(`${DDG}?q=${encodeURIComponent(query)}`);
      results = parseDdgHtml(html);
    } catch {
      results = [];
    }
  }

  return Response.json({ query, page, results: results.slice(0, PAGE_SIZE) });
};

export const config = {
  path: "/api/search",
};
