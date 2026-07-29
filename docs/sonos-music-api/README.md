# Sonos music-API reference material

Evidence behind **`plans/08-music-service-integration.md`** — read that first; it has the conclusions.
These two files are here because both are awkward to refetch and one is derived work that took a
96-page scrape to produce.

## `Sonos-1.19.6.wsdl` — the SMAPI contract

The current Sonos Music API (SMAPI) WSDL. **v1.19.6, dated 2023-10-24**, namespace
`http://www.sonos.com/Services/1.1`, 29 operations, 2074 lines.

```
https://sonos-partner-documentation.s3.amazonaws.com/ReadMe-External/content-services/soap-requests-and-responses/Sonoswsdl-1.19.6-20231024.wsdl
```

Linked from `docs.sonos.com/docs/soap-requests-and-responses`. Committed because it lives on an S3
bucket behind a docs page that has already moved once (`developer.sonos.com` → `docs.sonos.com`, with
the old URLs now 302ing to a Salesforce login wall).

**Why it matters here:** the WSDL is authoritative where the prose docs are not. The `credentials`
SOAP header is a three-way `xs:choice` of `sessionId` / `login` / `loginToken` — the prose page omits
`sessionId` entirely. If you ever implement anonymous SMAPI browsing (see plans/08), this file is the
contract, not the docs.

Diffing against the 2013-era WSDL (`github.com/davidwhitney/OpenSonos/blob/master/Sonos.wsdl`) shows
`getAppLink`, `getUserInfo` and `refreshAuthToken` were **added** and `getStreamingMetadata` removed —
confirming AppLink is a post-2013 bolt-on.

## `sonos-control-api-openapi-merged.json` — the cloud Control API surface

**Derived, not published by Sonos.** Every page under `docs.sonos.com/reference/*` embeds an OpenAPI
3.0.3 fragment; this is all 96 of them merged. Self-identified as
`v1.55.0-alpha.8-production-cloud`, "Sonos Control API (cloud)".

Structure is `{paths, schemas}` — a merge of the fragments, not a loadable standalone OpenAPI
document. **55 paths across 12 namespaces**, 52 schemas.

Its purpose is to make one claim checkable rather than asserted:

```python
import json, re
d = json.load(open('sonos-control-api-openapi-merged.json'))
len(d['paths'])                                                    # 55
[p for p in d['paths'] if re.search(r'browse|search|catalog|library', p, re.I)]   # []
```

There is no browse, search, catalog or library path. That is why the cloud API cannot enumerate a
music service's stations — see plans/08 for the other four reasons it is unsuitable for this project.

## Refetching

Two tricks that make `docs.sonos.com` tractable and are easy to forget:

- **Append `.md` to any docs path** for raw Markdown with an `updatedAt` frontmatter date, e.g.
  `https://docs.sonos.com/docs/getmetadata.md`. The dates matter — much of the community writing about
  SMAPI is a decade stale.
- **`https://docs.sonos.com/llms.txt`** is a complete page index (Guides / API Reference / Pages).

Captured 2026-07-29. The docs site has **no changelog**, and API surface has been removed silently
before (the whole `settings` namespace, `getPlaylist`, `joinSession`), so treat anything here as a
snapshot rather than a live contract.
