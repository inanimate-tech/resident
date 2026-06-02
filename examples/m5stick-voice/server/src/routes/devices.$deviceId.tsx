import { createFileRoute } from "@tanstack/react-router"
import { useVoiceMonitor } from "../hooks/useVoiceMonitor"
import { Header } from "../components/Header"
import { Transcript } from "../components/Transcript"
import { FFT } from "../components/FFT"
import { MicSim } from "../components/MicSim"

export const Route = createFileRoute("/devices/$deviceId")({
  component: DeviceViewer,
})

function DeviceViewer() {
  const { deviceId } = Route.useParams()
  const { status, transcript, agentStatus, agentMessage, setFrameHandler } =
    useVoiceMonitor(deviceId)

  return (
    <>
      <Header
        deviceId={deviceId}
        status={status}
        agentStatus={agentStatus}
        agentMessage={agentMessage}
      />
      <Transcript items={transcript} />
      <FFT setFrameHandler={setFrameHandler} />
      <MicSim deviceId={deviceId} />
    </>
  )
}
