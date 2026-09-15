"use strict";

// Need profile so that the protocol handler can resolve the path to the underlying file
do_get_profile();

function run_test() {
  // Check the protocol handler implements the correct interfaces
  let handler = Services.io.getProtocolHandler("moz-newtab-wallpaper");
  ok(
    handler instanceof Ci.nsIProtocolHandler,
    "moz-newtab-wallpaper handler provides nsIProtocolHandler interface"
  );
  ok(
    handler instanceof Ci.nsISubstitutingProtocolHandler,
    "moz-newtab-wallpaper handler provides nsISubstitutingProtocolHandler interface"
  );

  // Create a dummy loadinfo which we can hand to newChannel
  let dummyURI = Services.io.newURI("https://www.example.com/");
  let dummyChannel = NetUtil.newChannel({
    uri: dummyURI,
    loadUsingSystemPrincipal: true,
  });
  let dummyLoadInfo = dummyChannel.loadInfo;

  // Test that empty host fails
  let emptyHost = Services.io.newURI("moz-newtab-wallpaper://");
  Assert.throws(
    () => handler.newChannel(emptyHost, dummyLoadInfo),
    /NS_ERROR/i,
    "moz-newtab-wallpaper URI with empty host must not resolve"
  );

  // Test that valid host creates a channel (even if file doesn't exist yet)
  let validURI = Services.io.newURI("moz-newtab-wallpaper://wallpaper.jpg");
  let channel = handler.newChannel(validURI, dummyLoadInfo);
  ok(channel, "moz-newtab-wallpaper URI with valid host creates a channel");
}
