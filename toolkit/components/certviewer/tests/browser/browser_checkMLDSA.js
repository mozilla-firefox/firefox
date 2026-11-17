/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

add_task(async function test_mldsa() {
  let pem = await (
    await fetch(getRootDirectory(gTestPath) + "mldsa44.pem")
  ).text();
  let base64 = pem.replace(/-----[^-]+-----|\s/g, "");
  let url = `about:certificate?cert=${encodeURIComponent(base64)}`;

  await BrowserTestUtils.withNewTab(url, async function (browser) {
    await SpecialPowers.spawn(browser, [], async function () {
      let certificateSection = await ContentTaskUtils.waitForCondition(() => {
        return content.document.querySelector("certificate-section");
      }, "Certificate section found");

      function getInfo(sectionClass, itemClass) {
        let section = certificateSection.shadowRoot.querySelector(
          `.${sectionClass}`
        );
        let item = section.shadowRoot.querySelector(`.${itemClass}`);
        return item.shadowRoot.querySelector(".info");
      }

      Assert.equal(
        getInfo("public-key-info", "algorithm").textContent,
        "ML-DSA-44",
        "The key algorithm should be ML-DSA-44"
      );
      let publicValue = getInfo("public-key-info", "public-value");
      Assert.equal(
        publicValue.textContent.split(":").length,
        1312,
        "The public value should be the 1312 byte ML-DSA-44 public key"
      );
      await ContentTaskUtils.waitForCondition(
        () => publicValue.classList.contains("long-hex"),
        "The public value should be collapsed"
      );
      Assert.equal(
        getInfo("miscellaneous", "signature-algorithm").textContent,
        "ML-DSA-44",
        "The signature algorithm should be ML-DSA-44"
      );
    });
  });
});
