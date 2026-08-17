"""
Render a BedrockMap exported GLB/glTF file with a consistent Blender preview setup.

Usage:
  blender --python scripts/blender_render_preview.py
      Opens a Blender file chooser and renders the selected model.

  blender --background --python scripts/blender_render_preview.py -- --model model.glb
      Renders without UI.
"""

import argparse
import math
import os
import sys
from pathlib import Path

import bpy
from mathutils import Vector


def parse_script_args() -> argparse.Namespace:
    argv = sys.argv
    script_args = argv[argv.index("--") + 1 :] if "--" in argv else []
    parser = argparse.ArgumentParser(description="Render a BedrockMap GLB/glTF preview.")
    parser.add_argument("--model", help="Path to a .glb or .gltf file.")
    parser.add_argument("--output", help="Path to the output preview image.")
    parser.add_argument(
        "--quality",
        choices=("draft", "fine", "final"),
        default="fine",
        help="Render quality preset. Explicit --size/--width/--height/--samples override the preset values.",
    )
    parser.add_argument("--size", help="Output image size, such as 1920x1080 or 1920*1080.")
    parser.add_argument("--width", type=int, help="Output image width in pixels.")
    parser.add_argument("--height", type=int, help="Output image height in pixels.")
    parser.add_argument(
        "--frame-padding",
        type=float,
        default=1.90,
        help="Camera framing multiplier. Larger values leave more empty border around the model.",
    )
    parser.add_argument("--samples", type=int, help="Cycles sample count.")
    parser.add_argument("--brightness", type=float, default=1.0, help="Multiplier for the preview light preset.")
    parser.add_argument(
        "--transparent-alpha-cap",
        type=float,
        default=0.65,
        help="Clamp transparent material alpha to make nearly-opaque water readable. Use 1.0 to disable.",
    )
    parser.add_argument(
        "--transparent-alpha-mode",
        choices=("voxel-shell", "exact"),
        default="voxel-shell",
        help=(
            "How COLOR_0 alpha is applied. voxel-shell treats alpha as the whole voxel shell opacity; "
            "exact applies it directly to every rendered face."
        ),
    )
    parser.add_argument("--azimuth", type=float, help="Camera yaw angle in degrees. Try 45, 135, 225, or 315.")
    parser.add_argument("--elevation", type=float, help="Camera elevation angle in degrees. Default iso angle is 35.")
    parser.add_argument("--no-render-window", action="store_true", help="Do not open Blender's render preview window.")
    parser.add_argument(
        "--color-management",
        choices=("raw", "standard", "agx"),
        default="raw",
        help="Display transform for the rendered PNG. Raw is the default for direct voxel-color inspection.",
    )
    parser.add_argument(
        "--view",
        choices=("iso", "front", "side", "top", "low"),
        default="iso",
        help="Camera preset. 'low' is useful for checking underside/transparent layering.",
    )
    return parser.parse_args(script_args)


def progress(message: str) -> None:
    text = f"[BedrockMap Preview] {message}"
    print(text, flush=True)
    try:
        bpy.context.window_manager.progress_update(0)
        bpy.context.workspace.status_text_set(message)
    except Exception:
        pass


def install_render_progress_handlers(output_path: Path | None = None, open_after_render: bool = False) -> None:
    def render_init(*_args):
        progress("Render started. Cycles may take a while on large voxel meshes.")

    def render_complete(*_args):
        progress("Render completed.")
        if open_after_render and output_path is not None:
            progress(f"Opening preview image: {output_path}")
            open_rendered_image(output_path)

    def render_cancel(*_args):
        progress("Render cancelled.")

    def render_stats(*args):
        if not args:
            return
        message = args[-1]
        if message is None:
            return
        text = str(message).strip()
        if not text or text == "None":
            return
        print(f"[BedrockMap Preview] {text}", flush=True)

    bpy.app.handlers.render_init.append(render_init)
    bpy.app.handlers.render_complete.append(render_complete)
    bpy.app.handlers.render_cancel.append(render_cancel)
    if hasattr(bpy.app.handlers, "render_stats"):
        bpy.app.handlers.render_stats.append(render_stats)


