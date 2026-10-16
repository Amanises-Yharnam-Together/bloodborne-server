import * as React from "react";
import { Slot } from "radix-ui";
import { cva, type VariantProps } from "class-variance-authority";

import { cn } from "@/lib/utils";

// shadcn/ui button in the look of the game's team screen buttons: a dark plate, a thin gold
// edge and the menu glow on hover.
const buttonVariants = cva(
  "relative inline-flex shrink-0 items-center justify-center gap-2 whitespace-nowrap font-serif text-lg transition-[color,background-color,border-color,box-shadow] duration-300 outline-none disabled:pointer-events-none disabled:opacity-50 focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background [&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*='size-'])]:size-4",
  {
    variants: {
      variant: {
        default:
          "border border-title/60 bg-[linear-gradient(180deg,#2b2318,#120f0b)] text-bone shadow-[inset_0_0_0_1px_rgb(0_0_0/0.7)] hover:border-title hover:shadow-[inset_0_0_0_1px_rgb(0_0_0/0.7),0_0_30px_var(--glow)]",
        outline:
          "border border-silver/30 bg-black/30 text-foreground hover:border-silver/60 hover:bg-[radial-gradient(ellipse_70%_80%_at_50%_60%,var(--glow),transparent_75%)] hover:text-bone",
        ghost:
          "text-muted-foreground hover:text-foreground hover:bg-[linear-gradient(90deg,transparent,rgb(214_196_158/0.08),transparent)]",
        link: "text-title underline-offset-4 hover:underline",
      },
      size: {
        default: "h-11 px-6",
        sm: "h-9 px-4 text-xs",
        lg: "h-13 px-9 text-base",
        icon: "size-10",
      },
    },
    defaultVariants: {
      variant: "default",
      size: "default",
    },
  },
);

function Button({
  className,
  variant,
  size,
  asChild = false,
  ...props
}: React.ComponentProps<"button"> &
  VariantProps<typeof buttonVariants> & {
    asChild?: boolean;
  }) {
  const Comp = asChild ? Slot.Root : "button";

  return (
    <Comp
      data-slot="button"
      className={cn(buttonVariants({ variant, size, className }))}
      {...props}
    />
  );
}

export { Button, buttonVariants };
