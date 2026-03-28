/* Slash'EM libnhmain.c
 * WASM/library entry point that reuses the stock Unix main while exposing
 * a small JavaScript bridge similar to the NetHack 3.6 wasm build.
 */

#include "hack.h"
#include "dlb.h"
#include "patchlevel.h"
#include "date.h"
#include "func_tab.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#define main libnh_main
#include "../unix/unixmain.c"
#undef main

#ifdef __EMSCRIPTEN__
#ifdef USE_TILES
extern short glyph2tile[];

EMSCRIPTEN_KEEPALIVE
int
glyph_to_tile(int glyph)
{
    if (glyph < 0 || glyph >= MAX_GLYPH)
        return -1;
    return (int) glyph2tile[glyph];
}
#endif

EMSCRIPTEN_KEEPALIVE
int
nh3d_glyph_at(int x, int y)
{
    return glyph_at((xchar) x, (xchar) y);
}

EMSCRIPTEN_KEEPALIVE
int
nh_top_item_glyph_under_player(void)
{
    struct obj *item;

    if (Blind)
        return -1;

    item = vobj_at(u.ux, u.uy);
    if (!item || covers_objects(u.ux, u.uy))
        return -1;

    return obj_to_glyph(item);
}

EM_JS(void, js_helpers_init, (), {
    globalThis.nethackGlobal = globalThis.nethackGlobal || {};
    globalThis.nethackGlobal.helpers = globalThis.nethackGlobal.helpers || {};

    installHelper(getPointerValue, "getPointerValue");
    installHelper(setPointerValue, "setPointerValue");
    installHelper(mapglyphHelper, "mapglyphHelper");
    installHelper(tileIndexForGlyph, "tileIndexForGlyph");
    installHelper(glyphAtHelper, "glyphAtHelper");
    installHelper(topItemGlyphUnderPlayer, "topItemGlyphUnderPlayer");

    function mapglyphHelper(glyph, x, y, mgflags) {
        let ochar = _malloc(4);
        let ocolor = _malloc(4);
        let ospecial = _malloc(4);
        _mapglyph(glyph, ochar, ocolor, ospecial, x, y);
        let ch = getValue(ochar, "i32");
        let color = getValue(ocolor, "i32");
        let special = getValue(ospecial, "i32");
        _free(ochar);
        _free(ocolor);
        _free(ospecial);
        return { glyph, ch, color, special, tileIdx: _glyph_to_tile(glyph), x, y, mgflags };
    }

    function glyphAtHelper(x, y) {
        return _nh3d_glyph_at(x, y);
    }

    function topItemGlyphUnderPlayer() {
        return _nh_top_item_glyph_under_player();
    }

    function tileIndexForGlyph(glyph) {
        return _glyph_to_tile(glyph);
    }

    function getPointerValue(name, ptr, type) {
        switch (type) {
        case "s":
            return UTF8ToString(ptr);
        case "S": {
            let strPtr = ptr ? getValue(ptr, "*") : 0;
            return strPtr ? UTF8ToString(strPtr) : "";
        }
        case "p":
            return ptr ? getValue(ptr, "*") : 0;
        case "c":
            return String.fromCharCode(getValue(ptr, "i8"));
        case "b":
            return getValue(ptr, "i8") === 1;
        case "0":
            return getValue(ptr, "i8");
        case "1":
            return getValue(ptr, "i16");
        case "2":
        case "i":
        case "n":
            return getValue(ptr, "i32");
        case "f":
            return getValue(ptr, "float");
        case "d":
            return getValue(ptr, "double");
        case "v":
            return undefined;
        default:
            throw new TypeError("unknown type: " + type);
        }
    }

    function setPointerValue(name, ptr, type, value = 0) {
        switch (type) {
        case "p":
            setValue(ptr, value, "*");
            break;
        case "S":
            if (value === null || value === undefined) {
                setValue(ptr, 0, "*");
                break;
            }
            if (typeof value === "number") {
                setValue(ptr, value, "*");
                break;
            }
            throw new TypeError(`expected ${name} return type to be pointer or null`);
        case "s":
            if (typeof value !== "string") {
                throw new TypeError(`expected ${name} return type to be string`);
            }
            stringToUTF8(value, ptr, 256);
            break;
        case "i":
        case "2":
            setValue(ptr, value, "i32");
            break;
        case "1":
            setValue(ptr, value, "i16");
            break;
        case "c":
            setValue(ptr, value, "i8");
            break;
        case "b":
            setValue(ptr, value ? 1 : 0, "i8");
            break;
        case "f":
            setValue(ptr, value, "float");
            break;
        case "d":
            setValue(ptr, value, "double");
            break;
        case "v":
            break;
        default:
            throw new Error("unknown type");
        }
    }

    function installHelper(fn, name) {
        globalThis.nethackGlobal.helpers[name || fn.name] = fn;
    }
});