def parse_render_size(size: str | None) -> tuple[int | None, int | None]:
    if not size:
        return None, None
    normalized = size.lower().replace("×", "x").replace("*", "x")
    parts = normalized.split("x")
    if len(parts) != 2:
        raise ValueError("Render size must look like 1920x1080.")
    try:
        width = int(parts[0].strip())
        height = int(parts[1].strip())
    except ValueError as exc:
        raise ValueError("Render size must contain integer width and height.") from exc
    if width <= 0 or height <= 0:
        raise ValueError("Render size width and height must be positive.")
    return width, height


def quality_settings(quality: str, size: str | None, width: int | None, height: int | None, samples: int | None) -> tuple[int, int, int]:
    presets = {
        "draft": (1200, 64),
        "fine": (2200, 256),
        "final": (3200, 512),
    }
    preset_resolution, preset_samples = presets[quality]
    size_width, size_height = parse_render_size(size)
    render_width = width or size_width or preset_resolution
    render_height = height or size_height or render_width
    return render_width, render_height, samples or preset_samples


def clear_scene() -> None:
    progress("Clearing Blender scene.")
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def imported_mesh_objects(before_names: set[str]) -> list[bpy.types.Object]:
    return [
        obj
        for obj in bpy.context.scene.objects
        if obj.name not in before_names and obj.type == "MESH"
    ]


def all_imported_roots(before_names: set[str]) -> list[bpy.types.Object]:
    imported = [obj for obj in bpy.context.scene.objects if obj.name not in before_names]
    imported_set = set(imported)
    return [obj for obj in imported if obj.parent not in imported_set]


def import_model(model_path: Path) -> list[bpy.types.Object]:
    progress(f"Importing model: {model_path}")
    before_names = {obj.name for obj in bpy.context.scene.objects}
    bpy.ops.import_scene.gltf(filepath=str(model_path))
    meshes = imported_mesh_objects(before_names)
    if not meshes:
        raise RuntimeError(f"No mesh objects were imported from: {model_path}")
    for obj in all_imported_roots(before_names):
        obj.select_set(True)
    return meshes


def bounds_for_objects(objects: list[bpy.types.Object]) -> tuple[Vector, Vector]:
    minimum = Vector((math.inf, math.inf, math.inf))
    maximum = Vector((-math.inf, -math.inf, -math.inf))
    for obj in objects:
        for corner in obj.bound_box:
            world_corner = obj.matrix_world @ Vector(corner)
            minimum.x = min(minimum.x, world_corner.x)
            minimum.y = min(minimum.y, world_corner.y)
            minimum.z = min(minimum.z, world_corner.z)
            maximum.x = max(maximum.x, world_corner.x)
            maximum.y = max(maximum.y, world_corner.y)
            maximum.z = max(maximum.z, world_corner.z)
    return minimum, maximum


def world_corners_for_objects(objects: list[bpy.types.Object]) -> list[Vector]:
    corners: list[Vector] = []
    for obj in objects:
        corners.extend(obj.matrix_world @ Vector(corner) for corner in obj.bound_box)
    return corners


def center_imported_model(objects: list[bpy.types.Object]) -> tuple[Vector, Vector, float]:
    minimum, maximum = bounds_for_objects(objects)
    center = (minimum + maximum) * 0.5
    scene_objects = list(bpy.context.scene.objects)
    scene_object_set = set(scene_objects)
    roots = [obj for obj in scene_objects if obj.parent not in scene_object_set]

    for obj in roots:
        obj.matrix_world.translation -= center

    bpy.context.view_layer.update()
    minimum, maximum = bounds_for_objects(objects)
    size = maximum - minimum
    radius = max(size.length * 0.5, 1.0)
    return minimum, maximum, radius


