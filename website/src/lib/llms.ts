import { docs } from "@/content/docs";
import { faq } from "@/content/faq";
import { docToMarkdown } from "@/lib/markdown";
import { RELEASES_URL, REPO_URL, absoluteUrl, site } from "@/lib/site";

// /llms.txt (https://llmstxt.org): a short Markdown map of the site for language models, and
// /llms-full.txt with every docs page in full.

export function llmsIndex(): string {
  const lines = [
    `# ${site.name}`,
    "",
    `> ${site.description}`,
    "",
    "Key facts:",
    "- Runs on Bloodborne PC, a native Windows port of the PS4 game (no emulator). Requires the player's own Bloodborne for PS4, version 1.09; no game files are included.",
    "- Up to four players per team with the server netcode; every game keeps one UDP link to a self-hosted server, which owns the team's world. No peer-to-peer.",
    "- Status: the server netcode is in the main branch and in testing. The current public release (0.1.4) is the older peer-to-peer version, for up to three players.",
    "- Free and open source (GPL-2.0-or-later). Unofficial fan project, not affiliated with Sony Interactive Entertainment or FromSoftware.",
    "",
    "## Docs",
    "",
    ...docs.map((d) => `- [${d.title}](${absoluteUrl(`/docs/${d.slug}`)}): ${d.description}`),
    `- [All docs in one file](${absoluteUrl("/llms-full.txt")})`,
    "",
    "## Download",
    "",
    `- [Download page](${absoluteUrl("/download")}): the latest release for Windows`,
    `- [GitHub releases](${RELEASES_URL}): every release with its SHA-256`,
    "",
    "## Optional",
    "",
    `- [Source code](${REPO_URL})`,
    "",
  ];
  return lines.join("\n");
}

export function llmsFull(): string {
  const faqText = ["# Questions", ...faq.map(({ q, a }) => `## ${q}\n\n${a}`)].join("\n\n");
  return [llmsIndex(), ...docs.map(docToMarkdown), faqText + "\n"].join("\n\n---\n\n");
}