#define SET_CONSTANT(scope, name) set_const(scope, #name, name)
#define SET_CONSTANT_STRING(scope, name) set_const_str(scope, #name, name)
#define SET_POINTER(name) set_const_ptr(#name, (void *) &name)
#define CREATE_GLOBAL(var, type) create_global(#var, (void *) &var, type)
#define CREATE_GLOBAL_PATH(name, ptr, type) create_global(name, (void *) (ptr), type)

extern int n_dgns;

EM_JS(void, set_const, (char *scope_str, char *name_str, int num), {
    let scope = UTF8ToString(scope_str);
    let name = UTF8ToString(name_str);

    globalThis.nethackGlobal.constants = globalThis.nethackGlobal.constants || {};
    globalThis.nethackGlobal.constants[scope] = globalThis.nethackGlobal.constants[scope] || {};
    globalThis.nethackGlobal.constants[scope][name] = num;
    globalThis.nethackGlobal.constants[scope][num] = name;
});

EM_JS(void, set_const_str, (char *scope_str, char *name_str, char *input_str), {
    let scope = UTF8ToString(scope_str);
    let name = UTF8ToString(name_str);
    let str = UTF8ToString(input_str);

    globalThis.nethackGlobal.constants = globalThis.nethackGlobal.constants || {};
    globalThis.nethackGlobal.constants[scope] = globalThis.nethackGlobal.constants[scope] || {};
    globalThis.nethackGlobal.constants[scope][name] = str;
});

EM_JS(void, set_const_ptr, (char *name_str, void *ptr), {
    let name = UTF8ToString(name_str);

    globalThis.nethackGlobal = globalThis.nethackGlobal || {};
    globalThis.nethackGlobal.pointers = globalThis.nethackGlobal.pointers || {};
    globalThis.nethackGlobal.pointers[name] = ptr;
});

