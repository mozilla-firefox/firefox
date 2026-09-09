import { corsHeaders, json } from "./_shared/supabase-auth.mjs";

const FUN_SYSTEM = `You are Fun Smart, the built-in agent of Funxplorer by FUNCOMPUTER Labs.
You help the user browse, search, and act inside Funxplorer.
Prefer Funsearch (https://funsearchapp.netlify.app/) for web search.
Accounts are FunComputer. Privacy tools are FUNVPN and FUNTOR.
Be direct, useful, and concise. Use tools when they help.`;

function openaiBase() {
  const base = (
    globalThis.Netlify?.env?.get("OPENAI_BASE_URL") ||
    process.env.OPENAI_BASE_URL ||
    globalThis.Netlify?.env?.get("NETLIFY_AI_GATEWAY_BASE_URL") ||
    process.env.NETLIFY_AI_GATEWAY_BASE_URL ||
    ""
  ).replace(/\/$/, "");
  const key =
    globalThis.Netlify?.env?.get("OPENAI_API_KEY") ||
    process.env.OPENAI_API_KEY ||
    globalThis.Netlify?.env?.get("NETLIFY_AI_GATEWAY_KEY") ||
    process.env.NETLIFY_AI_GATEWAY_KEY ||
    "";
  return { base, key };
}

function mapModel(name) {
  const n = String(name || "").toLowerCase();
  if (n.includes("gpt-4.1")) {
    return "gpt-4.1";
  }
  if (n.includes("claude") || n.includes("opus") || n.includes("sonnet")) {
    return "gpt-4o";
  }
  if (n.includes("gpt-4o") && !n.includes("mini")) {
    return "gpt-4o";
  }
  return "gpt-4o-mini";
}

function withFunIdentity(messages) {
  const list = Array.isArray(messages) ? [...messages] : [];
  const hasSystem = list.some(m => m?.role === "system");
  if (!hasSystem) {
    list.unshift({ role: "system", content: FUN_SYSTEM });
  }
  return list;
}

export default async req => {
  if (req.method === "OPTIONS") {
    return new Response("", { status: 204, headers: corsHeaders() });
  }

  const url = new URL(req.url);
  const isModels = url.pathname.endsWith("/models");
  const isSearch = url.pathname.endsWith("/search");

  if (req.method === "GET" && isModels) {
    return json(200, {
      object: "list",
      data: [
        { id: "gpt-4o-mini", object: "model" },
        { id: "gpt-4o", object: "model" },
        { id: "gpt-4.1", object: "model" },
      ],
    });
  }

  if (req.method !== "POST") {
    return json(405, { error: { message: "Method not allowed" } });
  }

  let payload = {};
  try {
    payload = await req.json();
  } catch {
    return json(400, { error: { message: "Invalid JSON" } });
  }

  if (isSearch) {
    const query = String(payload.query || "").trim();
    const maxResults = Math.min(Math.max(Number(payload.max_results) || 10, 1), 10);
    if (!query) {
      return json(200, { results: [] });
    }
    const searchRes = await fetch(
      `${url.origin}/api/search?q=${encodeURIComponent(query)}`
    );
    const data = await searchRes.json().catch(() => ({ results: [] }));
    return json(200, {
      results: (data.results || []).slice(0, maxResults),
    });
  }

  const { base, key } = openaiBase();
  if (!base || !key) {
    return json(503, {
      error: {
        message:
          "Fun Smart is not connected. Enable Netlify AI Gateway on funsearchapp.netlify.app and redeploy.",
        type: "funsmart_unavailable",
      },
    });
  }

  const body = {
    ...payload,
    model: mapModel(payload.model),
    messages: withFunIdentity(payload.messages),
  };

  const upstream = await fetch(`${base}/chat/completions`, {
    method: "POST",
    headers: {
      authorization: `Bearer ${key}`,
      "content-type": "application/json",
    },
    body: JSON.stringify(body),
  });

  const headers = {
    ...corsHeaders(),
    "content-type": upstream.headers.get("content-type") || "application/json",
  };
  return new Response(upstream.body, { status: upstream.status, headers });
};

export const config = {
  path: ["/v1/chat/completions", "/v1/models", "/v1/search"],
};
