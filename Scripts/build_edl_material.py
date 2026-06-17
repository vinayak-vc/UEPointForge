"""
EDL (Eye-Dome Lighting) post-process material — UE 5.5.4.

Creates /Game/PointCloudData/matrial/M_EDL (or updates it if it exists).

After running:
  1. Open the Camera actor in your level (or add a PostProcessVolume).
  2. PostProcessSettings > Blendables > + → pick M_EDL, Weight = 1.0.
  3. Play in Editor — point cloud edges get depth shading.

Tunable scalar params (edit M_EDL instance or create a MID):
  EDLStrength  (default 0.5)  — higher = darker silhouettes; range 0.2–2.0
  EDLRadius    (default 1.0)  — neighbor tap offset in pixels; 1–3 typical

Algorithm (4-tap):
  For N / S / E / W neighbors at radius pixels away:
    response = max(0,  log2(depth_neighbor) - log2(depth_here) )
  average_response = (rN + rS + rE + rW) * 0.25
  edl_factor = exp2( -EDLStrength * average_response )
  output = SceneColor * edl_factor

Known UE 5.5.4 constraints (same as billboard script):
  - BlendableLocation enum value probed at runtime via dir().
  - MaterialSceneAttributeInputMode.Coordinates probed; integer 0 fallback.
  - MaterialExposedViewProperty RcpViewSize probed; 1/ViewSize fallback.
  - MaterialExpressionExp2 used; MaterialExpressionExp fallback if absent.
"""

import unreal

ASSET_PATH = "/Game/PointCloudData/matrial"
ASSET_NAME = "M_EDL"
ASSET      = ASSET_PATH + "/" + ASSET_NAME

# ---- 0. Load or create asset -----------------------------------------------
mat = unreal.load_asset(ASSET)
if mat is None:
    af  = unreal.AssetToolsHelpers.get_asset_tools()
    mat = af.create_asset(ASSET_NAME, ASSET_PATH, unreal.Material, unreal.MaterialFactoryNew())
if mat is None:
    raise RuntimeError("Could not load or create asset: " + ASSET)

mel = unreal.MaterialEditingLibrary
mel.delete_all_material_expressions(mat)

# Post-process domain.
mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
mat.set_editor_property("blend_mode",      unreal.BlendMode.BLEND_OPAQUE)

# BlendableLocation: Before Tonemapping (linear space, better for EDL).
for _nm in dir(unreal.BlendableLocation):
    if "BEFORE" in _nm.upper() and "TONE" in _nm.upper():
        try:
            mat.set_editor_property("blendable_location", getattr(unreal.BlendableLocation, _nm))
            unreal.log("BlendableLocation set to: " + _nm)
            break
        except Exception as _ex:
            unreal.log_warning("BlendableLocation: " + str(_ex))

# ---- 1. Node helpers --------------------------------------------------------
def E(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)

def C(s, sp, d, dp):
    mel.connect_material_expressions(s, sp, d, dp)

def K(v, x, y):
    n = E(unreal.MaterialExpressionConstant, x, y)
    n.set_editor_property("r", v)
    return n

