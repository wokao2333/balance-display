import math
from pathlib import Path

import bpy
from mathutils import Vector


# Parametric enclosure for ESP32-S3 N16R8 V1.0 / WKS35HV028-WCT.
# Units are millimeters. Tune these values after one printed fit check.
BOARD_W = 60.26
BOARD_H = 103.00
HOLE_X = 52.26
HOLE_Y = 95.00
MOUNT_HOLE_D = 3.10

# Single-side assembly clearance for FDM printed mating parts.
FIT_CLEARANCE = 0.20
BOARD_CLEARANCE = FIT_CLEARANCE * 2.0
COVER_EDGE_CLEARANCE = FIT_CLEARANCE
PORT_CLEARANCE = FIT_CLEARANCE * 2.0
WALL = 2.20
FRONT_THICK = 2.20
SHELL_DEPTH = 9.60
REAR_COVER_THICK = 1.80

SCREEN_OPEN_W = 51.00
SCREEN_OPEN_H = 75.70
# CTP visible area from the mechanical drawing:
# top offset 11.87 mm, visible height 74.44 mm.
SCREEN_CENTER_Y = (BOARD_H / 2.0) - 11.87 - (74.44 / 2.0)

POST_D = 7.20
POST_HEIGHT = 6.95
SCREW_NOMINAL_D = 2.50
SCREW_PILOT_D = 2.05
SCREW_CLEAR_D = SCREW_NOMINAL_D + FIT_CLEARANCE * 2.0
SCREW_HEAD_D = 5.20 + FIT_CLEARANCE * 2.0
FRONT_SHELL_BEVEL = 0.0
POST_BEVEL = 0.25
COVER_BEVEL = 0.35

OUTER_W = BOARD_W + 2 * (WALL + BOARD_CLEARANCE / 2.0)
OUTER_H = BOARD_H + 2 * (WALL + BOARD_CLEARANCE / 2.0)
INNER_W = BOARD_W + BOARD_CLEARANCE
INNER_H = BOARD_H + BOARD_CLEARANCE
COVER_W = OUTER_W - COVER_EDGE_CLEARANCE * 2.0
COVER_H = OUTER_H - COVER_EDGE_CLEARANCE * 2.0

OUT_DIR = Path(__file__).resolve().parent / "output"
OUT_DIR.mkdir(parents=True, exist_ok=True)
INCLUDE_REFERENCE_PARTS = False


def clean_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    bpy.context.scene.unit_settings.system = "METRIC"
    bpy.context.scene.unit_settings.scale_length = 0.001
    bpy.context.scene.unit_settings.length_unit = "MILLIMETERS"


def make_mat(name, color):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = color
    return mat


def cube_obj(name, size, loc, mat=None):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if mat:
        obj.data.materials.append(mat)
    return obj


def cyl_obj(name, radius, depth, loc, vertices=64, mat=None, axis="Z"):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=loc)
    obj = bpy.context.object
    obj.name = name
    if axis == "X":
        obj.rotation_euler[1] = math.radians(90)
    elif axis == "Y":
        obj.rotation_euler[0] = math.radians(90)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    if mat:
        obj.data.materials.append(mat)
    return obj


def apply_boolean(target, cutter, operation="DIFFERENCE"):
    mod = target.modifiers.new(f"{operation.lower()}_{cutter.name}", "BOOLEAN")
    mod.operation = operation
    mod.object = cutter
    try:
        mod.solver = "EXACT"
    except Exception:
        pass
    bpy.context.view_layer.objects.active = target
    target.select_set(True)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cutter, do_unlink=True)


def bevel(obj, width=0.6, segments=5):
    mod = obj.modifiers.new("print_soft_edges", "BEVEL")
    mod.width = width
    mod.segments = segments
    mod.affect = "EDGES"
    mod.profile = 0.5
    mod2 = obj.modifiers.new("weighted_normals", "WEIGHTED_NORMAL")
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.ops.object.modifier_apply(modifier=mod2.name)


def mount_xy():
    for x in (-HOLE_X / 2.0, HOLE_X / 2.0):
        for y in (-HOLE_Y / 2.0, HOLE_Y / 2.0):
            yield x, y


def side_slot(shell, side, y, h, z_center=6.5, z_h=6.8):
    # Extend the cutter past both the outside face and the inner cavity edge.
    # A previous version stopped just short of the cavity and left a thin skin.
    x_size = WALL + BOARD_CLEARANCE + 4.0 + PORT_CLEARANCE
    x = (OUTER_W / 2.0 - WALL / 2.0) if side == "right" else (-OUTER_W / 2.0 + WALL / 2.0)
    cutter = cube_obj(
        f"{side}_slot_{y:.1f}",
        (x_size, h + PORT_CLEARANCE, z_h + PORT_CLEARANCE),
        (x, y, z_center),
    )
    apply_boolean(shell, cutter)


