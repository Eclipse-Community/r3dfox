const DATA = {
  pdf: {
    mimetype: "application/pdf",
    content:
      "%PDF-1.\ntrailer<</Root<</Pages<</Kids[<</MediaBox[0 0 3 3]>>]>>>>>>",
  },
  text: {
    mimetype: "text/plain",
    content: "hello world",
  },
};

function handleRequest(request, response) {
  response.setHeader("Cache-Control", "no-cache", false);
  response.setHeader(
    "Link",
    "<data:text/css,body{background:red%20!important;}>; rel=stylesheet",
    false
  );
  response.setStatusLine(request.httpVersion, "200", "Found");
  const { mimetype, content } = DATA[request.queryString];
  response.setHeader("Content-Type", mimetype, false);
  response.write(content);
}
