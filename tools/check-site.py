#!/usr/bin/env python3
"""Check the product site's stitched page for the mistakes a browser won't flag.

    ./tools/check-site.py [path/to/index.html]

Defaults to web/index.html; pass a temp stitched copy while sections are still
being built by separate agents. Checks, each printed as it runs:

  a. every local href/src/srcset points at a file that exists under web/
  b. public repository and research links are present
  c. the 22 register strings each appear exactly once as an <li>'s text
  d. at least one mailto: link exists, every mailto: is the same address, and
     that address is on bankoti.dev rather than a personal mailbox
  e. no em-dash (U+2014) in visible text (repo rule: comma, colon, full stop)
  f. every <img> has a non-empty alt, or alt="" inside an aria-hidden ancestor
  g. <link rel="canonical"> is https://bankoti.dev/Orion/
  h. no personal webmail address (gmail, icloud, outlook, ...) in the page, CSS or JS
  i. no image under web/ (or inlined in its JS) carries EXIF, XMP or IPTC metadata
  j. no home-directory path (/Users/<name>/, /home/<name>/) in the page, CSS, JS or SVG
  k. every local file the page references is tracked by git, so the deploy has it
  l. download actions and metadata use GitHub Releases, including pre-releases

This is a parse over one HTML file, not a browser: it does not fetch remote
URLs, run scripts, or check that srcset candidates are the right size. It
exists because those are exactly the mistakes that look fine on screen and
ship anyway.
"""
import base64
import html.parser
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WEB = REPO / "web"
SOURCE_URL = "https://github.com/Nano-AI/Orion"
RELEASES_URL = SOURCE_URL + "/releases"

REGISTER_STRINGS = [
    "Exposure, contrast, highlights, shadows, whites, blacks",
    "White balance, color grading, tone curves",
    "Film looks from any .cube file",
    "One-click auto-enhance",
    "Haze removal, clarity, shadow recovery",
    "Noise reduction, sharpening, color mixer",
    "Spot and blemish removal",
    "Film grain, and a vignette you can dial in",
    "Crop, straighten, rotate",
    "Distortion and vignetting for 1,558 lenses",
    "Perspective and keystone correction",
    "Gradient, radial, brush, brightness and color masks",
    "Selecting a subject, a person or a sky",
    "Masks combined (add, subtract, intersect) and feathered to real edges",
    "Browsing a folder of 300 frames without waiting on it",
    "Saved versions of a photograph, to come back to later",
    "Presets, and copying settings between photos",
    "Export at 8 or 16 bits, resized, sharpened for screen or print",
    "Export with your location stripped by default",
    "Highlight reconstruction: the region fill runs and has a slider, but the "
    "detail transfer and the edge falloff are not built",
    "Fujifilm X-Trans sensors",
    "Windows",
]

VOID = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "source", "track", "wbr",
}