void
js_constants_init(void)
{
    EM_ASM({
        globalThis.nethackGlobal = globalThis.nethackGlobal || {};
        globalThis.nethackGlobal.constants = globalThis.nethackGlobal.constants || {};
        globalThis.nethackGlobal.pointers = globalThis.nethackGlobal.pointers || {};
    });

    SET_CONSTANT("WIN_TYPE", NHW_MESSAGE);
    SET_CONSTANT("WIN_TYPE", NHW_STATUS);
    SET_CONSTANT("WIN_TYPE", NHW_MAP);
    SET_CONSTANT("WIN_TYPE", NHW_MENU);
    SET_CONSTANT("WIN_TYPE", NHW_TEXT);

    SET_CONSTANT("ATTR", ATR_NONE);
    SET_CONSTANT("ATTR", ATR_BOLD);
    SET_CONSTANT("ATTR", ATR_DIM);
    SET_CONSTANT("ATTR", ATR_ULINE);
    SET_CONSTANT("ATTR", ATR_BLINK);
    SET_CONSTANT("ATTR", ATR_INVERSE);

    SET_CONSTANT("MENU_SELECT", PICK_NONE);
    SET_CONSTANT("MENU_SELECT", PICK_ONE);
    SET_CONSTANT("MENU_SELECT", PICK_ANY);

    SET_CONSTANT("GLYPH", GLYPH_MON_OFF);
    SET_CONSTANT("GLYPH", GLYPH_PET_OFF);
    SET_CONSTANT("GLYPH", GLYPH_INVIS_OFF);
    SET_CONSTANT("GLYPH", GLYPH_DETECT_OFF);
    SET_CONSTANT("GLYPH", GLYPH_BODY_OFF);
    SET_CONSTANT("GLYPH", GLYPH_RIDDEN_OFF);
    SET_CONSTANT("GLYPH", GLYPH_OBJ_OFF);
    SET_CONSTANT("GLYPH", GLYPH_CMAP_OFF);
    SET_CONSTANT("GLYPH", GLYPH_EXPLODE_OFF);
    SET_CONSTANT("GLYPH", GLYPH_ZAP_OFF);
    SET_CONSTANT("GLYPH", GLYPH_SWALLOW_OFF);
    SET_CONSTANT("GLYPH", GLYPH_WARNING_OFF);
    SET_CONSTANT("GLYPH", MAX_GLYPH);
    SET_CONSTANT("GLYPH", NO_GLYPH);
    SET_CONSTANT("GLYPH", GLYPH_INVISIBLE);

    SET_CONSTANT("COLORS", CLR_BLACK);
    SET_CONSTANT("COLORS", CLR_RED);
    SET_CONSTANT("COLORS", CLR_GREEN);
    SET_CONSTANT("COLORS", CLR_BROWN);
    SET_CONSTANT("COLORS", CLR_BLUE);
    SET_CONSTANT("COLORS", CLR_MAGENTA);
    SET_CONSTANT("COLORS", CLR_CYAN);
    SET_CONSTANT("COLORS", CLR_GRAY);
    SET_CONSTANT("COLORS", NO_COLOR);
    SET_CONSTANT("COLORS", CLR_ORANGE);
    SET_CONSTANT("COLORS", CLR_BRIGHT_GREEN);
    SET_CONSTANT("COLORS", CLR_YELLOW);
    SET_CONSTANT("COLORS", CLR_BRIGHT_BLUE);
    SET_CONSTANT("COLORS", CLR_BRIGHT_MAGENTA);
    SET_CONSTANT("COLORS", CLR_BRIGHT_CYAN);
    SET_CONSTANT("COLORS", CLR_WHITE);
    SET_CONSTANT("COLORS", CLR_MAX);

    SET_CONSTANT("ROLE_RACEMASK", MH_HUMAN);
    SET_CONSTANT("ROLE_RACEMASK", MH_ELF);
    SET_CONSTANT("ROLE_RACEMASK", MH_DWARF);
    SET_CONSTANT("ROLE_RACEMASK", MH_GNOME);
    SET_CONSTANT("ROLE_RACEMASK", MH_ORC);

    SET_CONSTANT("ROLE_GENDMASK", ROLE_MALE);
    SET_CONSTANT("ROLE_GENDMASK", ROLE_FEMALE);
    SET_CONSTANT("ROLE_GENDMASK", ROLE_NEUTER);

    SET_CONSTANT("ROLE_ALIGNMASK", ROLE_LAWFUL);
    SET_CONSTANT("ROLE_ALIGNMASK", ROLE_NEUTRAL);
    SET_CONSTANT("ROLE_ALIGNMASK", ROLE_CHAOTIC);

    SET_CONSTANT("MG", MG_CORPSE);
    SET_CONSTANT("MG", MG_INVIS);
    SET_CONSTANT("MG", MG_DETECT);
    SET_CONSTANT("MG", MG_PET);
    SET_CONSTANT("MG", MG_RIDDEN);

    SET_CONSTANT_STRING("COPYRIGHT", COPYRIGHT_BANNER_A);
    SET_CONSTANT_STRING("COPYRIGHT", COPYRIGHT_BANNER_B);
    SET_CONSTANT_STRING("COPYRIGHT", COPYRIGHT_BANNER_C);

    SET_POINTER(extcmdlist);
    SET_POINTER(roles);
    SET_POINTER(races);
    SET_POINTER(genders);
    SET_POINTER(aligns);
}

