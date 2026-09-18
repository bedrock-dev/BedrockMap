import os
from os import path
import sys
import json5
from collections import defaultdict
from typing import Dict, List, Optional,Any
from PIL import Image
import numpy as np


Generates = {
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
 "concretePowder":["concrete_powder"]
};

def save_to_json(data, file_path):
    with open(file_path, 'w', encoding='utf-8') as f:
        json5.dump(data, f, indent=2, ensure_ascii=False,  quote_keys=True,
               trailing_commas=False)

#read blocks.json
def get_blcok_texture(json_file):
    texture_map = {}
    with open(json_file, 'r', encoding='utf-8') as f:
        data = json5.load(f)

    for block, info in data.items():
        if isinstance(info, dict):
            textures = info.get('textures', {})
        else:
            texture_map[block] = []
            continue
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
        if not os.path.exists(image_path):
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
                a_non = a[non_transparent_mask]
                
                weights = a_non / 255.0
                
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
        print(f"Error: {e}")
        return None



def block_tag_filter(name: str):
    return name.split('/')[-1];


def export_block_colors(texture_mapping: Dict[str, List[str]], output_file: str) -> None:
    result = {}
    color_cache = {}

    unique_textures = set()
    for block_name, items in texture_mapping.items():
        for path in items:
            unique_textures.add(path)
    
    # 计算颜色
    for texture_path in unique_textures:
        avg_color = get_average_color_weighted(texture_path + ".png")
        if avg_color is None:
            print("try load tga file " + texture_path + ".tga")
            avg_color = get_average_color_weighted(texture_path + ".tga")
        if avg_color:
            color_cache[texture_path] = avg_color
        

    # 构建结果
    for block_name, items in texture_mapping.items():
        block_dict = {}
        seen = set()
        for item in items:
            if isinstance(item, str):
                if item not in seen and item in color_cache:
                    seen.add(item)
                    block_dict[block_tag_filter(item)] = color_cache[item]
        if len(block_dict.items()) == 0:
            continue
        result["minecraft:"+block_name] = block_dict

        #generate some unexpected
        if block_name in Generates:
            print("Generate texture for "+ block_name)
            for gene in Generates[block_name]:
                print(" - " + gene)
                result["minecraft:" + gene] = block_dict



    save_to_json(result, output_file)

if __name__ == "__main__":
    length = len(sys.argv)
    if length != 3:
        print("Use python dumper <texture_root_path> <block_color_output_path>")
        exit(0)
    root = sys.argv[1]
    output = sys.argv[2]

    block_to_texture = {}
    texture_to_path = {}

    BLOCKS_JSON = path.join(root,"blocks.json");
    TEXTURE_JSON = path.join(root,"textures", "terrain_texture.json");
    block_texture = get_blcok_texture(BLOCKS_JSON)
    texture_terrain =  get_texture_terrain(TEXTURE_JSON)
    mapping = map_block_textures(block_texture, texture_terrain)

    export_block_colors(mapping,output);

