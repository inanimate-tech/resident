import { useEffect, useMemo } from "react";
import { useFrame, type ThreeEvent } from "@react-three/fiber";
import * as THREE from "three";
import { XrayEdges } from "./XrayEdges";
import {
  BODY_WIDTH,
  BODY_HEIGHT,
  BODY_DEPTH,
  BODY_CORNER_RADIUS,
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  SCREEN_X,
  SCREEN_Y,
  SCREEN_Z,
  BTN_A_X,
  BTN_A_Y,
  BTN_A_Z,
  BTN_A_WIDTH,
  BTN_A_HEIGHT,
  BTN_A_THICKNESS,
  BTN_B_X,
  BTN_B_Y,
  BTN_B_Z,
  BTN_B_LENGTH,
  BTN_B_HEIGHT,
  BTN_B_PROTRUSION,
  BOTTOM_USBC_WIDTH,
  BOTTOM_USBC_DEPTH,
  BOTTOM_USBC_Z,
  BOTTOM_GROVE_WIDTH,
  BOTTOM_GROVE_DEPTH,
  BOTTOM_GROVE_Z,
  BOTTOM_FACE_Y,
  USER_BUTTON_COLOR,
  HOME_PITCH,
  HOME_YAW,
} from "../lib/stick-dimensions";

interface StickDeviceBodyProps {
  groupRef: React.RefObject<THREE.Group | null>;
  /** The 2D canvas the Lua runtime paints into — sampled as a CanvasTexture. */
  screenCanvas: HTMLCanvasElement | null;
  /** Click handler for the front pill button (BtnA). */
  onPressBtnA?: () => void;
  /** Click handler for the side button (BtnB). */
  onPressBtnB?: () => void;
}

/**
 * Body silhouette: rounded-corner rectangle.
 */
function bodyShape(): THREE.Shape {
  const w = BODY_WIDTH;
  const h = BODY_HEIGHT;
  const r = BODY_CORNER_RADIUS;
  const s = new THREE.Shape();
  s.moveTo(-w / 2 + r, -h / 2);
  s.lineTo(w / 2 - r, -h / 2);
  s.absarc(w / 2 - r, -h / 2 + r, r, -Math.PI / 2, 0, false);
  s.lineTo(w / 2, h / 2 - r);
  s.absarc(w / 2 - r, h / 2 - r, r, 0, Math.PI / 2, false);
  s.lineTo(-w / 2 + r, h / 2);
  s.absarc(-w / 2 + r, h / 2 - r, r, Math.PI / 2, Math.PI, false);
  s.lineTo(-w / 2, -h / 2 + r);
  s.absarc(-w / 2 + r, -h / 2 + r, r, Math.PI, (3 * Math.PI) / 2, false);
  return s;
}

/**
 * Capsule/pill shape: a rectangle with semicircular ends along the X axis.
 */
function capsuleShape(width: number, height: number): THREE.Shape {
  const r = height / 2;
  const s = new THREE.Shape();
  s.moveTo(-width / 2 + r, -r);
  s.lineTo(width / 2 - r, -r);
  s.absarc(width / 2 - r, 0, r, -Math.PI / 2, Math.PI / 2, false);
  s.lineTo(-width / 2 + r, r);
  s.absarc(-width / 2 + r, 0, r, Math.PI / 2, (3 * Math.PI) / 2, false);
  return s;
}

const setCursorPointer = (e: ThreeEvent<PointerEvent>) => {
  e.stopPropagation();
  document.body.style.cursor = "pointer";
};
const clearCursor = () => {
  document.body.style.cursor = "";
};

/**
 * 3D line-art model of the M5StickS3. Portrait body with rounded front
 * corners and a deep U-shaped Grove/USB-C cutout in the bottom edge,
 * a 1.14" portrait LCD, a coloured capsule front button (BtnA), a coloured
 * side button (BtnB), and an outline-only connector block sitting inside
 * the U-cut.
 */
