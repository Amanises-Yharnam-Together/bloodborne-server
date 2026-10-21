import { TrailerPlayer } from "@/components/trailer-player";
import { resolveTrailerId } from "@/lib/site";

/** The trailer frame on the landing page: the site's own poster until a visitor presses play. */
export function VideoSlot() {
  return <TrailerPlayer id={resolveTrailerId()} />;
}
