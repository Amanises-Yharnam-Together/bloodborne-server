import { resolveTrailerId, site } from "@/lib/site";

/**
 * The trailer frame on the landing page. With NEXT_PUBLIC_TRAILER_YOUTUBE_ID set it embeds the
 * video (privacy-enhanced mode, loaded lazily); without it, it keeps the space empty and says so.
 */
export function VideoSlot() {
  const id = resolveTrailerId();
  return (
    <figure className="panel relative mx-auto aspect-video w-full max-w-4xl overflow-hidden">
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
        <figcaption className="absolute inset-0 flex items-center justify-center text-lg text-muted-foreground italic">
          The trailer goes here.
        </figcaption>
      )}
    </figure>
  );
}
