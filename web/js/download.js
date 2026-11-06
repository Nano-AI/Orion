/*  Orion site: the download page. Offers the newest build GitHub has for the
 *  visitor's system, which the page's <head> has already read from what the
 *  browser reports (html.os-*), so the words and the branch are right from the
 *  first paint and the two can never disagree. A release
 *  that carries a Windows or Linux file turns that system's download on, with
 *  no change here. Without this file the page is the Mac page, its buttons on
 *  the release list.
 */
(() => {
  'use strict';

  const nav = navigator;
  const html = document.documentElement;
  const NAMES = { mac: 'macOS', windows: 'Windows', linux: 'Linux' };
  // Which release file belongs to which system. An arm64 build is offered only when there is no x64 one.
  const FILES = {
    mac: name => /\.dmg$/i.test(name),
    windows: name => /\.(msi|exe)$/i.test(name) || /win.*\.zip$/i.test(name),
    linux: name => /\.(AppImage|deb|rpm|flatpak)$/i.test(name) || /linux.*\.(tar\.gz|tar\.xz|zip)$/i.test(name),
  };
  const ARM = /arm64|aarch64/i;

  // ?os=windows shows another system's page, to check it before that build ships (the <head> reads it too).
  const asked = new URLSearchParams(location.search).get('os');
  let os = (/\bos-(mac|intel|windows|linux|phone)\b/.exec(html.className) || [])[1] || null;
  const found = {};   // system -> { release, asset }, from the newest release that has its file
  let looked = false;   // whether the release list has answered
  // A phone is told "desktop app" from the start (the page's own words), since
  // the newest alpha ships for all three systems; the release list can still
  // take it back to the Mac.
  const MAC_PHONE = 'It runs on Macs with Apple silicon. Open this page on your Mac to download it, or share the link to send it there.';

  const $ = s => document.querySelector(s);
  const $$ = s => document.querySelectorAll(s);
  const render = () => {
    // the build the page leads with: this system's, else the Mac one every other branch offers.
    // Until the release list answers, a Mac, Windows or Linux visitor is assumed to have one (the
    // newest alpha ships for all three), so the page does not redraw under them when it does.
    const lead = found[os] || (!looked && NAMES[os]) ? os : 'mac';
    // a Windows or Linux visitor whose system has no build yet: download.css shows the "Not yet" branch
    html.classList.toggle('no-build', os !== 'mac' && Boolean(NAMES[os]) && lead !== os);
    if (os === 'phone' && looked) {
      const desktop = found.windows || found.linux;
      if (!desktop) $('[data-title] .only-phone').textContent = 'Orion is a Mac app';
      $('[data-phone]').textContent = desktop ? `It runs on ${Object.keys(found).map(s => NAMES[s]).join(', ')}. ` +
        'Open this page on your computer to download it, or share the link to send it there.' : MAC_PHONE;
    }

    $$('[data-file]').forEach(a => {
      const f = found[a.dataset.file === 'visitor' ? lead : a.dataset.file];
      if (f) { a.href = f.asset.browser_download_url; a.hidden = false; }
    });
    for (const [system, f] of Object.entries(found)) {
      const row = $(`[data-row="${system}"]`);
      if (system === 'mac' || !row) continue;
      row.querySelector('.needs').textContent = f.asset.name;
      row.querySelector('.status').textContent = 'Alpha, available now';
      row.querySelector('.status').classList.add('is-ready');
    }

    const f = found[lead];
    if (f) {
      $('[data-notes]').href = f.release.html_url;
      const facts = {
        file: f.asset.name,
        size: `${(f.asset.size / 1e6).toFixed(1)} MB`,
        date: new Date(f.release.published_at).toLocaleDateString('en-GB',
          { day: 'numeric', month: 'long', year: 'numeric', timeZone: 'UTC' }),
        sha: (f.asset.digest || '').replace(/^sha256:/, ''),
      };
      for (const [key, value] of Object.entries(facts)) {
        const row = $(`[data-fact="${key}"]`);
        if (value) { row.querySelector('dd').textContent = value; row.hidden = false; row.removeAttribute('data-pending'); }
      }
    }
    // what it runs on, and the first-launch steps, are written for the Mac build only (download.css does the
    // same from html.os-* for the first paint; this keeps it true if the release list changes the lead)
    $('[data-fact="runs"]').hidden = lead !== 'mac';
    $('[data-steps]').hidden = lead !== 'mac';
  };
  render();

  // Only Chromium says which processor a Mac has; Safari and Firefox report
  // Intel on every Mac, so they keep the Apple silicon page.
  if (os === 'mac' && !asked && nav.userAgentData && nav.userAgentData.getHighEntropyValues) {
    nav.userAgentData.getHighEntropyValues(['architecture'])
      .then(v => { if (v.architecture === 'x86') { html.classList.replace('os-mac', 'os-intel'); os = 'intel'; render(); } })
      .catch(() => {});
  }

  // Newest first, pre-releases included: GitHub's "latest release" skips
  // pre-releases, and every Orion build so far is one.
  fetch('https://api.github.com/repos/Nano-AI/Orion/releases?per_page=20')
    .then(r => (r.ok ? r.json() : Promise.reject(r.status)))
    .then(releases => {
      for (const release of releases) {
        if (release.draft) continue;
        for (const system of Object.keys(FILES)) {
          // and an installer before a zip: Velopack's Orion-win-Setup.exe before its Portable.zip (#389)
          const asset = found[system] ? null : release.assets.filter(a => FILES[system](a.name))
            .sort((a, b) => ARM.test(a.name) - ARM.test(b.name) || /\.zip$/i.test(a.name) - /\.zip$/i.test(b.name))[0];
          if (asset) found[system] = { release, asset };
        }
      }
      looked = true;
      render();
    })
    .catch(() => {})   // rate-limited or offline: the buttons already open the release list
    .finally(() => $$('[data-pending]').forEach(row => { row.hidden = true; row.removeAttribute('data-pending'); }));   // a fact the release did not give

  const flash = (button, said) => {
    const was = button.textContent;
    button.textContent = said;
    setTimeout(() => { button.textContent = was; }, 1600);
  };
  $$('[data-copy]').forEach(button => {
    button.hidden = !nav.clipboard;
    button.addEventListener('click', () => nav.clipboard.writeText(button.dataset.copy).then(() => flash(button, 'Copied')));
  });
  const share = $('[data-share]');
  if (share) share.addEventListener('click', () => {
    const url = location.href.split('#')[0];
    if (nav.share) nav.share({ title: 'Orion', url }).catch(() => {});
    else if (nav.clipboard) nav.clipboard.writeText(url).then(() => flash(share, 'Link copied'));
  });
})();
