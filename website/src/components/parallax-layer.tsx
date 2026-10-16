"use client";

import { motion, useReducedMotion, useScroll, useSpring, useTransform } from "motion/react";

/*
 * One layer of the backdrop that drifts as the page scrolls. `depth` is how far it moves, in
 * viewport heights, between the top of the page and the bottom, so the speed stays slow on any
 * page length and the motion never stops before the end. Far layers get a small depth, near ones
 * a larger one; a negative depth moves the layer up. A spring smooths the wheel's steps.
 */
export function ParallaxLayer({ depth, children }: { depth: number; children: React.ReactNode }) {
  const reduce = useReducedMotion();
  const { scrollYProgress } = useScroll();
  const smooth = useSpring(scrollYProgress, { stiffness: 60, damping: 20, mass: 0.6 });
  const y = useTransform(smooth, [0, 1], ["0vh", `${depth}vh`]);
  return (
    <motion.div className="backdrop-layer" style={reduce ? undefined : { y }}>
      {children}
    </motion.div>
  );
}
