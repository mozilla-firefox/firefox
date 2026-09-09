/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const SESSION_FILE = "funcomputer-account.json";
const TOPIC = "funcomputer-accounts-changed";

function apiBase() {
  return Services.prefs
    .getStringPref(
      "funcomputer.accounts.apiBase",
      "https://funsearchapp.netlify.app"
    )
    .replace(/\/$/, "");
}

function sessionPath() {
  return PathUtils.join(PathUtils.profileDir, SESSION_FILE);
}

async function readSession() {
  try {
    return await IOUtils.readJSON(sessionPath());
  } catch {
    return null;
  }
}

async function writeSession(session) {
  if (!session) {
    try {
      await IOUtils.remove(sessionPath());
    } catch {}
  } else {
    await IOUtils.writeJSON(sessionPath(), session, { tmpPath: sessionPath() + ".tmp" });
  }
  Services.obs.notifyObservers(null, TOPIC);
}

function errorMessage(data, fallback) {
  return (
    data?.error?.message ||
    data?.msg ||
    data?.error_description ||
    data?.error ||
    fallback
  );
}

export const FunComputerAccounts = {
  TOPIC,

  async getSession() {
    let session = await readSession();
    if (!session?.access_token || !session?.email) {
      return null;
    }
    return session;
  },

  async signup(email, password) {
    return this.#auth("signup", { email, password });
  },

  async login(email, password) {
    return this.#auth("login", { email, password });
  },

  async logout() {
    let session = await readSession();
    try {
      if (session?.access_token) {
        await fetch(`${apiBase()}/api/auth/logout`, {
          method: "POST",
          headers: {
            "Content-Type": "application/json",
            Authorization: `Bearer ${session.access_token}`,
          },
        });
      }
    } catch {}
    await writeSession(null);
    return { ok: true };
  },

  async #auth(path, body) {
    let response;
    try {
      response = await fetch(`${apiBase()}/api/auth/${path}`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
    } catch (ex) {
      return { ok: false, error: String(ex) };
    }
    let data = {};
    try {
      data = await response.json();
    } catch {}
    if (!response.ok) {
      return { ok: false, error: errorMessage(data, "Request failed") };
    }
    let session = this.#sessionFromPayload(data);
    if (session) {
      await writeSession(session);
      return { ok: true, session, needsConfirmation: false };
    }
    if (path == "signup") {
      return {
        ok: true,
        session: null,
        needsConfirmation: true,
      };
    }
    return { ok: false, error: errorMessage(data, "Could not sign in") };
  },

  #sessionFromPayload(data) {
    let access = data?.access_token || data?.session?.access_token;
    let refresh = data?.refresh_token || data?.session?.refresh_token;
    let user = data?.user || data?.session?.user;
    let email = user?.email || data?.email;
    if (!access || !email) {
      return null;
    }
    return {
      access_token: access,
      refresh_token: refresh || "",
      email,
      user_id: user?.id || "",
    };
  },
};
