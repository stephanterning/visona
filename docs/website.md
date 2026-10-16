# Website (visona.org)

The site is static HTML generated from `site/` and the GitHub releases, and hosted on GitHub Pages.

- `site/index.template.html`: the home page. `site/build.py` fills in the download section.
- `site/static/`: copied as is: the stylesheet, favicon, 404 page and the manual (`site/static/manual/index.html`).
- `docs/images/visona-*.png`: copied to `/images/`.
- `.github/workflows/site.yml`: builds and deploys the site.

## Preview locally

Python 3.9 or later, standard library only:

```sh
python3 site/build.py --serve
```

This fetches the releases from the GitHub API, builds the site into `_site/` and serves it on <http://localhost:8000>. Without a token the API allows 60 requests an hour; set `GITHUB_TOKEN` (for example `GITHUB_TOKEN=$(gh auth token)`) if you hit the limit. `--releases-json <file>` builds from a saved API response instead, and `--serve 8080` picks another port.

## Download channels

`site/build.py` sorts releases into channels by tag:

| Tag | Channel |
| --- | --- |
| `v1.2.3` (not marked as prerelease) | stable |
| `v1.2.3-beta.N`, `v1.2.3-rc.N` | beta |
| `v1.2.3-alpha.N` | alpha |

Other tags, such as `dev-builds`, drafts and releases without files are skipped. The newest release of each channel is shown with each file's size and the SHA-256 digest GitHub computed for it. A channel without a release shows "Not released yet" until its first release.

For every file the site also writes a redirect page, so these links always lead to the newest build:

- `/download/<channel>/<file>/`, such as `/download/alpha/vst3-windows-x64/`
- `/download/latest/<file>/`: the most mature channel that has a release (stable, then beta, then alpha)
- `/download/<channel>/SHA256SUMS` and `/releases.json`

`<file>` is the asset name without `Visona-` and the extension: `macos-arm64`, `linux-arm64-pi`, `vst3-macos-arm64`, `sync-clap-windows-x64` and so on.

## When the site is deployed

On a push to `main` that changes `site/`, `docs/images/` or the workflow; when a release is published, edited or deleted; when the Release workflow finishes; and by hand from **Actions → Website → Run workflow**. A published release triggers the Release workflow, which uploads the files afterwards; the site skips the release until it has files and is rebuilt when the Release workflow finishes.

## One-time setup

1. **GitHub Pages.** In the repository, open **Settings → Pages** and set **Source** to **GitHub Actions**. Then run **Actions → Website → Run workflow** once.
2. **DNS at Loopia.** In Loopia Customer Zone, open the DNS editor for `visona.org` and replace the records for `@` and `www` with:

   | Host | Type | Value |
   | --- | --- | --- |
   | `@` | A | `185.199.108.153` |
   | `@` | A | `185.199.109.153` |
   | `@` | A | `185.199.110.153` |
   | `@` | A | `185.199.111.153` |
   | `@` | AAAA | `2606:50c0:8000::153` |
   | `@` | AAAA | `2606:50c0:8001::153` |
   | `@` | AAAA | `2606:50c0:8002::153` |
   | `@` | AAAA | `2606:50c0:8003::153` |
   | `www` | CNAME | `stephanterning.github.io.` |

   Remove Loopia's default parking records for `@` and `www` first. Leave MX and other records alone if the domain has e-mail.
3. **Verify the domain.** In your GitHub account, open **Settings → Pages → Add a domain**, add `visona.org` and put the TXT record GitHub shows into Loopia's DNS. This stops anyone else from claiming the domain for their Pages site.
4. **Custom domain.** Back in the repository's **Settings → Pages**, enter `visona.org` as the custom domain. When the DNS check passes, tick **Enforce HTTPS**. The certificate can take up to an hour; DNS changes at Loopia can take a few hours to spread.
5. Optionally, set the repository's **Website** field (the gear next to **About**) to `https://visona.org`.
