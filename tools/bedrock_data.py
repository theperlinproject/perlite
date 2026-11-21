#!/usr/bin/env python3
import argparse
import json
import re
import struct
import sys
from pathlib import Path

TAGS = {"end": 0, "byte": 1, "short": 2, "int": 3, "long": 4, "float": 5, "double": 6, "byteArray": 7,
        "string": 8, "list": 9, "compound": 10, "intArray": 11, "longArray": 12}

COLORS = ["white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan",
          "purple", "blue", "brown", "green", "red", "black"]

VERSIONS = {"legacy": 0, "data_driven": 1, "none": 2}


def varuint(value):
    out = bytearray()
    value &= (1 << 64) - 1
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def zigzag32(value):
    return varuint(((value << 1) ^ (value >> 31)) & 0xFFFFFFFF)


def zigzag64(value):
    return varuint(((value << 1) ^ (value >> 63)) & 0xFFFFFFFFFFFFFFFF)


def text(value):
    raw = value.encode("utf-8")
    return varuint(len(raw)) + raw


def long_value(value):
    if isinstance(value, list):
        high, low = value
        combined = ((high & 0xFFFFFFFF) << 32) | (low & 0xFFFFFFFF)
        return combined - (1 << 64) if combined >= 1 << 63 else combined
    return int(value)


def payload(tag, value):
    if tag == 1:
        return struct.pack("<b", value)
    if tag == 2:
        return struct.pack("<h", value)
    if tag == 3:
        return zigzag32(value)
    if tag == 4:
        return zigzag64(long_value(value))
    if tag == 5:
        return struct.pack("<f", value)
    if tag == 6:
        return struct.pack("<d", value)
    if tag == 7:
        return zigzag32(len(value)) + bytes(v & 0xFF for v in value)
    if tag == 8:
        return text(value)
    if tag == 9:
        element = TAGS[value["type"]]
        items = value["value"]
        return bytes([element]) + zigzag32(len(items)) + b"".join(payload(element, v) for v in items)
    if tag == 10:
        out = bytearray()
        for name, child in value.items():
            child_tag = TAGS[child["type"]]
            out.append(child_tag)
            out += text(name)
            out += payload(child_tag, child["value"])
        out.append(0)
        return bytes(out)
    if tag == 11:
        return zigzag32(len(value)) + b"".join(zigzag32(v) for v in value)
    if tag == 12:
        return zigzag32(len(value)) + b"".join(zigzag64(long_value(v)) for v in value)
    raise ValueError(f"unknown NBT tag {tag}")


def root(compound):
    return bytes([10]) + text("") + payload(10, compound)


def t_byte(v):
    return {"type": "byte", "value": int(v)}


def t_int(v):
    return {"type": "int", "value": int(v)}


def t_float(v):
    return {"type": "float", "value": float(v)}


def t_string(v):
    return {"type": "string", "value": v}


def t_compound(**fields):
    return {"type": "compound", "value": fields}


def t_list(kind, values):
    return {"type": "list", "value": {"type": kind, "value": values}}


def write_named_blobs(path, entries):
    out = bytearray(struct.pack("<I", len(entries)))
    for name, blob in entries:
        raw = name.encode("utf-8")
        out += struct.pack("<H", len(raw)) + raw + struct.pack("<I", len(blob)) + blob
    path.write_bytes(bytes(out))


def convert_items(items, out):
    palette = []
    components = []
    for item in items:
        name = "minecraft:" + item["name"]
        version = VERSIONS.get(item.get("version", "none"), 2)
        palette.append([name, int(item["id"]), version, 1 if item.get("componentBased") else 0])
        nbt = item.get("nbt")
        if nbt and nbt.get("value"):
            components.append((name, root(nbt["value"])))
    (out / "items.json").write_text(json.dumps(palette, separators=(",", ":")), encoding="utf-8")
    write_named_blobs(out / "item_components.bin", components)


def particles():
    return t_compound(particle_count=t_int(48), texture=t_string(""), tint_method=t_string("none"))


def liquid_detection():
    rule = {"canContainLiquid": t_byte(1), "liquidType": t_string("water"),
            "onLiquidTouches": t_string("blocking"), "stopsLiquidFromDirection": t_byte(0),
            "use_liquid_clipping": t_byte(0)}
    return t_compound(detectionRules=t_list("compound", [rule]))


