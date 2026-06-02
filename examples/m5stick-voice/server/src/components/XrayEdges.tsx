import { Edges } from "@react-three/drei";

interface XrayEdgesProps {
  /** Screen-pixel width for both passes. */
  lineWidth?: number;
  /** Front-facing colour (depth-tested). */
  color?: string;
  /** Back-facing "x-ray" colour (drawn under, no depth test). */
  dimColor?: string;
  /** Dihedral angle (degrees) above which an edge is drawn. Higher values
   *  suppress small-angle bevel segments (e.g. on rounded extrusions). */
  threshold?: number;
}

/**
 * Two-pass wireframe edges for a parent mesh: a dim pass drawn first with
 * depth test disabled (so the back edges bleed through the body), then a
 * bright pass drawn after the body with normal depth testing (so only
 * front-facing edges survive).
 *
 * Use as a child of any `<mesh>`; drei's `<Edges>` pulls geometry from the
 * nearest parent mesh. The parent's material should write depth but can have
 * `colorWrite: false` so the body itself is invisible.
 */
export function XrayEdges({
  lineWidth = 2,
  color = "#ffffff",
  dimColor = "#666666",
  threshold = 15,
}: XrayEdgesProps) {
  return (
    <>
      <Edges
        color={dimColor}
        lineWidth={lineWidth}
        renderOrder={-1}
        depthTest={false}
        depthWrite={false}
        threshold={threshold}
      />
      <Edges
        color={color}
        lineWidth={lineWidth}
        renderOrder={1}
        threshold={threshold}
      />
    </>
  );
}