def first_color_attribute_name(obj: bpy.types.Object) -> str | None:
    color_attributes = getattr(obj.data, "color_attributes", None)
    if color_attributes:
        active = getattr(color_attributes, "active_color", None)
        if active:
            return active.name
        if len(color_attributes) > 0:
            return color_attributes[0].name

    vertex_colors = getattr(obj.data, "vertex_colors", None)
    if vertex_colors and len(vertex_colors) > 0:
        return vertex_colors[0].name

    return None


def set_material_render_property(material: bpy.types.Material, name: str, values: tuple[str, ...]) -> None:
    if not hasattr(material, name):
        return
    for value in values:
        try:
            setattr(material, name, value)
            return
        except TypeError:
            continue


def color_attribute_alpha_for_loop(color_attribute, loop_index: int, vertex_index: int) -> float:
    domain = getattr(color_attribute, "domain", "CORNER")
    if domain == "POINT":
        return float(color_attribute.data[vertex_index].color[3])
    return float(color_attribute.data[loop_index].color[3])


def material_alpha_ranges(obj: bpy.types.Object, color_attribute_name: str) -> dict[int, tuple[float, float]]:
    color_attribute = obj.data.color_attributes.get(color_attribute_name)
    if color_attribute is None:
        return {}

    ranges: dict[int, tuple[float, float]] = {}
    for polygon in obj.data.polygons:
        minimum, maximum = ranges.get(polygon.material_index, (1.0, 0.0))
        for loop_index in polygon.loop_indices:
            loop = obj.data.loops[loop_index]
            alpha = color_attribute_alpha_for_loop(color_attribute, loop_index, loop.vertex_index)
            minimum = min(minimum, alpha)
            maximum = max(maximum, alpha)
        ranges[polygon.material_index] = (minimum, maximum)
    return ranges


def configure_vertex_color_material(
    material: bpy.types.Material,
    color_attribute_name: str,
    has_transparency: bool,
    transparent_alpha_cap: float,
    transparent_alpha_mode: str,
) -> None:
    material.use_nodes = True
    material.diffuse_color = (1.0, 1.0, 1.0, 1.0)
    if has_transparency:
        set_material_render_property(material, "blend_method", ("BLEND", "HASHED"))
        set_material_render_property(material, "surface_render_method", ("BLENDED", "DITHERED"))
        set_material_render_property(material, "shadow_method", ("HASHED", "NONE"))
        if hasattr(material, "show_transparent_back"):
            material.show_transparent_back = True
        if hasattr(material, "use_screen_refraction"):
            material.use_screen_refraction = True
    else:
        set_material_render_property(material, "blend_method", ("OPAQUE",))
        set_material_render_property(material, "surface_render_method", ("OPAQUE",))
        set_material_render_property(material, "shadow_method", ("OPAQUE",))

    node_tree = material.node_tree
    node_tree.nodes.clear()

    output = node_tree.nodes.new("ShaderNodeOutputMaterial")
    output.location = (680, 0)
    principled = node_tree.nodes.new("ShaderNodeBsdfPrincipled")
    principled.location = (400, 0)
    vertex_color = node_tree.nodes.new("ShaderNodeVertexColor")
    vertex_color.location = (-120, 0)
    vertex_color.layer_name = color_attribute_name

    if "Metallic" in principled.inputs:
        principled.inputs["Metallic"].default_value = 0.0
    if "Roughness" in principled.inputs:
        principled.inputs["Roughness"].default_value = 0.82
    if "Alpha" in principled.inputs:
        principled.inputs["Alpha"].default_value = 1.0

    node_tree.links.new(vertex_color.outputs["Color"], principled.inputs["Base Color"])
    if "Alpha" in vertex_color.outputs and "Alpha" in principled.inputs:
        alpha_socket = vertex_color.outputs["Alpha"]
        if has_transparency and transparent_alpha_cap < 0.999:
            alpha_cap = node_tree.nodes.new("ShaderNodeMath")
            alpha_cap.operation = "MINIMUM"
            alpha_cap.location = (140, -180)
            alpha_cap.inputs[1].default_value = max(0.01, min(1.0, transparent_alpha_cap))
            node_tree.links.new(alpha_socket, alpha_cap.inputs[0])
            alpha_socket = alpha_cap.outputs["Value"]

        if has_transparency and transparent_alpha_mode == "voxel-shell":
            # BedrockMap exports the visible voxel shell. A ray normally crosses
            # both the front and back faces of a transparent cube, so applying
            # the block alpha directly to every face makes glass/water look much
            # more opaque than in a real-time GLB viewer. Convert the desired
            # whole-shell opacity A to a per-face opacity a:
            #     1 - A = (1 - a)^2  =>  a = 1 - sqrt(1 - A)
            one_minus_alpha = node_tree.nodes.new("ShaderNodeMath")
            one_minus_alpha.operation = "SUBTRACT"
            one_minus_alpha.location = (300, -260)
            one_minus_alpha.inputs[0].default_value = 1.0
            node_tree.links.new(alpha_socket, one_minus_alpha.inputs[1])

            clamp_positive = node_tree.nodes.new("ShaderNodeMath")
            clamp_positive.operation = "MAXIMUM"
            clamp_positive.location = (480, -260)
            clamp_positive.inputs[1].default_value = 0.0
            node_tree.links.new(one_minus_alpha.outputs["Value"], clamp_positive.inputs[0])

            sqrt_transmittance = node_tree.nodes.new("ShaderNodeMath")
            sqrt_transmittance.operation = "SQRT"
            sqrt_transmittance.location = (660, -260)
            node_tree.links.new(clamp_positive.outputs["Value"], sqrt_transmittance.inputs[0])

            per_face_alpha = node_tree.nodes.new("ShaderNodeMath")
            per_face_alpha.operation = "SUBTRACT"
            per_face_alpha.location = (840, -260)
            per_face_alpha.inputs[0].default_value = 1.0
            node_tree.links.new(sqrt_transmittance.outputs["Value"], per_face_alpha.inputs[1])
            alpha_socket = per_face_alpha.outputs["Value"]

        node_tree.links.new(alpha_socket, principled.inputs["Alpha"])
    node_tree.links.new(principled.outputs["BSDF"], output.inputs["Surface"])


