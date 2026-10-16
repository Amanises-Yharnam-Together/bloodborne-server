import { DocArticle, docMetadata } from "@/components/doc-article";

export const metadata = docMetadata("credits");

export default function Page() {
  return <DocArticle slug="credits" />;
}