EM_JS(void, create_global, (char *name_str, void *ptr, char *type_str), {
    let name = UTF8ToString(name_str);
    let type = UTF8ToString(type_str);

    let getPointerValue = globalThis.nethackGlobal.helpers.getPointerValue;
    let setPointerValue = globalThis.nethackGlobal.helpers.setPointerValue;

    globalThis.nethackGlobal = globalThis.nethackGlobal || {};
    globalThis.nethackGlobal.globals = globalThis.nethackGlobal.globals || {};

    let { obj, prop } = createPath(globalThis.nethackGlobal.globals, name);
    Object.defineProperty(obj, prop, {
        get: getPointerValue.bind(null, name, ptr, type),
        set: setPointerValue.bind(null, name, ptr, type),
        configurable: true,
        enumerable: true
    });

    function createPath(obj, path) {
        path = path.split(".");
        let i;
        for (i = 0; i < path.length - 1; i++) {
            if (obj[path[i]] === undefined) {
                obj[path[i]] = {};
            }
            obj = obj[path[i]];
        }
        return { obj, prop: path[i] };
    }
});

void
create_dungeon_globals(void)
{
    int i;
    char name[64];

    for (i = 0; i < n_dgns; i++) {
        Sprintf(name, "dungeons.%d.dname", i);
        CREATE_GLOBAL_PATH(name, dungeons[i].dname, "s");

        Sprintf(name, "dungeons.%d.ledger_start", i);
        CREATE_GLOBAL_PATH(name, &dungeons[i].ledger_start, "i");

        Sprintf(name, "dungeons.%d.depth_start", i);
        CREATE_GLOBAL_PATH(name, &dungeons[i].depth_start, "i");
    }
}

void
js_globals_init(void)
{
    EM_ASM({
        globalThis.nethackGlobal = globalThis.nethackGlobal || {};
        globalThis.nethackGlobal.globals = globalThis.nethackGlobal.globals || {};
    });

    CREATE_GLOBAL(plname, "s");
#ifdef GOLDOBJ
    CREATE_GLOBAL(done_money, "i");
#endif
    CREATE_GLOBAL_PATH("killer.format", &killer_format, "i");
    CREATE_GLOBAL_PATH("killer.name", &killer, "S");

    CREATE_GLOBAL(u.uz.dnum, "0");
    CREATE_GLOBAL(u.uz.dlevel, "0");
    CREATE_GLOBAL(dungeon_topology.d_mines_dnum, "0");
    CREATE_GLOBAL(dungeon_topology.d_quest_dnum, "0");
    CREATE_GLOBAL(dungeon_topology.d_sokoban_dnum, "0");
    CREATE_GLOBAL(dungeon_topology.d_tower_dnum, "0");
    CREATE_GLOBAL(dungeon_topology.d_astral_level.dnum, "0");
    create_dungeon_globals();

    CREATE_GLOBAL(WIN_MAP, "i");
    CREATE_GLOBAL(WIN_MESSAGE, "i");
    CREATE_GLOBAL(WIN_INVEN, "i");
    CREATE_GLOBAL(WIN_STATUS, "i");

    CREATE_GLOBAL(iflags.window_inited, "b");
    CREATE_GLOBAL(iflags.wc_hilite_pet, "b");

    CREATE_GLOBAL(flags.initrole, "i");
    CREATE_GLOBAL(flags.initrace, "i");
    CREATE_GLOBAL(flags.initgend, "i");
    CREATE_GLOBAL(flags.initalign, "i");
    CREATE_GLOBAL(flags.showexp, "b");
    CREATE_GLOBAL(flags.time, "b");
    CREATE_GLOBAL(flags.female, "b");
    CREATE_GLOBAL(u.ux, "0");
    CREATE_GLOBAL(u.uy, "0");
}
#endif

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int
main(int argc, char *argv[])
{
#ifdef __EMSCRIPTEN__
    js_helpers_init();
    js_constants_init();
    js_globals_init();
#endif
    return libnh_main(argc, argv);
}