def building_block(block, family, shape):
    name = block["name"]
    wool = family == "wool"
    double = shape == "double_slab"
    tool = "minecraft:is_shears_item_destructible" if wool else "minecraft:is_pickaxe_item_destructible"
    components = {"minecraft:destructible_by_mining": t_compound(value=t_float(block["hardness"]))}
    if not double:
        components["minecraft:destruction_particles"] = particles()
        components["minecraft:liquid_detection"] = liquid_detection()
    components["minecraft:redstone_conductivity"] = t_compound(allowsWireToStepDown=t_byte(1),
                                                               redstoneConductor=t_byte(1 if double else 0))
    if shape == "stairs":
        components["minecraft:support"] = t_compound(shape=t_string("stair"))
        archetype = t_compound(base_block=t_string("minecraft:" + name[: -len("_stairs")]),
                               type=t_string("stair_block"))
        group = "minecraft:itemGroup.name.stairs"
    else:
        base = name[: -len("_double_slab")] if double else name[: -len("_slab")]
        partner = base + ("_slab" if double else "_double_slab")
        archetype = t_compound(double_slab_block=t_string("minecraft:" + partner),
                               is_double=t_byte(1 if double else 0), type=t_string("slab_block"))
        group = "minecraft:itemGroup.name.slab"
    data = {"block_archetype": archetype, "block_id": t_int(block["id"]), "material": t_string("solid")}
    if wool:
        data["can_dampen_vibrations"] = t_byte(1)
        data["can_occlude_vibrations"] = t_byte(1)
        if not double:
            data["translucency"] = t_float(block["hardness"])
    else:
        data["requires_correct_tool_for_drops"] = t_byte(1)
    menu = t_compound(category=t_string("none" if double else "construction"),
                      group=t_string("" if double else group),
                      is_hidden_in_commands=t_byte(1 if double else 0))
    return {
        "blockTags": t_list("string", [tool]),
        "components": t_compound(**components),
        "menu_category": menu,
        "molangVersion": t_int(13),
        "vanilla_block_data": t_compound(**data),
    }


def cross_plant(block, archetype="bush_block", material_name="plant", replaceable=True):
    geometry = t_compound(identifier=t_string("minecraft:geometry.cross"), culling=t_string(""),
                          culling_layer=t_string("minecraft:culling_layer.undefined"), culling_shape=t_string(""),
                          bone_visibility=t_compound(), ignoreGeometryForIsSolid=t_byte(0),
                          isV1Fullblock=t_byte(0), needsLegacyTopRotation=t_byte(0),
                          useBlockTypeLightAbsorption=t_byte(0), uv_lock=t_byte(0))
    material = t_compound(ambient_occlusion=t_float(0), packed_bools=t_byte(5),
                          render_method=t_string("alpha_test_single_sided"), texture=t_string(block["name"]),
                          tint_method=t_string("none"))
    components = {
        "minecraft:collision_box": t_compound(boxes=t_list("compound", []), enabled=t_byte(0)),
        "minecraft:destructible_by_mining": t_compound(value=t_float(block["hardness"])),
        "minecraft:destruction_particles": particles(),
        "minecraft:geometry": geometry,
        "minecraft:material_instances": t_compound(mappings=t_compound(), materials=t_compound(**{"*": material})),
        "minecraft:selection_box": t_compound(enabled=t_byte(1), origin=t_list("float", [-8.0, 0.0, -8.0]),
                                              size=t_list("float", [16.0, 16.0, 16.0])),
    }
    if replaceable:
        components["minecraft:replaceable"] = t_compound()
    return {
        "components": t_compound(**components),
        "menu_category": t_compound(category=t_string("nature"), group=t_string(""),
                                    is_hidden_in_commands=t_byte(0)),
        "molangVersion": t_int(13),
        "vanilla_block_data": t_compound(block_archetype=t_compound(type=t_string(archetype)),
                                         block_id=t_int(block["id"]), material=t_string(material_name)),
    }


SHAPE = re.compile(r"^(" + "|".join(COLORS) + r")_(concrete|wool)_(double_slab|slab|stairs)$")


def convert_blocks(blocks, out):
    entries = []
    for block in sorted(blocks, key=lambda b: b["name"]):
        match = SHAPE.match(block["name"])
        if match:
            properties = building_block(block, match[2], match[3])
        elif block["name"] == "red_shrub":
            properties = cross_plant(block)
        elif block["name"] == "shelf_mushroom":
            properties = cross_plant(block, "wall_foliage_block", "solid_plant", False)
        else:
            continue
        entries.append(("minecraft:" + block["name"], root(properties)))
    write_named_blobs(out / "block_properties.bin", entries)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--items", required=True, type=Path)
    parser.add_argument("--blocks", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    try:
        args.out.mkdir(parents=True, exist_ok=True)
        for stale in ("creative.json", "LICENSE", "COPYING"):
            (args.out / stale).unlink(missing_ok=True)
        convert_items(json.loads(args.items.read_text(encoding="utf-8")), args.out)
        convert_blocks(json.loads(args.blocks.read_text(encoding="utf-8")), args.out)
    except (OSError, ValueError, KeyError) as e:
        print(f"bedrock_data: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
