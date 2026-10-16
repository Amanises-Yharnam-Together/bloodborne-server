import * as React from "react";
import { Slot } from "radix-ui";
import { cva, type VariantProps } from "class-variance-authority";

import { cn } from "@/lib/utils";

// shadcn/ui button, restyled after Bloodborne's menus: square corners, engraved caps, a thin
// gilt rule and a soft band of light on focus and hover instead of a filled highlight.
const buttonVariants = cva(
  "relative inline-flex shrink-0 items-center justify-center gap-2 whitespace-nowrap font-display text-sm uppercase tracking-[0.18em] transition-[color,background-color,border-color,box-shadow] duration-300 outline-none disabled:pointer-events-none disabled:opacity-50 focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background [&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*='size-'])]:size-4",
  {
    variants: {
      variant: {
        default:
          "border border-gilt/70 bg-primary text-primary-foreground shadow-[inset_0_0_0_1px_rgb(0_0_0/0.6),inset_0_0_24px_rgb(0_0_0/0.45)] hover:border-gilt hover:bg-blood-bright hover:shadow-[inset_0_0_0_1px_rgb(0_0_0/0.6),0_0_24px_rgb(142_27_27/0.45)]",
        outline:
          "border border-gilt/45 bg-transparent text-foreground hover:border-gilt hover:bg-[linear-gradient(90deg,transparent,rgb(214_196_158/0.12),transparent)] hover:text-bone",
        ghost:
          "text-muted-foreground hover:text-foreground hover:bg-[linear-gradient(90deg,transparent,rgb(214_196_158/0.08),transparent)]",
        link: "font-body normal-case tracking-normal text-gilt underline-offset-4 hover:underline",
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
