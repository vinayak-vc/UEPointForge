"""
PointForge splat material — translucent, unlit, two-sided (UE 5.5 Python).

Run inside the editor: Tools > Execute Python Script, or the Output Log
"py" command. Creates (or rebuilds) /Game/PointCloudData/Materials/M_PointForgeSplat.

Inputs come from UPFSplatComponent / FPFSplatSceneProxy (billboard path):
  TexCoord 0  (UV0) = quad corner in [-1, 1]^2
  TexCoord 1  (UV1) = conic.a, conic.b          (inverse 2D covariance, pixels)
  TexCoord 2  (UV2) = conic.c, radiusPx         (radius = 3 sigma, pixels)
  Vertex colour     = RGB, alpha = opacity (0..1)

Shading: Gaussian falloff evaluated per fragment, same as PointForge's viewer:
  x = UV0.x * radiusPx,  y = UV0.y * radiusPx
  d2 = a*x*x + 2*b*x*y + c*y*y
  alpha = min(0.99, opacity * exp(-0.5*d2))
Blend: standard "over" (Translucent), emissive = RGB, opacity = alpha.

Verified-by-reading only: this script has not been run on a UE 5.5 install yet.
Check the Output Log for "PFSplat:" lines. If a call fails, the message names it.
"""

import unreal

ASSET_DIR = "/Game/PointCloudData/Materials"
ASSET_NAME = "M_PointForgeSplat"
ASSET_PATH = ASSET_DIR + "/" + ASSET_NAME

mel = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("PFSplat: " + msg)


def get_or_create_material():
    mat = unreal.load_asset(ASSET_PATH)
    if mat is not None:
        return mat
    unreal.EditorAssetLibrary.make_directory(ASSET_DIR)
    mat = asset_tools.create_asset(ASSET_NAME, ASSET_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError("PFSplat: could not create " + ASSET_PATH)
    return mat


def find_prop(*parts):
    """Resolve a unreal.MaterialProperty member by name fragments (version-tolerant)."""
    for nm in dir(unreal.MaterialProperty):
        upper = nm.upper()
        if all(p in upper for p in parts):
            return getattr(unreal.MaterialProperty, nm)
    return None


mat = get_or_create_material()

# Start clean (same API the billboard script uses on this UE build).
mel.delete_all_material_expressions(mat)

mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)

MP_EMISSIVE = find_prop("EMISSIVE")
MP_OPACITY = find_prop("OPACITY")
if MP_EMISSIVE is None or MP_OPACITY is None:
    raise RuntimeError("PFSplat: MaterialProperty enums for emissive/opacity not found")


def node(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


# --- TexCoords (UV0 corner, UV1 conic.ab, UV2 conic.c + radius) ---------------
uv0 = node(unreal.MaterialExpressionTextureCoordinate, -1200, 0)
uv0.set_editor_property("coordinate_index", 0)
uv1 = node(unreal.MaterialExpressionTextureCoordinate, -1200, 200)
uv1.set_editor_property("coordinate_index", 1)
uv2 = node(unreal.MaterialExpressionTextureCoordinate, -1200, 400)
uv2.set_editor_property("coordinate_index", 2)

# --- Vertex colour: RGB for emissive, A for opacity ----------------------------
vc = node(unreal.MaterialExpressionVertexColor, -1200, 600)

rgb = node(unreal.MaterialExpressionComponentMask, -900, 600)
rgb.set_editor_property("r", True)
rgb.set_editor_property("g", True)
rgb.set_editor_property("b", True)
rgb.set_editor_property("a", False)
mel.connect_material_expressions(vc, "", rgb, "")

alpha = node(unreal.MaterialExpressionComponentMask, -900, 800)
alpha.set_editor_property("r", False)
alpha.set_editor_property("g", False)
alpha.set_editor_property("b", False)
alpha.set_editor_property("a", True)
mel.connect_material_expressions(vc, "", alpha, "")

# --- Gaussian falloff as one Custom (HLSL) node --------------------------------
gauss = node(unreal.MaterialExpressionCustom, -500, 600)
gauss.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
gauss.set_editor_property("description", "PointForge splat Gaussian falloff")
gauss.set_editor_property("code",
    "float x = UV0.x * UV2.y;\n"
    "float y = UV0.y * UV2.y;\n"
    "float d2 = UV1.x * x * x + 2.0 * UV1.y * x * y + UV2.x * y * y;\n"
    "return min(0.99, Opacity * exp(-0.5 * d2));")

inputs = []
for name in ("UV0", "UV1", "UV2", "Opacity"):
    ci = unreal.CustomInput()
    ci.set_editor_property("input_name", name)
    inputs.append(ci)
gauss.set_editor_property("inputs", inputs)

mel.connect_material_expressions(uv0, "", gauss, "UV0")
mel.connect_material_expressions(uv1, "", gauss, "UV1")
mel.connect_material_expressions(uv2, "", gauss, "UV2")
mel.connect_material_expressions(alpha, "", gauss, "Opacity")

# --- Outputs --------------------------------------------------------------------
mel.connect_material_property(rgb, "", MP_EMISSIVE)
mel.connect_material_property(gauss, "", MP_OPACITY)

mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
log("built " + ASSET_PATH + " (translucent, unlit, two-sided)")
log("Assign it to UPFSplatComponent.SplatMaterial and check the splats render soft, not solid.")
