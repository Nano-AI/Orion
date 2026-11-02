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
  l. the download buttons and metadata go to the download page; no pinned or mirrored installer
  m. the download page: its files exist and are tracked, canonical, alt text and
     no em-dash, its buttons work without scripts, and its script reads the
     whole release list
  n. sitemap.xml parses and lists exactly the canonical URL of every index.html
  o. each page's JSON-LD parses, one block's url is the page's canonical and every
     block's url is a page's canonical; the home page's featureList is all register strings
  p. titles unique and at most 60 characters, descriptions unique and at most 160
  q. 404.html exists, names only tracked files from /Orion/, carries noindex,
     has no canonical and no em-dash

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
DOWNLOAD_URL = "https://bankoti.dev/Orion/download/"

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
    "Intel Macs",
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
        self.meta = {}             # <meta name=...> content
        self.title = []            # <title> text
        self.in_title = False

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

        if tag == "meta" and d.get("name"):
            self.meta[d["name"]] = d.get("content", "")
        self.in_title = tag == "title"
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
        if self.in_title:
            self.title.append(data)
            self.in_title = False
            return
        for buf in self.li_stack:
            buf.append(data)
        self.text_chunks.append(data)


def local_file(base, url):
    """The file a local href/src names: a folder means its index.html."""
    path = (base / url.split("#")[0].split("?")[0]).resolve()
    return path / "index.html" if path.is_dir() else path


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
    missing = [f"{attr}={url!r}" for attr, url in p.local_urls
               if url.split("#")[0].split("?")[0] and not local_file(WEB, url).exists()]
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
                        if url.split("#")[0].split("?")[0]
                        and (f := local_file(WEB, url)).exists()
                        and str(f.relative_to(REPO)) not in tracked})
    ok &= check(not untracked, "(k) every local file the page references is tracked by git",
                f"(k) referenced but not tracked, so missing once deployed: {untracked}")

    # (l) The buttons go to the download page (#352), which picks the visitor's system
    # and the newest build. It replaced sending everyone to the release list
    # (#283), which stays the fallback. /latest skips pre-releases and selects
    # an older build, and a pinned or mirrored .dmg goes stale with the next one.
    ctas = [link for link in p.links
            if "btn" in link.get("class", "").split()
            and "btn--ghost" not in link.get("class", "").split()]
    metadata = [json.loads(block) for block in re.findall(
        r'<script type="application/ld\+json">(.*?)</script>', src, re.S)]
    workflow = (REPO / ".github" / "workflows" / "pages.yml").read_text(encoding="utf-8")
    ok &= check(len(ctas) == 3 and all(link.get("href") == "download/" for link in ctas)
                and any(item.get("downloadUrl") == DOWNLOAD_URL for item in metadata)
                and RELEASES_URL in hrefs
                and not any(word in src for word in ("/releases/latest", ".dmg", "download/Orion", "Public download coming later"))
                and "gh release download" not in workflow
                and not list(WEB.rglob("*.dmg")),
                 "(l) all three download buttons and the metadata go to the download page; no pinned or mirrored installer",
                 "(l) download buttons/metadata must go to download/, without pinned or mirrored installers")

    ok &= check_download_page(tracked)
    ok &= check_metadata(tracked)

    if not ok:
        print("\ncheck-site.py: FAILED")
        sys.exit(1)
    print("\ncheck-site.py: all checks passed")


def check_download_page(tracked):
    """(m) What (a), (e), (f), (g) and (k) ask of the landing page, plus the
    download's own rules: without scripts every download button opens the
    release list, and the script reads the whole list, pre-releases included."""
    page = WEB / "download" / "index.html"
    if not page.exists():
        return check(False, "", "(m) web/download/index.html is missing")
    src = page.read_text(encoding="utf-8")
    script = (WEB / "js" / "download.js").read_text(encoding="utf-8")
    p = PageParser()
    p.feed(src)
    files = [(url, local_file(page.parent, url)) for _, url in p.local_urls
             if url.split("#")[0].split("?")[0]]
    buttons = [link for link in p.links if "data-file" in link]
    problems = [f"missing {url}" for url, f in files if not f.exists()]
    problems += [f"untracked {url}" for url, f in files
                 if f.exists() and str(f.relative_to(REPO)) not in tracked]
    problems += [f"bad alt on {d.get('src')}" for d, hidden in p.imgs
                 if d.get("alt") is None or (d.get("alt") == "" and not hidden)]
    if p.canonical != DOWNLOAD_URL:
        problems.append(f"canonical is {p.canonical!r}")
    if "—" in "".join(p.text_chunks):
        problems.append("an em-dash in visible text")
    if not buttons or any(link.get("href") != RELEASES_URL for link in buttons):
        problems.append("a download button does not open the release list without scripts")
    if any(word in src + script for word in ("/releases/latest", "releases/download/")):
        problems.append("a pinned installer or /releases/latest")
    if "api.github.com/repos/Nano-AI/Orion/releases?" not in script:
        problems.append("download.js does not read the release list")
    if not all(m[len("mailto:"):].split("?")[0].lower().endswith("@bankoti.dev") for m in p.mailtos):
        problems.append("a mailto: address not on bankoti.dev")
    return check(not problems, f"(m) the download page: {len(files)} local urls, "
                 f"{len(buttons)} download buttons on the release list without scripts",
                 "(m) the download page: " + "; ".join(problems))


