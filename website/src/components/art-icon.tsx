import type { IconName } from "@/content/docs";
import { cn } from "@/lib/utils";

/** One of the painted gold icons in public/art (made by scripts/art/make_art.py). */
export function ArtIcon({ name, className }: { name: IconName; className?: string }) {
  return (
    // eslint-disable-next-line @next/next/no-img-element -- a small static icon, already sized
    <img
      src={`/art/icon-${name}.webp`}
      alt=""
      width={192}
      height={192}
      loading="lazy"
      className={cn("size-12 shrink-0 object-contain", className)}
    />
  );
}
