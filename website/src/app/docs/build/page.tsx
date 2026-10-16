import { DocArticle, docMetadata } from "@/components/doc-article";

export const metadata = docMetadata("build");

export default function Page() {
  return <DocArticle slug="build" />;
}
