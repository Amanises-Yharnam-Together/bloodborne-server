# Website

The promo and documentation site for Bloodborne Seamless Co-op: a landing page with the trailer,
the download page (the latest GitHub release, looked up at build time and refreshed hourly) and
the docs. Next.js 16 (App Router, Server Components, Cache Components) with shadcn/ui restyled
after Bloodborne's menus.

```sh
npm ci
npm run dev      # http://localhost:3000
npm run check    # format, lint, type check, tests, production build
```

## Where things are

| Path                          | What it is                                                                |
| ----------------------------- | ------------------------------------------------------------------------- |
| `src/content/docs.ts`         | the documentation as data; the pages and `/llms-full.txt` both render it  |
| `src/content/faq.ts`          | the landing page's questions (also FAQPage structured data)               |
| `src/components/figures.tsx`  | the docs' diagrams and the traffic chart, as server-rendered SVG/HTML     |
| `public/docs/`                | screenshots used in the docs (test-rig accounts only)                     |
| `src/components/ui/`          | shadcn/ui components (`components.json` is set up for `npx shadcn add`)   |
| `src/app/globals.css`         | the theme: colours, type, the gilt frame                                  |
| `src/lib/site.ts`             | names, links, the deployment URL, the trailer id, the netcode status line |
| `assets-src/logo.png`         | the logo; `npm run icons` redraws the favicon and app icons from it       |
| `src/app/opengraph-image.tsx` | the link preview; `npm run og` redraws its backdrop from `public/art`     |
| `src/lib/metadata.ts`         | a page's title, description, canonical URL and link-card tags             |

Everything a crawler or a language model looks for is generated: `sitemap.xml`, `robots.txt`,
`manifest.webmanifest`, `llms.txt`, `llms-full.txt`, the Open Graph image, canonical URLs and
JSON-LD (WebSite, SoftwareApplication, FAQPage, TechArticle). The only client JavaScript is the
FAQ accordion.

When the server netcode ships in a public release, update `netcodeStatus` in `src/lib/site.ts`
and the "Play together" section of `src/content/docs.ts`.

## Deploy on Vercel

1. Import the repository in Vercel and set **Root Directory** to `website`. The framework preset
   (Next.js), build command and output are detected; nothing else needs changing.
2. Set the environment variables from `.env.example`: `NEXT_PUBLIC_SITE_URL` only to override
   the site's address (builds on Vercel use `SITE_DOMAIN` from `src/lib/site.ts`,
   bloodborne.seamlesscoop.dev), `NEXT_PUBLIC_TRAILER_YOUTUBE_ID` when the trailer is up.
3. Deploy. The download page refreshes the release from GitHub at most once an hour.

## Fonts and art

Cinzel and EB Garamond (SIL Open Font License; the copies in `src/assets/fonts` are for the social
image, with their licenses). The mark and the diagrams are this project's own. No FromSoftware or
Sony art is used for the design; the docs' screenshots show the mod running in the game.
