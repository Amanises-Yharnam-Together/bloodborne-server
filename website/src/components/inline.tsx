import Link from "next/link";
import { Fragment } from "react";

import { isExternal, parseInline, type Inline } from "@/lib/inline";

function Node({ node }: { node: Inline }) {
  switch (node.kind) {
    case "text":
      return node.text;
    case "code":
      return <code>{node.text}</code>;
    case "bold":
      return (
        <strong>
          <Nodes nodes={node.children} />
        </strong>
      );
    case "link":
      return isExternal(node.href) ? (
        <a href={node.href} rel="noopener">
          <Nodes nodes={node.children} />
        </a>
      ) : (
        <Link href={node.href}>
          <Nodes nodes={node.children} />
        </Link>
      );
  }
}

function Nodes({ nodes }: { nodes: Inline[] }) {
  return nodes.map((n, i) => (
    <Fragment key={i}>
      <Node node={n} />
    </Fragment>
  ));
}

export function InlineText({ text }: { text: string }) {
  return <Nodes nodes={parseInline(text)} />;
}
