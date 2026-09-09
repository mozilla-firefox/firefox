import {
  corsHeaders,
  json,
  supabaseAuth,
} from "./_shared/supabase-auth.mjs";

export default async req => {
  if (req.method === "OPTIONS") {
    return new Response("", { status: 204, headers: corsHeaders() });
  }

  const url = new URL(req.url);
  const action = url.pathname.split("/").pop();
  const authHeader = req.headers.get("authorization") || "";
  const accessToken = authHeader.startsWith("Bearer ")
    ? authHeader.slice(7)
    : "";

  if (action === "user" && req.method === "GET") {
    const result = await supabaseAuth("user", { accessToken });
    return json(result.status, result.data);
  }

  if (req.method !== "POST") {
    return json(405, { error: "Method not allowed" });
  }

  if (action === "logout") {
    const result = await supabaseAuth("logout", {
      method: "POST",
      accessToken,
    });
    return json(result.status, result.data || { ok: true });
  }

  let payload = {};
  try {
    payload = await req.json();
  } catch {
    return json(400, { error: "Invalid JSON" });
  }
  const email = String(payload.email || "").trim();
  const password = String(payload.password || "");
  if (!email || !password) {
    return json(400, { error: "Email and password are required." });
  }
  if (password.length < 6) {
    return json(400, { error: "Password must be at least 6 characters." });
  }

  if (action === "signup") {
    const result = await supabaseAuth("signup", {
      method: "POST",
      body: { email, password },
    });
    return json(result.status, result.data);
  }

  if (action === "login") {
    const result = await supabaseAuth("token?grant_type=password", {
      method: "POST",
      body: { email, password },
    });
    return json(result.status, result.data);
  }

  return json(404, { error: "Not found" });
};

export const config = {
  path: ["/api/auth/signup", "/api/auth/login", "/api/auth/logout", "/api/auth/user"],
};
