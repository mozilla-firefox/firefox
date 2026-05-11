const CHROME_PROCESS = E10SUtils.NOT_REMOTE;
const WEB_CONTENT_PROCESS = E10SUtils.WEB_REMOTE_TYPE;

add_task(async function () {
  let url = "javascript:dosomething()";

  ok(
    E10SUtils.canLoadURIInRemoteType(url, /* fission */ false, CHROME_PROCESS),
    "Check URL in chrome process."
  );
  ok(
    E10SUtils.canLoadURIInRemoteType(
      url,
      /* fission */ false,
      WEB_CONTENT_PROCESS
    ),
    "Check URL in web content process."
  );
});
