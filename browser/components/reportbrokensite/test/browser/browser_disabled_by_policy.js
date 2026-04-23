/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* Test that the Report Broken Site menu items are disabled
 * when the active tab is not on a reportable URL, and is hidden
 * when the feature is disabled via pref. Also ensure that the
 * Report Broken Site item that is automatically generated in
 * the app menu's help sub-menu is hidden.
 */

"use strict";

add_common_setup();

const { EnterprisePolicyTesting, PoliciesPrefTracker } =
  ChromeUtils.importESModule(
    "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
  );

add_task(async function testDisabledByPolicy() {
  ensureReportBrokenSitePreffedOn();

  async function ensure(menu, fn, test) {
    // the hidden/disabled state of all of the menuitems may not update until one
    // is rendered; then the related <command>'s state is propagated to them all.
    await menu.open();
    await menu.close();

    await menu.open();
    fn(menu.reportBrokenSite, `${fn.name}(${menu.menuDescription}) ${test} - `);
    await menu.close();
  }

  PoliciesPrefTracker.start();
  await EnterprisePolicyTesting.setupPolicyEngineWithJson({
    policies: {
      DisableFeedbackCommands: true,
    },
  });

  const appMenu = AppMenu();
  const helpMenu = HelpMenu();
  const protectionsPanel = ProtectionsPanel();

  await BrowserTestUtils.withNewTab(REPORTABLE_PAGE_URL, async function () {
    ReportBrokenSite.enableOrDisableMenuitems(window);
    const test = "when disabled by DisableFeedbackCommands enterprise policy";
    await ensure(appMenu, isMenuItemHidden, test);
    await ensure(helpMenu, isMenuItemHidden, test);
    await ensure(protectionsPanel, isMenuItemHidden, test);
  });

  PoliciesPrefTracker.stop();
  await EnterprisePolicyTesting.setupPolicyEngineWithJson("");
});
