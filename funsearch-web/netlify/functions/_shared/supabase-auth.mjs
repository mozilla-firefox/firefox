function config() {
  const url = (
    globalThis.Netlify?.env?.get("SUPABASE_URL") ||
    process.env.SUPABASE_URL ||
    ""
  ).replace(/\/$/, "");
  const anon =
    globalThis.Netlify?.env?.get("SUPABASE_ANON_KEY") ||
    process.env.SUPABASE_ANON_KEY ||
    "";
  return { url, anon };
}

export function corsHeaders() {
  return {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Headers": "content-type, authorization",
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
  };
}

export function json(status, body) {
  return Response.json(body, { status, headers: corsHeaders() });
}

export async function supabaseAuth(path, { method = "GET", body, accessToken } = {}) {
  const { url, anon } = config();
  if (!url || !anon) {
    return {
      ok: false,
      status: 503,
      data: {
        error:
          "Set SUPABASE_URL and SUPABASE_ANON_KEY in the Netlify site environment.",
      },
    };
  }
  const response = await fetch(`${url}/auth/v1/${path}`, {
    method,
    headers: {
      apikey: anon,
      Authorization: `Bearer ${accessToken || anon}`,
      "Content-Type": "application/json",
    },
    body: body ? JSON.stringify(body) : undefined,
  });
  let data = {};
  try {
    data = await response.json();
  } catch {}
  return { ok: response.ok, status: response.status, data };
}
