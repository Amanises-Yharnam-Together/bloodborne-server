import { cn } from "@/lib/utils";

/** The site's emblem (scripts/emblem.mjs): an engraved medallion with a moon and a drop of blood. */
export function Emblem({ className, priority }: { className?: string; priority?: boolean }) {
  return (
    // A static SVG: next/image would only add a wrapper for it.
    // eslint-disable-next-line @next/next/no-img-element
    <img
      src="/emblem.svg"
      alt=""
      width={512}
      height={512}
      fetchPriority={priority ? "high" : undefined}
      className={cn("size-40", className)}
    />
  );
}

/** The small mark: the moon and the drop alone, for sizes where the ring would blur. */
export function LogoMark({ className }: { className?: string }) {
  return (
    // eslint-disable-next-line @next/next/no-img-element
    <img src="/mark.svg" alt="" width={32} height={32} className={cn("size-8", className)} />
  );
}
