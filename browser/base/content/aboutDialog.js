/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

async function init(aEvent) {
  if (aEvent.target != document) {
    return;
  }

  var distroId = Services.prefs.getCharPref("distribution.id", "");
  if (distroId) {
    var distroAbout = Services.prefs.getStringPref("distribution.about", "");
    // If there is about text, we always show it.
    if (distroAbout) {
      var distroField = document.getElementById("distribution");
      distroField.value = distroAbout;
      distroField.style.display = "block";
    }
    // If it's not a mozilla distribution, show the rest,
    // unless about text exists, then we always show.
    if (!distroId.startsWith("mozilla-") || distroAbout) {
      var distroVersion = Services.prefs.getCharPref(
        "distribution.version",
        ""
      );
      if (distroVersion) {
        distroId += " - " + distroVersion;
      }

      var distroIdField = document.getElementById("distributionId");
      distroIdField.value = distroId;
      distroIdField.style.display = "block";
    }
  }

  // Display current version number
  let versionField = document.getElementById("versionNumber");
  versionField.innerHTML = AppConstants.MOZ_APP_VERSION_DISPLAY;

  // If pref "librewolf.aboutMenu.checkVersion" is set to true,
  // check for new version with the link given in "librewolf.aboutMenu.versionCheckGitlabUrl"
  if (Services.prefs.getBoolPref("librewolf.aboutMenu.checkVersion", false)) {
    let versionDiv = document.getElementById("version");
    const loader = document.createElement("div");
    loader.classList.add("loader");
    versionDiv.appendChild(loader);

    function isNewerVersion(newVersionString, oldVersionString) {
      let [oldVersion, oldRelease] = oldVersionString.replace(/^v/, "").split("-");
      let [newVersion, newRelease] = newVersionString.replace(/^v/, "").split("-");
      console.log(oldVersionString, newVersionString)
      if (oldVersion && newVersion) {
        if (!oldRelease) oldRelease = "0";
        if (!newRelease) newRelease = "0";

        // Check version
        for (let i = 0; i < newVersion.split(".").length; i++) {
          if (Number(newVersion.split(".")[i]) > Number(oldVersion?.split(".")[i] || "0")) return true;
        }

        // Check release
        if (Number(newRelease) > Number(oldRelease)) return true;
      }
      return false;
    }

    fetch(
      Services.prefs.getStringPref(
        "librewolf.aboutMenu.versionCheckGitlabUrl",
        "https://codeberg.org/api/v1/repos/librewolf/source/releases"
      )
    )
      .then(response => response.json())
      .then(data => {
        if (data.length > 0) {
          const latestVersion = data[0].tag_name;
          if (isNewerVersion(latestVersion, AppConstants.MOZ_APP_VERSION_DISPLAY)) {
            const updateNotice = document.createElement("a");
            updateNotice.classList.add("text-link");
            updateNotice.href = data[0]._links.self;
            updateNotice.onclick = () => window.openWebLinkIn(data[0]._links.self, "tab")
            updateNotice.innerText = "(Update available)";
            versionDiv.appendChild(updateNotice);
          } else {
            const upToDateNotice = document.createElement("div")
            upToDateNotice.innerText = "(Up to date)";
            versionDiv.appendChild(upToDateNotice);
          }
        }
        loader.remove();
      })
  }

  window.sizeToContent();

  if (AppConstants.platform == "macosx") {
    window.moveTo(
      screen.availWidth / 2 - window.outerWidth / 2,
      screen.availHeight / 5
    );
  }
}
