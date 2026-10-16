// A tiny inline formatter for the docs' text: `code`, **bold** and [label](href). Nothing else,
// on purpose: the docs are data, and both the pages and /llms-full.txt read them.

export type Inline =
  | { kind: "text"; text: string }
  | { kind: "code"; text: string }
  | { kind: "bold"; children: Inline[] }
  | { kind: "link"; href: string; children: Inline[] };

const TOKEN = /`([^`]+)`|\*\*(.+?)\*\*|\[([^\]]+)\]\(([^)\s]+)\)/g;

export function parseInline(source: string): Inline[] {
  const out: Inline[] = [];
  let last = 0;
  for (const m of source.matchAll(TOKEN)) {
    const at = m.index ?? 0;
    if (at > last) out.push({ kind: "text", text: source.slice(last, at) });
    if (m[1] !== undefined) out.push({ kind: "code", text: m[1] });
    else if (m[2] !== undefined) out.push({ kind: "bold", children: parseInline(m[2]) });
    else out.push({ kind: "link", href: m[4], children: parseInline(m[3]) });
    last = at + m[0].length;
  }
  if (last < source.length) out.push({ kind: "text", text: source.slice(last) });
  return out;
}

/** Plain text without the markup, for meta descriptions and structured data. */
export function stripInline(source: string): string {
  return parseInline(source)
    .map(function text(node: Inline): string {
      if (node.kind === "text" || node.kind === "code") return node.text;
      return node.children.map(text).join("");
    })
    .join("");
}

export function isExternal(href: string): boolean {
  return /^https?:\/\//.test(href);
}
