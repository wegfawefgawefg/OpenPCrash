#!/usr/bin/env python3
"""Write tools/extract/ui_tables.inc: where the original's UI tables sit in the XBE and how to read them.

Reads the decomp (../phantom-crash-decomp, read only): the declarations in src/data/ui_*.c and the other UI
data files give each table's name, address, record type and length; include/tables.h gives the record
layouts; the screen code under src/ gives which functions read each table, and from their names which screen
it belongs to. Only names, addresses and layouts go into the .inc; the values themselves are read from the
retail XBE by pc_extract's ui family (tools/extract/ui_layouts.cpp), so nothing from the game is committed.

    python3 tools/ui_tables.py [--decomp DIR]
"""
import argparse
import re
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "extract" / "ui_tables.inc"

# files read whole: every table in them is UI
UI_FILES = ["ui_a.c", "ui_a2.c", "ui_a3.c", "ui_a4.c", "ui_a4b.c", "ui_b.c", "ui_b2.c", "ui_b2a.c",
            "ui_c.c", "ui_d.c", "ui_e.c", "ui_e2.c", "ui_f.c", "misc_e.c", "portraits.c",
            "portrait_bias.c", "pepper_talk.c", "save_dialogs.c", "jp_tutorial.c"]
# files mixing UI and other tables: only typed UI records, or tables UI screens read
MIXED_FILES = ["misc_a.c", "misc_b.c", "misc_c.c", "misc_d.c", "misc_f.c", "misc_k.c", "stage_scene.c",
               "misc_g.c", "misc_h.c", "misc_i.c", "misc_j.c", "state_tables.c", "shop_strings.c",
               "sv_attach.c"]
UI_TYPES = {"SpriteSlot", "MsgBox", "HudRow", "ScaleAlphaKey", "PortraitActor", "PortraitSprite",
            "PepperTalkItem", "DealerScreenRows"}
# the sprite player's records: ui_timeline.cpp reads those itself
SKIP_TYPES = {"AnimCommand", "AnimFrame", "AnimResource", "AnimSpriteKey", "AnimPart"}

# function name prefix -> screen, first match wins; a table's screen is its readers' screen
SCREENS = [
    ("sonic_amp", "sonic_amp"), ("title_", "title"), ("name_entry", "name_entry"),
    ("options_", "options"), ("player_config", "options"), ("plus_tech", "plus_tech"),
    ("records_check", "record"), ("record_", "record"), ("wild_machines", "wild_machines"),
    ("dealer", "dealer"), ("C::dealer", "dealer"), ("hud_", "hud"), ("reticle_box", "hud"),
    ("pepper_talk", "pepper_talk"), ("level_meter", "level_meter"), ("media_", "media_playlist"),
    ("music_catalogue", "media_playlist"), ("keep_sv", "keep_sv"), ("save_", "save"),
    ("key_config", "key_config"), ("vs_", "vs_mode"), ("sound_config", "sound_config"),
    ("system_screen", "system"), ("club_wired", "club_wired"), ("pad_missing", "pause"),
    ("pause_", "pause"), ("garage_", "garage"), ("hangar_", "hangar"), ("town_map", "town_map"),
    ("area_select", "area_select"), ("schedule_", "schedule"), ("spec_", "spec_scoobee"),
    ("stock_", "stock_module"), ("job_", "job"), ("adjust_screen", "adjust"), ("gamma_", "adjust"),
    ("battle_result", "result"), ("result_", "result"), ("replay_", "replay"),
    ("assemble_screen", "assemble"), ("attract_", "attract"), ("portrait_", "portraits"),
    ("font_", "font"), ("glyph_", "font"), ("fullwidth_", "font"), ("ascii_char", "font"),
    ("msg_box", "message_box"), ("talk_policy", "talk"), ("uplift", "uplift"),
    ("customize", "customize"), ("ending_data", "ending_data"), ("logo_", "logo"),
    ("scoobee_parts", "model_viewer"), ("model_viewer", "model_viewer"),
    ("sprite_keyframe", "common"), ("screen_tint", "common"), ("sprite_rows", "common"),
    ("day_draw", "common"), ("money_draw", "common"), ("disc_error", "common"),
    ("shot_model", "battle"), ("battle_update", "battle"),
]

