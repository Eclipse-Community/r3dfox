/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const kInChildProcess =
  Services.appinfo.processType == Services.appinfo.PROCESS_TYPE_CONTENT;

const kPrivilegedAboutPrefs = new Set([
  // NB: please leave the testing prefs at the top, and sort the rest alphabetically if you add
  // anything.
  "testing.allowed-prefs.some-bool-pref",
  "testing.allowed-prefs.some-char-pref",
  "testing.allowed-prefs.some-int-pref",

  "browser.contentblocking.report.hide_vpn_banner",
  "browser.contentblocking.report.show_mobile_app",
]);

/**
 * This set of prefs is exposed to web content processes. By default,
 * AsyncPrefs is obviously only available to privileged code, but in the
 * case of a compromised content process, we would still want to avoid it
 * being able to set security-relevant prefs. If in doubt, talk to the
 * security team before adding more prefs to this list.
 */
const kUnprivilegedExposedPrefs = new Set([
  "narrate.rate",
  "narrate.voice",

  "reader.font_size",
  "reader.font_type",
  "reader.font_weight",
  "reader.color_scheme",
  "reader.content_width",
  "reader.line_height",
  "reader.text_alignment",
  "reader.character_spacing",
  "reader.word_spacing",
  "reader.custom_colors.foreground",
  "reader.custom_colors.background",
  "reader.custom_colors.unvisited-links",
  "reader.custom_colors.visited-links",
  "reader.custom_colors.selection-highlight",
]);

const kPrefTypeMap = new Map([
  ["boolean", Services.prefs.PREF_BOOL],
  ["number", Services.prefs.PREF_INT],
  ["string", Services.prefs.PREF_STRING],
]);

function maybeReturnErrorForOperation(operation, pref, remoteType) {
  let isPrivilegedRemote =
    remoteType == "privilegedabout" || remoteType == "parent";
  let isUnprivilegedRemote =
    remoteType == "file" ||
    remoteType == "web" ||
    remoteType.startsWith("webIsolated=");
  if (!isPrivilegedRemote && !isUnprivilegedRemote) {
    return `Unknown remote type ${remoteType} when trying to ${operation} pref ${pref}.`;
  }
  if (
    isPrivilegedRemote &&
    !kPrivilegedAboutPrefs.has(pref) &&
    !kUnprivilegedExposedPrefs.has(pref)
  ) {
    return `Not allowed to ${operation} pref ${pref} from ${remoteType} process.`;
  }
  if (isUnprivilegedRemote && !kUnprivilegedExposedPrefs.has(pref)) {
    return `Not allowed to ${operation} pref ${pref} from ${remoteType} process.`;
  }
  return false;
}

function maybeReturnErrorForReset(pref, remoteType = "web") {
  return maybeReturnErrorForOperation("reset", pref, remoteType);
}

function maybeReturnErrorForSet(pref, value, remoteType = "web") {
  let error = maybeReturnErrorForOperation("set", pref, remoteType);
  if (error) {
    return error;
  }

  let valueType = typeof value;
  if (!kPrefTypeMap.has(valueType)) {
    return `Can't set pref ${pref} to value of type ${valueType}.`;
  }
  let prefType = Services.prefs.getPrefType(pref);
  if (
    prefType != Services.prefs.PREF_INVALID &&
    prefType != kPrefTypeMap.get(valueType)
  ) {
    return `Can't set pref ${pref} to a value with type ${valueType} that doesn't match the pref's type ${prefType}.`;
  }
  return false;
}

export class AsyncPrefsChild extends JSProcessActorChild {
  set(pref, value) {
    let error = maybeReturnErrorForSet(
      pref,
      value,
      Services.appinfo.remoteType
    );
    if (error) {
      return Promise.reject(error);
    }

    return this.sendQuery("AsyncPrefs:SetPref", {
      pref,
      value,
    });
  }

  reset(pref) {
    let error = maybeReturnErrorForReset(pref, Services.appinfo.remoteType);
    if (error) {
      return Promise.reject(error);
    }

    return this.sendQuery("AsyncPrefs:ResetPref", { pref });
  }
}

export var AsyncPrefs = {
  set(pref, value) {
    if (kInChildProcess) {
      return ChromeUtils.domProcessChild
        .getActor("AsyncPrefs")
        .set(pref, value);
    }
    return AsyncPrefsParent.set(pref, value, "parent");
  },

  reset(pref) {
    if (kInChildProcess) {
      return ChromeUtils.domProcessChild.getActor("AsyncPrefs").reset(pref);
    }
    return AsyncPrefsParent.reset(pref, "parent");
  },
};

const methodForType = {
  number: "setIntPref",
  boolean: "setBoolPref",
  string: "setCharPref",
};

export class AsyncPrefsParent extends JSProcessActorParent {
  static set(pref, value, remoteType) {
    let error = maybeReturnErrorForSet(pref, value, remoteType);
    if (error) {
      return Promise.reject(error);
    }
    let methodToUse = methodForType[typeof value];
    try {
      Services.prefs[methodToUse](pref, value);
    } catch (ex) {
      console.error(ex);
      return Promise.reject(ex.message);
    }

    return Promise.resolve(value);
  }

  static reset(pref, remoteType) {
    let error = maybeReturnErrorForReset(pref, remoteType);
    if (error) {
      return Promise.reject(error);
    }

    try {
      Services.prefs.clearUserPref(pref);
    } catch (ex) {
      console.error(ex);
      return Promise.reject(ex.message);
    }

    return Promise.resolve();
  }

  receiveMessage(msg) {
    if (msg.name == "AsyncPrefs:SetPref") {
      return AsyncPrefsParent.set(
        msg.data.pref,
        msg.data.value,
        this.manager.remoteType
      );
    }
    return AsyncPrefsParent.reset(msg.data.pref, this.manager.remoteType);
  }
}
