import Image from "next/image";

import { figures } from "@/components/figures";
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
        <aside className="frame my-6 bg-card/60 px-5 py-4 text-base text-muted-foreground">
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
    case "figure": {
      const Figure = figures[block.figure];
      return (
        <figure className="my-8">
          <Figure />
          <figcaption className="mt-3 text-center text-sm text-muted-foreground">
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
            sizes="(min-width: 1024px) 720px, 100vw"
            className="frame h-auto w-full bg-card"
          />
          <figcaption className="mt-3 text-center text-sm text-muted-foreground">
            <InlineText text={block.caption} />
          </figcaption>
        </figure>
      );
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
    <div className="prose-hunter">
      {page.sections.map((s) => (
        <section key={s.id} aria-labelledby={s.id}>
          <h2 id={s.id}>
            <a href={`#${s.id}`} className="no-underline! text-inherit!">
              {s.title}
            </a>
          </h2>
          {s.blocks.map((b, i) => (
            <DocBlock key={i} block={b} />
          ))}
        </section>
      ))}
    </div>
  );
}