# the decomp leaves these tables (and the BSS tables the model viewer's points live in) as D_<address>:
# real names from their readers. The converted data keeps the D_ name beside the new one
# ((decomp D_...)), so a search for the address still finds the table.
TABLE_NAMES = {
    "D_001D6F34": "attract_next_areas",
    "D_001D7050": "job_route_mark_points",
    "D_001DADC0": "assemble_lines",
    "D_001DAED4": "assemble_points",
    "D_001DAF38": "part_card_rows_y",
    "D_001DB090": "garage_plates",
    "D_001DB180": "garage_category_places",
    "D_001DB49C": "garage_menu_help",
    "D_001DB4C0": "garage_menu_help_at",
    "D_001DB594": "garage_menu_items",
    "D_001DB5B4": "garage_menu_lit",
    "D_001DB838": "hangar_list_lines",
    "D_001DC4CC": "wild_machines_menu_icons",
    "D_001DC58C": "wild_machines_buy_help",
    "D_001DCC5C": "wild_machines_sell_help",
    "D_001DCC80": "wild_machines_chip_help",
    "D_001DCE04": "wild_machines_used_help",
    "D_001DCE08": "wild_machines_chip_leave_help",
    "D_001DCE18": "wild_machines_menu_rows",
    "D_001DCF08": "wild_machines_frame_top",
    "D_001DCF18": "wild_machines_frame_bottom",
    "D_001DCF28": "wild_machines_frame_lit",
    "D_001DCF90": "wild_machines_tab_pictures",
    "D_001DCF9C": "wild_machines_card_left",
    "D_001DCFBC": "wild_machines_card_right",
    "D_001DCFDC": "wild_machines_class_marks",
    "D_001DCFFC": "wild_machines_used_class_marks",
    "D_001DD0C8": "wild_machines_used_places",
    "D_001DD100": "wild_machines_part_sprites",
    "D_001DD398": "wild_machines_box_places",
    "D_001DD41C": "wild_machines_garage_marks",
    "D_001DD444": "wild_machines_sell_garage_marks",
    "D_001F93DC": "title_menu_sprites",
    "D_001F9404": "title_menu_at",
    "D_001F94E0": "name_entry_layout",
    "D_001FA750": "plus_tech_labels",
    "D_001FA75C": "plus_tech_tab_help",
    "D_001FA770": "plus_tech_blurbs_body",
    "D_001FA7B0": "plus_tech_blurbs_legs",
    "D_001FA850": "plus_tech_blurbs_arm",
    "D_001FA990": "plus_tech_blurbs_back",
    "D_001FAAA8": "plus_tech_blurbs_chip",
    "D_001FAC28": "plus_tech_blurbs_option",
    "D_001FAE20": "plus_tech_chip_help",
    "D_001FAFA0": "plus_tech_chip_list_help",
    "D_001FAFB8": "plus_tech_menu_icons",
    "D_001FAFC8": "plus_tech_menu_items",
    "D_001FAFD4": "plus_tech_menu_lit",
    "D_001FB0E8": "plus_tech_frame_ids",
    "D_001FB0FC": "plus_tech_tab_sprites",
    "D_001FB110": "plus_tech_overlay_sprites",
    "D_001FB120": "plus_tech_overlay_at",
    "D_001FB138": "plus_tech_overlay_speed",
    "D_001FB150": "plus_tech_overlay_alpha",
    "D_001FB15C": "plus_tech_overlay_height",
    "D_001FB168": "plus_tech_card_left",
    "D_001FB17C": "plus_tech_card_right",
    "D_001FB190": "plus_tech_class_marks",
    "D_001FB23C": "plus_tech_garage_marks",
    "D_001FC774": "schedule_day_digits",
    "D_001FCB4C": "schedule_marks",
    "D_001FDD30": "sound_config_items",
    "D_001FDE68": "sound_config_items_at",
    "D_001FDEC8": "spec_scoobee_help",
    "D_001FDECC": "spec_scoobee_help_page2",
    "D_001FE138": "stock_module_lines",
    "D_001FE7CC": "stock_module_kind_starts",
    "D_001FEC34": "vs_area_lines",
    "D_001FFD08": "keep_sv_help",
    "D_001FFE10": "keep_sv_boxes",
    "D_00200720": "keep_sv_row_places",
    "D_002417C8": "model_viewer_spread_b",
    "D_00241BB8": "model_viewer_spread_a",
    "D_00242038": "model_viewer_part_places",
    "D_00242648": "model_viewer_class_points",
    "D_002426BC": "model_viewer_fit_body",
    "D_002426E0": "model_viewer_fit_leg",
    "D_00242738": "model_viewer_fit_arm",
    "D_002427C0": "model_viewer_fit_back",
    "D_0098C518": "model_viewer_points_c",
    "D_0098C618": "model_viewer_points_a",
    "D_0098C718": "model_viewer_points_d",
    "D_0098C818": "model_viewer_points_b",
}

