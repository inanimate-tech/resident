interface Props {
  css: string
}

/**
 * M1: a full-viewport layer the realtime model paints via apply_css. We render
 * the agent-supplied CSS into an inline <style> element and a fixed-position
 * #bg div behind everything else, matching the original viewer markup so the
 * model's prompt about "#bg" continues to work unchanged.
 */
export function Background({ css }: Props) {
  return (
    <>
      <style>{css}</style>
      <div id="bg" style={{ position: "fixed", inset: 0, zIndex: -1, background: "#0b0b10" }} />
    </>
  )
}
