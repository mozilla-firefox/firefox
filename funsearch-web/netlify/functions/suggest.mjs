export default async req => {
  if (req.method !== "GET") {
    return new Response("Method not allowed", { status: 405 });
  }

  const query = new URL(req.url).searchParams.get("q") || "";
  if (!query.trim()) {
    return Response.json([query, []]);
  }

  try {
    const response = await fetch(
      `https://www.bing.com/osjson.aspx?query=${encodeURIComponent(query)}`,
      {
        headers: {
          "User-Agent":
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:146.0) Gecko/20100101 Funxplorer/146.0",
        },
      }
    );
    if (!response.ok) {
      return Response.json([query, []]);
    }
    const data = await response.json();
    if (Array.isArray(data) && Array.isArray(data[1])) {
      return Response.json([query, data[1].slice(0, 8)]);
    }
  } catch {
    // Ignore and return an empty suggestion list.
  }

  return Response.json([query, []]);
};

export const config = {
  path: "/api/suggest",
};