def configure_materials(objects: list[bpy.types.Object], transparent_alpha_cap: float, transparent_alpha_mode: str) -> None:
    progress("Configuring vertex-color materials and transparency.")
    configured: set[tuple[str, str]] = set()
    for obj in objects:
        color_attribute_name = first_color_attribute_name(obj)
        if not color_attribute_name:
            continue
        alpha_ranges = material_alpha_ranges(obj, color_attribute_name)
        for material_index, slot in enumerate(obj.material_slots):
            material = slot.material
            if not material:
                continue
            key = (material.name, color_attribute_name)
            if key in configured:
                continue
            alpha_range = alpha_ranges.get(material_index, (1.0, 1.0))
            has_transparency = alpha_range[0] < 0.999
            configure_vertex_color_material(
                material, color_attribute_name, has_transparency, transparent_alpha_cap, transparent_alpha_mode
            )
            configured.add(key)


def look_at(obj: bpy.types.Object, target: Vector) -> None:
    direction = target - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def direction_from_angles(azimuth: float, elevation: float) -> Vector:
    azimuth_radians = math.radians(azimuth)
    elevation_radians = math.radians(elevation)
    horizontal = math.cos(elevation_radians)
    return Vector(
        (
            math.cos(azimuth_radians) * horizontal,
            math.sin(azimuth_radians) * horizontal,
            math.sin(elevation_radians),
        )
    ).normalized()


def camera_direction(view: str, azimuth: float | None = None, elevation: float | None = None) -> Vector:
    if azimuth is not None:
        if elevation is None:
            elevation = -16.0 if view == "low" else 35.0
        return direction_from_angles(azimuth, elevation)

    directions = {
        "iso": Vector((1.45, -1.75, 1.15)),
        "front": Vector((0.0, -2.0, 0.35)),
        "side": Vector((2.0, -0.15, 0.35)),
        "top": Vector((0.4, -0.55, 2.3)),
        "low": Vector((1.3, -1.65, -0.55)),
    }
    return directions[view].normalized()


