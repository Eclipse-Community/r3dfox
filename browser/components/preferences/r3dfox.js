/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from preferences.js */

var gR3dfoxPane = {
  init() {},
};

Preferences.addAll([
  { id: "widget.override-win-version", type: "int" },
  { id: "r3dfox.colors", type: "int" },
  { id: "r3dfox.customizations.enabled", type: "bool" },
  { id: "r3dfox.drag-space.enabled", type: "bool" },
  { id: "r3dfox.backgrounds.enabled", type: "bool" },
  { id: "r3dfox.transparent.menubar", type: "bool" },
  { id: "r3dfox.translucent.navbar", type: "bool" },
  { id: "r3dfox.aero.fog", type: "int" },
  { id: "r3dfox.caption.text.color", type: "int" },
  { id: "browser.urlbar.oneOffsInstant", type: "bool" },
  { id: "browser.menu.viewImage", type: "bool" },
  { id: "browser.menu.navigationIcons", type: "bool",  inverted: true },
  { id: "security.csp.enable", type: "bool",  inverted: true },
  { id: "security.port.blocking.enabled", type: "bool",  inverted: true },
  { id: "network.stricttransportsecurity.enabled", type: "bool",  inverted: true },
  { id: "content.cors.disable", type: "bool" },
  { id: "content.cors.bypass_preflight_request", type: "bool" },
  { id: "security.same_origin_policy.enabled", type: "bool",  inverted: true },
  { id: "browser.urlbar.secondaryActions.switchToTab", type: "bool" },
  { id: "screenshots.browser.component.enabled", type: "bool" },
  { id: "widget.override-aero-caption-buttons-mask-height", type: "int" },
  { id: "widget.override-aero-caption-buttons-mask-width", type: "int" },
  { id: "browser.display.windows.non_native_menus", type: "int" },
  { id: "general.useragent.stealth", type: "bool" },
  { id: "general.useragent.site_specific_overrides", type: "bool",  inverted: true },
  { id: "dom.webaudio.enabled", type: "bool",  inverted: true },
  { id: "browser.tabs.dropToPin.enabled", type: "bool",  inverted: true },
  { id: "browser.taskbarTabs.enabled", type: "bool",  inverted: true },
//  { id: "browser.urlbar.scotchBonnet.enableOverride", type: "bool",  inverted: true },
  { id: "browser.urlbar.formatting.enabled", type: "bool",  inverted: true },
  { id: "geo.enabled", type: "bool",  inverted: true },
  { id: "browser.ui.oldaboutconfig", type: "bool" },
  { id: "prompts.tab_modal.enabled", type: "bool",  inverted: true },
  { id: "prompts.headerAppIcon.enabled", type: "bool" },
  { id: "prompts.tab_modal.sound.enabled", type: "bool" },
  { id: "browser.tabs.unloadOnLowMemory", type: "bool" },
  { id: "browser.low_physical_memory_threshold_mb", type: "int" },
  { id: "browser.memory_poll_interval_ms", type: "int" },
  { id: "browser.tabs.unloadActiveForegroundTab", type: "bool" },
]);
