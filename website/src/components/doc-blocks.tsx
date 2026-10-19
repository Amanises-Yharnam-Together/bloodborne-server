import Image from "next/image";

import { ArtIcon } from "@/components/art-icon";
import { figures } from "@/components/figures";
import { ImageTabs } from "@/components/image-tabs";
import { InlineText } from "@/components/inline";
import type { Block, DocPage } from "@/content/docs";

function DocBlock({ block }: { block: Block }) {
  switch (block.type) {
    case "p":
      return (
        <p>
          <InlineText text={block.text} />
        </p>
      );
    case "h3":
      return <h3>{block.text}</h3>;
    case "note":
      return (
        <aside className="panel my-6 max-w-[68ch] px-5 py-4 text-base text-muted-foreground italic">
          <InlineText text={block.text} />
        </aside>
      );
    case "ul":
    case "ol": {
      const List = block.type;
      return (
        <List>
          {block.items.map((item, i) => (
            <li key={i}>
              <InlineText text={item} />
            </li>
          ))}
        </List>
      );
    }
    case "terms":
      return (
        <dl className="my-5 max-w-[72ch]">
          {block.items.map(([term, text]) => (
            <div
              key={term}
              className="grid gap-x-6 gap-y-1 border-b border-silver/15 py-3 sm:grid-cols-[12rem_minmax(0,1fr)]"
            >
              <dt className="text-bone">
                <InlineText text={term} />
              </dt>
              <dd className="text-foreground">
                <InlineText text={text} />
              </dd>
            </div>
          ))}
        </dl>
      );
    case "figure": {
      const Figure = figures[block.figure];
      return (
        <figure className="my-8">
          <Figure />
          <figcaption className="mt-3 text-sm text-muted-foreground italic">
            <InlineText text={block.caption} />
          </figcaption>
        </figure>
      );
    }
    case "image":
      return (
        <figure className="my-8">
          <Image
            src={block.src}
            alt={block.alt}
            width={block.width}
            height={block.height}
            sizes="(min-width: 1024px) 832px, 100vw"
            className="mx-auto h-auto w-full border-y border-silver/30 bg-card shadow-[0_20px_60px_rgb(0_0_0/0.6)]"
            style={{ maxWidth: block.width }}
          />
          <figcaption className="mt-3 text-sm text-muted-foreground italic">
            <InlineText text={block.caption} />
          </figcaption>
        </figure>
      );
    case "gallery":
      return <ImageTabs items={block.items} />;
    case "code":
      return (
        <pre>
          <code>{block.text}</code>
        </pre>
      );
    case "table":
      return (
        <div className="overflow-x-auto">
          <table>
            <thead>
              <tr>
                {block.head.map((h) => (
                  <th key={h} scope="col">
                    {h}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {block.rows.map((row, r) => (
                <tr key={r}>
                  {row.map((cell, c) => (
                    <td key={c}>
                      <InlineText text={cell} />
                    </td>
                  ))}
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      );
  }
}

export function DocBody({ page }: { page: DocPage }) {
  return (
    <div className="guide">
      {page.sections.map((s) => (
        <section key={s.id} aria-labelledby={s.id}>
          <h2 id={s.id}>
            {s.icon ? (
              <ArtIcon name={s.icon} className="mr-3 inline-block size-10 align-middle" />
            ) : null}
            <a href={`#${s.id}`} className="no-underline! text-inherit! hover:text-bone!">
              {s.title}
            </a>
          </h2>
          <div className="min-w-0">
            {s.blocks.map((b, i) => (
              <DocBlock key={i} block={b} />
            ))}
          </div>
        </section>
      ))}
    </div>
  );
}
