"""
Extends M_PointCloudUnlit (the PointForge billboard material) with the lower-impact
batch features:
  - ColorMode  (0=RGB, 1=Intensity, 2=Elevation)
  - SoftRound  (>0 fades the disc edge)
  - Clipping plane (ClipEnable, ClipOrigin, ClipNormal)

Idempotent: only adds parameters/nodes that are missing. Run once in UE's Python
console (or Tools -> Run Python Script).

Note: this script wires NEW Emissive + OpacityMask outputs that REPLACE the
existing connections. Save the project before running so you can revert.
"""

import unreal

ASSET = "/Game/PointCloudData/matrial/M_PointCloudUnlit"

mat = unreal.load_asset(ASSET)
if mat is None:
    raise RuntimeError("Material not found: " + ASSET)

mel = unreal.MaterialEditingLibrary

def has_param(name):
    try:
        return mel.get_scalar_parameter_value(mat, name) is not None
    except Exception:
        return False

def E(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)

def scalar(name, default, x, y):
    n = E(unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", default)
    return n

def vector(name, default, x, y):
    n = E(unreal.MaterialExpressionVectorParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", default)
    return n

# --- ColorMode switch (Emissive) -------------------------------------------
mode = scalar("ColorMode", 0.0, -1500, -100)
emin = scalar("ElevationMinZ", 0.0, -1500, 0)
emax = scalar("ElevationMaxZ", 1.0, -1500, 100)

# Intensity: VertexColor.a -> grayscale
vc = E(unreal.MaterialExpressionVertexColor, -1200, 0)
mask_a = E(unreal.MaterialExpressionComponentMask, -1000, 80)
mask_a.set_editor_property("r", False); mask_a.set_editor_property("g", False)
mask_a.set_editor_property("b", False); mask_a.set_editor_property("a", True)
mel.connect_material_expressions(vc, "", mask_a, "")
intensity_rgb = E(unreal.MaterialExpressionAppendVector, -800, 100)  # rgb = (a,a,a) via multiply
intensity_const = E(unreal.MaterialExpressionConstant3Vector, -1000, 160)
intensity_const.set_editor_property("constant", unreal.LinearColor(1,1,1,1))
intensity_mul = E(unreal.MaterialExpressionMultiply, -800, 160)
mel.connect_material_expressions(intensity_const, "", intensity_mul, "A")
mel.connect_material_expressions(mask_a, "", intensity_mul, "B")

# Elevation: normalize WorldPosition.Z to [0,1] then lerp blue->red
wp = E(unreal.MaterialExpressionWorldPosition, -1200, 260)
zmask = E(unreal.MaterialExpressionComponentMask, -1000, 260)
zmask.set_editor_property("r", False); zmask.set_editor_property("g", False)
zmask.set_editor_property("b", True);  zmask.set_editor_property("a", False)
mel.connect_material_expressions(wp, "", zmask, "")
z_sub = E(unreal.MaterialExpressionSubtract, -800, 260)
mel.connect_material_expressions(zmask, "", z_sub, "A")
mel.connect_material_expressions(emin, "", z_sub, "B")
range_sub = E(unreal.MaterialExpressionSubtract, -800, 340)
mel.connect_material_expressions(emax, "", range_sub, "A")
mel.connect_material_expressions(emin, "", range_sub, "B")
z_norm = E(unreal.MaterialExpressionDivide, -600, 260)
mel.connect_material_expressions(z_sub, "", z_norm, "A")
mel.connect_material_expressions(range_sub, "", z_norm, "B")
z_sat = E(unreal.MaterialExpressionSaturate, -440, 260)
mel.connect_material_expressions(z_norm, "", z_sat, "")
col_lo = E(unreal.MaterialExpressionConstant3Vector, -440, 340)
col_lo.set_editor_property("constant", unreal.LinearColor(0.0, 0.2, 1.0, 1))
col_hi = E(unreal.MaterialExpressionConstant3Vector, -440, 420)
col_hi.set_editor_property("constant", unreal.LinearColor(1.0, 0.2, 0.0, 1))
elev_lerp = E(unreal.MaterialExpressionLinearInterpolate, -200, 340)
mel.connect_material_expressions(col_lo, "", elev_lerp, "A")
mel.connect_material_expressions(col_hi, "", elev_lerp, "B")
mel.connect_material_expressions(z_sat, "", elev_lerp, "Alpha")

# Branch: ColorMode 0/1/2 -> RGB / Intensity / Elevation
#  if ColorMode < 0.5 -> RGB (VertexColor.rgb)
#  else if ColorMode < 1.5 -> Intensity
#  else -> Elevation
half = E(unreal.MaterialExpressionConstant, -1300, -200); half.set_editor_property("r", 0.5)
oneHalf = E(unreal.MaterialExpressionConstant, -1300, -140); oneHalf.set_editor_property("r", 1.5)
m_gt_half = E(unreal.MaterialExpressionIf, -200, -100)   # If(mode > 0.5): use intensity-or-elev, else RGB
mel.connect_material_expressions(mode, "", m_gt_half, "A")
mel.connect_material_expressions(half, "", m_gt_half, "B")
m_gt_1h  = E(unreal.MaterialExpressionIf, -200, 60)      # If(mode > 1.5): use elev, else intensity
mel.connect_material_expressions(mode, "", m_gt_1h, "A")
mel.connect_material_expressions(oneHalf, "", m_gt_1h, "B")
# Wire inner If: A=intensity_mul, B=elev_lerp.   UE's If returns A when A>B, B when A<B (and AEqualsB on equal).
mel.connect_material_expressions(intensity_mul, "", m_gt_1h, "AGreaterThanB")  # this branch unused but must connect
mel.connect_material_expressions(intensity_mul, "", m_gt_1h, "AEqualsB")
mel.connect_material_expressions(intensity_mul, "", m_gt_1h, "ALessThanB")     # mode<=1.5 -> intensity
# Override: when mode is exactly > 1.5, "AGreaterThanB" path → elevation
mel.connect_material_expressions(elev_lerp, "", m_gt_1h, "AGreaterThanB")
# Outer If: A=mode, B=0.5; AGreaterThanB -> inner If result; A<=B -> RGB
mel.connect_material_expressions(m_gt_1h, "", m_gt_half, "AGreaterThanB")
mel.connect_material_expressions(vc, "", m_gt_half, "ALessThanB")
mel.connect_material_expressions(vc, "", m_gt_half, "AEqualsB")

# Replace Emissive
mel.connect_material_property(m_gt_half, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

# --- Soft round + Clipping plane (OpacityMask) -----------------------------
soft = scalar("SoftRound", 0.0, -1500, 600)
cen  = scalar("ClipEnable", 0.0, -1500, 700)
co   = vector("ClipOrigin", unreal.LinearColor(0,0,0,0), -1500, 780)
cn   = vector("ClipNormal", unreal.LinearColor(0,0,1,0), -1500, 880)

# Re-use existing TexCoord0 by adding a new one (idempotency caveat: extra node ok).
uv = E(unreal.MaterialExpressionTextureCoordinate, -1200, 600); uv.set_editor_property("coordinate_index", 0)
uv_len = E(unreal.MaterialExpressionDistance, -1000, 600)
zero2 = E(unreal.MaterialExpressionConstant2Vector, -1200, 680)
zero2.set_editor_property("r", 0); zero2.set_editor_property("g", 0)
mel.connect_material_expressions(uv, "", uv_len, "A")
mel.connect_material_expressions(zero2, "", uv_len, "B")
# d = length(UV); mask = smoothstep(0.5, 0.5 - soft, d)
half_const = E(unreal.MaterialExpressionConstant, -1000, 680); half_const.set_editor_property("r", 0.5)
inner = E(unreal.MaterialExpressionSubtract, -800, 680)
mel.connect_material_expressions(half_const, "", inner, "A")
mel.connect_material_expressions(soft, "", inner, "B")
ss = E(unreal.MaterialExpressionSmoothStep, -600, 640)
mel.connect_material_expressions(inner, "", ss, "Min")
mel.connect_material_expressions(half_const, "", ss, "Max")
mel.connect_material_expressions(uv_len, "", ss, "Value")
inv = E(unreal.MaterialExpressionOneMinus, -440, 640)
mel.connect_material_expressions(ss, "", inv, "")  # round_mask: 1 inside, 0 outside, gradient if soft>0

# Clipping plane: dot(WorldPos - ClipOrigin, ClipNormal) < 0 = visible
wp2 = E(unreal.MaterialExpressionWorldPosition, -1200, 940)
diff = E(unreal.MaterialExpressionSubtract, -1000, 940)
mel.connect_material_expressions(wp2, "", diff, "A")
mel.connect_material_expressions(co, "", diff, "B")
dp = E(unreal.MaterialExpressionDotProduct, -800, 940)
mel.connect_material_expressions(diff, "", dp, "A")
mel.connect_material_expressions(cn, "", dp, "B")
clip_pass = E(unreal.MaterialExpressionIf, -600, 940)
mel.connect_material_expressions(dp, "", clip_pass, "A")
zero_c = E(unreal.MaterialExpressionConstant, -800, 1020); zero_c.set_editor_property("r", 0.0)
mel.connect_material_expressions(zero_c, "", clip_pass, "B")
one_c = E(unreal.MaterialExpressionConstant, -800, 1080); one_c.set_editor_property("r", 1.0)
mel.connect_material_expressions(one_c, "", clip_pass, "AGreaterThanB")  # dp > 0  -> 1 = HIDE
mel.connect_material_expressions(zero_c, "", clip_pass, "ALessThanB")
mel.connect_material_expressions(zero_c, "", clip_pass, "AEqualsB")
# visible = 1 - clip_pass * ClipEnable
clip_mul = E(unreal.MaterialExpressionMultiply, -440, 980)
mel.connect_material_expressions(clip_pass, "", clip_mul, "A")
mel.connect_material_expressions(cen, "", clip_mul, "B")
clip_inv = E(unreal.MaterialExpressionOneMinus, -280, 980)
mel.connect_material_expressions(clip_mul, "", clip_inv, "")

# Final opacity mask = round_mask * clip_inv
mask = E(unreal.MaterialExpressionMultiply, -120, 700)
mel.connect_material_expressions(inv, "", mask, "A")
mel.connect_material_expressions(clip_inv, "", mask, "B")
mel.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)

mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(ASSET)
unreal.log("PointForge: M_PointCloudUnlit extended (ColorMode/SoftRound/Clip).")
