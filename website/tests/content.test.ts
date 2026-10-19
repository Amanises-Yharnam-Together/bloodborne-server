import { describe, expect, it } from "vitest";

import { figures } from "@/components/figures";
import { docs } from "@/content/docs";
import { faq } from "@/content/faq";
import { packetRates } from "@/content/traffic";
import { parseInline, stripInline } from "@/lib/inline";
import { llmsFull, llmsIndex } from "@/lib/llms";
import { existsSync } from "node:fs";

describe("parseInline", () => {
  it("reads code, bold and links", () => {
    expect(parseInline("a `b` **c [d](/e)** f")).toEqual([
      { kind: "text", text: "a " },
      { kind: "code", text: "b" },
      { kind: "text", text: " " },
      {
        kind: "bold",
        children: [
          { kind: "text", text: "c " },
          { kind: "link", href: "/e", children: [{ kind: "text", text: "d" }] },
        ],
      },
      { kind: "text", text: " f" },
    ]);
  });
  it("strips markup to plain text", () => {
    expect(stripInline("**One** `world`, [docs](/docs)")).toBe("One world, docs");
  });
});

describe("docs content", () => {
  const internal = new Set(["/", "/download", "/docs", ...docs.map((d) => `/docs/${d.slug}`)]);

  it("has unique slugs and section ids", () => {
    expect(new Set(docs.map((d) => d.slug)).size).toBe(docs.length);
    for (const d of docs) expect(new Set(d.sections.map((s) => s.id)).size).toBe(d.sections.length);
  });

  it("links only to pages that exist", () => {
    for (const d of docs)
      for (const s of d.sections)
        for (const b of s.blocks) {
          const texts =
            b.type === "ul" || b.type === "ol"
              ? b.items
              : b.type === "table"
                ? b.rows.flat()
                : b.type === "terms"
                  ? b.items.flat()
                  : b.type === "gallery"
                    ? b.items.map((i) => i.caption)
                    : "text" in b
                      ? [b.text]
                      : [b.caption];
          for (const t of texts)
            for (const m of t.matchAll(/\]\((\/[^)#]*)/g))
              expect(internal, `${d.slug}: ${m[1]}`).toContain(m[1]);
        }
  });

  it("has a route for every docs page", () => {
    for (const d of docs) expect(existsSync(`src/app/docs/${d.slug}/page.tsx`), d.slug).toBe(true);
  });

  it("uses figures that exist and images that are in public/", () => {
    for (const d of docs)
      for (const s of d.sections)
        for (const b of s.blocks) {
          if (b.type === "figure") expect(figures).toHaveProperty(b.figure);
          for (const img of b.type === "image" ? [b] : b.type === "gallery" ? b.items : []) {
            expect(existsSync(`public${img.src}`), img.src).toBe(true);
            expect(img.alt.length).toBeGreaterThan(10);
          }
        }
  });

  it("never names a real player or a test account: players are Player 1, Player 2 and so on", () => {
    const all = llmsFull();
    for (const name of ["Lisara", "krolebord", "Krolebord", "rig_"])
      expect(all).not.toContain(name);
  });

  it("shows no packet channel codes or game addresses", () => {
    const all = llmsFull() + JSON.stringify(packetRates);
    expect(all).not.toMatch(/(?<![\w/])0x[0-9a-f]+/i);
    expect(all).not.toMatch(/\bchannels? \d/i);
  });
});

describe("llms.txt", () => {
  it("lists every docs page and the full file", () => {
    const index = llmsIndex();
    expect(index.startsWith("# ")).toBe(true);
    for (const d of docs) expect(index).toContain(`/docs/${d.slug}`);
    expect(index).toContain("/llms-full.txt");
  });
  it("carries every page and question in full, with absolute links", () => {
    const full = llmsFull();
    for (const d of docs) expect(full).toContain(`# ${d.title}`);
    for (const { q } of faq) expect(full).toContain(q);
    expect(full).not.toMatch(/\]\(\/[^/]/);
  });
});
