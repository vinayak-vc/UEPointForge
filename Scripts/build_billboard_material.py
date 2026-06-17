"""
PointForge billboard material — UE 5.5.4 build (Default Lit).

Known UE 5.5.4 Python constraints (verified empirically):
  - Material.get_editor_only_data() doesn't exist on this build.
  - 'expressions' on Material is protected → use delete_all_material_expressions.
  - MaterialProperty enum has NO MP_WORLD_POSITION_OFFSET (and no int/enum cast).
  - Therefore WPO cannot be wired via MaterialEditingLibrary.connect_material_property.

Strategy:
  1. Wipe with delete_all_material_expressions (verified exists on MEL).
  2. Build all nodes (no enum required to CREATE nodes).
  3. Connect BaseColor / Emissive / OpacityMask via standard connect_material_property.
  4. Try to wire WPO via set_editor_property struct mutation, then VERIFY by
     reading back. If verify fails, log a clear instruction for the one-time
     manual drag the user must do in the material editor.

Material setup: Default Lit, Masked, Two-Sided. Base Color + Emissive
(scaled by EmissiveBoost) both driven by the mode-switched color.
"""

import unreal

ASSET = "/Game/PointCloudData/matrial/M_PointCloudUnlit"
mat = unreal.load_asset(ASSET)
if mat is None:
    raise RuntimeError("Material not found: " + ASSET)

mel = unreal.MaterialEditingLibrary

# --- 0. Resolve the property enums that DO exist in 5.5.4 -------------------
def _find_prop(*parts):
    for nm in dir(unreal.MaterialProperty):
        U = nm.upper()
        if all(p in U for p in parts):
            return getattr(unreal.MaterialProperty, nm)
    return None

MP_EMISSIVE    = _find_prop("EMISSIVE")
MP_BASECOLOR   = _find_prop("BASE", "COLOR")
MP_OPACITYMASK = _find_prop("OPACITY", "MASK")
MP_NORMAL      = _find_prop("NORMAL")
missing = [n for n, v in [("emissive", MP_EMISSIVE), ("base_color", MP_BASECOLOR), ("opacity_mask", MP_OPACITYMASK), ("normal", MP_NORMAL)] if v is None]
if missing:
    avail = sorted([n for n in dir(unreal.MaterialProperty) if n.startswith("MP_")])
    unreal.log_error("Missing enums: " + ", ".join(missing))
    unreal.log_error("Available: " + ", ".join(avail))
    raise RuntimeError("Required enums missing.")

# --- 1. Wipe (verified API: delete_all_material_expressions) -----------------
mel.delete_all_material_expressions(mat)

# Disconnect what we can — silently ignore failures.
for p in (MP_EMISSIVE, MP_BASECOLOR, MP_OPACITYMASK):
    try:
        # Some 5.5 builds have disconnect_material_property; many don't. Skip if absent.
        if hasattr(mel, "disconnect_material_property"):
            mel.disconnect_material_property(mat, p)
    except Exception:
        pass

# Settings.
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
mat.set_editor_property("blend_mode",    unreal.BlendMode.BLEND_MASKED)
mat.set_editor_property("two_sided",     True)
mat.set_editor_property("tangent_space_normal", True)

# --- 2. Build helpers --------------------------------------------------------
COL = {"param": -2400, "split": -1900, "basis": -1400, "math": -900,
       "wpo": -400, "emiss": 100, "out": 600}

def E(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)