def projected_camera_bounds(points: list[Vector], camera: bpy.types.Object) -> tuple[float, float, float, float]:
    inverse_camera = camera.matrix_world.inverted()
    projected = [inverse_camera @ point for point in points]
    return (
        min(point.x for point in projected),
        max(point.x for point in projected),
        min(point.y for point in projected),
        max(point.y for point in projected),
    )


def center_camera_on_projected_points(points: list[Vector], camera: bpy.types.Object) -> tuple[float, float]:
    minimum_x, maximum_x, minimum_y, maximum_y = projected_camera_bounds(points, camera)
    offset_x = (minimum_x + maximum_x) * 0.5
    offset_y = (minimum_y + maximum_y) * 0.5
    rotation = camera.matrix_world.to_quaternion()
    camera.location += rotation @ Vector((offset_x, offset_y, 0.0))
    bpy.context.view_layer.update()
    return offset_x, offset_y


def orthographic_scale_for_points(
    points: list[Vector],
    camera: bpy.types.Object,
    render_width: int,
    render_height: int,
    frame_padding: float,
) -> float:
    minimum_x, maximum_x, minimum_y, maximum_y = projected_camera_bounds(points, camera)
    projected_width = maximum_x - minimum_x
    projected_height = maximum_y - minimum_y
    aspect = max(1, render_width) / max(1, render_height)
    required_height = max(projected_height, projected_width / aspect)
    return max(required_height * max(frame_padding, 1.0), 1.0)


def setup_camera_and_lighting(
    bounds_min: Vector,
    bounds_max: Vector,
    radius: float,
    view: str,
    brightness: float,
    render_width: int,
    render_height: int,
    objects: list[bpy.types.Object],
    frame_padding: float,
    azimuth: float | None,
    elevation: float | None,
) -> None:
    angle_text = f", azimuth={azimuth:g}" if azimuth is not None else ""
    if elevation is not None:
        angle_text += f", elevation={elevation:g}"
    progress(f"Setting up studio camera and lights: view={view}{angle_text}, brightness={brightness:.2f}")
    size = bounds_max - bounds_min
    center = (bounds_min + bounds_max) * 0.5
    max_axis = max(size.x, size.y, size.z, 1.0)
    brightness = max(brightness, 0.05)
    light_power = max(420.0, radius * radius * 110.0) * brightness

    camera_data = bpy.data.cameras.new("BedrockMap Preview Camera")
    camera = bpy.data.objects.new("BedrockMap Preview Camera", camera_data)
    bpy.context.collection.objects.link(camera)
    camera.location = center + camera_direction(view, azimuth, elevation) * radius * 2.8
    camera_data.type = "ORTHO"
    camera_data.lens = 70
    camera_data.clip_start = 0.01
    camera_data.clip_end = max(radius * 8.0, 1000.0)
    look_at(camera, center)
    bpy.context.view_layer.update()
    framing_points = world_corners_for_objects(objects)
    offset_x, offset_y = center_camera_on_projected_points(framing_points, camera)
    camera_data.ortho_scale = orthographic_scale_for_points(
        framing_points, camera, render_width, render_height, frame_padding
    )
    progress(
        f"Camera framing: aspect={render_width}x{render_height}, ortho_scale={camera_data.ortho_scale:.2f}, "
        f"padding={frame_padding:.2f}, shift=({offset_x:.2f},{offset_y:.2f}), clip_end={camera_data.clip_end:.2f}"
    )
    bpy.context.scene.camera = camera

    key_data = bpy.data.lights.new("BedrockMap Key Light", type="AREA")
    key = bpy.data.objects.new("BedrockMap Key Light", key_data)
    bpy.context.collection.objects.link(key)
    key.location = center + Vector((-0.8, -1.2, 1.6)).normalized() * radius * 2.4
    key_data.energy = light_power
    key_data.size = max_axis * 1.9
    look_at(key, center)

    fill_data = bpy.data.lights.new("BedrockMap Soft Fill", type="AREA")
    fill = bpy.data.objects.new("BedrockMap Soft Fill", fill_data)
    bpy.context.collection.objects.link(fill)
    fill.location = center + Vector((1.4, 1.1, 0.8)).normalized() * radius * 2.2
    fill_data.energy = light_power * 0.26
    fill_data.size = max_axis * 2.8
    look_at(fill, center)

    sun_data = bpy.data.lights.new("BedrockMap Directional Light", type="SUN")
    sun = bpy.data.objects.new("BedrockMap Directional Light", sun_data)
    bpy.context.collection.objects.link(sun)
    sun.location = center + Vector((0.25, -0.35, 1.0)).normalized() * radius * 2.0
    sun_data.energy = 0.45 * brightness
    look_at(sun, center)

    world = bpy.context.scene.world or bpy.data.worlds.new("BedrockMap Preview World")
    bpy.context.scene.world = world
    world.color = (0.58, 0.60, 0.64)
    world.use_nodes = True
    background = world.node_tree.nodes.get("Background")
    if background:
        background.inputs["Color"].default_value = (0.58, 0.60, 0.64, 1.0)
        background.inputs["Strength"].default_value = 0.18 * brightness


