import { useEffect, useRef } from "react"

interface Props {
  setFrameHandler: (cb: ((buf: ArrayBuffer) => void) | null) => void
}

const N = 512
const NUM_BARS = 48

/**
 * FFT bar visualizer — ported from the original viewer.ts FFT loop. Runs the
 * analyser in a requestAnimationFrame loop and only renders pixels; no React
 * state per frame.
 */
export function FFT({ setFrameHandler }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null)

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext("2d")
    if (!ctx) return

    const win = new Float32Array(N)
    for (let i = 0; i < N; i++) win[i] = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (N - 1)))
    const re = new Float32Array(N)
    const im = new Float32Array(N)
    const mags = new Float32Array(NUM_BARS)
    const bars = new Float32Array(NUM_BARS)
    const ranges: [number, number][] = []
    const half = N / 2
    let prev = 1
    for (let b = 0; b < NUM_BARS; b++) {
      let hi = Math.round(1 * Math.pow(half / 1, (b + 1) / NUM_BARS))
      if (hi <= prev) hi = prev + 1
      if (hi > half) hi = half
      ranges.push([prev, hi])
      prev = hi
    }

    const fft = (re: Float32Array, im: Float32Array) => {
      const n = re.length
      let j = 0
      for (let i = 1; i < n; i++) {
        let bit = n >> 1
        for (; j & bit; bit >>= 1) j ^= bit
        j ^= bit
        if (i < j) {
          ;[re[i], re[j]] = [re[j], re[i]]
          ;[im[i], im[j]] = [im[j], im[i]]
        }
      }
      for (let len = 2; len <= n; len <<= 1) {
        const hlen = len >> 1
        const ang = (-2 * Math.PI) / len
        const wr = Math.cos(ang)
        const wi = Math.sin(ang)
        for (let i = 0; i < n; i += len) {
          let cr = 1
          let ci = 0
          for (let k = 0; k < hlen; k++) {
            const ur = re[i + k]
            const ui = im[i + k]
            const vr = re[i + k + hlen] * cr - im[i + k + hlen] * ci
            const vi = re[i + k + hlen] * ci + im[i + k + hlen] * cr
            re[i + k] = ur + vr
            im[i + k] = ui + vi
            re[i + k + hlen] = ur - vr
            im[i + k + hlen] = ui - vi
            const ncr = cr * wr - ci * wi
            ci = cr * wi + ci * wr
            cr = ncr
          }
        }
      }
    }

    const onFrame = (buf: ArrayBuffer) => {
      const pcm = new Int16Array(buf)
      if (pcm.length < N) return
      for (let i = 0; i < N; i++) { re[i] = (pcm[i] / 32768) * win[i]; im[i] = 0 }
      fft(re, im)
      const scale = N / 4
      for (let b = 0; b < NUM_BARS; b++) {
        const [lo, hi] = ranges[b]
        let peak = 0
        for (let k = lo; k < hi; k++) {
          const mg = Math.sqrt(re[k] * re[k] + im[k] * im[k])
          if (mg > peak) peak = mg
        }
        const v = peak / scale
        const db = 20 * Math.log10(v + 1e-9)
        const norm = (db + 60) / 60
        mags[b] = norm < 0 ? 0 : norm > 1 ? 1 : norm
      }
    }
    setFrameHandler(onFrame)

    const resize = () => {
      const dpr = window.devicePixelRatio || 1
      canvas.width = canvas.clientWidth * dpr
      canvas.height = canvas.clientHeight * dpr
    }
    window.addEventListener("resize", resize)
    resize()

    let raf = 0
    const draw = () => {
      const w = canvas.width
      const h = canvas.height
      const dpr = window.devicePixelRatio || 1
      ctx.clearRect(0, 0, w, h)
      const gap = 2 * dpr
      const bw = (w - gap * (NUM_BARS + 1)) / NUM_BARS
      for (let b = 0; b < NUM_BARS; b++) {
        const target = mags[b]
        bars[b] += (target - bars[b]) * (target > bars[b] ? 0.6 : 0.12)
        const bh = bars[b] * h
        const x = gap + b * (bw + gap)
        const hue = 200 - 160 * (b / NUM_BARS)
        ctx.fillStyle = `hsl(${hue},80%,${40 + 30 * bars[b]}%)`
        ctx.fillRect(x, h - bh, bw, bh)
      }
      raf = requestAnimationFrame(draw)
    }
    raf = requestAnimationFrame(draw)

    return () => {
      cancelAnimationFrame(raf)
      window.removeEventListener("resize", resize)
      setFrameHandler(null)
    }
  }, [setFrameHandler])

  return (
    <div style={{
      height: 120,
    }}>
      <canvas ref={canvasRef} style={{ width: "100%", height: "100%", display: "block" }} />
    </div>
  )
}
