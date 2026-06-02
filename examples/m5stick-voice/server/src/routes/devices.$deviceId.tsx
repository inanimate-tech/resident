import { createFileRoute } from "@tanstack/react-router"
import { useVoiceMonitor } from "../hooks/useVoiceMonitor"
import { Header } from "../components/Header"
import { Transcript } from "../components/Transcript"
import { FFT } from "../components/FFT"
import { SimM5Stick } from "../components/SimM5Stick"
import { MicSim } from "../components/MicSim"

export const Route = createFileRoute("/devices/$deviceId")({
  component: DeviceViewer,
})

function DeviceViewer() {
  const { deviceId } = Route.useParams()
  const {
    status, transcript, agentStatus, agentMessage, currentApp, setFrameHandler,
  } = useVoiceMonitor(deviceId)

  return (
    <>
      <Header
        deviceId={deviceId}
        status={status}
        agentStatus={agentStatus}
        agentMessage={agentMessage}
      />
      <SimM5Stick code={currentApp?.code ?? null} version={currentApp?.version ?? 0} />
      <Transcript items={transcript} />
      <FFT setFrameHandler={setFrameHandler} />
      <MicSim deviceId={deviceId} />
    </>
  )
}