class PageParser(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.errors = []
        self.local_urls = []       # (attr, value)
        self.mailtos = []
        self.links = []
        self.canonical = None
        self.aria_hidden_depth = 0
        self.stack = []            # (tag, whether this tag opened aria-hidden)
        self.imgs = []             # (attrs, hidden)
        self.li_stack = []         # list of text buffers, one per open <li>
        self.li_texts = []         # completed <li> visible text
        self.text_chunks = []      # whole-page visible text, for em-dash check

    def _url_attrs(self, tag, attrs):
        d = dict(attrs)
        for attr in ("href", "src"):
            if attr in d and d[attr] is not None:
                yield attr, d[attr]
        if "srcset" in d and d["srcset"]:
            for candidate in d["srcset"].split(","):
                url = candidate.strip().split()[0] if candidate.strip() else ""
                if url:
                    yield "srcset", url

    def handle_starttag(self, tag, attrs):
        d = dict(attrs)
        opens_hidden = d.get("aria-hidden") == "true"
        hidden = self.aria_hidden_depth > 0 or opens_hidden
        if tag not in VOID:
            self.stack.append((tag, opens_hidden))
            if opens_hidden:
                self.aria_hidden_depth += 1

        if tag == "img":
            self.imgs.append((d, hidden))
        if tag == "a":
            self.links.append(d)

        if tag == "link" and d.get("rel") == "canonical":
            self.canonical = d.get("href")

        for attr, url in self._url_attrs(tag, attrs):
            if url.startswith(("http://", "https://", "mailto:", "#", "data:", "//")):
                if url.startswith("mailto:"):
                    self.mailtos.append(url)
                continue
            self.local_urls.append((attr, url))

        if tag == "li":
            self.li_stack.append([])

    def handle_startendtag(self, tag, attrs):
        # self-closing form, e.g. <img ... />
        self.handle_starttag(tag, attrs)

    def handle_endtag(self, tag):
        if tag == "li" and self.li_stack:
            self.li_texts.append("".join(self.li_stack.pop()).strip())
        if self.stack and self.stack[-1][0] == tag:
            _, opened_hidden = self.stack.pop()
            if opened_hidden:
                self.aria_hidden_depth -= 1

    def handle_data(self, data):
        for buf in self.li_stack:
            buf.append(data)
        self.text_chunks.append(data)


def check(condition, ok_msg, fail_msg):
    print(("  ok  " if condition else " FAIL ") + (ok_msg if condition else fail_msg))
    return condition


def main():
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else WEB / "index.html"
    src = target.read_text(encoding="utf-8")

    p = PageParser()
    p.feed(src)

    ok = True

    # (a) local URLs resolve under web/. Resolved against WEB, not the target
    # file's own directory: the page's paths ("css/base.css", "img/...") are
    # always relative to web/, even when the target is a stitched temp copy
    # built somewhere else for pre-integration testing.
    missing = []
    for attr, url in p.local_urls:
        clean = url.split("#")[0].split("?")[0]
        if not clean:
            continue
        candidate = (WEB / clean).resolve()
        if not candidate.exists():
            missing.append(f"{attr}={url!r}")
    ok &= check(not missing, f"(a) {len(p.local_urls)} local urls all exist under web/",
                f"(a) missing local files: {missing}")

    # (b) the repository is public again (#283).
    scan_files = [target] + sorted(WEB.glob("js/*.js")) + sorted(WEB.glob("css/*.css"))
    hrefs = {link.get("href") for link in p.links}
    ok &= check({SOURCE_URL, SOURCE_URL + "/tree/main/research"} <= hrefs,
                 "(b) public source and research links are present",
                 "(b) missing public source or research link")

    # (c) the 22 register strings, each exactly once as an <li>'s text
    from collections import Counter
    counts = Counter(p.li_texts)
    bad = []
    for s in REGISTER_STRINGS:
        n = counts.get(s, 0)
        if n != 1:
            bad.append(f"{n}x: {s!r}")
    ok &= check(not bad, "(c) all 22 register strings appear exactly once as an <li>",
                "(c) register string problems:\n      " + "\n      ".join(bad))

    # (d) mailto: links exist and all match
    ok &= check(bool(p.mailtos), "(d) at least one mailto: link exists",
                "(d) no mailto: link found")
    distinct = set(p.mailtos)
    ok &= check(len(distinct) <= 1, "(d) every mailto: link is the same address",
                f"(d) mailto: links disagree: {distinct}")
    addrs = {m[len("mailto:"):].split("?")[0].lower() for m in p.mailtos}
    ok &= check(all(a.endswith("@bankoti.dev") for a in addrs),
                "(d) the mailto: address is on bankoti.dev",
                f"(d) {len(addrs)} mailto: address(es) not on bankoti.dev")

    # (e) no em-dash in visible text
    text = "".join(p.text_chunks)
    ok &= check("—" not in text, "(e) no em-dash (U+2014) in visible text",
                "(e) found an em-dash in visible text")

    # (f) every <img> has alt, or alt="" inside an aria-hidden ancestor
    bad_imgs = []
    for d, hidden in p.imgs:
        alt = d.get("alt")
        if alt is None:
            bad_imgs.append(d.get("src", "<no src>"))
        elif alt == "" and not hidden:
            bad_imgs.append(d.get("src", "<no src>") + " (empty alt, not aria-hidden)")
    ok &= check(not bad_imgs, f"(f) all {len(p.imgs)} <img> tags have a valid alt",
                f"(f) bad alt on: {bad_imgs}")

    # (g) canonical link
    ok &= check(p.canonical == "https://bankoti.dev/Orion/",
                "(g) canonical link is https://bankoti.dev/Orion/",
                f"(g) canonical link is {p.canonical!r}")

    # (h) no personal mailbox anywhere a visitor can read. The developer's own
    # address sat on this page for a session before anyone asked where it came
    # from (#257). The count is printed, never the address.
    webmail = re.compile(r"[\w.+-]+@(gmail|googlemail|icloud|me|mac|outlook|hotmail|live|"
                         r"yahoo|proton|protonmail|pm|aol)\.[a-z.]+", re.I)
    hits = {m.group(0).lower() for f in scan_files if f.exists()
            for m in webmail.finditer(f.read_text(encoding="utf-8"))}
    ok &= check(not hits, "(h) no personal webmail address in the page, CSS or JS",
                f"(h) {len(hits)} personal email address(es) found")

    # (i) no photograph carries a camera's metadata. EXIF holds the body and lens
    # serial numbers and GPS, XMP and IPTC can hold names and places; a render
    # exported with its metadata kept would publish all of it (#266). Byte
    # markers per format, so no image library is needed.
    markers = {
        ".jpg": [(b"Exif\x00\x00", "EXIF"), (b"ns.adobe.com/xap/1.0/", "XMP"), (b"Photoshop 3.0\x00", "IPTC")],
        ".png": [(b"eXIf", "EXIF"), (b"XML:com.adobe.xmp", "XMP")],
    }
    markers[".jpeg"] = markers[".jpg"]
    blobs = [(f.name, f.suffix.lower(), f.read_bytes()) for f in sorted(WEB.rglob("*"))
             if f.suffix.lower() in markers]
    for js in sorted(WEB.rglob("*.js")):
        for kind, b64 in re.findall(r"data:image/(jpeg|png);base64,([A-Za-z0-9+/=]+)", js.read_text(encoding="utf-8")):
            blobs.append((f"{js.name} ({kind})", "." + kind, base64.b64decode(b64)))
    tagged = [f"{name} ({label})" for name, suffix, data in blobs
              for marker, label in markers[suffix] if marker in data]
    ok &= check(not tagged, f"(i) none of {len(blobs)} images carries EXIF, XMP or IPTC metadata",
                f"(i) image metadata found: {tagged}")

    # (j) no home directory: an absolute path off the build machine names its user
    home = re.compile(r"/(Users|home)/[\w.-]+/")
    text_files = [target] + sorted(WEB.rglob("*.js")) + sorted(WEB.rglob("*.css")) + sorted(WEB.rglob("*.svg"))
    homes = sorted({f.name for f in text_files if f.exists()
                    and home.search(f.read_text(encoding="utf-8", errors="ignore"))})
    ok &= check(not homes, "(j) no home-directory path in the page, CSS, JS or SVG",
                f"(j) home-directory path in: {homes}")

    # (k) every local file the page references is tracked by git. Pages deploys a
    # checkout, so a file that exists here but is untracked, or ignored like the
    # repository-wide vendor/ rule that kept GSAP, ScrollTrigger and Lenis out of
    # the first deploy (#267), 404s live while the local preview works.
    import subprocess
    tracked = set(subprocess.run(["git", "ls-files", "-z", "web"], cwd=REPO,
                                 capture_output=True, text=True).stdout.split("\0"))
    untracked = sorted({url for _, url in p.local_urls
                        if (clean := url.split("#")[0].split("?")[0])
                        and (WEB / clean).exists()
                        and str((WEB / clean).resolve().relative_to(REPO)) not in tracked})
    ok &= check(not untracked, "(k) every local file the page references is tracked by git",
                f"(k) referenced but not tracked, so missing once deployed: {untracked}")

    # (l) /latest skips pre-releases and currently selects an older build.
    # Keep visitors on the release list, with notes and installation guidance.
    ctas = [link for link in p.links
            if "btn" in link.get("class", "").split()
            and "btn--ghost" not in link.get("class", "").split()]
    metadata = [json.loads(block) for block in re.findall(
        r'<script type="application/ld\+json">(.*?)</script>', src, re.S)]
    workflow = (REPO / ".github" / "workflows" / "pages.yml").read_text(encoding="utf-8")
    ok &= check(len(ctas) == 3 and all(link.get("href") == RELEASES_URL for link in ctas)
                and any(item.get("downloadUrl") == RELEASES_URL for item in metadata)
                and RELEASES_URL in hrefs
                and not any(word in src for word in ("/releases/latest", ".dmg", "download/Orion", "Public download coming later"))
                and "gh release download" not in workflow
                and not list(WEB.rglob("*.dmg")),
                 "(l) all three download buttons and metadata use GitHub Releases; no mirrored installer",
                 "(l) download buttons/metadata must use the release list, without pinned or mirrored installers")

    if not ok:
        print("\ncheck-site.py: FAILED")
        sys.exit(1)
    print("\ncheck-site.py: all checks passed")


if __name__ == "__main__":
    main()
