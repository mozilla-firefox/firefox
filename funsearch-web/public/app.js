const PAGE_SIZE = 10;
const SESSION_KEY = "funcomputer-session";

function pageParams() {
  return new URLSearchParams(window.location.search);
}

function session() {
  try {
    return JSON.parse(localStorage.getItem(SESSION_KEY) || "null");
  } catch {
    return null;
  }
}

function showAuthStatus(message, isError) {
  const el = document.getElementById("auth-status");
  el.classList.toggle("hidden", !message);
  el.classList.toggle("error", !!isError);
  el.textContent = message || "";
}

function setMenuOpen(open) {
  const menu = document.getElementById("account-menu");
  menu.hidden = !open;
  menu.classList.toggle("hidden", !open);
  document.getElementById("sign-in-button").setAttribute("aria-expanded", String(open));
  document.getElementById("avatar-button").setAttribute("aria-expanded", String(open));
}

function renderAccount() {
  const current = session();
  const signed = !!(current && current.email);
  document.getElementById("signed-in").classList.toggle("hidden", !signed);
  document.getElementById("auth-form").classList.toggle("hidden", signed);
  document.getElementById("sign-in-button").classList.toggle("hidden", signed);
  document.getElementById("avatar-button").classList.toggle("hidden", !signed);
  if (signed) {
    const email = current.email;
    document.getElementById("signed-in-email").textContent = email;
    document.getElementById("avatar-button").textContent = email
      .trim()
      .charAt(0)
      .toUpperCase();
    document.getElementById("avatar-button").title = email;
  }
}

async function authRequest(path, body, token) {
  const response = await fetch(`/api/auth/${path}`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
    },
    body: body ? JSON.stringify(body) : undefined,
  });
  const data = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(
      data.error?.message || data.error || data.msg || "Request failed"
    );
  }
  return data;
}

function saveFromPayload(data) {
  const access = data.access_token || data.session?.access_token;
  const email = data.user?.email || data.session?.user?.email || data.email;
  if (!access || !email) {
    return false;
  }
  localStorage.setItem(
    SESSION_KEY,
    JSON.stringify({
      access_token: access,
      refresh_token: data.refresh_token || data.session?.refresh_token || "",
      email,
    })
  );
  return true;
}

function bindSearchForm(form) {
  form.addEventListener("submit", event => {
    event.preventDefault();
    const query = form.querySelector('input[type="search"]').value.trim();
    if (query) {
      window.location.href = "/?q=" + encodeURIComponent(query);
    }
  });
}

function renderHome() {
  document.body.classList.add("home-mode");
  document.getElementById("home").classList.remove("hidden");
  document.getElementById("header-search").classList.add("hidden");
  document.getElementById("q").focus();
}

function renderStatus(text) {
  const status = document.getElementById("status");
  status.hidden = false;
  status.classList.remove("hidden");
  status.textContent = text;
}

async function renderResults(query, page) {
  document.body.classList.remove("home-mode");
  document.getElementById("header-search").classList.remove("hidden");
  document.getElementById("q-header").value = query;
  const resultsEl = document.getElementById("results");
  const pager = document.getElementById("pager");
  renderStatus("Searching…");
  resultsEl.replaceChildren();
  pager.replaceChildren();

  try {
    const response = await fetch(
      `/api/search?q=${encodeURIComponent(query)}&page=${page}`
    );
    if (!response.ok) {
      throw new Error("search-failed");
    }
    const data = await response.json();
    const results = data.results || [];
    if (!results.length) {
      renderStatus(`No results found for “${query}”.`);
      return;
    }
    renderStatus(`${results.length} results`);
    for (const result of results) {
      const article = document.createElement("article");
      article.className = "result";
      const heading = document.createElement("h2");
      const link = document.createElement("a");
      link.href = result.url;
      link.textContent = result.title;
      heading.appendChild(link);
      const urlEl = document.createElement("div");
      urlEl.className = "url";
      urlEl.textContent = result.url;
      article.append(heading, urlEl);
      if (result.snippet) {
        const snippet = document.createElement("p");
        snippet.className = "snippet";
        snippet.textContent = result.snippet;
        article.appendChild(snippet);
      }
      resultsEl.appendChild(article);
    }
    resultsEl.classList.remove("hidden");
    if (page > 1) {
      const prev = document.createElement("a");
      prev.href = `/?q=${encodeURIComponent(query)}&page=${page - 1}`;
      prev.textContent = "Previous";
      pager.appendChild(prev);
    }
    if (results.length >= PAGE_SIZE) {
      const next = document.createElement("a");
      next.href = `/?q=${encodeURIComponent(query)}&page=${page + 1}`;
      next.textContent = "Next";
      pager.appendChild(next);
    }
    pager.classList.toggle("hidden", !pager.childElementCount);
  } catch {
    renderStatus("Funsearch could not load results. Check your connection and try again.");
  }
}

bindSearchForm(document.getElementById("home-search"));
bindSearchForm(document.getElementById("header-search"));

document.getElementById("sign-in-button").addEventListener("click", event => {
  event.stopPropagation();
  const menu = document.getElementById("account-menu");
  setMenuOpen(menu.hidden);
});

document.getElementById("avatar-button").addEventListener("click", event => {
  event.stopPropagation();
  const menu = document.getElementById("account-menu");
  setMenuOpen(menu.hidden);
});

document.addEventListener("click", event => {
  const slot = document.querySelector(".account-slot");
  if (!slot.contains(event.target)) {
    setMenuOpen(false);
  }
});

document.getElementById("auth-form").addEventListener("submit", async event => {
  event.preventDefault();
  showAuthStatus("");
  try {
    const data = await authRequest("login", {
      email: document.getElementById("email").value.trim(),
      password: document.getElementById("password").value,
    });
    if (!saveFromPayload(data)) {
      throw new Error("Could not sign in.");
    }
    renderAccount();
    setMenuOpen(false);
  } catch (ex) {
    showAuthStatus(ex.message, true);
  }
});

document.getElementById("signup").addEventListener("click", async () => {
  showAuthStatus("");
  try {
    const data = await authRequest("signup", {
      email: document.getElementById("email").value.trim(),
      password: document.getElementById("password").value,
    });
    if (saveFromPayload(data)) {
      renderAccount();
      setMenuOpen(false);
      return;
    }
    showAuthStatus("Account created. Confirm your email, then sign in.");
  } catch (ex) {
    showAuthStatus(ex.message, true);
  }
});

document.getElementById("header-logout").addEventListener("click", async () => {
  const current = session();
  try {
    if (current?.access_token) {
      await authRequest("logout", {}, current.access_token);
    }
  } catch {}
  localStorage.removeItem(SESSION_KEY);
  renderAccount();
  setMenuOpen(false);
});

const params = pageParams();
const query = (params.get("q") || "").trim();
document.getElementById("q").value = query;
renderAccount();
if (query) {
  document.title = `${query} · Funsearch`;
  const page = Math.max(parseInt(params.get("page") || "1", 10) || 1, 1);
  renderResults(query, page);
} else {
  renderHome();
}
