import type { Block, DocPage } from "@/content/docs";
import { absoluteUrl } from "@/lib/site";

// The docs as Markdown for /llms-full.txt. Inline text is already Markdown; site-relative links
// become absolute so they still work out of context.

function absolutize(text: string): string {
  return text.replace(/\]\((\/[^)\s]*)\)/g, (_, path: string) => `](${absoluteUrl(path)})`);
}

function block(b: Block): string {
  switch (b.type) {
    case "p":
      return absolutize(b.text);
    case "h3":
      return `### ${b.text}`;
    case "note":
      return `> ${absolutize(b.text)}`;
    case "ul":
      return b.items.map((i) => `- ${absolutize(i)}`).join("\n");
    case "ol":
      return b.items.map((i, n) => `${n + 1}. ${absolutize(i)}`).join("\n");
    case "terms":
      return b.items.map(([t, d]) => `- **${absolutize(t)}**: ${absolutize(d)}`).join("\n");
    case "figure":
      return `*Figure: ${absolutize(b.caption)}* ${b.alt}`;
    case "image":
      return `![${b.alt}](${absoluteUrl(b.src)})\n*${absolutize(b.caption)}*`;
    case "code":
      return `\`\`\`${b.lang ?? ""}\n${b.text}\n\`\`\``;
    case "table": {
      const row = (cells: string[]) => `| ${cells.map((c) => absolutize(c)).join(" | ")} |`;
      return [row(b.head), row(b.head.map(() => "---")), ...b.rows.map(row)].join("\n");
    }
  }
}

export function docToMarkdown(page: DocPage): string {
  const parts = [
    `# ${page.title}`,
    `Source: ${absoluteUrl(`/docs/${page.slug}`)}`,
    page.description,
  ];
  for (const s of page.sections) {
    parts.push(`## ${s.title}`, ...s.blocks.map(block));
  }
  return parts.join("\n\n") + "\n";
}
