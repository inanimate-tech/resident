import { createFileRoute } from "@tanstack/react-router"
import { useVoiceMonitor } from "../hooks/useVoiceMonitor"
import { Header } from "../components/Header"
import { Transcript } from "../components/Transcript"
import { FFT } from "../components/FFT"
import { Background } from "../components/Background"

export const Route = createFileRoute("/devices/$deviceId")({
  component: DeviceViewer,
})

function DeviceViewer() {
  const { deviceId } = Route.useParams()
  const { status, transcript, css, setFrameHandler } = useVoiceMonitor(deviceId)

  return (
    <>
      <Background css={css} />
      <Header deviceId={deviceId} status={status} agentStatus="idle" />
      <Transcript items={transcript} />
      <FFT setFrameHandler={setFrameHandler} />
    </>
  )
}
