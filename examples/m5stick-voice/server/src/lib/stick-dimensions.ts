/**
 * Stick device dimensions for the M5StickS3 (48 mm tall × 24 mm wide ×
 * 15 mm deep, portrait). Spec uses an origin at the front face's top-left
 * corner (x = 0..24 right, y = 0..48 down); converted into scene coords
 * (centred, +X right, +Y up, +Z out of the screen) via the helpers below.
 */

/** 1 mm in scene units. The 48 mm long-axis maps to 3 scene units. */
export const UNITS_PER_MM = 0.0625;

const mm = (n: number) => n * UNITS_PER_MM;

// --- Body -----------------------------------------------------------------

export const STICK_WIDTH_MM = 24; // X
export const STICK_HEIGHT_MM = 48; // Y (long axis, portrait)
export const STICK_DEPTH_MM = 15; // Z

export const BODY_WIDTH = mm(STICK_WIDTH_MM);
export const BODY_HEIGHT = mm(STICK_HEIGHT_MM);
export const BODY_DEPTH = mm(STICK_DEPTH_MM);

export const BODY_CORNER_RADIUS_MM = 3;
export const BODY_CORNER_RADIUS = mm(BODY_CORNER_RADIUS_MM);

const BODY_LEFT_X = -BODY_WIDTH / 2;
const BODY_RIGHT_X = BODY_WIDTH / 2;
const BODY_TOP_Y = BODY_HEIGHT / 2;
const BODY_BOTTOM_Y = -BODY_HEIGHT / 2;

const Z_OFFSET = 0.001;
export const FRONT_FACE_Z = BODY_DEPTH / 2 + Z_OFFSET;
export const BACK_FACE_Z = -BODY_DEPTH / 2;

// Convert "spec coords" (origin top-left of front face, x:0..24, y:0..48 down)
// to scene coords (centred, +Y up).
const sceneX = (xTopLeftMm: number) => mm(xTopLeftMm) + BODY_LEFT_X;
const sceneY = (yTopLeftMm: number) => BODY_TOP_Y - mm(yTopLeftMm);

// --- Display --------------------------------------------------------------
// Spec: 15 mm wide × 25 mm high, x≈4.5, y≈4 (top-left).
// Lua canvas is 240×135 (landscape); the texture is rotated 90° in the
// renderer so the long pixel axis runs along the screen's long side.

export const SCREEN_WIDTH = mm(15);
export const SCREEN_HEIGHT = mm(25);

export const SCREEN_X = sceneX(4.5 + 15 / 2); // = 0 (centred)
export const SCREEN_Y = sceneY(4.5 + 25 / 2);
export const SCREEN_Z = FRONT_FACE_Z;

// --- BtnA: pill-shaped front button ---------------------------------------
// Spec: 12.5 × 3 mm capsule, centred horizontally, y ≈ 37.

export const BTN_A_WIDTH = mm(12.5); // along X
export const BTN_A_HEIGHT = mm(3); // along Y
export const BTN_A_THICKNESS = mm(0.8); // along Z (proud)
export const BTN_A_RADIUS = BTN_A_HEIGHT / 2; // capsule end-cap radius

export const BTN_A_X = sceneX(12);
export const BTN_A_Y = sceneY(37);
export const BTN_A_Z = FRONT_FACE_Z + BTN_A_THICKNESS / 2;

// --- BtnB: side button on +X edge ----------------------------------------
// Spec: 6.5 mm long × 5.5 mm high × 1.2 mm proud, lower half (y ≈ 33).

export const BTN_B_LENGTH = mm(6.5); // along Y
export const BTN_B_HEIGHT = mm(5.5); // along Z
export const BTN_B_PROTRUSION = mm(1.2); // along X

export const BTN_B_X = BODY_RIGHT_X + BTN_B_PROTRUSION / 2;
export const BTN_B_Y = sceneY(33);
export const BTN_B_Z = 0;

// --- Bottom-face connectors — outline only --------------------------------
// Two cutouts on the -Y face. USB-C is the smaller capsule (fully rounded
// ends), Grove HY2.0-4P is the larger rectangle. Stacked along Z (depth)
// with USB-C front-biased and Grove back-biased.

export const BOTTOM_USBC_WIDTH = mm(8.5); // along X
export const BOTTOM_USBC_DEPTH = mm(3); // along Z (capsule short axis)
export const BOTTOM_USBC_Z = mm(2.5);

export const BOTTOM_GROVE_WIDTH = mm(9.5); // along X
export const BOTTOM_GROVE_DEPTH = mm(4.5); // along Z
export const BOTTOM_GROVE_Z = -mm(2.5);

export const BOTTOM_FACE_Y = BODY_BOTTOM_Y - Z_OFFSET;

// --- Colours --------------------------------------------------------------

/** Bright tint used for the two interactive user buttons (BtnA + BtnB). */
export const USER_BUTTON_COLOR = "#fb923c"; // tailwind orange-400

// --- Home pose -----------------------------------------------------------

/** Slight backward tilt (around X) to expose the top edge / GPIO header. */
export const HOME_PITCH = -Math.PI / 10;
/** Slight rotation (around Y) so the side BtnB and right edge are visible. */
export const HOME_YAW = Math.PI / 8;
