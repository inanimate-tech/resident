import { createFileRoute } from "@tanstack/react-router"
import { useVoiceMonitor } from "../hooks/useVoiceMonitor"
import { Header } from "../components/Header"
import { Transcript } from "../components/Transcript"
import { FFT } from "../components/FFT"
import { Background } from "../components/Background"
import { SimM5Stick } from "../components/SimM5Stick"
import { MicSim } from "../components/MicSim"
import { DoneToast } from "../components/DoneToast"
import { DEFAULT_APP } from "../lib/default-app"

export const Route = createFileRoute("/devices/$deviceId")({
  component: DeviceViewer,
})

function DeviceViewer() {
  const { deviceId } = Route.useParams()
  const {
    status, transcript, agentStatus, workingLines, retryCount,
    lastDone, dismissDone, currentApp, css, setFrameHandler,
  } = useVoiceMonitor(deviceId)

  const simCode = currentApp?.code ?? DEFAULT_APP
  const simVersion = currentApp?.version ?? 0

  return (
    <>
      <Background css={css} />
      <Header
        deviceId={deviceId}
        status={status}
        agentStatus={agentStatus}
        workingLines={workingLines}
        retryCount={retryCount}
      />
      <SimM5Stick code={simCode} version={simVersion} />
      <Transcript items={transcript} />
      <FFT setFrameHandler={setFrameHandler} />
      <MicSim deviceId={deviceId} />
      {lastDone && (
        <DoneToast
          success={lastDone.success}
          message={lastDone.message}
          onDismiss={dismissDone}
        />
      )}
    </>
  )
}
