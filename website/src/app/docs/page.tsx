import type { Metadata } from "next";
import Link from "next/link";

import { DocsShell } from "@/components/doc-article";
import { MenuTitle } from "@/components/ornament";
import {
  Accordion,
  AccordionContent,
  AccordionItem,
  AccordionTrigger,
} from "@/components/ui/accordion";
import { docs } from "@/content/docs";
import { faq } from "@/content/faq";
import { JsonLd } from "@/lib/json-ld";

export const metadata: Metadata = {
  title: "Guide",
  description:
    "The Bloodborne Seamless Co-op guide: installing, the launcher, playing as a team, and how the port and its server work.",
  alternates: { canonical: "/docs" },
};

export default function DocsIndex() {
  return (
    <DocsShell>
      <MenuTitle as="h1">Guide</MenuTitle>
      <dl className="mx-auto mt-8 max-w-3xl">
        {docs.map((d) => (
          <div
            key={d.slug}
            className="grid gap-x-8 gap-y-1 border-b border-silver/15 py-4 sm:grid-cols-[minmax(0,13rem)_minmax(0,1fr)]"
          >
            <dt className="text-xl sm:text-right">
              <Link href={`/docs/${d.slug}`} className="text-bone hover:text-title">
                {d.title}
              </Link>
            </dt>
            <dd className="text-muted-foreground">{d.description}</dd>
          </div>
        ))}
      </dl>

      <section aria-labelledby="questions" className="mx-auto mt-20 max-w-3xl">
        <MenuTitle id="questions">Questions</MenuTitle>
        <Accordion type="single" collapsible className="mt-4">
          {faq.map((f) => (
            <AccordionItem key={f.q} value={f.q}>
              <AccordionTrigger>{f.q}</AccordionTrigger>
              <AccordionContent className="text-foreground">{f.a}</AccordionContent>
            </AccordionItem>
          ))}
        </Accordion>
      </section>
      <JsonLd
        data={{
          "@context": "https://schema.org",
          "@type": "FAQPage",
          mainEntity: faq.map((f) => ({
            "@type": "Question",
            name: f.q,
            acceptedAnswer: { "@type": "Answer", text: f.a },
          })),
        }}
      />
    </DocsShell>
  );
}
