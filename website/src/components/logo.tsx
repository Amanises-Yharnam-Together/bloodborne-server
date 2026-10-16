import { cn } from "@/lib/utils";

/** The logo: two hunters back to back before the moon, in an engraved ring (public/logo.webp,
 *  made from assets-src/logo.png by scripts/generate-icons.mjs). */
export function Emblem({ className, priority }: { className?: string; priority?: boolean }) {
  return (
    // A static image the size it is shown at: next/image would only add a wrapper.
    // eslint-disable-next-line @next/next/no-img-element
    <img
      src="/logo.webp"
      alt=""
      width={512}
      height={512}
      fetchPriority={priority ? "high" : undefined}
      className={cn("size-40", className)}
    />
  );
}

/** The same logo at header size. */
export function LogoMark({ className }: { className?: string }) {
  return (
    // eslint-disable-next-line @next/next/no-img-element
    <img src="/logo.webp" alt="" width={32} height={32} className={cn("size-8", className)} />
  );
}