# record layouts the decomp leaves with f_ names, named from their readers (none left today)
RENAMES = {}
SPRITE_FIELDS = {("SpriteSlot", "sprite"), ("HudRow", "sprite"), ("HudElemRow", "sprite"),
                 ("PortraitSprite", "base"),
                 ("PortraitSprite", "phase"), ("PortraitSprite", "overlay")}
HEX_NAMES = re.compile(r"color|flags")
# statics symbols.txt doesn't list (docs/port/data.md gives their address)
STATIC_VAS = {"jp_tutorial": 0x244728}
# static char * help lines declared in code files: screen, name, address, readers
CODE_STATICS = [
    ("plus_tech", "shop_level_help", 0x1FAE1C, ["plus_tech_part_confirm_enter"]),
    ("plus_tech", "chip_level_help", 0x1FAFA4, ["plus_tech_chip_list_enter"]),
]
# int arrays in .rdata the data files don't declare: screen, name, address, count, readers (VS
# setup's area labels: where they slide from, to, and to with three or four players)
CODE_INT_ARRAYS = [
    ("vs_mode", "D_0019EAC8", 0x19EAC8, 6, ["vs_area_select_screen_step7"]),
    ("vs_mode", "D_0019EAE0", 0x19EAE0, 6, ["vs_area_select_draw"]),
    ("vs_mode", "D_0019EB04", 0x19EB04, 3, ["vs_area_select_draw"]),
]
# int arrays whose words the readers split (font_record 0xBCF2F: {i16 sprite, u16 advance}, the
# sprite a render registry row, kind R)
RETYPED = {"number_fonts": ("NumberFontEntry", 4, [["sprite", "R", 0, 1, 2], ["advance", "H", 2, 1, 2]])}

PRIM = {"int": ("i", 4), "unsigned int": ("u", 4), "unsigned": ("u", 4), "float": ("f", 4),
        "short": ("h", 2), "unsigned short": ("H", 2), "char": ("b", 1), "unsigned char": ("B", 1),
        "Thunk": ("p", 4)}

DECL = re.compile(r"^(?:static\s+)?(?:const\s+)?([A-Za-z_][\w ]*?)\s*(\*?)\s*\b([A-Za-z_]\w*)"
                  r"((?:\[[^\]]*\])*)\s*(=|;)", re.M)
FUNC = re.compile(r"^// (0x[0-9a-fA-F]+)\n([^\n]*?)\b([A-Za-z_][\w:]*)\s*\(", re.M)
SET_LOAD = re.compile(r"texture_set_load\(\s*(0x[0-9a-fA-F]+|\d+)\s*,")
SPRITE_LIT = re.compile(r"(?:SPRITE_INIT|draw_params_set_sprite)\(\s*&?\w+\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*\)")


def screen_of(func):
    for prefix, screen in SCREENS:
        if func.startswith(prefix):
            return screen
    return None