def scalar(name, default, x, y):
    n = E(unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", default)
    return n

def vec_param(name, default, x, y):
    n = E(unreal.MaterialExpressionVectorParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", default)
    return n

def const(value, x, y):
    n = E(unreal.MaterialExpressionConstant, x, y)
    n.set_editor_property("r", value)
    return n

def const3(rgba, x, y):
    n = E(unreal.MaterialExpressionConstant3Vector, x, y)
    n.set_editor_property("constant", rgba)
    return n

def mask(src, r, g, b, a, x, y):
    n = E(unreal.MaterialExpressionComponentMask, x, y)
    n.set_editor_property("r", r); n.set_editor_property("g", g)
    n.set_editor_property("b", b); n.set_editor_property("a", a)
    mel.connect_material_expressions(src, "", n, "")
    return n

def connect(s, sp, d, dp):
    mel.connect_material_expressions(s, sp, d, dp)

# --- 3. Parameters -----------------------------------------------------------
ps    = scalar("PointSize",      8.0, COL["param"], -900)
atten = scalar("Attenuate",      0.0, COL["param"], -800)
softR = scalar("SoftRound",      0.0, COL["param"], -700)
roundP= scalar("Round",          1.0, COL["param"], -600)
mode  = scalar("ColorMode",      0.0, COL["param"], -500)
emin  = scalar("ElevationMinZ",  0.0, COL["param"], -400)
emax  = scalar("ElevationMaxZ",  1.0, COL["param"], -300)
clipE = scalar("ClipEnable",     0.0, COL["param"], -200)
ebst  = scalar("EmissiveBoost",  0.4, COL["param"], -100)
clipO = vec_param("ClipOrigin",  unreal.LinearColor(0,0,0,0), COL["param"], 0)
clipN = vec_param("ClipNormal",  unreal.LinearColor(0,0,1,0), COL["param"], 100)

# --- 4. Inputs ---------------------------------------------------------------
uv  = E(unreal.MaterialExpressionTextureCoordinate, COL["param"], 250); uv.set_editor_property("coordinate_index", 0)
vc  = E(unreal.MaterialExpressionVertexColor,       COL["param"], 350)
wp  = E(unreal.MaterialExpressionWorldPosition,     COL["param"], 450)
cam = E(unreal.MaterialExpressionCameraPositionWS,  COL["param"], 550)

uvx = mask(uv, True,  False, False, False, COL["split"], 220)
uvy = mask(uv, False, True,  False, False, COL["split"], 280)
wpz = mask(wp, False, False, True,  False, COL["split"], 440)

# View basis (View -> World).
rightVS = const3(unreal.LinearColor(1, 0, 0, 0), COL["split"], 0)
upVS    = const3(unreal.LinearColor(0, 1, 0, 0), COL["split"], 90)
rightWS = E(unreal.MaterialExpressionTransform, COL["basis"], 0)
upWS    = E(unreal.MaterialExpressionTransform, COL["basis"], 90)
for t in (rightWS, upWS):
    t.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_VIEW)
    t.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
connect(rightVS, "", rightWS, "")
connect(upVS,    "", upWS,    "")

# Billboard base offset.
mulR = E(unreal.MaterialExpressionMultiply, COL["math"], 0)
connect(rightWS, "", mulR, "A"); connect(uvx, "", mulR, "B")
mulU = E(unreal.MaterialExpressionMultiply, COL["math"], 80)
connect(upWS, "", mulU, "A"); connect(uvy, "", mulU, "B")
baseOff = E(unreal.MaterialExpressionAdd, COL["math"], 40)
connect(mulR, "", baseOff, "A"); connect(mulU, "", baseOff, "B")

# Size.
# Size based on projected Depth instead of radial distance.
camFwdVS = const3(unreal.LinearColor(1, 0, 0, 0), COL["split"], 180)
camFwdWS = E(unreal.MaterialExpressionTransform, COL["basis"], 180)
camFwdWS.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_VIEW)
camFwdWS.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
connect(camFwdVS, "", camFwdWS, "")

camToWp = E(unreal.MaterialExpressionSubtract, COL["math"], 140)
connect(wp, "", camToWp, "A"); connect(cam, "", camToWp, "B")
depth = E(unreal.MaterialExpressionDotProduct, COL["math"], 180)
connect(camToWp, "", depth, "A"); connect(camFwdWS, "", depth, "B")

# Clamp depth to 10.0 so points don't shrink to 0 when very close.
minDepthC = const(10.0, COL["math"], 240)
clampDepth = E(unreal.MaterialExpressionMax, COL["math"], 220)
connect(depth, "", clampDepth, "A"); connect(minDepthC, "", clampDepth, "B")

kSize = const(0.002, COL["math"], 280)
distK = E(unreal.MaterialExpressionMultiply, COL["math"], 260)
connect(clampDepth, "", distK, "A"); connect(kSize, "", distK, "B")
worldScale = E(unreal.MaterialExpressionMultiply, COL["wpo"], -120)
oneC = const(1.0, COL["wpo"], -80)
connect(ps, "", worldScale, "A"); connect(oneC, "", worldScale, "B")
screenScale = E(unreal.MaterialExpressionMultiply, COL["wpo"], 0)
connect(ps, "", screenScale, "A"); connect(distK, "", screenScale, "B")
sizeScale = E(unreal.MaterialExpressionLinearInterpolate, COL["wpo"], 80)
connect(screenScale, "", sizeScale, "A"); connect(worldScale, "", sizeScale, "B"); connect(atten, "", sizeScale, "Alpha")

