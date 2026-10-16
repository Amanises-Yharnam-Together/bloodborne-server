import Link from "next/link";

import { SectionHeading } from "@/components/ornament";
import { Button } from "@/components/ui/button";

export default function NotFound() {
  return (
    <div className="mx-auto max-w-2xl px-4 pt-28 text-center sm:px-6">
      <SectionHeading eyebrow="Lost in the nightmare" title="This page does not exist">
        <p>The path you followed leads nowhere. Return to the Dream.</p>
      </SectionHeading>
      <Button asChild variant="outline" className="mt-10">
        <Link href="/">Back to the start</Link>
      </Button>
    </div>
  );
}
