import argparse
from concurrent.futures import ThreadPoolExecutor
from os import path
from typing import Dict, Iterable, List, Optional, Tuple, Union

import json5
import numpy as np
from PIL import Image


# Color rules are evaluated strictly from top to bottom. The key is a source
# entry and a list value contains entries to generate from it. A None value
# deletes the key itself. A name is a whole block entry, while "block.tag"
# addresses one color inside that block. For example,
# "red_flower.flower_rose": ["red_flower.poppy"] creates the poppy tag from
# the already dumped flower_rose tag.
BLOCK_COLOR_RULES = {
 "pink_petals.pink_petals_stem": None,
 "wildflowers.wildflowers_stem": None,
 "red_mushroom_block.mushroom_block_inside": None,
 "red_mushroom_block.mushroom_block_skin_stem": None,
 "cherry_leaves.cherry_leaves_opaque": None,
 "redstone_wire": None,

 "grass": ["grass_block"],
 "stone_slab": ["stone_block_slab"],
 "stone_slab2": ["stone_block_slab2"],
 "stone_slab3": ["stone_block_slab3"],
 "stone_slab4": ["stone_block_slab4"],
 "double_stone_slab": ["double_stone_block_slab"],
 "double_stone_slab2": ["double_stone_block_slab2"],
 "double_stone_slab3": ["double_stone_block_slab3"],
 "double_stone_slab4": ["double_stone_block_slab4"],
 "seaLantern": ["sea_lantern"],
 "tripWire": ["trip_wire"],
 "concretePowder": ["concrete_powder"],
 "redstone_block": ["redstone_wire"],
 "red_flower.flower_rose": ["red_flower.poppy", "poppy"],
}

def save_to_json(data, file_path):
    with open(file_path, 'w', encoding='utf-8') as f:
        json5.dump(data, f, indent=2, ensure_ascii=False,  quote_keys=True,
               trailing_commas=False)

def get_block_texture(json_file):
    """Map each block name to its ordered texture references from blocks.json."""
    texture_map = {}
    with open(json_file, 'r', encoding='utf-8') as f:
        data = json5.load(f)

    for block, info in data.items():
        if not isinstance(info, dict):
            texture_map[block] = []
            continue
        textures = info.get('textures', {})
        if "light_block" in block:
            textures = info.get('carried_textures')

        if isinstance(textures, dict):
            up_textures = []
            other_textures = []
            for direction, tex in textures.items():
                if tex and isinstance(tex, str):
                    if direction in ['up', 'top']:
                        if tex not in up_textures:
                            up_textures.append(tex)
                    else:
                        if tex not in other_textures:
                            other_textures.append(tex)

            texture_map[block] = up_textures + [t for t in other_textures if t not in up_textures]
        elif isinstance(textures, str) and textures:
            texture_map[block] = [textures]
        else:
            texture_map[block] = []
    return texture_map


#read textures/terrain_texture.json
def get_texture_terrain(json_file_path: str) -> Dict[str, List[str]]:
    try:
        with open(json_file_path, 'r', encoding='utf-8') as file:
            data = json5.load(file)
        texture_data = data.get('texture_data', {})
        result = {}
        for texture_name, texture_info in texture_data.items():
            if isinstance(texture_info, dict):
                texture_list = texture_info.get('textures', [])
                if isinstance(texture_list, list):
                    textures = []
                    for texture in texture_list:
                        #grass_top, etc.
                        if isinstance(texture, str):
                            textures.append(texture)

                        #grass side, etc.
                        elif isinstance(texture, Dict):
                            textures.append(texture["path"])
                    result[texture_name] = textures
                #barrier, etc.
                elif isinstance(texture_list, str):
                    result[texture_name] = [texture_list]
                    
        return result
    
    except Exception as e:
        print(f"Error in read terrain_texture.json: {e}")
        return {}
    

#
def map_block_textures(block_mapping: Dict[str, List[str]], 
                      texture_terrain: Dict[str, List[str]]) -> Dict[str, List[str]]:
    result = {}
    
    for block_name, texture_names in block_mapping.items():
        found_textures = []
        
        for texture_name in texture_names:
            if texture_name in texture_terrain:
                actual_paths = texture_terrain[texture_name]
                found_textures.extend(actual_paths)
                break 
        
        if found_textures:
            result[block_name] = found_textures
        else:
            result[block_name] = []
            print(f"Warning: block '{block_name}' of texture '{texture_names}' was not found")
    return result



def get_average_color_weighted(image_path: str) -> Optional[List[int]]:
    try:
        if not path.exists(image_path):
            return None

        with Image.open(image_path) as img:
            if img.mode != 'RGBA':
                img = img.convert('RGBA')

            img_array = np.array(img)

            r = img_array[:, :, 0]
            g = img_array[:, :, 1]
            b = img_array[:, :, 2]
            a = img_array[:, :, 3]

            non_transparent_mask = a > 0

            if np.any(non_transparent_mask):
                r_non = r[non_transparent_mask]
                g_non = g[non_transparent_mask]
                b_non = b[non_transparent_mask]
                weights = a[non_transparent_mask] / 255.0

                total_weight = np.sum(weights)
                if total_weight > 0:
                    avg_r = np.sum(r_non * weights) / total_weight
                    avg_g = np.sum(g_non * weights) / total_weight
                    avg_b = np.sum(b_non * weights) / total_weight
                else:
                    avg_r = avg_g = avg_b = 0
            else:
                avg_r = avg_g = avg_b = 0
            avg_a = np.mean(a).astype(int)
            return [
                int(np.clip(avg_r, 0, 255)),
                int(np.clip(avg_g, 0, 255)),
                int(np.clip(avg_b, 0, 255)),
                int(avg_a)
            ]
    except Exception as e:
        print(f"Error: {image_path}: {e}")
        return None