# WPO output (the node whose output goes to World Position Offset).
wpo = E(unreal.MaterialExpressionMultiply, COL["wpo"], 200)
connect(baseOff, "", wpo, "A"); connect(sizeScale, "", wpo, "B")
# Rename for the user to identify in the material editor.
try:
    wpo.set_editor_property("desc", "WPO_OUTPUT — connect this to World Position Offset")
except Exception:
    pass

# --- 5. Color mode switch ----------------------------------------------------
# Intensity: ComponentMask on Alpha (reliable — pin-name "A" fails silently in 5.5.4 Python).
intensityAlpha = E(unreal.MaterialExpressionComponentMask, COL["math"], 375)
intensityAlpha.set_editor_property("r", False)
intensityAlpha.set_editor_property("g", False)
intensityAlpha.set_editor_property("b", False)
intensityAlpha.set_editor_property("a", True)
connect(vc, "", intensityAlpha, "")
white = const3(unreal.LinearColor(1, 1, 1, 1), COL["math"], 460)
intensityRGB = E(unreal.MaterialExpressionMultiply, COL["math"], 400)
connect(white, "", intensityRGB, "A"); connect(intensityAlpha, "", intensityRGB, "B")

zSub   = E(unreal.MaterialExpressionSubtract, COL["math"], 520)
connect(wpz, "", zSub, "A"); connect(emin, "", zSub, "B")
rngSub = E(unreal.MaterialExpressionSubtract, COL["math"], 600)
connect(emax, "", rngSub, "A"); connect(emin, "", rngSub, "B")
zDiv   = E(unreal.MaterialExpressionDivide,   COL["math"], 560)
connect(zSub, "", zDiv, "A"); connect(rngSub, "", zDiv, "B")
zSat   = E(unreal.MaterialExpressionSaturate, COL["math"], 640)
connect(zDiv, "", zSat, "")
elevLo = const3(unreal.LinearColor(0.05, 0.20, 1.00, 1), COL["math"], 700)
elevHi = const3(unreal.LinearColor(1.00, 0.20, 0.05, 1), COL["math"], 760)
elevRGB= E(unreal.MaterialExpressionLinearInterpolate, COL["math"], 720)
connect(elevLo, "", elevRGB, "A"); connect(elevHi, "", elevRGB, "B"); connect(zSat, "", elevRGB, "Alpha")

# --- 5b. Classification palette (ASPRS LAS 1.4, read from UV1.X) ----------
# Read UV1.X which holds the classification code as a float.
classUV = E(unreal.MaterialExpressionTextureCoordinate, COL["param"], 650)
classUV.set_editor_property("coordinate_index", 1)
classCode = mask(classUV, True, False, False, False, COL["split"], 650)

# ASPRS classification palette — built as a chain of If(classCode, threshold, A, B, Eq).
# classCode is a float 0..255. We compare with midpoints (e.g. 0.5, 1.5, 2.5, ...)
# to bucket into integer classes.
# Class 0: Never classified — grey
# Class 1: Unassigned       — light grey
# Class 2: Ground           — brown
# Class 3: Low Vegetation   — light green
# Class 4: Medium Vegetation — green
# Class 5: High Vegetation  — dark green
# Class 6: Building         — red
# Class 7: Low Point        — magenta
# Class 8: Reserved/Model Key — dark grey
# Class 9: Water            — blue
# Default (>9)              — yellow

palette = {
    0: unreal.LinearColor(0.50, 0.50, 0.50, 1),   # grey (never classified)
    1: unreal.LinearColor(0.70, 0.70, 0.70, 1),   # light grey (unassigned)
    2: unreal.LinearColor(0.55, 0.35, 0.15, 1),   # brown (ground)
    3: unreal.LinearColor(0.40, 0.80, 0.20, 1),   # light green (low veg)
    4: unreal.LinearColor(0.20, 0.65, 0.10, 1),   # green (med veg)
    5: unreal.LinearColor(0.05, 0.45, 0.05, 1),   # dark green (high veg)
    6: unreal.LinearColor(0.90, 0.15, 0.10, 1),   # red (building)
    7: unreal.LinearColor(0.80, 0.10, 0.80, 1),   # magenta (low point/noise)
    8: unreal.LinearColor(0.35, 0.35, 0.35, 1),   # dark grey (reserved)
    9: unreal.LinearColor(0.10, 0.30, 0.90, 1),   # blue (water)
}
default_class_color = const3(unreal.LinearColor(0.90, 0.85, 0.10, 1), COL["emiss"], -100)  # yellow (other)

