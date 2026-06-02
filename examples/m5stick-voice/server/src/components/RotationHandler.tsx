import { useEffect, useRef } from "react";
import { useFrame, useThree } from "@react-three/fiber";
import * as THREE from "three";

interface RotationHandlerProps {
  groupRef: React.RefObject<THREE.Group | null>;
}

const FRICTION_PER_60FPS_FRAME = 0.94;
const MIN_VELOCITY = 0.0008; // rad/ms below which we stop inertia
const DRAG_TO_RAD = 0.01;
const CLICK_THRESHOLD_PX = 3;

const Y_AXIS = new THREE.Vector3(0, 1, 0);
const X_AXIS = new THREE.Vector3(1, 0, 0);

type DragAxis = "x" | "y" | null;

/**
 * Two-axis pointer drag for the M5Stick group. When a drag engages (after
 * the click threshold) the handler locks to whichever axis had more
 * accumulated movement up to that point and stays on that axis until
 * release — so mid-drag wiggles don't bleed yaw into pitch or vice versa.
 *
 * Below the click threshold (≤ 3 px combined movement) the handler stays
 * passive so R3F's mesh-level `onClick` can still fire on button presses.
 * Both yaw and pitch drags carry momentum after release. Listeners
 * attach to `window` while pressed so dragging off the canvas keeps
 * tracking, without `setPointerCapture` stealing the pointer from R3F's
 * raycaster.
 */
export function RotationHandler({ groupRef }: RotationHandlerProps) {
  const { gl } = useThree();
  const pressedRef = useRef(false);
  const draggingRef = useRef(false);
  const axisRef = useRef<DragAxis>(null);
  const totalDxRef = useRef(0);
  const totalDyRef = useRef(0);
  // Per-axis angular velocity (radians/ms) and recent samples for
  // release-velocity estimation. Yaw = rotation around world Y,
  // pitch = rotation around world X.
  const yawVelRef = useRef(0);
  const yawSamplesRef = useRef<{ d: number; t: number }[]>([]);
  const pitchVelRef = useRef(0);
  const pitchSamplesRef = useRef<{ d: number; t: number }[]>([]);
  const lastXRef = useRef(0);
  const lastYRef = useRef(0);
  const lastTRef = useRef(0);

  useEffect(() => {
    const el = gl.domElement;

    const onWindowMove = (e: PointerEvent) => {
      const group = groupRef.current;
      if (!pressedRef.current || !group) return;
      const dx = e.clientX - lastXRef.current;
      const dy = e.clientY - lastYRef.current;
      const dt = Math.max(1, e.timeStamp - lastTRef.current);
      lastXRef.current = e.clientX;
      lastYRef.current = e.clientY;
      lastTRef.current = e.timeStamp;
      totalDxRef.current += dx;
      totalDyRef.current += dy;

      if (!draggingRef.current) {
        const totalAbs = Math.abs(totalDxRef.current) + Math.abs(totalDyRef.current);
        if (totalAbs < CLICK_THRESHOLD_PX) return;
        draggingRef.current = true;
        // Lock to the axis with more accumulated movement so far. Ties go
        // to the X axis (yaw) since horizontal drags are the more common
        // gesture.
        axisRef.current =
          Math.abs(totalDxRef.current) >= Math.abs(totalDyRef.current) ? "x" : "y";
      }

      if (axisRef.current === "x") {
        if (dx !== 0) {
          group.rotateOnWorldAxis(Y_AXIS, dx * DRAG_TO_RAD);
          const samples = yawSamplesRef.current;
          samples.push({ d: dx, t: dt });
          if (samples.length > 5) samples.shift();
        }
      } else if (axisRef.current === "y") {
        if (dy !== 0) {
          group.rotateOnWorldAxis(X_AXIS, dy * DRAG_TO_RAD);
          const samples = pitchSamplesRef.current;
          samples.push({ d: dy, t: dt });
          if (samples.length > 5) samples.shift();
        }
      }
    };

    const onWindowUp = () => {
      if (!pressedRef.current) return;
      pressedRef.current = false;
      window.removeEventListener("pointermove", onWindowMove);
      window.removeEventListener("pointerup", onWindowUp);
      window.removeEventListener("pointercancel", onWindowUp);
      const wasDragging = draggingRef.current;
      const lockedAxis = axisRef.current;
      draggingRef.current = false;
      axisRef.current = null;
      if (!wasDragging) {
        // Click — no inertia on either axis.
        yawVelRef.current = 0;
        pitchVelRef.current = 0;
        return;
      }
      // Estimate release velocity from recent samples on the locked axis.
      const samples =
        lockedAxis === "x" ? yawSamplesRef.current : pitchSamplesRef.current;
      let releaseVel = 0;
      if (samples.length > 0) {
        let sumD = 0;
        let sumDt = 0;
        for (const s of samples) {
          sumD += s.d;
          sumDt += s.t;
        }
        releaseVel = sumDt > 0 ? (sumD / sumDt) * DRAG_TO_RAD : 0;
      }
      if (lockedAxis === "x") {
        yawVelRef.current = releaseVel;
        pitchVelRef.current = 0;
      } else {
        pitchVelRef.current = releaseVel;
        yawVelRef.current = 0;
      }
    };

    const onPointerDown = (e: PointerEvent) => {
      pressedRef.current = true;
      draggingRef.current = false;
      axisRef.current = null;
      totalDxRef.current = 0;
      totalDyRef.current = 0;
      yawVelRef.current = 0;
      pitchVelRef.current = 0;
      yawSamplesRef.current = [];
      pitchSamplesRef.current = [];
      lastXRef.current = e.clientX;
      lastYRef.current = e.clientY;
      lastTRef.current = e.timeStamp;
      window.addEventListener("pointermove", onWindowMove);
      window.addEventListener("pointerup", onWindowUp);
      window.addEventListener("pointercancel", onWindowUp);
    };

    el.addEventListener("pointerdown", onPointerDown);
    return () => {
      el.removeEventListener("pointerdown", onPointerDown);
      window.removeEventListener("pointermove", onWindowMove);
      window.removeEventListener("pointerup", onWindowUp);
      window.removeEventListener("pointercancel", onWindowUp);
    };
  }, [gl, groupRef]);

  // Decay loop. While pressed/dragging, velocity is set by pointermove
  // and we don't decay; after release, we apply rotation per axis and
  // shrink velocity until it falls below the threshold.
  useFrame((_, deltaSec) => {
    if (draggingRef.current || pressedRef.current) return;
    const group = groupRef.current;
    if (!group) return;
    const dtMs = deltaSec * 1000;
    const factor = Math.pow(FRICTION_PER_60FPS_FRAME, deltaSec * 60);

    const yaw = yawVelRef.current;
    if (Math.abs(yaw) >= MIN_VELOCITY) {
      group.rotateOnWorldAxis(Y_AXIS, yaw * dtMs);
      yawVelRef.current = yaw * factor;
    } else if (yaw !== 0) {
      yawVelRef.current = 0;
    }

    const pitch = pitchVelRef.current;
    if (Math.abs(pitch) >= MIN_VELOCITY) {
      group.rotateOnWorldAxis(X_AXIS, pitch * dtMs);
      pitchVelRef.current = pitch * factor;
    } else if (pitch !== 0) {
      pitchVelRef.current = 0;
    }
  });

  return null;
}
