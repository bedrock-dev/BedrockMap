import argparse
from concurrent.futures import ThreadPoolExecutor
from os import path
from typing import Dict, Iterable, List, Optional, Tuple

import json5
import numpy as np
from PIL import Image


# Colors reused from another entry, keyed by the block that is dumped from textures.
# A list value names blocks that get an identical top-level entry: "grass" -> "grass_block".
# A dict value adds tags inside this block's own entry, each taking the color of an existing
# tag of the same block: "red_flower": {"poppy": "flower_rose"}.
BLOCK_COLOR_REUSE = {
 "grass":["grass_block"],
 "stone_slab":["stone_block_slab"],
 "stone_slab2":["stone_block_slab2"],
 "stone_slab3":["stone_block_slab3"],
 "stone_slab4":["stone_block_slab4"],
 "double_stone_slab":["double_stone_block_slab"],
 "double_stone_slab2":["double_stone_block_slab2"],
 "double_stone_slab3":["double_stone_block_slab3"],
 "double_stone_slab4":["double_stone_block_slab4"],
 "seaLantern":["sea_lantern"],
 "tripWire":["trip_wire"],
 "concretePowder":["concrete_powder"],
 "redstone_block":["redstone_wire"],
 "red_flower":{"poppy":"flower_rose"}
}

# Textures that must not be dumped, keyed by block name.
# A set value lists texture tags (the last path segment) to skip for that block;
# None skips the block entirely.
BLOCK_COLOR_BLACKLIST = {
 "pink_petals":{"pink_petals_stem"},
 "wildflowers":{"wildflowers_stem"},
 "red_mushroom_block":{"mushroom_block_inside", "mushroom_block_skin_stem"},
 "cherry_leaves":{"cherry_leaves_opaque"},
 "redstone_wire":None
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


def export_block_colors(texture_mapping: Dict[str, List[str]], output_file: str, root: str) -> None:
    # Unique keys, keeping first-seen order so the output stays reproducible.
    unique_textures = dict.fromkeys(
        item for items in texture_mapping.values() for item in items if isinstance(item, str)
    )
    color_cache = collect_texture_colors(root, unique_textures)

    result = {}
    for block_name, items in texture_mapping.items():
        blacklisted = BLOCK_COLOR_BLACKLIST.get(block_name, ())
        if blacklisted is None:
            continue  # the whole block is blacklisted
        block_dict = {}
        seen = set()
        for item in items:
            if isinstance(item, str) and item not in seen and item in color_cache:
                seen.add(item)
                tag = block_tag_filter(item)
                if tag in blacklisted:
                    continue
                block_dict[tag] = format_color(color_cache[item])

        reuse = BLOCK_COLOR_REUSE.get(block_name)
        if isinstance(reuse, dict):
            for alias, source in reuse.items():
                if source in block_dict:
                    block_dict[alias] = block_dict[source]
            reuse = ()  # tag aliases only, no extra block entries
        if not block_dict:
            continue
        result["minecraft:" + block_name] = block_dict

        # Blocks that reuse another block's textures.
        for generated in reuse or ():
            print("Generate texture for " + block_name + " -> " + generated)
            result["minecraft:" + generated] = block_dict

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