# Build the If-chain from the bottom up (highest class first).
prev = default_class_color  # >9 fallback
for cls_id in sorted(palette.keys(), reverse=True):
    col_node = const3(palette[cls_id], COL["emiss"], -100 - (10 - cls_id) * 40)
    threshold = const(cls_id + 0.5, COL["emiss"], -100 - (10 - cls_id) * 40 + 20)
    if_node = E(unreal.MaterialExpressionIf, COL["emiss"], -100 - (10 - cls_id) * 40 + 10)
    connect(classCode, "", if_node, "A")
    connect(threshold, "", if_node, "B")
    connect(prev, "", if_node, "A>B")      # class > threshold → higher class color
    connect(col_node, "", if_node, "A<B")   # class < threshold → this class color
    connect(col_node, "", if_node, "A==B")  # class == threshold → this class color
    prev = if_node

classRGB = prev  # The final If-chain result

# --- 5c. Mode switch chain: RGB(0) → Intensity(1) → Elevation(2) → Classification(3)
modeSat1 = E(unreal.MaterialExpressionSaturate, COL["emiss"], 0)
connect(mode, "", modeSat1, "")
mix1 = E(unreal.MaterialExpressionLinearInterpolate, COL["emiss"], 80)
connect(vc, "", mix1, "A"); connect(intensityRGB, "", mix1, "B"); connect(modeSat1, "", mix1, "Alpha")

oneScalar = const(1.0, COL["emiss"], 160)
modeM1    = E(unreal.MaterialExpressionSubtract, COL["emiss"], 200)
connect(mode, "", modeM1, "A"); connect(oneScalar, "", modeM1, "B")
modeSat2  = E(unreal.MaterialExpressionSaturate, COL["emiss"], 240)
connect(modeM1, "", modeSat2, "")
mix2 = E(unreal.MaterialExpressionLinearInterpolate, COL["emiss"], 320)
connect(mix1, "", mix2, "A"); connect(elevRGB, "", mix2, "B"); connect(modeSat2, "", mix2, "Alpha")

twoScalar = const(2.0, COL["emiss"], 360)
modeM2    = E(unreal.MaterialExpressionSubtract, COL["emiss"], 400)
connect(mode, "", modeM2, "A"); connect(twoScalar, "", modeM2, "B")
modeSat3  = E(unreal.MaterialExpressionSaturate, COL["emiss"], 440)
connect(modeM2, "", modeSat3, "")
finalColor = E(unreal.MaterialExpressionLinearInterpolate, COL["emiss"], 500)
connect(mix2, "", finalColor, "A"); connect(classRGB, "", finalColor, "B"); connect(modeSat3, "", finalColor, "Alpha")

mel.connect_material_property(finalColor, "", MP_BASECOLOR)

# Generate Sphere Normal in Tangent Space for gorgeous lighting
twoC = const(2.0, COL["math"], 320)
uvX2 = E(unreal.MaterialExpressionMultiply, COL["split"], 320)
connect(uvx, "", uvX2, "A"); connect(twoC, "", uvX2, "B")
uvY2 = E(unreal.MaterialExpressionMultiply, COL["split"], 380)
connect(uvy, "", uvY2, "A"); connect(twoC, "", uvY2, "B")

u2 = E(unreal.MaterialExpressionMultiply, COL["math"], 320)
connect(uvX2, "", u2, "A"); connect(uvX2, "", u2, "B")
v2 = E(unreal.MaterialExpressionMultiply, COL["math"], 380)
connect(uvY2, "", v2, "A"); connect(uvY2, "", v2, "B")

uvSq = E(unreal.MaterialExpressionAdd, COL["math"], 420)
connect(u2, "", uvSq, "A"); connect(v2, "", uvSq, "B")

oneNorm = const(1.0, COL["math"], 460)
subUvSq = E(unreal.MaterialExpressionSubtract, COL["math"], 480)
connect(oneNorm, "", subUvSq, "A"); connect(uvSq, "", subUvSq, "B")

zeroNorm = const(0.0, COL["math"], 500)
maxSq = E(unreal.MaterialExpressionMax, COL["math"], 520)
connect(subUvSq, "", maxSq, "A"); connect(zeroNorm, "", maxSq, "B")

zNorm = E(unreal.MaterialExpressionSquareRoot, COL["math"], 560)
connect(maxSq, "", zNorm, "")

app1 = E(unreal.MaterialExpressionAppendVector, COL["out"], 320)
connect(uvX2, "", app1, "A"); connect(uvY2, "", app1, "B")
sphereNormal = E(unreal.MaterialExpressionAppendVector, COL["out"], 400)
connect(app1, "", sphereNormal, "A"); connect(zNorm, "", sphereNormal, "B")