def load_texture_color(file_path: str) -> Optional[List[int]]:
    """Average color of a texture file, falling back from .png to .tga."""
    return (get_average_color_weighted(file_path + ".png")
            or get_average_color_weighted(file_path + ".tga"))


def collect_texture_colors(root: str, texture_paths: Iterable[str]) -> Dict[str, List[int]]:
    """Decode all unique textures concurrently; PIL/numpy release the GIL."""
    def load(texture_path: str) -> Tuple[str, Optional[List[int]]]:
        # terrain_texture.json paths are pack-relative, not cwd-relative.
        return texture_path, load_texture_color(path.join(root, texture_path))

    with ThreadPoolExecutor() as pool:
        return {tex: color for tex, color in pool.map(load, texture_paths) if color}


def block_tag_filter(name: str) -> str:
    return name.split('/')[-1]


def format_color(color: List[int]) -> str:
    """#rrggbbaa, the form the C++ color table reader expects."""
    return "#{:02x}{:02x}{:02x}{:02x}".format(*color)


def split_color_entry(reference: str) -> Tuple[str, Optional[str]]:
    """Split a BLOCK_COLOR_REUSE reference into its block and optional tag."""
    block, separator, tag = reference.partition('.')
    return block, tag if separator else None


def color_entry_key(block: str) -> str:
    return block if block.startswith("minecraft:") else "minecraft:" + block


ReuseValue = Union[Dict[str, str], str]


def resolve_reuse_source(result: Dict[str, Dict[str, str]], source: str) -> Optional[ReuseValue]:
    """Resolve a source reference against colors already dumped."""
    source_block, source_tag = split_color_entry(source)
    source_entry = result.get(color_entry_key(source_block))
    if not source_entry:
        return None

    if source_tag is not None:
        color = source_entry.get(source_tag)
        return color

    return dict(source_entry)


def delete_color_entry(result: Dict[str, Dict[str, str]], reference: str) -> None:
    """Delete a whole block or one tagged color from the result."""
    block, tag = split_color_entry(reference)
    key = color_entry_key(block)
    if tag is None:
        result.pop(key, None)
        return

    entry = result.get(key)
    if entry is None:
        return
    entry.pop(tag, None)
    if not entry:
        result.pop(key, None)


def apply_color_rules(result: Dict[str, Dict[str, str]]) -> None:
    """Apply reuse and deletion rules in their strict declaration order.

    A whole-block source can generate either whole blocks or tagged entries. If
    a tagged target has the same tag in the source, that color is used; a
    single-tag source is also unambiguous. A tagged source can generate a
    tagged target directly, or a one-tag whole-block target carrying that tag.
    """
    for source, targets in BLOCK_COLOR_RULES.items():
        if targets is None:
            delete_color_entry(result, source)
            continue
        if not isinstance(targets, list) or not all(isinstance(target, str) for target in targets):
            print(f"Warning: invalid color rule for '{source}'")
            continue

        _, source_tag = split_color_entry(source)
        source_value = resolve_reuse_source(result, source)
        if source_value is None:
            print(f"Warning: no reusable color entry found for '{source}'")
            continue

        for target in targets:
            target_block, target_tag = split_color_entry(target)
            target_key = color_entry_key(target_block)

            if target_tag is None:
                if source_tag is None:
                    if not isinstance(source_value, dict):
                        continue
                    result[target_key] = dict(source_value)
                else:
                    if not isinstance(source_value, str):
                        continue
                    result[target_key] = {source_tag: source_value}
                continue

            if source_tag is not None:
                if not isinstance(source_value, str):
                    continue
                color = source_value
            else:
                if not isinstance(source_value, dict):
                    continue
                color = source_value.get(target_tag)
                if color is None and len(source_value) == 1:
                    color = next(iter(source_value.values()))
                if color is None:
                    print(f"Warning: source '{source}' has no color for target '{target}'")
                    continue
            result.setdefault(target_key, {})[target_tag] = color


def export_block_colors(texture_mapping: Dict[str, List[str]], output_file: str, root: str) -> None:
    # Unique keys, keeping first-seen order so the output stays reproducible.
    unique_textures = dict.fromkeys(
        item for items in texture_mapping.values() for item in items if isinstance(item, str)
    )
    color_cache = collect_texture_colors(root, unique_textures)

    result = {}
    for block_name, items in texture_mapping.items():
        block_dict = {}
        seen = set()
        for item in items:
            if isinstance(item, str) and item not in seen and item in color_cache:
                seen.add(item)
                tag = block_tag_filter(item)
                block_dict[tag] = format_color(color_cache[item])

        if not block_dict:
            continue
        result["minecraft:" + block_name] = block_dict

    apply_color_rules(result)

    save_to_json(result, output_file)


def main() -> None:
    parser = argparse.ArgumentParser(description="Dump average block colors from a resource pack.")
    parser.add_argument("texture_root_path", help="resource pack root (contains blocks.json)")
    parser.add_argument("block_color_output_path", help="output json file")
    args = parser.parse_args()

    root = args.texture_root_path
    block_texture = get_block_texture(path.join(root, "blocks.json"))
    texture_terrain = get_texture_terrain(path.join(root, "textures", "terrain_texture.json"))
    mapping = map_block_textures(block_texture, texture_terrain)

    export_block_colors(mapping, args.block_color_output_path, root)


if __name__ == "__main__":
    main()

