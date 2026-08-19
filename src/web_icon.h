#pragma once
// ════════════════════════════════════════════════════════════════════════════
// web_icon.h — the camellia this firmware serves as its site icon
//
// One definition, two forms, because there are two servers on this device and
// they cannot share a route: the web config server (port 80) answers
// /favicon.ico, and the VNC viewer is a separate raw-socket server on its own
// port, where an /favicon.ico of its own would mean adding a route to a server
// that has exactly one page. That page embeds the icon instead.
//
// SVG rather than a real .ico: ~700 bytes of flash against several KB of
// multi-resolution bitmap, it is text so it lives beside the markup it belongs
// to, and it scales to whatever size the browser asks for.
//
// Five outer petals rotated around the centre, three darker inner ones — a
// camellia is a layered bloom, and without the inner ring it reads as a daisy —
// and a gold centre. The pink is --accent from the web config palette.
// ════════════════════════════════════════════════════════════════════════════

// Raw markup, for serving with Content-Type: image/svg+xml.
#define CAMELLIA_FAVICON_SVG \
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'>" \
    "<g fill='#d7869d'>" \
    "<ellipse cx='16' cy='7.5' rx='5.6' ry='7.6'/>" \
    "<ellipse cx='16' cy='7.5' rx='5.6' ry='7.6' transform='rotate(72 16 16)'/>" \
    "<ellipse cx='16' cy='7.5' rx='5.6' ry='7.6' transform='rotate(144 16 16)'/>" \
    "<ellipse cx='16' cy='7.5' rx='5.6' ry='7.6' transform='rotate(216 16 16)'/>" \
    "<ellipse cx='16' cy='7.5' rx='5.6' ry='7.6' transform='rotate(288 16 16)'/>" \
    "</g>" \
    "<g fill='#c06a86'>" \
    "<ellipse cx='16' cy='11.5' rx='3.6' ry='4.4'/>" \
    "<ellipse cx='16' cy='11.5' rx='3.6' ry='4.4' transform='rotate(120 16 16)'/>" \
    "<ellipse cx='16' cy='11.5' rx='3.6' ry='4.4' transform='rotate(240 16 16)'/>" \
    "</g>" \
    "<circle cx='16' cy='16' r='3.2' fill='#f7c948'/>" \
    "</svg>"

// The same icon as a data: URI, for a page that has nowhere to fetch it from.
// Percent-encoded: '#' would start a fragment, and the angle brackets and spaces
// are encoded too so the value survives being pasted into an HTML attribute.
#define CAMELLIA_FAVICON_DATA_URI \
    "data:image/svg+xml," \
    "%3Csvg%20xmlns='http://www.w3.org/2000/svg'%20viewBox='0%200%2032%2032'%3E" \
    "%3Cg%20fill='%23d7869d'%3E" \
    "%3Cellipse%20cx='16'%20cy='7.5'%20rx='5.6'%20ry='7.6'/%3E" \
    "%3Cellipse%20cx='16'%20cy='7.5'%20rx='5.6'%20ry='7.6'%20transform='rotate(72%2016%2016)'/%3E" \
    "%3Cellipse%20cx='16'%20cy='7.5'%20rx='5.6'%20ry='7.6'%20transform='rotate(144%2016%2016)'/%3E" \
    "%3Cellipse%20cx='16'%20cy='7.5'%20rx='5.6'%20ry='7.6'%20transform='rotate(216%2016%2016)'/%3E" \
    "%3Cellipse%20cx='16'%20cy='7.5'%20rx='5.6'%20ry='7.6'%20transform='rotate(288%2016%2016)'/%3E" \
    "%3C/g%3E" \
    "%3Cg%20fill='%23c06a86'%3E" \
    "%3Cellipse%20cx='16'%20cy='11.5'%20rx='3.6'%20ry='4.4'/%3E" \
    "%3Cellipse%20cx='16'%20cy='11.5'%20rx='3.6'%20ry='4.4'%20transform='rotate(120%2016%2016)'/%3E" \
    "%3Cellipse%20cx='16'%20cy='11.5'%20rx='3.6'%20ry='4.4'%20transform='rotate(240%2016%2016)'/%3E" \
    "%3C/g%3E" \
    "%3Ccircle%20cx='16'%20cy='16'%20r='3.2'%20fill='%23f7c948'/%3E" \
    "%3C/svg%3E"