if MP_NORMAL:
    mel.connect_material_property(sphereNormal, "", MP_NORMAL)

# Emissive = finalColor * EmissiveBoost.
emissiveBoosted = E(unreal.MaterialExpressionMultiply, COL["out"], 320)
connect(finalColor, "", emissiveBoosted, "A")
connect(ebst,        "", emissiveBoosted, "B")
mel.connect_material_property(emissiveBoosted, "", MP_EMISSIVE)

# --- 6. Opacity mask ---------------------------------------------------------
uvZero = E(unreal.MaterialExpressionConstant2Vector, COL["split"], 600)
uvZero.set_editor_property("r", 0); uvZero.set_editor_property("g", 0)
uvDist = E(unreal.MaterialExpressionDistance, COL["basis"], 600)
connect(uv, "", uvDist, "A"); connect(uvZero, "", uvDist, "B")
halfC = const(0.5, COL["basis"], 680)
innerEdge = E(unreal.MaterialExpressionSubtract, COL["math"], 680)
connect(halfC, "", innerEdge, "A"); connect(softR, "", innerEdge, "B")
ss = E(unreal.MaterialExpressionSmoothStep, COL["wpo"], 680)
connect(innerEdge, "", ss, "Min"); connect(halfC, "", ss, "Max"); connect(uvDist, "", ss, "Value")
discMask = E(unreal.MaterialExpressionOneMinus, COL["emiss"], 680)
connect(ss, "", discMask, "")
oneForMask = const(1.0, COL["emiss"], 760)
roundedMask = E(unreal.MaterialExpressionLinearInterpolate, COL["emiss"], 720)
connect(oneForMask, "", roundedMask, "A"); connect(discMask, "", roundedMask, "B"); connect(roundP, "", roundedMask, "Alpha")
diff = E(unreal.MaterialExpressionSubtract, COL["split"], 900)
connect(wp, "", diff, "A"); connect(clipO, "", diff, "B")
dp = E(unreal.MaterialExpressionDotProduct, COL["basis"], 900)
connect(diff, "", dp, "A"); connect(clipN, "", dp, "B")
zeroC = const(0.0, COL["math"], 920)
negDp = E(unreal.MaterialExpressionSubtract, COL["math"], 880)
connect(zeroC, "", negDp, "A"); connect(dp, "", negDp, "B")
visibleSide = E(unreal.MaterialExpressionSaturate, COL["wpo"], 880)
connect(negDp, "", visibleSide, "")
oneForClip = const(1.0, COL["wpo"], 940)
clipMul = E(unreal.MaterialExpressionLinearInterpolate, COL["emiss"], 900)
connect(oneForClip, "", clipMul, "A"); connect(visibleSide, "", clipMul, "B"); connect(clipE, "", clipMul, "Alpha")
finalMask = E(unreal.MaterialExpressionMultiply, COL["out"], 720)
connect(roundedMask, "", finalMask, "A"); connect(clipMul, "", finalMask, "B")
mel.connect_material_property(finalMask, "", MP_OPACITYMASK)

# --- 7. WPO — try struct mutation; verify by reading back -------------------
wpo_wired = False
try:
    wpo_input = mat.get_editor_property("world_position_offset")
    wpo_input.set_editor_property("expression", wpo)
    mat.set_editor_property("world_position_offset", wpo_input)
    # Verify
    check = mat.get_editor_property("world_position_offset")
    if check.get_editor_property("expression") is wpo:
        wpo_wired = True
except Exception as e:
    unreal.log_warning(f"WPO struct-mutation path failed: {e}")

mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(ASSET)

if wpo_wired:
    unreal.log("=================================================================")
    unreal.log(" PointForge: M_PointCloudUnlit rebuilt (Default Lit + WPO WIRED).")
    unreal.log("=================================================================")
else:
    unreal.log_warning("==================================================================================")
    unreal.log_warning(" PointForge: rebuilt ALL nodes, but Python can't wire WPO on this 5.5.4 Python build.")
    unreal.log_warning(" Do this ONCE in the editor:")
    unreal.log_warning(f"   1. Open material asset: {ASSET}")
    unreal.log_warning("   2. Find the Multiply node positioned at the right ('wpo' — output of baseOff*sizeScale).")
    unreal.log_warning("   3. Drag a wire from its output to the material's 'World Position Offset' pin (right side).")
    unreal.log_warning("   4. Save the material. Done — re-runs will preserve this wire if you AVOID re-running this script.")
    unreal.log_warning("==================================================================================")