def side_notch_for_cover(cover, side, y, h):
    x_size = WALL + BOARD_CLEARANCE + 4.0 + PORT_CLEARANCE
    x = (COVER_W / 2.0 - WALL / 2.0) if side == "right" else (-COVER_W / 2.0 + WALL / 2.0)
    cutter = cube_obj(
        f"{side}_cover_notch_{y:.1f}",
        (x_size, h + PORT_CLEARANCE, REAR_COVER_THICK + 0.8),
        (x, y, REAR_COVER_THICK / 2.0),
    )
    apply_boolean(cover, cutter)


def rear_window(cover, name, x, y, w, h, corner_holes=False):
    cutter = cube_obj(name, (w, h, REAR_COVER_THICK + 0.8), (x, y, REAR_COVER_THICK / 2.0))
    apply_boolean(cover, cutter)
    if corner_holes:
        for sx in (-1, 1):
            for sy in (-1, 1):
                c = cyl_obj(
                    f"{name}_relief",
                    1.2,
                    REAR_COVER_THICK + 1.0,
                    (x + sx * w / 2.0, y + sy * h / 2.0, REAR_COVER_THICK / 2.0),
                    vertices=24,
                )
                apply_boolean(cover, c)


def create_front_shell(mat):
    shell = cube_obj("ESP32S3_front_shell", (OUTER_W, OUTER_H, SHELL_DEPTH), (0, 0, SHELL_DEPTH / 2), mat)

    cavity = cube_obj(
        "board_cavity",
        (INNER_W, INNER_H, SHELL_DEPTH - FRONT_THICK + 0.5),
        (0, 0, FRONT_THICK + (SHELL_DEPTH - FRONT_THICK) / 2.0 + 0.25),
    )
    apply_boolean(shell, cavity)

    screen = cube_obj(
        "screen_window",
        (SCREEN_OPEN_W, SCREEN_OPEN_H, FRONT_THICK + 1.0),
        (0, SCREEN_CENTER_Y, FRONT_THICK / 2.0),
    )
    apply_boolean(shell, screen)

    # Side-facing openings from the placement drawing. The slots run nearly to
    # the rear edge, so USB/TF plugs do not hit a hidden upper lip.
    side_slot(shell, "right", 12.0, 10.0, z_center=6.55, z_h=6.6)   # COM / USB-to-UART
    side_slot(shell, "right", -6.0, 9.5, z_center=6.55, z_h=6.6)    # USB
    side_slot(shell, "right", -31.0, 17.0, z_center=6.55, z_h=6.6)  # TF card
    side_slot(shell, "left", -13.0, 22.0, z_center=6.55, z_h=6.6)   # UART header

    if FRONT_SHELL_BEVEL > 0:
        bevel(shell, width=FRONT_SHELL_BEVEL, segments=5)
    parts = [shell]

    for i, (x, y) in enumerate(mount_xy(), start=1):
        post = cyl_obj(
            f"front_post_{i}",
            POST_D / 2.0,
            POST_HEIGHT,
            (x, y, POST_HEIGHT / 2.0),
            vertices=72,
            mat=mat,
        )
        pilot = cyl_obj(
            f"front_post_pilot_{i}",
            SCREW_PILOT_D / 2.0,
            POST_HEIGHT + 0.8,
            (x, y, POST_HEIGHT / 2.0),
            vertices=48,
        )
        apply_boolean(post, pilot)
        if POST_BEVEL > 0:
            bevel(post, width=POST_BEVEL, segments=4)
        parts.append(post)

    return parts


def create_rear_cover(mat):
    cover = cube_obj("ESP32S3_rear_cover", (COVER_W, COVER_H, REAR_COVER_THICK), (0, 0, REAR_COVER_THICK / 2), mat)

    for i, (x, y) in enumerate(mount_xy(), start=1):
        clear = cyl_obj(
            f"cover_screw_clear_{i}",
            SCREW_CLEAR_D / 2.0,
            REAR_COVER_THICK + 1.0,
            (x, y, REAR_COVER_THICK / 2.0),
            vertices=48,
        )
        apply_boolean(cover, clear)
        head = cyl_obj(
            f"cover_screw_head_{i}",
            SCREW_HEAD_D / 2.0,
            0.85,
            (x, y, REAR_COVER_THICK - 0.25),
            vertices=56,
        )
        apply_boolean(cover, head)

    # Component-side service openings from the placement drawing.
    for name, x, y, d in [
        ("reset_poke", -18.2, 36.9, 6.0),
        ("boot_poke", 19.2, 36.9, 6.0),
    ]:
        c = cyl_obj(name, d / 2.0, REAR_COVER_THICK + 1.0, (x, y, REAR_COVER_THICK / 2), vertices=48)
        apply_boolean(cover, c)

    # The USB/COM/TF/UART connectors are side-access parts, not large back-panel
    # holes. Only notch the cover edge where it meets those side openings.
    side_notch_for_cover(cover, "right", 12.0, 10.0)
    side_notch_for_cover(cover, "right", -6.0, 9.5)
    side_notch_for_cover(cover, "right", -31.0, 17.0)
    side_notch_for_cover(cover, "left", -13.0, 22.0)

    # A small vent/relief grid over the ESP32 module region.
    for gx in range(-2, 3):
        for gy in range(-2, 3):
            vent = cyl_obj(
                f"module_vent_{gx}_{gy}",
                0.75,
                REAR_COVER_THICK + 1.0,
                (gx * 3.0, 20.0 + gy * 3.0, REAR_COVER_THICK / 2.0),
                vertices=18,
            )
            apply_boolean(cover, vent)

    if COVER_BEVEL > 0:
        bevel(cover, width=COVER_BEVEL, segments=4)
    return cover


