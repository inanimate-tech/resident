interface Props {
  css: string
}

/**
 * M1: a full-viewport layer the realtime model paints via apply_css. We render
 * the agent-supplied CSS into a `<style>` element AFTER the #bg div, so its
 * rules override #bg's default (set in the root route's static style block).
 * #bg has no inline background — inline always beats a stylesheet, which
 * would silently neutralise the model's apply_css call.
 */
export function Background({ css }: Props) {
  return (
    <>
      <div id="bg" style={{ position: "fixed", inset: 0, zIndex: -1 }} />
      <style>{css}</style>
    </>
  )
}