def check_metadata(tracked):
    """(n) to (q): what a search engine reads. A page added under web/ must join
    the sitemap; GitHub Pages serves 404.html at any depth under /Orion/, so it
    names its files from the site root."""
    import xml.etree.ElementTree as ET
    ok = True
    pages = {}
    for page in sorted(WEB.rglob("index.html")):
        p = PageParser()
        src = page.read_text(encoding="utf-8")
        p.feed(src)
        pages[page] = (p, [json.loads(b) for b in re.findall(
            r'<script type="application/ld\+json">(.*?)</script>', src, re.S)])
    canonicals = {p.canonical for p, _ in pages.values()}

    ns = {"s": "http://www.sitemaps.org/schemas/sitemap/0.9"}
    try:
        locs = [u.text for u in ET.parse(WEB / "sitemap.xml").getroot().findall("s:url/s:loc", ns)]
    except (OSError, ET.ParseError) as e:
        locs = [f"unreadable: {e}"]
    ok &= check(sorted(locs) == sorted(canonicals), f"(n) sitemap.xml lists the {len(locs)} canonical URLs",
                f"(n) sitemap.xml lists {sorted(locs)}, the pages' canonicals are {sorted(canonicals)}")

    bad = [f"{page.relative_to(WEB)}: url {urls} against canonical {p.canonical}"
           for page, (p, blocks) in pages.items()
           if p.canonical not in (urls := [b["url"] for b in blocks if "url" in b]) or not set(urls) <= canonicals]
    features = [f for b in pages[WEB / "index.html"][1] for f in b.get("featureList", [])]
    bad += [f"featureList item not a register string: {f!r}" for f in features if f not in REGISTER_STRINGS[:19]]  # not the Not yet group
    ok &= check(not bad, f"(o) JSON-LD parses on {len(pages)} pages, urls are canonicals, {len(features)} features from the register",
                "(o) " + "; ".join(bad))

    titles = ["".join(p.title).strip() for p, _ in pages.values()]
    descs = [p.meta.get("description", "") for p, _ in pages.values()]
    ok &= check(len(set(titles)) == len(titles) and all(0 < len(t) <= 60 for t in titles)
                and len(set(descs)) == len(descs) and all(0 < len(d) <= 160 for d in descs),
                f"(p) {len(titles)} titles unique and at most 60 characters, descriptions unique and at most 160",
                f"(p) titles {titles} or descriptions {[len(d) for d in descs]} repeat or run long")

    page = WEB / "404.html"
    p = PageParser()
    p.feed(page.read_text(encoding="utf-8") if page.exists() else "")
    files = [url for _, url in p.local_urls]
    problems = [] if page.exists() else ["missing"]
    problems += [f"not under /Orion/: {u}" for u in files if not u.startswith("/Orion/")]
    problems += [f"untracked {u}" for u in files if u.startswith("/Orion/")
                 and str(local_file(WEB, u[len("/Orion/"):]).relative_to(REPO)) not in tracked]
    problems += ["no noindex"] * ("noindex" not in p.meta.get("robots", ""))
    problems += ["a canonical link"] * (p.canonical is not None)
    problems += ["an em-dash"] * ("\u2014" in "".join(p.text_chunks))
    ok &= check(not problems, f"(q) 404.html: noindex, no canonical, {len(files)} files all tracked under /Orion/",
                "(q) 404.html: " + "; ".join(problems))
    return ok


if __name__ == "__main__":
    main()
