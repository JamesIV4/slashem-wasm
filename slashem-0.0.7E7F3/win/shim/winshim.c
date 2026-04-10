/* Slash'EM winshim.c
 * Shim window port for WASM builds. All window callbacks are forwarded
 * through a single JavaScript function name via Asyncify.
 */

#include "hack.h"
#include <string.h>

#ifdef SHIM_GRAPHICS
#include <stdarg.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#undef SHIM_DEBUG

#ifdef SHIM_DEBUG
#define debugf printf
#else
#define debugf(...)
#endif

#define NH3D_TRACKED_MONSTER_NONE (-1)
#define NH3D_TRACKED_PLAYER_ID 0
#define NH3D_MONSTER_ATTACK_TRACKING_MAX 128
#define NH3D_MONSTER_KILLER_STACK_MAX 8

struct nh3d_recent_attack {
    unsigned attacker_id;
    int target_id;
    long turn;
};

static struct nh3d_recent_attack
    nh3d_recent_attacks[NH3D_MONSTER_ATTACK_TRACKING_MAX];
static int nh3d_monster_killer_stack[NH3D_MONSTER_KILLER_STACK_MAX];
static int nh3d_monster_killer_depth = 0;

static boolean nh3d_monster_has_trackable_glyph(struct monst *);
static int nh3d_monster_id_from_tile(XCHAR_P, XCHAR_P, int);
static int nh3d_get_recent_attack_target_id(int);
static void nh3d_record_recent_attack(unsigned, int);
static void nh3d_queue_shim_notification(const char *, int, int, int, int);

void nh3d_note_monster_attack(struct monst *, struct monst *);
void nh3d_push_monster_killer(struct monst *);
void nh3d_pop_monster_killer(void);
int nh3d_get_current_monster_killer_id(void);
void nh3d_emit_monster_killed(int, int, int, int);

enum shim_status_fields {
    SHIM_BL_CHARACTERISTICS = -3,
    SHIM_BL_RESET = -2,
    SHIM_BL_FLUSH = -1,
    SHIM_BL_TITLE = 0,
    SHIM_BL_STR,
    SHIM_BL_DX,
    SHIM_BL_CO,
    SHIM_BL_IN,
    SHIM_BL_WI,
    SHIM_BL_CH,
    SHIM_BL_ALIGN,
    SHIM_BL_SCORE,
    SHIM_BL_CAP,
    SHIM_BL_GOLD,
    SHIM_BL_ENE,
    SHIM_BL_ENEMAX,
    SHIM_BL_XP,
    SHIM_BL_AC,
    SHIM_BL_HD,
    SHIM_BL_TIME,
    SHIM_BL_HUNGER,
    SHIM_BL_HP,
    SHIM_BL_HPMAX,
    SHIM_BL_LEVELDESC,
    SHIM_BL_EXP,
    SHIM_BL_CONDITION,
    SHIM_MAXBLSTATS
};

#define SHIM_RAW_STAT_HELD 0x00000100UL

static void FDECL(shim_status_raw_bridge, (int, int, const char **));
static void NDECL(shim_status_bridge_reconfig);
static void FDECL(shim_status_bridge_emit_string, (int, const char *));
static void FDECL(shim_status_bridge_emit_mask, (unsigned long));
static void NDECL(shim_status_bridge_flush);

#ifdef __EMSCRIPTEN__
static char *shim_callback_name = (char *) 0;
void shim_graphics_set_callback(char *cb_name);
void local_callback(const char *cb_name, const char *shim_name,
                    void *ret_ptr, const char *fmt_str, void *args);

void
shim_graphics_set_callback(char *cb_name)
{
    if (shim_callback_name != 0)
        free(shim_callback_name);
    if (cb_name && *cb_name)
        shim_callback_name = strdup(cb_name);
    else
        shim_callback_name = (char *) 0;
}