def configure_cycles_device() -> None:
    try:
        preferences = bpy.context.preferences.addons["cycles"].preferences
        preferences.get_devices()
        if preferences.devices:
            for device in preferences.devices:
                device.use = True
            bpy.context.scene.cycles.device = "GPU"
            progress("Cycles GPU rendering enabled when available.")
    except Exception:
        progress("Cycles GPU setup skipped; using Blender's current render device.")


def set_enum_value(owner, attribute: str, candidates: tuple[str, ...]) -> str | None:
    for candidate in candidates:
        try:
            setattr(owner, attribute, candidate)
            return candidate
        except TypeError:
            continue
    return None


def set_color_management(color_management: str) -> None:
    view_settings = bpy.context.scene.view_settings
    if color_management == "raw":
        transform = set_enum_value(view_settings, "view_transform", ("Raw", "Standard"))
        look = set_enum_value(view_settings, "look", ("None",))
        exposure = 0.0
    elif color_management == "agx":
        transform = set_enum_value(view_settings, "view_transform", ("AgX", "Filmic", "Standard"))
        look = set_enum_value(
            view_settings,
            "look",
            ("AgX - Base Contrast", "AgX - Medium High Contrast", "Medium High Contrast", "Medium Contrast", "None"),
        )
        exposure = -0.15
    else:
        transform = set_enum_value(view_settings, "view_transform", ("Standard",))
        look = set_enum_value(view_settings, "look", ("None",))
        exposure = 0.0

    view_settings.exposure = exposure
    view_settings.gamma = 1.0
    progress(f"Color management: {transform or 'unchanged'}, look={look or 'unchanged'}, exposure={exposure}")


def setup_render_settings(output_path: Path, width: int, height: int, samples: int, quality: str, color_management: str) -> None:
    progress(f"Configuring render settings: quality={quality}, resolution={width}x{height}, samples={samples}")
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    configure_cycles_device()
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    if hasattr(scene.cycles, "use_fast_gi"):
        scene.cycles.use_fast_gi = True
    scene.cycles.max_bounces = 8
    scene.cycles.diffuse_bounces = 4
    scene.cycles.glossy_bounces = 4
    scene.cycles.transparent_max_bounces = 12
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.filepath = str(output_path)

    set_color_management(color_management)


def output_path_for(model_path: Path, requested_output: str | None) -> Path:
    if requested_output:
        return Path(requested_output).expanduser().resolve()
    return model_path.with_name(f"{model_path.stem}_blender_preview.png")


def open_rendered_image(path: Path) -> None:
    try:
        if sys.platform.startswith("win"):
            os.startfile(path)  # type: ignore[attr-defined]
        elif sys.platform == "darwin":
            import subprocess

            subprocess.Popen(["open", str(path)])
        else:
            import subprocess

            subprocess.Popen(["xdg-open", str(path)])
    except Exception:
        print(f"Preview image written to: {path}")


