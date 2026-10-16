import { DocArticle, docMetadata } from "@/components/doc-article";

export const metadata = docMetadata("play");

export default function Page() {
  return <DocArticle slug="play" />;
}