#define A2P &
#define P2V (void *)
#define DECLCB(ret_type, name, fn_args, fmt, ...) \
ret_type name fn_args; \
ret_type name fn_args { \
    void *args[] = { __VA_ARGS__ }; \
    ret_type ret = (ret_type) 0; \
    debugf("SHIM GRAPHICS: " #name "\n"); \
    if (!shim_callback_name) return ret; \
    local_callback(shim_callback_name, #name, (void *) &ret, fmt, args); \
    return ret; \
}

#define VDECLCB(name, fn_args, fmt, ...) \
void name fn_args; \
void name fn_args { \
    void *args[] = { __VA_ARGS__ }; \
    debugf("SHIM GRAPHICS: " #name "\n"); \
    if (!shim_callback_name) return; \
    local_callback(shim_callback_name, #name, (void *) 0, fmt, args); \
}

#else
typedef void (*shim_callback_t)(const char *name, void *ret_ptr,
                                const char *fmt, ...);
static shim_callback_t shim_graphics_callback = (shim_callback_t) 0;
void shim_graphics_set_callback(shim_callback_t cb);

void
shim_graphics_set_callback(shim_callback_t cb)
{
    shim_graphics_callback = cb;
}

#define A2P
#define P2V
#define DECLCB(ret_type, name, fn_args, fmt, ...) \
ret_type name fn_args; \
ret_type name fn_args { \
    ret_type ret = (ret_type) 0; \
    debugf("SHIM GRAPHICS: " #name "\n"); \
    if (!shim_graphics_callback) return ret; \
    shim_graphics_callback(#name, (void *) &ret, fmt, ## __VA_ARGS__); \
    return ret; \
}

#define VDECLCB(name, fn_args, fmt, ...) \
void name fn_args; \
void name fn_args { \
    debugf("SHIM GRAPHICS: " #name "\n"); \
    if (!shim_graphics_callback) return; \
    shim_graphics_callback(#name, (void *) 0, fmt, ## __VA_ARGS__); \
}
#endif

static boolean
nh3d_monster_has_trackable_glyph(mtmp)
struct monst *mtmp;
{
    return (boolean) (mtmp && (mtmp == u.usteed || canspotmon(mtmp)));
}

static int
nh3d_monster_id_from_tile(x, y, glyph)
XCHAR_P x, y;
int glyph;
{
    struct monst *mtmp;

    if (glyph_is_ridden_monster(glyph) && u.usteed && x == u.ux && y == u.uy)
        return (int) u.usteed->m_id;
    if (x == u.ux && y == u.uy)
        return NH3D_TRACKED_PLAYER_ID;
    if (!glyph_is_monster(glyph))
        return NH3D_TRACKED_MONSTER_NONE;

    mtmp = m_at((int) x, (int) y);
    return mtmp ? (int) mtmp->m_id : NH3D_TRACKED_MONSTER_NONE;
}

static int
nh3d_get_recent_attack_target_id(monster_id)
int monster_id;
{
    int i;

    if (monster_id <= 0)
        return NH3D_TRACKED_MONSTER_NONE;
    for (i = 0; i < NH3D_MONSTER_ATTACK_TRACKING_MAX; ++i)
        if (nh3d_recent_attacks[i].attacker_id == (unsigned) monster_id
            && nh3d_recent_attacks[i].turn == monstermoves)
            return nh3d_recent_attacks[i].target_id;
    return NH3D_TRACKED_MONSTER_NONE;
}

static void
nh3d_record_recent_attack(attacker_id, target_id)
unsigned attacker_id;
int target_id;
{
    int i, empty_slot = -1;

    if (!attacker_id)
        return;

    for (i = 0; i < NH3D_MONSTER_ATTACK_TRACKING_MAX; ++i) {
        if (nh3d_recent_attacks[i].attacker_id == attacker_id) {
            nh3d_recent_attacks[i].target_id = target_id;
            nh3d_recent_attacks[i].turn = monstermoves;
            return;
        }
        if (empty_slot < 0 && nh3d_recent_attacks[i].attacker_id == 0)
            empty_slot = i;
    }

    if (empty_slot < 0)
        empty_slot = (int) (attacker_id % NH3D_MONSTER_ATTACK_TRACKING_MAX);
    nh3d_recent_attacks[empty_slot].attacker_id = attacker_id;
    nh3d_recent_attacks[empty_slot].target_id = target_id;
    nh3d_recent_attacks[empty_slot].turn = monstermoves;
}

void
nh3d_note_monster_attack(attacker, target)
struct monst *attacker, *target;
{
    boolean attacker_visible, target_visible;
    int attacker_id, target_id, target_x, target_y, emitted_attacker_id;

    if (!attacker || !attacker->m_id)
        return;

    attacker_visible = nh3d_monster_has_trackable_glyph(attacker);
    target_visible = target ? nh3d_monster_has_trackable_glyph(target) : TRUE;
    if (!attacker_visible && !target_visible)
        return;

    attacker_id = (int) attacker->m_id;
    if (target) {
        target_id =
            target->m_id ? (int) target->m_id : NH3D_TRACKED_MONSTER_NONE;
        target_x = target->mx;
        target_y = target->my;
    } else {
        target_id = NH3D_TRACKED_PLAYER_ID;
        target_x = u.ux;
        target_y = u.uy;
    }

    if (attacker_visible && (target == 0 || target_visible))
        nh3d_record_recent_attack(attacker->m_id, target_id);

    if (target && !target_visible)
        return;

    emitted_attacker_id =
        attacker_visible ? attacker_id : NH3D_TRACKED_MONSTER_NONE;
    nh3d_queue_shim_notification("shim_monster_attack", emitted_attacker_id,
                                 target_id, target_x, target_y);
}

void
nh3d_push_monster_killer(killer)
struct monst *killer;
{
    int killer_id = NH3D_TRACKED_MONSTER_NONE;

    if (killer && nh3d_monster_has_trackable_glyph(killer) && killer->m_id)
        killer_id = (int) killer->m_id;

    if (nh3d_monster_killer_depth < NH3D_MONSTER_KILLER_STACK_MAX)
        nh3d_monster_killer_stack[nh3d_monster_killer_depth++] = killer_id;
}

void
nh3d_pop_monster_killer()
{
    if (nh3d_monster_killer_depth > 0)
        --nh3d_monster_killer_depth;
}

int
nh3d_get_current_monster_killer_id()
{
    return (nh3d_monster_killer_depth > 0)
               ? nh3d_monster_killer_stack[nh3d_monster_killer_depth - 1]
               : NH3D_TRACKED_MONSTER_NONE;
}

void
nh3d_emit_monster_killed(monster_id, killer_id, x, y)
int monster_id, killer_id, x, y;
{
    if (monster_id <= 0)
        return;
    nh3d_queue_shim_notification("shim_monster_killed", monster_id, killer_id,
                                 x, y);
}

static void
nh3d_queue_shim_notification(name, a, b, c, d)
const char *name;
int a, b, c, d;
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        globalThis.nethackGlobal = globalThis.nethackGlobal || {};
        if (!Array.isArray(globalThis.nethackGlobal.pendingShimNotifications)) {
            globalThis.nethackGlobal.pendingShimNotifications = [];
        }
        globalThis.nethackGlobal.pendingShimNotifications.push([
            UTF8ToString($0),
            $1,
            $2,
            $3,
            $4,
        ]);
    }, name, a, b, c, d);
#else
    if (!shim_graphics_callback)
        return;
    shim_graphics_callback(name, NULL, "viiii", a, b, c, d);
#endif
}

void
shim_init_nhwindows(argcp, argv)
int *argcp;
char **argv;
{
#ifdef __EMSCRIPTEN__
    void *args[] = { P2V argcp, P2V argv };
#endif
    debugf("SHIM GRAPHICS: shim_init_nhwindows\n");
    bot_set_handler(shim_status_raw_bridge);
#ifdef __EMSCRIPTEN__
    if (!shim_callback_name) return;
    local_callback(shim_callback_name, "shim_init_nhwindows", (void *) 0,
                   "vpp", args);
#else
    if (!shim_graphics_callback) return;
    shim_graphics_callback("shim_init_nhwindows", (void *) 0,
                           "vpp", argcp, argv);
#endif
}
VDECLCB(shim_player_selection, (void), "v")
VDECLCB(shim_askname, (void), "v")
VDECLCB(shim_get_nh_event, (void), "v")
void
shim_exit_nhwindows(str)
const char *str;
{
#ifdef __EMSCRIPTEN__
    void *args[] = { P2V str };
#endif
    debugf("SHIM GRAPHICS: shim_exit_nhwindows\n");
    bot_set_handler((void (*)()) 0);
#ifdef __EMSCRIPTEN__
    if (!shim_callback_name) return;
    local_callback(shim_callback_name, "shim_exit_nhwindows", (void *) 0,
                   "vs", args);
#else
    if (!shim_graphics_callback) return;
    shim_graphics_callback("shim_exit_nhwindows", (void *) 0, "vs", str);
#endif
}
VDECLCB(shim_suspend_nhwindows, (const char *str), "vs", P2V str)
VDECLCB(shim_resume_nhwindows, (void), "v")
DECLCB(winid, shim_create_nhwindow, (int type), "ii", A2P type)
VDECLCB(shim_clear_nhwindow, (winid window), "vi", A2P window)
VDECLCB(shim_display_nhwindow, (winid window, BOOLEAN_P blocking), "vib",
        A2P window, A2P blocking)
VDECLCB(shim_destroy_nhwindow, (winid window), "vi", A2P window)
VDECLCB(shim_curs, (winid a, int x, int y), "viii", A2P a, A2P x, A2P y)
VDECLCB(shim_putstr, (winid w, int attr, const char *str), "viis",
        A2P w, A2P attr, P2V str)
#ifdef FILE_AREAS
VDECLCB(shim_display_file,
        (const char *area, const char *name, BOOLEAN_P complain),
        "vssb", P2V area, P2V name, A2P complain)
#else
VDECLCB(shim_display_file, (const char *name, BOOLEAN_P complain),
        "vsb", P2V name, A2P complain)
#endif
VDECLCB(shim_start_menu, (winid window), "vi", A2P window)
VDECLCB(shim_add_menu,
        (winid window, int glyph, const ANY_P *identifier, CHAR_P ch,
         CHAR_P gch, int attr, const char *str, BOOLEAN_P preselected),
        "viipccisb",
        A2P window, A2P glyph, P2V identifier, A2P ch, A2P gch, A2P attr,
        P2V str, A2P preselected)
VDECLCB(shim_end_menu, (winid window, const char *prompt), "vis",
        A2P window, P2V prompt)
DECLCB(int, shim_select_menu, (winid window, int how, MENU_ITEM_P **menu_list),
       "iiip", A2P window, A2P how, P2V menu_list)
DECLCB(char, shim_message_menu, (CHAR_P let, int how, const char *mesg),
       "ciis", A2P let, A2P how, P2V mesg)

#ifdef __EMSCRIPTEN__
void
shim_update_inventory(void)
{
    EM_ASM({
        globalThis.nethackGlobal = globalThis.nethackGlobal || {};
        globalThis.nethackGlobal.pendingInventoryUpdate = true;
    });
}
#else
VDECLCB(shim_update_inventory, (void), "v")
#endif

VDECLCB(shim_mark_synch, (void), "v")
VDECLCB(shim_wait_synch, (void), "v")
#ifdef CLIPPING
VDECLCB(shim_cliparound, (int x, int y), "vii", A2P x, A2P y)
#endif
#ifdef POSITIONBAR
VDECLCB(shim_update_positionbar, (char *posbar), "vs", P2V posbar)
#endif
void
shim_print_glyph(w, x, y, glyph)
winid w;
XCHAR_P x, y;
int glyph;
{
    int monster_id = nh3d_monster_id_from_tile(x, y, glyph);
    int attacking_target_id = nh3d_get_recent_attack_target_id(monster_id);

    debugf("SHIM GRAPHICS: shim_print_glyph\n");
#ifdef __EMSCRIPTEN__
    if (shim_callback_name) {
        void *args[] = { A2P w, A2P x, A2P y, A2P glyph, A2P monster_id,
                         A2P attacking_target_id };
        local_callback(shim_callback_name, "shim_print_glyph", (void *) 0,
                       "vi00iii", args);
    }
#else
    if (shim_graphics_callback)
        shim_graphics_callback("shim_print_glyph", (void *) 0, "vi00iii", w,
                               x, y, glyph, monster_id,
                               attacking_target_id);
#endif
}
VDECLCB(shim_raw_print, (const char *str), "vs", P2V str)
VDECLCB(shim_raw_print_bold, (const char *str), "vs", P2V str)
DECLCB(int, shim_nhgetch, (void), "i")
DECLCB(int, shim_nh_poskey, (int *x, int *y, int *mod), "ippp",
       P2V x, P2V y, P2V mod)
VDECLCB(shim_nhbell, (void), "v")
DECLCB(int, shim_doprev_message, (void), "i")
DECLCB(char, shim_yn_function, (const char *query, const char *resp, CHAR_P def),
       "cssc", P2V query, P2V resp, A2P def)
VDECLCB(shim_getlin, (const char *query, char *bufp), "vsp", P2V query, P2V bufp)
DECLCB(int, shim_get_ext_cmd, (void), "i")
VDECLCB(shim_number_pad, (int state), "vi", A2P state)
VDECLCB(shim_delay_output, (void), "v")
VDECLCB(shim_status_init, (void), "v")
VDECLCB(shim_status_enablefield,
        (int fieldidx, const char *nm, const char *fmt, BOOLEAN_P enable),
        "vippb",
        A2P fieldidx, P2V nm, P2V fmt, A2P enable)
VDECLCB(shim_status_update,
        (int fldidx, genericptr_t ptr, int chg, int percent, int color,
         unsigned long *colormasks),
        "vipiiip",
        A2P fldidx, P2V ptr, A2P chg, A2P percent, A2P color, P2V colormasks)
#ifdef CHANGE_COLOR
VDECLCB(shim_change_color, (int color, long rgb, int reverse), "viii",
        A2P color, A2P rgb, A2P reverse)
#ifdef MAC
VDECLCB(shim_change_background, (int white_or_black), "vi", A2P white_or_black)
DECLCB(short, set_shim_font_name, (winid window_type, char *font_name),
       "2is", A2P window_type, P2V font_name)
#endif
DECLCB(char *, shim_get_color_string, (void), "s")
#endif
VDECLCB(shim_start_screen, (void), "v")
VDECLCB(shim_end_screen, (void), "v")
VDECLCB(shim_outrip, (winid tmpwin, int how), "vii", A2P tmpwin, A2P how)
VDECLCB(shim_preference_update, (const char *pref), "vs", P2V pref)

static void
shim_status_bridge_emit_string(fieldidx, value)
int fieldidx;
const char *value;
{
    shim_status_update(fieldidx, (genericptr_t) (value ? value : ""), 0, 0, 0,
                       (unsigned long *) 0);
}

static void
shim_status_bridge_emit_mask(mask)
unsigned long mask;
{
    unsigned long condmask = mask;

    shim_status_update(SHIM_BL_CONDITION, (genericptr_t) &condmask, 0, 0, 0,
                       (unsigned long *) 0);
}

static void
shim_status_bridge_flush()
{
    shim_status_update(SHIM_BL_FLUSH, (genericptr_t) 0, 0, 0, 0,
                       (unsigned long *) 0);
}

static void
shim_status_bridge_reconfig()
{
    static const char fmt_string[] = "%s";

    shim_status_init();
    shim_status_enablefield(SHIM_BL_TITLE, "title", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_STR, "strength", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_DX, "dexterity", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_CO, "constitution", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_IN, "intelligence", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_WI, "wisdom", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_CH, "charisma", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_ALIGN, "alignment", fmt_string, TRUE);
#ifdef SCORE_ON_BOTL
    shim_status_enablefield(SHIM_BL_SCORE, "score", fmt_string,
                            flags.showscore ? TRUE : FALSE);
#endif
    shim_status_enablefield(SHIM_BL_LEVELDESC, "dlevel", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_GOLD, "gold", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_HP, "hp", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_HPMAX, "hpmax", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_ENE, "pw", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_ENEMAX, "pwmax", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_AC, "ac", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_XP, Upolyd ? "hitdice" : "elevel",
                            fmt_string, TRUE);
#ifdef EXP_ON_BOTL
    shim_status_enablefield(SHIM_BL_EXP, "experience", fmt_string,
                            flags.showexp ? TRUE : FALSE);
#endif
    shim_status_enablefield(SHIM_BL_TIME, "time", fmt_string,
                            flags.time ? TRUE : FALSE);
    shim_status_enablefield(SHIM_BL_HUNGER, "hunger", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_CAP, "encumberance", fmt_string, TRUE);
    shim_status_enablefield(SHIM_BL_CONDITION, "flags", fmt_string, TRUE);
}

static void
shim_status_raw_bridge(reconfig, nv, values)
int reconfig, nv;
const char **values;
{
    int idx = 0;
    unsigned long condition_mask = 0UL;

    (void) nv;

    if (reconfig) {
        shim_status_bridge_reconfig();
        return;
    }

    if (!values)
        return;

    shim_status_bridge_emit_string(SHIM_BL_TITLE, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_STR, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_DX, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_CO, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_IN, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_WI, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_CH, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_ALIGN, values[idx++]);
#ifdef SCORE_ON_BOTL
    if (flags.showscore)
        shim_status_bridge_emit_string(SHIM_BL_SCORE, values[idx++]);
#endif
    shim_status_bridge_emit_string(SHIM_BL_LEVELDESC, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_GOLD, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_HP, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_HPMAX, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_ENE, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_ENEMAX, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_AC, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_XP, values[idx++]);
#ifdef EXP_ON_BOTL
    if (flags.showexp)
        shim_status_bridge_emit_string(SHIM_BL_EXP, values[idx++]);
#endif
#ifdef SHOW_WEIGHT
    if (flags.showweight) {
        idx++;
        idx++;
    }
#endif
    if (flags.time)
        shim_status_bridge_emit_string(SHIM_BL_TIME, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_HUNGER, values[idx++]);
    shim_status_bridge_emit_string(SHIM_BL_CAP, values[idx++]);
    idx++; /* raw flags string; condition mask is rebuilt below */

    if (Levitation)
        condition_mask |= RAW_STAT_LEVITATION;
    if (Confusion)
        condition_mask |= RAW_STAT_CONFUSION;
    if (Sick && (u.usick_type & SICK_VOMITABLE))
        condition_mask |= RAW_STAT_FOODPOIS;
    if (Sick && (u.usick_type & SICK_NONVOMITABLE))
        condition_mask |= RAW_STAT_ILL;
    if (Blind)
        condition_mask |= RAW_STAT_BLIND;
    if (Stunned)
        condition_mask |= RAW_STAT_STUNNED;
    if (Hallucination)
        condition_mask |= RAW_STAT_HALLUCINATION;
    if (Slimed)
        condition_mask |= RAW_STAT_SLIMED;
    if (u.ustuck && !u.uswallow && !sticks(youmonst.data))
        condition_mask |= SHIM_RAW_STAT_HELD;

    shim_status_bridge_emit_mask(condition_mask);
    shim_status_bridge_flush();
}

struct window_procs shim_procs = {
    "shim",
    (0L
     | WC_ASCII_MAP
     | WC_TILED_MAP
     | WC_MOUSE_SUPPORT
     | WC_COLOR
     | WC_HILITE_PET
     | WC_INVERSE
     | WC_EIGHT_BIT_IN
     | WC_PLAYER_SELECTION),
    (0L
     | WC2_FULLSCREEN
     | WC2_SOFTKEYBOARD
     | WC2_WRAPTEXT),
    shim_init_nhwindows,
    shim_player_selection,
    shim_askname,
    shim_get_nh_event,
    shim_exit_nhwindows,
    shim_suspend_nhwindows,
    shim_resume_nhwindows,
    shim_create_nhwindow,
    shim_clear_nhwindow,
    shim_display_nhwindow,
    shim_destroy_nhwindow,
    shim_curs,
    shim_putstr,
    shim_display_file,
    shim_start_menu,
    shim_add_menu,
    shim_end_menu,
    shim_select_menu,
    shim_message_menu,
    shim_update_inventory,
    shim_mark_synch,
    shim_wait_synch,
#ifdef CLIPPING
    shim_cliparound,
#endif
#ifdef POSITIONBAR
    shim_update_positionbar,
#endif
    shim_print_glyph,
    shim_raw_print,
    shim_raw_print_bold,
    shim_nhgetch,
    shim_nh_poskey,
    shim_nhbell,
    shim_doprev_message,
    shim_yn_function,
    shim_getlin,
    shim_get_ext_cmd,
    shim_number_pad,
    shim_delay_output,
#ifdef CHANGE_COLOR
    shim_change_color,
#ifdef MAC
    shim_change_background,
    set_shim_font_name,
#endif
    shim_get_color_string,
#endif
    shim_start_screen,
    shim_end_screen,
    shim_outrip,
    shim_preference_update,
};

#ifdef __EMSCRIPTEN__
EM_JS(void, local_callback,
      (const char *cb_name, const char *shim_name, void *ret_ptr,
       const char *fmt_str, void *args), {
    if (globalThis.nethackGlobal && globalThis.nethackGlobal.pendingInventoryUpdate) {
        globalThis.nethackGlobal.pendingInventoryUpdate = false;
        let pendingCbName = UTF8ToString(cb_name);
        let pendingCb = globalThis[pendingCbName];
        if (pendingCb) {
            pendingCb.call(null, "shim_update_inventory");
        }
    }
    if (globalThis.nethackGlobal
        && Array.isArray(globalThis.nethackGlobal.pendingShimNotifications)
        && globalThis.nethackGlobal.pendingShimNotifications.length > 0) {
        let pendingCbName = UTF8ToString(cb_name);
        let pendingCb = globalThis[pendingCbName];
        if (pendingCb) {
            const pendingNotifications =
                globalThis.nethackGlobal.pendingShimNotifications.splice(0);
            for (let i = 0; i < pendingNotifications.length; ++i) {
                pendingCb.call(null, ...pendingNotifications[i]);
            }
        }
    }

    Asyncify.handleSleep(wakeUp => {
        let name = UTF8ToString(shim_name);
        let fmt = UTF8ToString(fmt_str);
        let cbName = UTF8ToString(cb_name);
        let getPointerValue = globalThis.nethackGlobal.helpers.getPointerValue;
        let setPointerValue = globalThis.nethackGlobal.helpers.setPointerValue;

        reentryGuardEnter(name);

        let argTypes = fmt.split("");
        let retType = argTypes.shift();
        let jsArgs = [];

        for (let i = 0; i < argTypes.length; i++) {
            let ptr = args + (4 * i);
            let val = getArg(ptr, argTypes[i]);
            jsArgs.push(val);
        }

        let userCallback = globalThis[cbName];
        userCallback.call(this, name, ...jsArgs).then(retVal => {
            setPointerValue(name, ret_ptr, retType, retVal);
            reentryGuardExit();
            wakeUp();
        });

        function getArg(ptr, type) {
            return (type === "p")
                ? getValue(ptr, "*")
                : getPointerValue(name, getValue(ptr, "*"), type);
        }

        function reentryGuardEnter(currentName) {
            globalThis.nethackGlobal = globalThis.nethackGlobal || {};
            if (globalThis.nethackGlobal.shimFunctionRunning) {
                console.error(
                    `'${currentName}' attempted local_callback reentry before ` +
                    `'${globalThis.nethackGlobal.shimFunctionRunning}' finished`
                );
            }
            globalThis.nethackGlobal.shimFunctionRunning = currentName;
        }

        function reentryGuardExit() {
            globalThis.nethackGlobal.shimFunctionRunning = null;
        }
    });
});
#endif

#endif /* SHIM_GRAPHICS */
