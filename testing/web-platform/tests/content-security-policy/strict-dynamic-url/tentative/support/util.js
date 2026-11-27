// How tests with variants deliver the policy: "header" or "metatag".
const test_type = location.search.substring(1);

// Returns the URL of the support document |file|, with |policy| delivered
// either in the response header (|test_type| "header", loading |file|.sub.html)
// or in a meta tag (|test_type| "metatag", loading |file|_meta.sub.html), and
// the additional query parameters |params|.
function get_support_url(test_type, file, policy, params = {}) {
  const query = new URLSearchParams(params);
  if (test_type == "header") {
    query.set("pipe", `header(Content-Security-Policy,${policy})`);
    return `support/${file}.sub.html?${query}`;
  }
  if (test_type == "metatag") {
    query.set("policy", policy);
    return `support/${file}_meta.sub.html?${query}`;
  }
  throw new Error(`Unknown test type "${test_type}"`);
}

// Loads |src| in an iframe and resolves with the first message the iframe
// posts, i.e. "SCRIPT_RAN" or "CSP_VIOLATION".
function load_frame(t, src) {
  const frame = document.createElement('iframe');
  frame.src = src;
  t.add_cleanup(() => frame.remove());
  const result = new Promise(resolve => {
    window.onmessage = e => {
      if (e.source === frame.contentWindow) {
        resolve(e.data);
      }
    };
  });
  document.body.appendChild(frame);
  return result;
}
