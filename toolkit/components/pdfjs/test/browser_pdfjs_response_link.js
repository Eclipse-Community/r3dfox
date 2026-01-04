/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const RELATIVE_DIR = "toolkit/components/pdfjs/test/";
const TESTROOT = "https://example.com/browser/" + RELATIVE_DIR;

function getBodyBackgroundColor(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    return content.getComputedStyle(content.document.querySelector("body"))
      .backgroundColor;
  });
}

// Sanity check: the pdf test does not trivially pass due to the lack of support
// for Link header.
add_task(async function test_plain_text_with_link_in_response() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: `${TESTROOT}pdf_response_link.sjs?text` },
    async function (browser) {
      const bodyBackgroundColor = await getBodyBackgroundColor(browser);
      Assert.equal(
        bodyBackgroundColor,
        "rgb(255, 0, 0)",
        "Body background is red"
      );
    }
  );
});

add_task(async function test_pdf_with_link_in_response() {
  makePDFJSHandler();

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async function (browser) {
      await waitForPdfJSCanvas(browser, `${TESTROOT}pdf_response_link.sjs?pdf`);
      const bodyBackgroundColor = await getBodyBackgroundColor(browser);
      Assert.notEqual(
        bodyBackgroundColor,
        "rgb(255, 0, 0)",
        "Body background is not red"
      );
      await waitForPdfJSClose(browser);
    }
  );
});
