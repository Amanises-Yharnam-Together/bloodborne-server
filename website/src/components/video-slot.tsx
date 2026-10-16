import { Play } from "lucide-react";

import { resolveTrailerId, site } from "@/lib/site";

/**
 * The trailer frame on the landing page. With NEXT_PUBLIC_TRAILER_YOUTUBE_ID set it embeds the
 * video (privacy-enhanced mode, loaded lazily); without it, it keeps the space with a placeholder.
 */
export function VideoSlot() {
  const id = resolveTrailerId();
  return (
    <figure className="frame relative mx-auto aspect-video w-full max-w-4xl overflow-hidden bg-card">
      {id ? (
        <iframe
          className="absolute inset-0 size-full"
          src={`https://www.youtube-nocookie.com/embed/${id}?rel=0`}
          title={`${site.name} trailer`}
          loading="lazy"
          allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share"
          referrerPolicy="strict-origin-when-cross-origin"
          allowFullScreen
        />
      ) : (
        <div className="absolute inset-0 flex flex-col items-center justify-center gap-5 bg-[radial-gradient(ellipse_at_center,rgb(232_220_196/0.06),transparent_65%)]">
          <span className="flex size-16 items-center justify-center rounded-full border border-gilt/50 text-gilt">
            <Play className="ml-1 size-6" aria-hidden />
          </span>
          <figcaption className="eyebrow">Trailer coming soon</figcaption>
        </div>
      )}
    </figure>
  );
}
