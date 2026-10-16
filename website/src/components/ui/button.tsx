import * as React from "react";
import { Slot } from "radix-ui";
import { cva, type VariantProps } from "class-variance-authority";

import { cn } from "@/lib/utils";

// shadcn/ui button in the look of the launcher's: a slate plate with cut corners and a thin gold
// edge that brightens on hover. No glow.
const buttonVariants = cva(
  "relative inline-flex shrink-0 items-center justify-center gap-2 whitespace-nowrap font-serif text-lg transition-[color,background-color,border-color,box-shadow] duration-300 outline-none disabled:pointer-events-none disabled:opacity-50 focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background [&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*='size-'])]:size-4",
  {
    variants: {
      variant: {
        default:
          "cut-corners tracking-[0.06em] text-bone [--cut-edge:rgb(217_184_120/0.55)] [--cut-fill:linear-gradient(180deg,#262a31,#14171c)] hover:[--cut-edge:#d9b878] hover:[--cut-fill:linear-gradient(180deg,#2d323a,#181b21)]",
        outline:
          "cut-corners text-foreground [--cut-edge:#4a505a] [--cut-fill:#12151a] hover:text-bone hover:[--cut-edge:rgb(217_184_120/0.7)]",
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