export function StickDeviceBody({
  groupRef,
  screenCanvas,
  onPressBtnA,
  onPressBtnB,
}: StickDeviceBodyProps) {
  // Body — extruded rounded rectangle, centred about Z so the front face
  // sits at +BODY_DEPTH/2. curveSegments=3 keeps the polyline chunky enough
  // that the rounded corners' segment-to-segment dihedrals (30°) pass the
  // edge threshold and connect front+back outlines.
  const bodyGeo = useMemo(() => {
    const geo = new THREE.ExtrudeGeometry(bodyShape(), {
      depth: BODY_DEPTH,
      bevelEnabled: false,
      curveSegments: 3,
    });
    geo.translate(0, 0, -BODY_DEPTH / 2);
    return geo;
  }, []);

  const screenGeo = useMemo(
    () => new THREE.PlaneGeometry(SCREEN_WIDTH, SCREEN_HEIGHT),
    [],
  );

  // BtnA — extruded capsule, centred about Z so its lower disk is flush.
  const btnAGeo = useMemo(() => {
    const geo = new THREE.ExtrudeGeometry(
      capsuleShape(BTN_A_WIDTH, BTN_A_HEIGHT),
      { depth: BTN_A_THICKNESS, bevelEnabled: false, curveSegments: 8 },
    );
    geo.translate(0, 0, -BTN_A_THICKNESS / 2);
    return geo;
  }, []);

  // BtnB — small box on the +X edge.
  const btnBGeo = useMemo(
    () => new THREE.BoxGeometry(BTN_B_PROTRUSION, BTN_B_LENGTH, BTN_B_HEIGHT),
    [],
  );

  // USB-C — outline-only capsule on the bottom face.
  const usbcGeo = useMemo(
    () =>
      new THREE.ShapeGeometry(capsuleShape(BOTTOM_USBC_WIDTH, BOTTOM_USBC_DEPTH)),
    [],
  );

  // Grove — outline-only rectangle on the bottom face.
  const groveGeo = useMemo(
    () => new THREE.PlaneGeometry(BOTTOM_GROVE_WIDTH, BOTTOM_GROVE_DEPTH),
    [],
  );

  // Live texture sourced from the hidden 2D canvas the Lua runtime paints into.
  // Rotated 90° so the canvas's long pixel axis (240) maps to the screen's
  // long physical axis (portrait).
  const screenTexture = useMemo(() => {
    if (!screenCanvas) return null;
    const t = new THREE.CanvasTexture(screenCanvas);
    t.minFilter = THREE.NearestFilter;
    t.magFilter = THREE.NearestFilter;
    t.center.set(0.5, 0.5);
    t.rotation = -Math.PI / 2;
    return t;
  }, [screenCanvas]);

  useEffect(() => {
    return () => {
      bodyGeo.dispose();
      screenGeo.dispose();
      btnAGeo.dispose();
      btnBGeo.dispose();
      usbcGeo.dispose();
      groveGeo.dispose();
    };
  }, [bodyGeo, screenGeo, btnAGeo, btnBGeo, usbcGeo, groveGeo]);

  useEffect(() => {
    return () => {
      screenTexture?.dispose();
    };
  }, [screenTexture]);

  // Reset body cursor on unmount in case the pointer was over a button when
  // the component disappeared.
  useEffect(() => clearCursor, []);

  // Set the home pose once on mount. Subsequent rotations from
  // RotationHandler spin around world Y on top of this base.
  useEffect(() => {
    const group = groupRef.current;
    if (!group) return;
    group.rotation.set(0, 0, 0);
    group.rotateOnWorldAxis(new THREE.Vector3(1, 0, 0), HOME_PITCH);
    group.rotateOnWorldAxis(new THREE.Vector3(0, 1, 0), HOME_YAW);
  }, [groupRef]);

  // Mark the canvas texture dirty each frame so the screen reflects whatever
  // the Lua runtime painted on the hidden 2D canvas.
  useFrame(() => {
    if (screenTexture) {
      screenTexture.needsUpdate = true;
    }
  });

  const handleClickBtnA = (e: ThreeEvent<MouseEvent>) => {
    e.stopPropagation();
    onPressBtnA?.();
  };
  const handleClickBtnB = (e: ThreeEvent<MouseEvent>) => {
    e.stopPropagation();
    onPressBtnB?.();
  };

  return (
    <group ref={groupRef}>
      {/* Inner orientation group — geometry is authored in portrait coords
          (long axis = Y), but the scene shows it landscape (long axis = X).
          A static +π/2 roll around Z reorients the whole device without
          touching any of the underlying spec-coord positions. */}
      <group rotation={[0, 0, Math.PI / 2]}>
      {/* Body. Invisible (colorWrite off) but writes depth — gates the bright
          edge pass + screen plane so back-facing geometry is occluded. The
          edge threshold (10°) lets the curve-segment dihedrals (30°) draw,
          giving visible vertical lines on each rounded corner that connect
          the front and back face outlines. */}
      <mesh geometry={bodyGeo}>
        <meshBasicMaterial colorWrite={false} />
        <XrayEdges threshold={10} />
      </mesh>

      {/* Display — black/glass plane on the front face, displaying the live
          Lua canvas (rotated to portrait). */}
      <mesh position={[SCREEN_X, SCREEN_Y, SCREEN_Z]} geometry={screenGeo}>
        {screenTexture ? (
          <meshBasicMaterial map={screenTexture} />
        ) : (
          <meshBasicMaterial color="#808080" />
        )}
      </mesh>

      {/* BtnA — coloured capsule pill on the front face, just below the screen. */}
      <mesh
        position={[BTN_A_X, BTN_A_Y, BTN_A_Z]}
        geometry={btnAGeo}
        onClick={handleClickBtnA}
        onPointerOver={setCursorPointer}
        onPointerOut={clearCursor}
      >
        <meshBasicMaterial color={USER_BUTTON_COLOR} />
        <XrayEdges color="#ffffff" dimColor="#7c2d12" threshold={15} />
      </mesh>

      {/* BtnB — coloured side button protruding from the +X long edge. */}
      <mesh
        position={[BTN_B_X, BTN_B_Y, BTN_B_Z]}
        geometry={btnBGeo}
        onClick={handleClickBtnB}
        onPointerOver={setCursorPointer}
        onPointerOut={clearCursor}
      >
        <meshBasicMaterial color={USER_BUTTON_COLOR} />
        <XrayEdges color="#ffffff" dimColor="#7c2d12" />
      </mesh>

      {/* USB-C socket — outline-only capsule on the -Y face, front-biased. */}
      <mesh
        position={[0, BOTTOM_FACE_Y, BOTTOM_USBC_Z]}
        rotation={[Math.PI / 2, 0, 0]}
        geometry={usbcGeo}
      >
        <meshBasicMaterial colorWrite={false} />
        <XrayEdges />
      </mesh>

      {/* Grove HY2.0 socket — outline-only rectangle on the -Y face, back-biased. */}
      <mesh
        position={[0, BOTTOM_FACE_Y, BOTTOM_GROVE_Z]}
        rotation={[Math.PI / 2, 0, 0]}
        geometry={groveGeo}
      >
        <meshBasicMaterial colorWrite={false} />
        <XrayEdges />
      </mesh>
      </group>
    </group>
  );
}