def parse_structs(text):
    """struct name -> [(field, ctype, is_pointer, count)] from include/tables.h"""
    structs = {}
    for m in re.finditer(r"^struct (\w+)(?: : \w+)? \{\n(.*?)^\};", text, re.M | re.S):
        fields = []
        for line in m.group(2).split("\n"):
            line = line.split("//")[0].strip()
            if not line.endswith(";"):
                continue
            line = line[:-1]
            fm = re.match(r"(.+?)\s*\(\s*\*\s*(\w+)\s*\)\s*(\[.*\])?$", line)
            if fm:  # pointer to array
                fields.append((fm.group(2), "void", True, 1))
                continue
            fm = re.match(r"(.+?)\s*(\*+)?\s*(\w+)\s*((?:\[[^\]]+\])*)$", line)
            if not fm:
                continue
            ctype, ptr, name, dims = fm.groups()
            count = 1
            for dim in re.findall(r"\[([^\]]+)\]", dims or ""):
                count *= int(dim, 0)
            fields.append((name, ctype.replace("const ", "").strip(), bool(ptr), count))
        structs[m.group(1)] = fields
    return structs


def split_initializer(text, start):
    """the initializer text from start (after '=') up to its ';'"""
    i, depth, n = start, 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            i += 1
            while text[i] != c:
                i += 2 if text[i] == "\\" else 1
        elif c == "/" and text[i + 1] == "/":
            i = text.index("\n", i)
        elif c in "{(":
            depth += 1
        elif c in "})":
            depth -= 1
        elif c == ";" and depth == 0:
            return text[start:i]
        i += 1
    return text[start:]


def leaves(init):
    """the initializer as nested lists of leaf token strings"""
    stack = [[]]
    token = ""
    i, n = 0, len(init)
    while i < n:
        c = init[i]
        if c == '"' or c == "'":
            j = i + 1
            while init[j] != c:
                j += 2 if init[j] == "\\" else 1
            token += init[i:j + 1]
            i = j + 1
            continue
        if c == "/" and i + 1 < n and init[i + 1] == "/":
            i = init.index("\n", i) if "\n" in init[i:] else n
            continue
        if c == "{":
            stack.append([])
        elif c == "}":
            if token.strip():
                stack[-1].append(token.strip())
            token = ""
            group = stack.pop()
            stack[-1].append(group)
        elif c == "," :
            if token.strip():
                stack[-1].append(token.strip())
            token = ""
        else:
            token += c
        i += 1
    if token.strip():
        stack[-1].append(token.strip())
    return stack[0]


def flat(group):
    out = []
    for item in group:
        out.extend(flat(item) if isinstance(item, list) else [item])
    return out


def leaf_kind(token):
    if '"' in token:
        return "s"
    body = re.sub(r"^\((?:[\w ]+\*?)\)\s*", "", token).lstrip("&-+ ")
    if re.match(r"[A-Za-z_]", body):
        return "p"
    return None


def leaf_symbols(token):
    body = re.sub(r"^\((?:[\w ]+\*?)\)\s*", "", token)
    return re.findall(r"&?\s*\b([A-Za-z_]\w*)", body) if leaf_kind(token) == "p" else []