def render_model(
    model_path_text: str,
    output_path_text: str | None,
    size: str | None,
    width: int | None,
    height: int | None,
    samples: int | None,
    view: str,
    brightness: float,
    transparent_alpha_cap: float,
    transparent_alpha_mode: str,
    azimuth: float | None,
    elevation: float | None,
    frame_padding: float,
    quality: str,
    show_render_window: bool,
    color_management: str,
) -> Path:
    model_path = Path(model_path_text).expanduser().resolve()
    if not model_path.exists():
        raise FileNotFoundError(model_path)
    if model_path.suffix.lower() not in {".glb", ".gltf"}:
        raise ValueError("Please select a .glb or .gltf file.")

    output_path = output_path_for(model_path, output_path_text)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    render_width, render_height, render_samples = quality_settings(quality, size, width, height, samples)
    use_render_window = show_render_window and not bpy.app.background
    install_render_progress_handlers(output_path, use_render_window)

    clear_scene()
    objects = import_model(model_path)
    configure_materials(objects, transparent_alpha_cap, transparent_alpha_mode)
    progress("Centering model and measuring bounds.")
    bounds_min, bounds_max, radius = center_imported_model(objects)
    setup_camera_and_lighting(
        bounds_min,
        bounds_max,
        radius,
        view,
        brightness,
        render_width,
        render_height,
        objects,
        frame_padding,
        azimuth,
        elevation,
    )
    setup_render_settings(output_path, render_width, render_height, render_samples, quality, color_management)

    progress(f"Rendering to: {output_path}")
    if use_render_window:
        progress("Starting Blender render window. Progress is shown in the Render Result window.")
        result = bpy.ops.render.render("INVOKE_DEFAULT", write_still=True)
        progress(f"Render operator state: {result}")
    else:
        bpy.ops.render.render(write_still=True)
        progress(f"Opening preview image: {output_path}")
        open_rendered_image(output_path)
    return output_path


class BEDROCKMAP_OT_render_preview(bpy.types.Operator):
    bl_idname = "bedrockmap.render_glb_preview"
    bl_label = "Render BedrockMap GLB Preview"
    bl_options = {"REGISTER"}

    filepath: bpy.props.StringProperty(subtype="FILE_PATH")
    filter_glob: bpy.props.StringProperty(default="*.glb;*.gltf", options={"HIDDEN"})

    def invoke(self, context: bpy.types.Context, event: bpy.types.Event):
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context: bpy.types.Context):
        args = parse_script_args()
        try:
            output_path = render_model(
                self.filepath,
                args.output,
                args.size,
                args.width,
                args.height,
                args.samples,
                args.view,
                args.brightness,
                args.transparent_alpha_cap,
                args.transparent_alpha_mode,
                args.azimuth,
                args.elevation,
                args.frame_padding,
                args.quality,
                not args.no_render_window,
                args.color_management,
            )
        except Exception as exc:
            self.report({"ERROR"}, str(exc))
            return {"CANCELLED"}
        self.report({"INFO"}, f"Rendered preview: {output_path}")
        return {"FINISHED"}


def register() -> None:
    bpy.utils.register_class(BEDROCKMAP_OT_render_preview)


def unregister() -> None:
    bpy.utils.unregister_class(BEDROCKMAP_OT_render_preview)


def main() -> None:
    args = parse_script_args()
    if args.model:
        output_path = render_model(
            args.model,
            args.output,
            args.size,
            args.width,
            args.height,
            args.samples,
            args.view,
            args.brightness,
            args.transparent_alpha_cap,
            args.transparent_alpha_mode,
            args.azimuth,
            args.elevation,
            args.frame_padding,
            args.quality,
            not args.no_render_window,
            args.color_management,
        )
        print(f"Rendered preview: {output_path}")
        return

    if bpy.app.background:
        raise SystemExit("Background mode requires -- --model path/to/model.glb")

    register()
    bpy.ops.bedrockmap.render_glb_preview("INVOKE_DEFAULT")


if __name__ == "__main__":
    main()
