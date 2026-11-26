/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Maps each hostname in a newline-separated file to its base domain via
 * Services.eTLD.getBaseDomainFromHost, which is what Firefox uses when
 * checking a URL against the list. Hosts that are rewritten are logged.
 * Unresolvable hosts are logged and skipped.
 *
 * Usage, from the root of a Firefox checkout (paths must be absolute):
 *   macOS:
 *     B=obj-default/dist/Nightly.app/Contents
 *     $B/MacOS/xpcshell -g $B/Resources -a $B/Resources/browser /abs/map-base-domains.js /abs/in.txt /abs/out.txt
 *   Linux/Windows (-g/-a are usually unnecessary; if needed, point both at dist/bin):
 *     obj-default/dist/bin/xpcshell /abs/map-base-domains.js /abs/in.txt /abs/out.txt
 */
const [inPath, outPath] = arguments;
let done = false;
(async () => {
  const lines = (await IOUtils.readUTF8(inPath)).split("\n").filter(Boolean);
  const out = [];
  for (const d of lines) {
    const host = d.trim();
    try {
      const base = Services.eTLD.getBaseDomainFromHost(host);
      if (base !== host.toLowerCase()) {
        dump(`Rewrote ${host} -> ${base}\n`);
      }
      out.push(base);
    } catch (e) {
      dump(`Skipping ${d}: ${e.name}\n`);
    }
  }
  await IOUtils.writeUTF8(outPath, out.join("\n") + "\n");
  dump(`Wrote ${out.length} base domains to ${outPath}\n`);
})()
  .catch(e => dump(e + "\n"))
  .finally(() => (done = true));
Services.tm.spinEventLoopUntil("map-base-domains", () => done);