def layout_fields(struct, structs):
    """[(name, kind, offset, count, size)] with natural alignment, and the record size"""
    out, offset = [], 0
    names = RENAMES.get(struct)
    for index, (name, ctype, pointer, count) in enumerate(structs[struct]):
        if names:
            name = names[index]
        if pointer:
            kind, size = ("s" if ctype == "char" else "p"), 4
        elif ctype in PRIM:
            kind, size = PRIM[ctype]
        else:
            raise ValueError(f"{struct}.{name}: can't lay out {ctype}")
        offset = (offset + size - 1) // size * size
        if (struct, name) in SPRITE_FIELDS:
            kind = "r"
        elif kind in "iu" and HEX_NAMES.search(name):
            kind = "x"
        out.append([name, kind, offset, count, size])
        offset += size * count
    return out, (offset + 3) // 4 * 4


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--decomp", default=str(HERE.parent.parent / "phantom-crash-decomp"))
    args = parser.parse_args()
    decomp = Path(args.decomp)

    structs = parse_structs((decomp / "include" / "tables.h").read_text(encoding="latin1"))
    symbols = {}
    for line in (decomp / "data" / "symbols.txt").read_text(encoding="latin1").splitlines():
        m = re.match(r"([\w:]+) = (0x[0-9a-fA-F]+)", line)
        if m:
            symbols[m.group(1)] = int(m.group(2), 16)

    # who reads what: identifiers in each function of the screen code, and literal sprite ids
    readers = defaultdict(set)
    sprite_uses = set()
    set_loads = set()
    for path in sorted((decomp / "src").rglob("*.c")):
        if path.parent.name == "data" or "data" in path.relative_to(decomp / "src").parts[:1]:
            continue
        text = path.read_text(encoding="latin1")
        found = list(FUNC.finditer(text))
        for i, m in enumerate(found):
            body = text[m.start(): found[i + 1].start() if i + 1 < len(found) else len(text)]
            func = m.group(3)
            for ident in set(re.findall(r"\b[A-Za-z_]\w*\b", body)):
                readers[ident].add(func)
            screen = screen_of(func)
            for lit in SET_LOAD.findall(body):
                set_loads.add((int(lit, 0), screen or "", func))
            if screen:
                for lit in SPRITE_LIT.findall(body):
                    sprite = int(lit, 0)
                    if 0 <= sprite < 3763:
                        sprite_uses.add((sprite, screen, func))

    # every table declared in the UI files
    tables = []
    for name in UI_FILES + MIXED_FILES:
        path = decomp / "src" / "data" / name
        text = path.read_text(encoding="latin1")
        for m in DECL.finditer(text):
            ctype, pointer, ident, dims = m.group(1).strip(), m.group(2), m.group(3), m.group(4)
            if ctype in ("struct", "typedef", "return") or ctype in SKIP_TYPES:
                continue
            init = split_initializer(text, m.end()) if m.group(5) == "=" else ""
            tables.append(dict(file=path.stem, ctype=ctype, pointer=bool(pointer), name=ident,
                               dims=dims, init=init, mixed=name in MIXED_FILES))

    # data -> data references, for tables no code names
    referenced_by = defaultdict(set)
    for t in tables:
        for token in flat(leaves(t["init"])) if t["init"].strip().startswith("{") else []:
            for sym in leaf_symbols(token):
                referenced_by[sym].add(t["name"])

    layouts = {}
    rows = []
    pointer_targets = set()
    # plain char arrays: a word holding one's address ((int)&D_...) is a string, printed as text
    char_arrays = {t["name"] for t in tables if t["ctype"] == "char" and not t["pointer"]}
    for t in tables:
        if re.fullmatch(r"D_[0-9A-F]{8}", t["name"]):
            va = int(t["name"][2:], 16)
        else:
            va = symbols.get(t["name"], STATIC_VAS.get(t["name"]))
        if va is None:
            continue
        ctype = t["ctype"]
        count = 1
        for dim in re.findall(r"\[([^\]]*)\]", t["dims"]):
            count *= int(dim, 0) if dim else 0
        init = t["init"].strip()
        if ctype == "char" and not t["pointer"]:
            if not readers.get(t["name"]):
                continue  # a string some table points at: printed there
            if count == 0:
                count = len(eval_c_string(init)) + 1
            layout_name, fields, size = f"char[{count}]", [["text", "c", 0, count, 1]], count
            count = 1
        elif t["pointer"]:
            layout_name, fields, size = ("string" if ctype == "char" else "pointer"), [["value", "s" if ctype == "char" else "p", 0, 1, 4]], 4
            count = max(count, 1)
        elif t["name"] in RETYPED:
            layout_name, size, fields = RETYPED[t["name"]]
            count = count * PRIM[ctype][1] // size
        elif ctype in PRIM:
            kind, size = PRIM[ctype]
            layout_name, fields = ctype, [["value", kind, 0, 1, size]]
            count = max(count, 1)
        elif ctype in structs:
            try:
                fields, size = layout_fields(ctype, structs)
            except ValueError as error:
                print(f"skipped {t['name']}: {error}")
                continue
            layout_name = ctype
            count = max(count, 1)
        else:
            continue

        # column kinds the initializer shows: string and pointer words in int and void * columns
        fields = [list(f) for f in fields]
        groups = leaves(init)[0] if init.startswith("{") else []
        row_groups = groups if (groups and isinstance(groups[0], list)) else [groups]
        if ctype in PRIM or t["pointer"]:
            row_groups = [[g] for g in flat(groups)]
        # a column whose words are char array addresses and no other pointer is a string column
        other_pointer = set()
        targets = set()
        for group in row_groups:
            tokens = flat(group) if isinstance(group, list) else [group]
            slot = 0
            for fi, f in enumerate(fields):
                for k in range(f[3] if f[1] != "c" else 1):
                    if slot >= len(tokens):
                        break
                    kind = leaf_kind(tokens[slot])
                    syms = leaf_symbols(tokens[slot])
                    for sym in syms:
                        targets.add(sym)
                    if kind == "p":
                        if syms and "+" not in tokens[slot] and all(s in char_arrays for s in syms):
                            kind = "s"
                        else:
                            other_pointer.add(fi)
                    if kind and f[1] in "iupx":
                        f[1] = "s" if kind == "s" or f[1] == "s" else "p"
                    elif kind == "s" and f[1] == "p":
                        f[1] = "s"
                    slot += 1
        for fi, f in enumerate(fields):
            if f[1] == "s" and fi in other_pointer:
                f[1] = "p"
        if len(fields) == 1 and fields[0][0] == "value" and fields[0][1] in "sp":
            layout_name = "string" if fields[0][1] == "s" else "pointer"
        key = layout_name + "".join(f"|{f[0]}:{f[1]}@{f[2]}*{f[3]}" for f in fields)
        users = sorted(readers.get(t["name"], []))
        # the screen with the most readers first: the table's file
        votes = defaultdict(int)
        for user in users:
            if screen_of(user):
                votes[screen_of(user)] += 1
        screens = sorted(votes, key=lambda s: (-votes[s], s))
        how = "readers" if screens else ""
        if not screens:
            parents = referenced_by.get(t["name"], set())
            parent_users = {u for p in parents for u in readers.get(p, [])}
            screens = sorted({s for s in (screen_of(u) for u in parent_users) if s})
            how = "pointed-at" if screens else ""
        # read only by code no screen owns (shaders, collision, stages): not UI
        other = bool(users) and not screens and ctype not in UI_TYPES
        if t["mixed"] and not screens and ctype not in UI_TYPES:
            other = True
        dims = re.findall(r"\[([^\]]+)\]", t["dims"])
        dims = " ".join(str(int(d, 0)) for d in dims) if len(dims) > 1 else ""
        rows.append(dict(name=t["name"], va=va, file=t["file"], key=key, dims=dims, layout_name=layout_name,
                         size=size, fields=fields, count=count, users=users, screens=screens,
                         how=how, other=other, targets=targets))

    # screens for the rest from their nearest read neighbour in the same file (a screen's tables sit
    # together); a neighbour read by non-UI code makes the rest non-UI too
    by_file = defaultdict(list)
    for r in rows:
        by_file[r["file"]].append(r)
    for file_rows in by_file.values():
        file_rows.sort(key=lambda r: r["va"])
        for order in (file_rows, list(reversed(file_rows))):
            last = None
            for r in order:
                if r["screens"] and r["how"] != "neighbour":
                    last = r["screens"]
                elif r["other"] and r["users"]:
                    last = "other"
                elif not r["screens"] and not r["other"] and not r["users"] and last:
                    if last == "other":
                        r["other"] = True
                    else:
                        r["screens"], r["how"] = last, "neighbour"
    rows = [r for r in rows if not r["other"]]
    # symbols the kept tables point at
    for r in rows:
        pointer_targets |= r["targets"]
    # statics in code files that screens read (string pointers the data files don't declare)
    for screen, name, va, users in CODE_STATICS:
        rows.append(dict(name=name, va=va, file="code", key="string|value:s@0*1", dims="",
                         layout_name="string", size=4, fields=[["value", "s", 0, 1, 4]], count=1,
                         users=users, screens=[screen], how="readers", other=False,
                         targets=set()))
    for screen, name, va, count, users in CODE_INT_ARRAYS:
        rows.append(dict(name=name, va=va, file="code", key="int|value:i@0*1", dims="",
                         layout_name="int", size=4, fields=[["value", "i", 0, 1, 4]], count=count,
                         users=users, screens=[screen], how="readers", other=False,
                         targets=set()))
    for r in rows:
        if not r["screens"]:
            r["screens"], r["how"] = [r["file"]], "file"
        if r["key"] not in layouts:
            variant = sum(1 for k in layouts if k.split("|")[0] == r["layout_name"])
            name = r["layout_name"] if variant == 0 else f"{r['layout_name']}~{variant}"
            layouts[r["key"]] = (name, r["size"], r["fields"])
        r["layout"] = layouts[r["key"]][0]

    # the D_ tables by their names (TABLE_NAMES), the D_ name kept for the data
    for r in rows:
        r["decomp"] = r["name"] if r["name"] in TABLE_NAMES else ""
        r["name"] = TABLE_NAMES.get(r["name"], r["name"])

    # symbols pointers may name: the tables, and whatever their initializers point at
    named = {r["name"]: r["va"] for r in rows}
    for sym in pointer_targets:
        if sym in named:
            continue
        if sym in TABLE_NAMES:
            named[TABLE_NAMES[sym]] = int(sym[2:], 16)
        elif re.fullmatch(r"D_[0-9A-F]{8}", sym):
            named[sym] = int(sym[2:], 16)
        elif sym in symbols:
            named[sym] = symbols[sym]

    out = ["// GENERATED by tools/ui_tables.py from the decomp's declarations, layouts and readers.",
           "// Do not edit by hand. Names and addresses only: pc_extract reads the values from the XBE.",
           "// clang-format off", ""]
    out.append("// record layouts: name, size, fields as name:kind@offset*count (kinds in ui_layouts.cpp)")
    out.append("const UiLayoutDef ui_layout_defs[] = {")
    for name, size, fields in layouts.values():
        spec = " ".join(f"{f[0]}:{f[1]}@{f[2]}*{f[3]}" for f in fields)
        out.append(f'    {{"{name}", {size}, "{spec}"}},')
    out.append("};")
    out.append("")
    out.append("// tables: screen, name, address, layout, count, dims of a 2D array, decomp file, how the")
    out.append("// screen was found (readers, pointed-at, neighbour, file), readers, the decomp's D_ name")
    out.append("// when the table is named here (tools/ui_tables.py TABLE_NAMES)")
    out.append("const UiTableDef ui_table_defs[] = {")
    for r in sorted(rows, key=lambda r: (r["screens"][0], r["va"])):
        users = " ".join(r["users"])
        out.append(f'    {{"{" ".join(r["screens"])}", "{r["name"]}", 0x{r["va"]:X}, "{r["layout"]}", {r["count"]}, '
                   f'"{r["dims"]}", "{r["file"]}", "{r["how"]}",')
        out.append(f'     "{users}", "{r["decomp"]}"}},')
    out.append("};")
    out.append("")
    out.append("// symbols a pointer word may hold")
    out.append("const UiSymbolDef ui_symbol_defs[] = {")
    for sym, va in sorted(named.items(), key=lambda kv: kv[1]):
        out.append(f'    {{0x{va:X}, "{sym}"}},')
    out.append("};")
    out.append("")
    out.append("// sprite ids screen code draws as literals (SPRITE_INIT, draw_params_set_sprite)")
    out.append("const UiSpriteUseDef ui_sprite_use_defs[] = {")
    for sprite, screen, func in sorted(sprite_uses):
        out.append(f'    {{{sprite}, "{screen}", "{func}"}},')
    out.append("};")
    out.append("")
    out.append("// texture sets code loads by a literal asset id (texture_set_load): asset, screen, function")
    out.append("const UiSpriteUseDef ui_set_load_defs[] = {")
    for asset, screen, func in sorted(set_loads):
        out.append(f'    {{{asset}, "{screen}", "{func}"}},')
    out.append("};")
    OUT.write_text("\n".join(out) + "\n")
    print(f"{OUT}: {len(layouts)} layouts, {len(rows)} tables, {len(named)} symbols, "
          f"{len(sprite_uses)} sprite uses")


def eval_c_string(init):
    """byte length of a C string literal initializer (adjacent pieces joined)"""
    data = b""
    for piece in re.findall(r'"((?:[^"\\]|\\.)*)"', init):
        data += piece.encode("latin1").decode("unicode_escape").encode("latin1")
    return data


if __name__ == "__main__":
    main()
