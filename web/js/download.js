/*  Orion site: the download page. Picks the visitor's system from what the
 *  browser reports and offers the newest build GitHub has for it. A release
 *  that carries a Windows or Linux file turns that system's download on, with
 *  no change here. Without this file the page is the Mac page, its buttons on
 *  the release list.
 */
(() => {
  'use strict';

  const nav = navigator;
  const ua = nav.userAgent;
  const platform = (nav.userAgentData && nav.userAgentData.platform) || nav.platform || '';
  const NAMES = { mac: 'macOS', windows: 'Windows', linux: 'Linux' };
  const LEDES = {
    mac: 'The alpha for Macs with Apple silicon. Free to download, with no account to make.',
    windows: 'The alpha for Windows. Free to download, with no account to make.',
    linux: 'The alpha for Linux. Free to download, with no account to make.',
  };
  // Which release file belongs to which system. An arm64 build is offered only when there is no x64 one.
  const FILES = {
    mac: name => /\.dmg$/i.test(name),
    windows: name => /\.(msi|exe)$/i.test(name) || /win.*\.zip$/i.test(name),
    linux: name => /\.(AppImage|deb|rpm|flatpak)$/i.test(name) || /linux.*\.(tar\.gz|tar\.xz|zip)$/i.test(name),
  };
  const ARM = /arm64|aarch64/i;

  // ?os=windows shows another system's page, to check it before that build ships.
  const asked = new URLSearchParams(location.search).get('os');
  // iPadOS asks for desktop pages and reports a Mac; a Mac has no touch screen.
  let os = ['mac', 'intel', 'windows', 'linux', 'phone'].includes(asked) ? asked
    : /iPhone|iPad|iPod|Android/i.test(ua) || (/Mac/i.test(platform) && nav.maxTouchPoints > 1) ? 'phone'
    : /Mac/i.test(platform) ? 'mac'
    : /Win/i.test(platform) ? 'windows'
    : /Linux|X11/i.test(platform) ? 'linux'
    : null;
  const found = {};   // system -> { release, asset }, from the newest release that has its file

  const $ = s => document.querySelector(s);
  const $$ = s => document.querySelectorAll(s);
  const render = () => {
    // the build the page leads with: this system's, else the Mac one every other branch offers
    const lead = found[os] ? os : 'mac';
    if (os) {
      const branch = os === 'phone' || os === 'intel' ? os : lead === os ? 'ready' : 'soon';
      const desktop = found.windows || found.linux;
      $$('[data-for]').forEach(el => { el.hidden = el.dataset.for !== branch; });
      $$('[data-name]').forEach(el => { el.textContent = NAMES[os] || 'macOS'; });
      $$('[data-row]').forEach(el => { el.querySelector('.dl__here').hidden = el.dataset.row !== os; });
      $('[data-title]').textContent = os === 'phone' ? `Orion is a ${desktop ? 'desktop' : 'Mac'} app`
        : `Orion for ${NAMES[os] || 'macOS'}`;
      if (desktop) $('[data-phone]').textContent = `It runs on ${Object.keys(found).map(s => NAMES[s]).join(', ')}. ` +
        'Open this page on your computer to download it, or share the link to send it there.';
    }
    $('[data-lede]').textContent = LEDES[lead];
    $('[data-label]').textContent = `Download for ${NAMES[lead]}`;

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
      $('[data-only]').hidden = true;
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
        if (value) { row.querySelector('dd').textContent = value; row.hidden = false; }
      }
    }
    // what it runs on, and the first-launch steps, are written for the Mac build only
    $('[data-fact="runs"]').hidden = lead !== 'mac';
    $('[data-steps]').hidden = lead !== 'mac';
  };
  render();

  // Only Chromium says which processor a Mac has; Safari and Firefox report
  // Intel on every Mac, so they keep the Apple silicon page.
  if (os === 'mac' && !asked && nav.userAgentData && nav.userAgentData.getHighEntropyValues) {
    nav.userAgentData.getHighEntropyValues(['architecture'])
      .then(v => { if (v.architecture === 'x86') { os = 'intel'; render(); } })
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
      render();
    })
    .catch(() => {});   // rate-limited or offline: the buttons already open the release list

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