def create_board_reference(mat_board, mat_lcd):
    board = cube_obj("reference_board_60.26x103_do_not_print", (BOARD_W, BOARD_H, 1.6), (0, 0, 7.7), mat_board)
    lcd = cube_obj("reference_screen_opening", (55.26, 84.69, 1.1), (0, (BOARD_H / 2) - 9.48 - 84.69 / 2, 6.85), mat_lcd)
    for x, y in mount_xy():
        hole = cyl_obj("reference_mount_hole", MOUNT_HOLE_D / 2, 2.0, (x, y, 7.7), vertices=32)
        apply_boolean(board, hole)
    return board, lcd


def join_objects(objects, name):
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = objects[0]
    for obj in objects:
        obj.select_set(True)
    bpy.ops.object.join()
    joined = bpy.context.object
    joined.name = name
    joined.data.name = f"{name}_mesh"
    return joined


def export_stl(obj_or_objects, path):
    objects = obj_or_objects if isinstance(obj_or_objects, (list, tuple)) else [obj_or_objects]
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    try:
        bpy.ops.wm.stl_export(filepath=str(path), export_selected_objects=True, apply_modifiers=True)
    except TypeError:
        bpy.ops.wm.stl_export(filepath=str(path), export_selected_objects=True)


def setup_camera():
    try:
        bpy.context.scene.render.engine = "BLENDER_WORKBENCH"
        bpy.context.scene.display.shading.light = "STUDIO"
        bpy.context.scene.display.shading.color_type = "MATERIAL"
        bpy.context.scene.display.shading.studio_light = "studio.exr"
    except Exception:
        pass
    bpy.context.scene.view_settings.view_transform = "Standard"
    bpy.context.scene.view_settings.exposure = 0
    bpy.context.scene.view_settings.gamma = 1
    bpy.context.scene.world.color = (0.92, 0.93, 0.94)
    bpy.ops.object.light_add(type="AREA", location=(0, -95, 105))
    light = bpy.context.object
    light.name = "large_softbox"
    light.data.energy = 850
    light.data.size = 80

    target = Vector((OUTER_W / 2 + 6, 0, 5.5))
    bpy.ops.object.camera_add(location=(88, -135, 85))
    camera = bpy.context.object
    direction = target - camera.location
    camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.camera = camera
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 135
    bpy.context.scene.render.resolution_x = 1800
    bpy.context.scene.render.resolution_y = 1300
    bpy.context.scene.eevee.taa_render_samples = 64


def main():
    clean_scene()
    mat_case = make_mat("preview_front_shell", (0.56, 0.58, 0.60, 1))
    mat_cover = make_mat("preview_rear_cover", (0.36, 0.38, 0.40, 1))
    mat_board = make_mat("transparent_board_reference", (0.0, 0.45, 0.16, 0.22))
    mat_board.use_nodes = True
    for node in mat_board.node_tree.nodes:
        if node.type == "BSDF_PRINCIPLED" and "Alpha" in node.inputs:
            node.inputs["Alpha"].default_value = 0.22
            break
    mat_board.blend_method = "BLEND"
    mat_lcd = make_mat("smoked_screen_reference", (0.02, 0.02, 0.025, 0.45))

    shell_parts = create_front_shell(mat_case)
    shell = join_objects(shell_parts, "ESP32S3_front_shell")
    cover = create_rear_cover(mat_cover)
    cover.location.x = OUTER_W + 12

    if INCLUDE_REFERENCE_PARTS:
        board, lcd = create_board_reference(mat_board, mat_lcd)
        board.location.x = 0
        lcd.location.x = 0

    export_stl(shell, OUT_DIR / "esp32s3_n16r8_front_shell.stl")
    cover.location.x = 0
    export_stl(cover, OUT_DIR / "esp32s3_n16r8_rear_cover.stl")
    cover.location.x = OUTER_W + 12

    setup_camera()
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT_DIR / "esp32s3_n16r8_case_preview.blend"))
    bpy.context.scene.render.filepath = str(OUT_DIR / "esp32s3_n16r8_case_preview.png")
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    main()
