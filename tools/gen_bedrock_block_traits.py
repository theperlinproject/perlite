#!/usr/bin/env python3
"""Writes src/world/level/levelgen/bedrock/BlockTraitsData.cpp: the physical traits the Bedrock world
generator asks of a block (solid, the faces it supports, its collision box, how it meets liquids, what it
may be placed on, transparency, replaceability), per block and state, from a Rust block definition
crate (Apache-2.0) whose impl/ folder holds one definition per block.

usage: gen_bedrock_block_traits.py <block crate src folder> <output .cpp>
"""
import os
import re
import sys


def matching(text, start, open_ch, close_ch):
    depth = 0
    for i in range(start, len(text)):
        if text[i] == open_ch:
            depth += 1
        elif text[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i
    raise ValueError("unbalanced")


def split_top(text):
    """Splits a component list at top-level commas."""
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            if cur.strip():
                out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


LIQUID = {
    "CONTAINS_WATER": "cw",
    "CONTAINS_AND_BROKEN_BY_WATER": "cbw",
    "CONTAINS_AND_BROKEN_BY_WATER_AND_LAVA": "cbwbl",
    "BROKEN_BY_WATER": "bw",
    "BROKEN_BY_LAVA": "bl",
    "BROKEN_BY_WATER_AND_LAVA": "bwbl",
}


def num(v):
    v = v.strip().replace("f32::MAX", "3.4e38").replace("f32::MIN", "-3.4e38")
    return repr(float(eval(v)))


def traits(components):
    out = []
    for c in split_top(components):
        c = " ".join(c.split())
        if c.startswith("SolidComponent"):
            out.append("S1" if "true" in c else "S0")
        elif c.startswith("SupportComponent::"):
            out.append({"SupportComponent::TOP": "U1", "SupportComponent::ALL": "U2"}[c])
        elif c.startswith("CollisionBoxComponent::enabled("):
            out.append("C1" if "true" in c else "C0")
        elif c.startswith("CollisionBoxComponent::new("):
            vals = re.findall(r"Vec3::new\(([^)]*)\)", c)
            nums = [num(x) for v in vals for x in v.split(",")]
            out.append("B" + ",".join(nums))
        elif c.startswith("CollisionBoxComponent::default()") or c == "CollisionBoxComponent":
            out.append("C1")
        elif c.startswith("LiquidDetectionComponent::"):
            out.append("L" + LIQUID[c.split("::")[1]])
        elif c.startswith("TransparentComponent"):
            out.append("T1" if "true" in c else "T0")
        elif c.startswith("ReplaceableComponent"):
            out.append("R0" if "false" in c else "R1")
        elif c.startswith("PlacementFilterComponent::new(placement::"):
            out.append("P" + c[len("PlacementFilterComponent::new(placement::"):-1])
        elif c.startswith("PlacementFilterComponent::with_denied("):
            args = split_top(c[len("PlacementFilterComponent::with_denied("):-1])
            rules = args[0].replace("placement::", "")
            out.append("P" + ("" if rules == "&[]" else rules) + "/" + args[1].replace("placement::", ""))
    return out


def condition(expr):
    expr = re.sub(r'it\["([^"]+)"\]', r"\1", expr)
    expr = expr.replace('"', "").replace(" ", "")
    return expr


def main():
    src, out_path = sys.argv[1], sys.argv[2]
    ids = {}
    for m in re.finditer(r'pub const (\w+): &str = "([^"]+)";', open(os.path.join(src, "block_id.rs")).read()):
        ids[m.group(1)] = m.group(2)
    lines = []
    impl = os.path.join(src, "impl")
    for name in sorted(os.listdir(impl)):
        if not name.endswith(".rs") or name == "mod.rs":
            continue
        text = open(os.path.join(impl, name)).read()
        for m in re.finditer(r"const_block!\s*\{", text):
            body = text[m.end() - 1:matching(text, m.end() - 1, "{", "}") + 1]
            ident = re.search(r"identifier:\s*block_id::(\w+)", body)
            if not ident:
                continue
            block = ids[ident.group(1)]
            cs = body.index("components:")
            ob = body.index("[", cs)
            base = traits(body[ob + 1:matching(body, ob, "[", "]")])
            perms = []
            for pm in re.finditer(r"const_permutation!\s*\{", body):
                pbody = body[pm.end() - 1:matching(body, pm.end() - 1, "{", "}") + 1]
                cond = re.search(r"condition:\s*\|it\|\s*(.*?),\s*components:", pbody, re.S).group(1)
                pc = pbody.index("components:")
                pob = pbody.index("[", pc)
                perms.append((condition(cond), traits(pbody[pob + 1:matching(pbody, pob, "[", "]")])))
            line = block + "|" + " ".join(base)
            for cond, t in perms:
                if t:
                    line += ";" + cond + "=>" + " ".join(t)
            lines.append(line)
    rules = []
    ptext = open(os.path.join(src, "placement.rs")).read()
    for m in re.finditer(r"pub const (\w+): &\[PlacementRule\] = &\[", ptext):
        end = matching(ptext, m.end() - 1, "[", "]")
        body = ptext[m.end():end]
        groups = []
        for r in re.finditer(r"PlacementRule\s*\{", body):
            rb = body[r.end() - 1:matching(body, r.end() - 1, "{", "}") + 1]
            faces = re.findall(r"PlacementFace::(\w+)", rb)
            blocks = [ids[b] for b in re.findall(r"block_id::(\w+)", rb.split("block_filter")[1])]
            groups.append(",".join(f.lower() for f in faces) + ":" + ",".join(blocks))
        rules.append("#" + m.group(1) + "|" + ";".join(groups))
    for m in re.finditer(r"pub const (\w+): &\[&str\] = &\[([^\]]*)\]", ptext):
        rules.append("#!" + m.group(1) + "|" + ",".join(ids[b] for b in re.findall(r"block_id::(\w+)", m.group(2))))
    data = "\n".join(rules + lines) + "\n"
    with open(out_path, "w") as f:
        f.write("// Generated by tools/gen_bedrock_block_traits.py; do not edit.\n")
        f.write("// Block traits for the Bedrock world generator, one block per line:\n")
        f.write("// name|base traits;condition=>traits;...; lines starting with # are placement rules,\n")
        f.write("// #! the blocks a block may not be placed on.\n")
        f.write('#include "world/level/levelgen/bedrock/BlockTraits.hpp"\n\n')
        f.write("namespace perlite::bedrock {\n")
        f.write("    // One entry per line (MSVC limits the length of a single string literal).\n")
        f.write("    const char* const kBlockTraitsLines[] = {\n")
        for line in data.splitlines():
            esc = line.replace("\\", "\\\\").replace('"', '\\"')
            f.write('        "' + esc + '",\n')
        f.write("        nullptr,\n    };\n}\n")
    print(len(lines), "blocks,", len(rules), "placement lists")


if __name__ == "__main__":
    main()
