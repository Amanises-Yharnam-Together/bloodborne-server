import { DocArticle, docMetadata } from "@/components/doc-article";

export const metadata = docMetadata("architecture");

export default function Page() {
  return <DocArticle slug="architecture" />;
}
