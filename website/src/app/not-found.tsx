import Link from "next/link";

import { Heading } from "@/components/ornament";

export default function NotFound() {
  return (
    <div className="mx-auto max-w-4xl px-4 pt-28 sm:px-6">
      <Heading as="h1">Nothing here</Heading>
      <p className="mt-6 text-muted-foreground">
        This page does not exist. Try the{" "}
        <Link href="/" className="text-title hover:text-bone">
          home page
        </Link>{" "}
        or the{" "}
        <Link href="/docs/install" className="text-title hover:text-bone">
          install guide
        </Link>
        .
      </p>
    </div>
  );
}
