import { Download } from "lucide-react";

import { Button } from "@/components/ui/button";
import { getLatestRelease } from "@/lib/github";
import { LATEST_RELEASE_URL } from "@/lib/site";

/** The primary call to action: the latest release's installer, or its release page when GitHub
 *  could not be asked. */
export async function DownloadButton({ size = "lg" }: { size?: "default" | "lg" }) {
  const release = await getLatestRelease();
  const asset = release?.assets[0];
  return (
    <Button asChild size={size}>
      <a href={asset?.url ?? LATEST_RELEASE_URL} rel="noopener">
        <Download aria-hidden />
        {release ? `Download ${release.tag}` : "Download"}
      </a>
    </Button>
  );
}
