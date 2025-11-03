/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Verify that menu popups are not populated when doing so would create a
 * recursive hierarchy. Some consumers may walk menu trees and could enter an
 * infinite loop if recursion were allowed.
 */

"use strict";

async function openMenuAndWaitForPopup(menu) {
  let popup = menu.menupopup;
  let popupShown = BrowserTestUtils.waitForEvent(popup, "popupshown");
  // This seems necessary, at least for the toolbar.
  EventUtils.synthesizeNativeMouseEvent({
    type: "mousemove",
    target: menu,
    atCenter: true,
  });
  menu.open = true;
  await popupShown;
  return popup;
}

function findNode(guid, view) {
  for (let node of view.childNodes) {
    console.log("visiting node", node, node._placesNode?.bookmarkGuid);
    if (node._placesNode?.bookmarkGuid == guid) {
      return node;
    }
  }
  return null;
}

function fakeOpenMenu(popup) {
  popup.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
  popup.dispatchEvent(new MouseEvent("popupshown", { bubbles: true }));
}

function findPlacesNode(guid, container) {
  for (let i = 0; i < container.childCount; i++) {
    let node = container.getChild(i);
    if (node.bookmarkGuid == guid) {
      return node;
    }
  }
  return null;
}

add_task(async function test() {
  // Make sure the bookmarks bar is visible and restore its state on cleanup.
  let toolbar = document.getElementById("PersonalToolbar");
  ok(toolbar, "PersonalToolbar should not be null");
  if (toolbar.collapsed) {
    await promiseSetToolbarVisibility(toolbar, true);
    registerCleanupFunction(function () {
      return promiseSetToolbarVisibility(toolbar, false);
    });
  }

  let menubar = document.getElementById("toolbar-menubar");
  // Force the menu to be shown.
  const kAutohide = menubar.getAttribute("autohide");
  menubar.removeAttribute("autohide");
  registerCleanupFunction(function () {
    menubar.setAttribute("autohide", kAutohide);
  });

  registerCleanupFunction(PlacesUtils.bookmarks.eraseEverything);

  // Create a folder structure with a recursive shortcut.
  let folderA = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Folder A",
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
  });
  let selfShortcut = await PlacesUtils.bookmarks.insert({
    parentGuid: folderA.guid,
    title: "Shortcut to A",
    url: `place:parent=${folderA.guid}`,
  });
  let toolbarShortcut = await PlacesUtils.bookmarks.insert({
    parentGuid: folderA.guid,
    title: "Shortcut to toolbar",
    url: `place:parent=${PlacesUtils.bookmarks.toolbarGuid}`,
  });

  // This also ensures that toolbar content is updated.
  Assert.ok(!(await PlacesToolbarHelper.getIsEmpty()), "Toolbar is not empty");

  // Open the popup for the shortcut from the bookmarks toolbar.
  let toolbarItems = document.getElementById("PlacesToolbarItems");
  let folderANode = findNode(folderA.guid, toolbarItems);
  let folderAPopup = await openMenuAndWaitForPopup(folderANode);
  let selfShortcutNode = findNode(selfShortcut.guid, folderAPopup);
  let selfShortcutPopup = await openMenuAndWaitForPopup(selfShortcutNode);

  Assert.ok(
    !selfShortcutPopup._placesNode.containerOpen,
    "Self shortcut container is not open"
  );
  Assert.ok(
    selfShortcutPopup.hasAttribute("emptyplacesresult"),
    "Self shortcut popup is empty"
  );

  let toolbarShortcutNode = findNode(
    toolbarShortcut.guid,
    folderANode.menupopup
  );
  let toolbarShortcutPopup = await openMenuAndWaitForPopup(toolbarShortcutNode);

  Assert.ok(
    !toolbarShortcutPopup._placesNode.containerOpen,
    "Toolbar shortcut container is not open"
  );
  Assert.ok(
    toolbarShortcutPopup.hasAttribute("emptyplacesresult"),
    "Toolbar shortcut popup is empty"
  );

  // Also insert a toolbar shortcut in the bookmarks menu and check the
  // previously inserted recursive toolbar shortcut there.
  info("Test native bookmarks menu");
  let shortcutInMenu = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.menuGuid,
    title: "Shortcut to menu",
    url: `place:parent=${PlacesUtils.bookmarks.menuGuid}`,
  });

  if (Services.appinfo.nativeMenubar) {
    // With native menubar we can only simulate popupshowing as we cannot
    // directly open menus.
    let bookmarksMenuPopup = document.getElementById("bookmarksMenuPopup");
    fakeOpenMenu(bookmarksMenuPopup);
    let shortcutInMenuNode = findNode(shortcutInMenu.guid, bookmarksMenuPopup);
    fakeOpenMenu(shortcutInMenuNode.menupopup);
    Assert.ok(
      !shortcutInMenuNode.menupopup._placesNode.containerOpen,
      "menu shortcut container is not open"
    );
  } else {
    let bookmarksMenu = document.getElementById("bookmarksMenu");
    let bookmarksPopup = await openMenuAndWaitForPopup(bookmarksMenu);
    let shortcutInMenuNode = findNode(shortcutInMenu.guid, bookmarksPopup);
    let shortcutInMenuPopup = await openMenuAndWaitForPopup(
      shortcutInMenuNode,
      true
    );
    Assert.ok(
      !shortcutInMenuPopup._placesNode.containerOpen,
      "Toolbar shortcut container is not open"
    );
    Assert.ok(
      shortcutInMenuPopup.hasAttribute("emptyplacesresult"),
      "Toolbar shortcut popup is empty"
    );
  }
});
