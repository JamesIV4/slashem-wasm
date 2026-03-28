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

VDECLCB(shim_init_nhwindows, (int *argcp, char **argv), "vpp",
        P2V argcp, P2V argv)
VDECLCB(shim_player_selection, (void), "v")
VDECLCB(shim_askname, (void), "v")
VDECLCB(shim_get_nh_event, (void), "v")
VDECLCB(shim_exit_nhwindows, (const char *str), "vs", P2V str)
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
VDECLCB(shim_print_glyph, (winid w, XCHAR_P x, XCHAR_P y, int glyph),
        "vi00i", A2P w, A2P x, A2P y, A2P glyph)
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