def scalar(nm, dv, x, y):
    n = E(unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", nm)
    n.set_editor_property("default_value",  dv)
    return n

def Add(a, b, x, y):
    n = E(unreal.MaterialExpressionAdd,      x, y); C(a,"",n,"A"); C(b,"",n,"B"); return n
def Sub(a, b, x, y):
    n = E(unreal.MaterialExpressionSubtract, x, y); C(a,"",n,"A"); C(b,"",n,"B"); return n
def Mul(a, b, x, y):
    n = E(unreal.MaterialExpressionMultiply, x, y); C(a,"",n,"A"); C(b,"",n,"B"); return n
def Div(a, b, x, y):
    n = E(unreal.MaterialExpressionDivide,   x, y); C(a,"",n,"A"); C(b,"",n,"B"); return n
def Max(a, b, x, y):
    n = E(unreal.MaterialExpressionMax,      x, y); C(a,"",n,"A"); C(b,"",n,"B"); return n
def Clamp(a, mn, mx, x, y):
    n = E(unreal.MaterialExpressionClamp, x, y)
    C(a,"",n,"Input")
    n.set_editor_property("min_default", mn)
    n.set_editor_property("max_default", mx)
    return n
def Log2(a, x, y):
    n = E(unreal.MaterialExpressionLogarithm2, x, y); C(a,"",n,""); return n
def Append(a, b, x, y):
    n = E(unreal.MaterialExpressionAppendVector, x, y); C(a,"",n,"A"); C(b,"",n,"B"); return n

def Exp2(a, x, y):
    # 1st: MaterialExpressionPower — Power(2, a) = 2^a = exp2(a)
    try:
        base2 = K(2.0, x - 140, y + 60)
        n = E(unreal.MaterialExpressionPower, x, y)
        C(base2, "", n, "Base"); C(a, "", n, "Exp")
        return n
    except Exception:
        pass
    # 2nd: CustomExpression HLSL exp2() — MaterialExpressionExp2/Exp absent in 5.5.4 Python.
    try:
        n = E(unreal.MaterialExpressionCustom, x, y)
        n.set_editor_property("code", "return exp2(In0);")
        try:
            n.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        except Exception:
            pass
        C(a, "", n, "In0")
        return n
    except Exception:
        pass
    # 3rd: linear clamp(1 + a, 0, 1) — a is negative so this is clamp(1 - |a|, 0, 1).
    one = K(1.0, x - 80, y + 60)
    return Clamp(Add(one, a, x - 40, y), 0.0, 1.0, x, y)

# ---- 2. Resolve SceneDepth coordinate input mode ---------------------------
# Coordinates mode = 0 in UE source (EMaterialSceneAttributeInputMode).
_SD_COORD = 0
if hasattr(unreal, "MaterialSceneAttributeInputMode"):
    for _nm in dir(unreal.MaterialSceneAttributeInputMode):
        if "COORD" in _nm.upper():
            try:
                _SD_COORD = getattr(unreal.MaterialSceneAttributeInputMode, _nm)
                unreal.log("SceneDepth coord mode: " + _nm)
                break
            except Exception:
                pass

def DepthAt(uv_node, x, y):
    sd = E(unreal.MaterialExpressionSceneDepth, x, y)
    try:
        sd.set_editor_property("input_mode", _SD_COORD)
    except Exception as _ex:
        unreal.log_error("SceneDepth input_mode failed — neighbor taps may be wrong: " + str(_ex))
    C(uv_node, "", sd, "Input")
    return sd

# ---- 3. Parameters ----------------------------------------------------------
p_strength = scalar("EDLStrength", 0.5, -2000, -200)
p_radius   = scalar("EDLRadius",   1.0, -2000, -100)

# ---- 4. Screen UV -----------------------------------------------------------
uv = E(unreal.MaterialExpressionTextureCoordinate, -2000, 0)
uv.set_editor_property("coordinate_index", 0)

# ---- 5. Texel size = 1 / ViewSize (try RcpViewSize first) ------------------
texel_node = E(unreal.MaterialExpressionViewProperty, -1800, 100)
_rcp_set = False
for _nm in dir(unreal.MaterialExposedViewProperty):
    if "RCP" in _nm.upper() and "SIZE" in _nm.upper():
        try:
            texel_node.set_editor_property("property", getattr(unreal.MaterialExposedViewProperty, _nm))
            _rcp_set = True
            unreal.log("Using ViewProperty: " + _nm)
            break
        except Exception:
            pass

if not _rcp_set:
    # Try plain ViewSize and divide.
    for _nm in dir(unreal.MaterialExposedViewProperty):
        if "VIEW" in _nm.upper() and "SIZE" in _nm.upper() and "RCP" not in _nm.upper():
            try:
                texel_node.set_editor_property("property", getattr(unreal.MaterialExposedViewProperty, _nm))
                unreal.log("ViewProperty fallback: " + _nm + " (will divide)")
                break
            except Exception:
                pass
    texel_node = Div(K(1.0, -1800, 200), texel_node, -1600, 100)

# offset_uv = EDLRadius * texel_size (2D scale per pixel)
off_uv = Mul(p_radius, texel_node, -1400, 0)

# ---- 6. Neighbor UV offsets (cardinal directions) --------------------------
cn1 = K(-1.0, -1400, 200)
cz  = K( 0.0, -1400, 300)
cp1 = K( 1.0, -1400, 400)

dir_n = Append(cz,  cn1, -1200, 200)   # ( 0,-1)
dir_s = Append(cz,  cp1, -1200, 300)   # ( 0,+1)
dir_e = Append(cp1, cz,  -1200, 400)   # (+1, 0)
dir_w = Append(cn1, cz,  -1200, 500)   # (-1, 0)

def neighbor_uv(d, dy):
    off = Mul(d, off_uv, -1000, dy)
    return Add(uv, off, -900, dy)

uv_n = neighbor_uv(dir_n, 200)
uv_s = neighbor_uv(dir_s, 300)
uv_e = neighbor_uv(dir_e, 400)
uv_w = neighbor_uv(dir_w, 500)

# ---- 7. Clamped log2 depth at current pixel --------------------------------
# Clamp to [1, 1e9] to avoid log(0) on sky/background near-infinite depth.
log_cur = Log2(Clamp(DepthAt(uv, -700, -100), 1.0, 1e9, -600, -100), -500, -100)

# ---- 8. Clamped log2 depth at each neighbor --------------------------------
def log_neigh(uv_nd, dy):
    return Log2(Clamp(DepthAt(uv_nd, -700, dy), 1.0, 1e9, -600, dy), -500, dy)

ln_n = log_neigh(uv_n, 200)
ln_s = log_neigh(uv_s, 300)
ln_e = log_neigh(uv_e, 400)
ln_w = log_neigh(uv_w, 500)

# ---- 9. EDL responses: max(0, log_neighbor - log_current) ------------------
# Positive when neighbor is farther (current pixel is a foreground edge).
zero = K(0.0, -200, 700)

def resp(ln, dy):
    return Max(zero, Sub(ln, log_cur, -300, dy), -200, dy)

r_n = resp(ln_n, 200)
r_s = resp(ln_s, 300)
r_e = resp(ln_e, 400)
r_w = resp(ln_w, 500)

# ---- 10. Average the 4 responses -------------------------------------------
sum_ns  = Add(r_n, r_s, 0, 250)
sum_ew  = Add(r_e, r_w, 0, 450)
sum_all = Add(sum_ns, sum_ew, 100, 350)
avg     = Mul(sum_all, K(0.25, 100, 450), 200, 350)

# ---- 11. EDL factor = exp2(-EDLStrength * avg) -----------------------------
neg_prod = Mul(Mul(p_strength, avg, 300, 350), K(-1.0, 300, 450), 400, 350)
edl_fac  = Exp2(neg_prod, 500, 350)

# ---- 12. Modulate scene color and output to Emissive -----------------------
sc  = E(unreal.MaterialExpressionSceneColor, 600, 250)
out = Mul(sc, edl_fac, 700, 300)

MP_EMISSIVE = None
for _nm in dir(unreal.MaterialProperty):
    if "EMISSIVE" in _nm.upper():
        MP_EMISSIVE = getattr(unreal.MaterialProperty, _nm)
        break
if MP_EMISSIVE is None:
    raise RuntimeError("MP_EMISSIVE not found. Available: " +
        str([n for n in dir(unreal.MaterialProperty) if n.startswith("MP_")]))

mel.connect_material_property(out, "", MP_EMISSIVE)
mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(ASSET)

unreal.log("=" * 60)
unreal.log("M_EDL built and saved: " + ASSET)
unreal.log("Next steps:")
unreal.log("  1. Select your Camera actor (or a PostProcessVolume).")
unreal.log("  2. Details > PostProcessSettings > Blendables > +")
unreal.log("  3. Pick M_EDL, set Weight = 1.0.")
unreal.log("  4. PIE — point cloud edges should show depth shading.")
unreal.log("  Tune: EDLStrength (0.2-2.0), EDLRadius (1-3 pixels).")
unreal.log("=" * 60)
