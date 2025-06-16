/*
 *    See LICENSE for more info
 *
 *    simple xorg window manager
 *    sxwm is a user-friendly, easily configurable yet powerful
 *    tiling window manager inspired by window managers such as
 *    DWM and i3.
 *
 *    The userconfig is designed to be as user-friendly as
 *    possible, and I hope it is easy to configure even without
 *    knowledge of C or programming, although most people who
 *    will use this will probably be programmers :)
 *
 *    (C) Abhinav Prasai 2025
 */

#include <X11/X.h>
#include <err.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>

#include <X11/extensions/Xinerama.h>
#include <X11/Xcursor/Xcursor.h>

#include "defs.h"
#include "parser.h"

// New: Layout enum for TILE, HORIZONTAL, MONOCLE
typedef enum { TILE, HORIZONTAL, MONOCLE } Layout;

Client *add_client(Window w, int ws);
void change_workspace(int ws);
int clean_mask(int mask);
void close_focused(void);
void dec_gaps(void);
void startup_exec(void);
Window find_toplevel(Window w);
void focus_next(void);
void focus_prev(void);
void focus_next_mon(void);
void focus_prev_mon(void);
void move_next_mon(void);
void move_prev_mon(void);
int get_monitor_for(Client *c);
void grab_keys(void);
void hdl_button(XEvent *xev);
void hdl_button_release(XEvent *xev);
void hdl_client_msg(XEvent *xev);
void hdl_config_ntf(XEvent *xev);
void hdl_config_req(XEvent *xev);
void hdl_dummy(XEvent *xev);
void hdl_destroy_ntf(XEvent *xev);
void hdl_keypress(XEvent *xev);
void hdl_map_req(XEvent *xev);
void hdl_motion(XEvent *xev);
void hdl_root_property(XEvent *xev);
void hdl_unmap_ntf(XEvent *xev);
void inc_gaps(void);
void init_defaults(void);
void move_master_next(void);
void move_master_prev(void);
void move_to_workspace(int ws);
void other_wm(void);
int other_wm_err(Display *dpy, XErrorEvent *ee);
long parse_col(const char *hex);
void quit(void);
void reload_config(void);
void resize_master_add(void);
void resize_master_sub(void);
void resize_stack_add(void);
void resize_stack_sub(void);
void run(void);
void scan_existing_windows(void);
void send_wm_take_focus(Window w);
void setup(void);
void setup_atoms(void);
Bool window_should_float(Window w);
void spawn(const char **argv);
void swap_clients(Client *a, Client *b);
void tile(void);
void toggle_floating(void);
void toggle_floating_global(void);
void toggle_fullscreen(void);
void toggle_horizontal(void); // New: Horizontal layout
void toggle_monocle(void);   // New: Monocle layout
void update_borders(void);
void update_client_desktop_properties(void);
void update_monitors(void);
void update_net_client_list(void);
void update_struts(void);
void update_workarea(void);
void warp_cursor(Client *c);
int xerr(Display *dpy, XErrorEvent *ee);
void xev_case(XEvent *xev);
#include "config.h"

Atom atom_net_active_window;
Atom atom_net_current_desktop;
Atom atom_net_supported;
Atom atom_net_wm_state;
Atom atom_net_wm_state_fullscreen;
Atom atom_wm_window_type;
Atom atom_net_wm_window_type_dock;
Atom atom_net_workarea;
Atom atom_wm_delete;
Atom atom_wm_strut;
Atom atom_wm_strut_partial;
Atom atom_net_supporting_wm_check;
Atom atom_net_wm_name;
Atom atom_utf8_string;
Atom atom_net_wm_desktop;
Atom atom_net_client_list;

Cursor c_normal, c_move, c_resize;
Client *workspaces[NUM_WORKSPACES] = {NULL};
Config default_config;
Config user_config;
int current_ws = 0;
DragMode drag_mode = DRAG_NONE;
Client *drag_client = NULL;
Client *swap_target = NULL;
Client *focused = NULL;
EventHandler evtable[LASTEvent];
Display *dpy;
Window root;
Window wm_check_win;
Monitor *mons = NULL;
int monsn = 0;
int current_monitor = 0;
Bool global_floating = False;
Bool in_ws_switch = False;
Bool backup_binds = False;
Bool running = False;

long last_motion_time = 0;
int scr_width;
int scr_height;
int open_windows = 0;
int drag_start_x, drag_start_y;
int drag_orig_x, drag_orig_y, drag_orig_w, drag_orig_h;

int reserve_left = 0;
int reserve_right = 0;
int reserve_top = 0;
int reserve_bottom = 0;

Bool next_should_float = False;

// New: Array to track layout per monitor
static Layout layout[MAX_MONITORS] = {TILE}; // Default to TILE for all monitors

Client *add_client(Window w, int ws)
{
    Client *c = malloc(sizeof(Client));
    if (!c) {
        fprintf(stderr, "sxwm: could not alloc memory for client\n");
        return NULL;
    }

    c->win = w;
    c->next = NULL;
    c->ws = ws;

    if (!workspaces[ws]) {
        workspaces[ws] = c;
    }
    else {
        Client *tail = workspaces[ws];
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = c;
    }

    open_windows++;
    XSelectInput(dpy, w,
                 EnterWindowMask | LeaveWindowMask | FocusChangeMask | PropertyChangeMask | StructureNotifyMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask);

    XGrabButton(dpy, Button1, 0, w, False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
    XGrabButton(dpy, Button1, user_config.modkey, w, False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
    XGrabButton(dpy, Button1, user_config.modkey | ShiftMask, w, False, ButtonPressMask, GrabModeSync, GrabModeAsync,
                None, None);
    XGrabButton(dpy, Button3, user_config.modkey, w, False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);

    Atom protos[] = {atom_wm_delete};
    XSetWMProtocols(dpy, w, protos, 1);

    XWindowAttributes wa;
    XGetWindowAttributes(dpy, w, &wa);
    c->x = wa.x;
    c->y = wa.y;
    c->w = wa.width;
    c->h = wa.height;

    /* set monitor based on pointer location */
    Window root_ret, child_ret;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    int pointer_mon = 0;

    if (XQueryPointer(dpy, root, &root_ret, &child_ret, &root_x, &root_y, &win_x, &win_y, &mask)) {
        for (int i = 0; i < monsn; i++) {
            if (root_x >= mons[i].x && root_x < mons[i].x + mons[i].w && root_y >= mons[i].y &&
                root_y < mons[i].y + mons[i].h) {
                pointer_mon = i;
                break;
            }
        }
    }

    c->mon = pointer_mon;
    c->fixed = False;
    c->floating = False;
    c->fullscreen = False;
    c->mapped = True;
    c->custom_stack_height = 0;

    // New: Set new windows to non-floating in MONOCLE mode
    if (layout[c->mon] == MONOCLE) {
        c->floating = False;
    } else if (global_floating) {
        c->floating = True;
    }

    if (ws == current_ws && !focused) {
        focused = c;
    }

    long desktop = ws;
    XChangeProperty(dpy, w, XInternAtom(dpy, "_NET_WM_DESKTOP", False), XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&desktop, 1);

    XRaiseWindow(dpy, w);
    return c;
}

void change_workspace(int ws)
{
    if (ws >= NUM_WORKSPACES || ws == current_ws) {
        return;
    }

    in_ws_switch = True;
    XGrabServer(dpy);

    /* unmap those still marked mapped */
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->mapped) {
            XUnmapWindow(dpy, c->win);
        }
    }

    current_ws = ws;

    /* map those still marked mapped */
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->mapped) {
            XMapWindow(dpy, c->win);
        }
    }

    tile();

    focused = NULL;
    if (workspaces[current_ws]) {
        focused = workspaces[current_ws];
        Window focused_win = find_toplevel(focused->win);
        XSetInputFocus(dpy, focused_win, RevertToPointerRoot, CurrentTime);
        if (user_config.warp_cursor) {
            warp_cursor(focused);
        }
        update_borders();
    }
    else {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    }

    long cd = current_ws;
    XChangeProperty(dpy, root, XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False), XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&cd, 1);
    update_client_desktop_properties();

    XUngrabServer(dpy);
    XSync(dpy, False);
    in_ws_switch = False;
}

int clean_mask(int mask)
{
    return mask & ~(LockMask | Mod2Mask | Mod3Mask);
}

void close_focused(void)
{
    if (!focused) {
        return;
    }

    Atom *protos;
    int n;
    if (XGetWMProtocols(dpy, focused->win, &protos, &n) && protos) {
        for (int i = 0; i < n; i++) {
            if (protos[i] == atom_wm_delete) {
                XEvent ev = {.xclient = {.type = ClientMessage,
                                         .window = focused->win,
                                         .message_type = XInternAtom(dpy, "WM_PROTOCOLS", False),
                                         .format = 32}};
                ev.xclient.data.l[0] = atom_wm_delete;
                ev.xclient.data.l[1] = CurrentTime;
                XSendEvent(dpy, focused->win, False, NoEventMask, &ev);
                XFree(protos);
                return;
            }
        }
        XUnmapWindow(dpy, focused->win);
        XFree(protos);
    }
    XUnmapWindow(dpy, focused->win);
    XKillClient(dpy, focused->win);
}

void dec_gaps(void)
{
    if (user_config.gaps > 0) {
        user_config.gaps--;
        tile();
        update_borders();
    }
}

void startup_exec(void)
{
    for (int i = 0; i < 256; i++) {
        if (user_config.torun[i]) {
            const char **argv = build_argv(user_config.torun[i]);
            if (argv) {
                spawn(argv);
                for (int j = 0; argv[j]; j++) {
                    free((char *)argv[j]);
                }
                free(argv);
            }
        }
    }
}

Window find_toplevel(Window w)
{
    Window root = None;
    Window parent;
    Window *kids;
    unsigned nkids;

    while (True) {
        if (w == root) {
            break;
        }
        if (XQueryTree(dpy, w, &root, &parent, &kids, &nkids) == 0) {
            break;
        }
        XFree(kids);
        if (parent == root || parent == None) {
            break;
        }
        w = parent;
    }
    return w;
}

void focus_next(void)
{
    if (!workspaces[current_ws]) {
        return;
    }

    Client *start = focused ? focused : workspaces[current_ws];
    Client *c = start;

    /* loop until we find a mapped client or return to start */
    do {
        c = c->next ? c->next : workspaces[current_ws];
    } while (!c->mapped && c != start);

    /* this stops invisible windows being detected or focused */
    if (!c->mapped) {
        return;
    }

    focused = c;
    current_monitor = c->mon;
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
    XRaiseWindow(dpy, c->win);
    if (user_config.warp_cursor)
        warp_cursor(c);
    update_borders();
}

void focus_prev(void)
{
    if (!workspaces[current_ws]) {
        return;
    }

    Client *start = focused ? focused : workspaces[current_ws];
    Client *c = start;

    /* loop until we find a mapped client or return to start */
    do {
        Client *p = workspaces[current_ws], *prev = NULL;
        while (p && p != c) {
            prev = p;
            p = p->next;
        }

        if (prev) {
            c = prev;
        }
        else {
            /* wrap to tail */
            p = workspaces[current_ws];
            while (p->next)
                p = p->next;
            c = p;
        }
    } while (!c->mapped && c != start);

    /* this stops invisible windows being detected or focused */
    if (!c->mapped) {
        return;
    }

    focused = c;
    current_monitor = c->mon;

    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
    XRaiseWindow(dpy, c->win);
    if (user_config.warp_cursor) {
        warp_cursor(c);
    }
    update_borders();
}

void focus_next_mon(void)
{
    if (monsn <= 1) {
        return; /* only one monitor, nothing to switch to */
    }

    /* use current_monitor if no focused window, otherwise use focused window's monitor */
    int current_mon = focused ? focused->mon : current_monitor;
    int target_mon = (current_mon + 1) % monsn;

    /* find the first window on the target monitor in current workspace */
    Client *target_client = NULL;
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->mon == target_mon && c->mapped && !c->fullscreen) {
            target_client = c;
            break;
        }
    }

    if (target_client) {
        /* focus the window on target monitor */
        focused = target_client;
        current_monitor = target_mon;
        XSetInputFocus(dpy, focused->win, RevertToPointerRoot, CurrentTime);
        XRaiseWindow(dpy, focused->win);
        if (user_config.warp_cursor) {
            warp_cursor(focused);
        }
        update_borders();
    }
    else {
        /* no windows on target monitor, just move cursor to center and update current_monitor */
        current_monitor = target_mon;
        int center_x = mons[target_mon].x + mons[target_mon].w / 2;
        int center_y = mons[target_mon].y + mons[target_mon].h / 2;
        XWarpPointer(dpy, None, root, 0, 0, 0, 0, center_x, center_y);
        XSync(dpy, False);
    }
}

void focus_prev_mon(void)
{
    if (monsn <= 1) {
        return; /* only one monitor, nothing to switch to */
    }

    /* use current_monitor if no focused window, otherwise use focused window's monitor */
    int current_mon = focused ? focused->mon : current_monitor;
    int target_mon = (current_mon - 1 + monsn) % monsn;

    /* find the first window on the target monitor in current workspace */
    Client *target_client = NULL;
    for (Client *c = workspaces[current_ws]; c; c = c->next) {
        if (c->mon == target_mon && c->mapped && !c->fullscreen) {
            target_client = c;
            break;
        }
    }

    if (target_client) {
        /* focus the window on target monitor */
        focused = target_client;
        current_monitor = target_mon;
        XSetInputFocus(dpy, focused->win, RevertToPointerRoot, CurrentTime);
        XRaiseWindow(dpy, focused->win);
        if (user_config.warp_cursor) {
            warp_cursor(focused);
        }
        update_borders();
    }
    else {
        /* no windows on target monitor, just move cursor to center and update current_monitor */
        current_monitor = target_mon;
        int center_x = mons[target_mon].x + mons[target_mon].w / 2;
        int center_y = mons[target_mon].y + mons[target_mon].h / 2;
        XWarpPointer(dpy, None, root, 0, 0, 0, 0, center_x, center_y);
        XSync(dpy, False);
    }
}

void move_next_mon(void)
{
    if (!focused || monsn <= 1) {
        return; /* no focused window or only one monitor */
    }

    int target_mon = (focused->mon + 1) % monsn;

    /* update window's monitor assignment */
    focused->mon = target_mon;
    current_monitor = target_mon;

    /* if window is floating, center it on the target monitor */
    if (focused->floating) {
        int mx = mons[target_mon].x, my = mons[target_mon].y;
        int mw = mons[target_mon].w, mh = mons[target_mon].h;
        int x = mx + (mw - focused->w) / 2;
        int y = my + (mh - focused->h) / 2;

        /* ensure window stays within monitor bounds */
        if (x < mx)
            x = mx;
        if (y < my)
            y = my;
        if (x + focused->w > mx + mw)
            x = mx + mw - focused->w;
        if (y + focused->h > my + mh)
            y = my + mh - focused->h;

        focused->x = x;
        focused->y = y;
        XMoveWindow(dpy, focused->win, x, y);
    }

    /* retile to update layouts on both monitors */
    tile();

    /* follow the window with cursor if enabled */
    if (user_config.warp_cursor) {
        warp_cursor(focused);
    }

    update_borders();
}

void move_prev_mon(void)
{
    if (!focused || monsn <= 1) {
        return; /* no focused window or only one monitor */
    }

    int target_mon = (focused->mon - 1 + monsn) % monsn;

    /* update window's monitor assignment */
    focused->mon = target_mon;
    current_monitor = target_mon;

    /* if window is floating, center it on the target monitor */
    if (focused->floating) {
        int mx = mons[target_mon].x, my = mons[target_mon].y;
        int mw = mons[target_mon].w, mh = mons[target_mon].h;
        int x = mx + (mw - focused->w) / 2;
        int y = my + (mh - focused->h) / 2;

        /* ensure window stays within monitor bounds */
        if (x < mx)
            x = mx;
        if (y < my)
            y = my;
        if (x + focused->w > mx + mw)
            x = mx + mw - focused->w;
        if (y + focused->h > my + mh)
            y = my + mh - focused->h;

        focused->x = x;
        focused->y = y;
        XMoveWindow(dpy, focused->win, x, y);
    }

    /* retile to update layouts on both monitors */
    tile();

    /* follow the window with cursor if enabled */
    if (user_config.warp_cursor) {
        warp_cursor(focused);
    }

    update_borders();
}

int get_monitor_for(Client *c)
{
    int cx = c->x + c->w / 2, cy = c->y + c->h / 2;
    for (int i = 0; i < monsn; i++) {
        if (cx >= (int)mons[i].x && cx < mons[i].x + mons[i].w && cy >= (int)mons[i].y && cy < mons[i].y + mons[i].h) {
            return i;
        }
    }
    return 0;
}

void grab_keys(void)
{
    const int guards[] = {0,
                          LockMask,
                          Mod2Mask,
                          LockMask | Mod2Mask,
                          Mod5Mask,
                          LockMask | Mod5Mask,
                          Mod2Mask | Mod5Mask,
                          LockMask | Mod2Mask | Mod5Mask};
    XUngrabKey(dpy, AnyKey, AnyModifier, root);

    for (int i = 0; i < user_config.bindsn; i++) {
        Binding *b = &user_config.binds[i];

        if ((b->type == TYPE_CWKSP && b->mods != user_config.modkey) ||
            (b->type == TYPE_MWKSP && b->mods != (user_config.modkey | ShiftMask))) {
            continue;
        }

        KeyCode kc = XKeysymToKeycode(dpy, b->keysym);
        if (!kc) {
            continue;
        }

        for (size_t g = 0; g < sizeof guards / sizeof *guards; g++) {
            XGrabKey(dpy, kc, b->mods | guards[g], root, True, GrabModeAsync, GrabModeAsync);
        }
    }
}

void hdl_button(XEvent *xev)
{
    XButtonEvent *e = &xev->xbutton;
    Window w = (e->subwindow != None) ? e->subwindow : e->window;
    w = find_toplevel(w);

    XAllowEvents(dpy, ReplayPointer, e->time);
    if (!w) {
        return;
    }

    Client *head = workspaces[current_ws];
    for (Client *c = head; c; c = c->next) {
        if (c->win != w) {
            continue;
        }

        /* begin swap drag mode */
        if ((e->state & user_config.modkey) && (e->state & ShiftMask) && e->button == Button1 && !c->floating) {
            drag_client = c;
            drag_start_x = e->x_root;
            drag_start_y = e->y_root;
            drag_orig_x = c->x;
            drag_orig_y = c->y;
            drag_orig_w = c->w;
            drag_orig_h = c->h;
            drag_mode = DRAG_SWAP;
            XGrabPointer(dpy, root, True, ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None,
                         c_move, CurrentTime);
            focused = c;
            XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
            XSetWindowBorder(dpy, c->win, user_config.border_swap_col);
            XRaiseWindow(dpy, c->win);
            return;
        }

        if ((e->state & user_config.modkey) && (e->button == Button1 || e->button == Button3) && !c->floating) {
            focused = c;
            toggle_floating();
        }

        if (!(e->state & user_config.modkey) && e->button == Button1) {
            focused = c;
            XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
            send_wm_take_focus(c->win);
            XRaiseWindow(dpy, c->win);
            update_borders();
            return;
        }

        if (!c->floating) {
            return;
        }

        if (c->fixed && e->button == Button3) {
            return;
        }

        Cursor cur = (e->button == Button1) ? c_move : c_resize;
        XGrabPointer(dpy, root, True, ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, cur,
                     CurrentTime);

        drag_client = c;
        drag_start_x = e->x_root;
        drag_start_y = e->y_root;
        drag_orig_x = c->x;
        drag_orig_y = c->y;
        drag_orig_w = c->w;
        drag_orig_h = c->h;
        drag_mode = (e->button == Button1) ? DRAG_MOVE : DRAG_RESIZE;
        focused = c;

        XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
        update_borders();
        XRaiseWindow(dpy, c->win);
        return;
    }
}

void hdl_button_release(XEvent *xev)
{
    (void)xev;

    if (drag_mode == DRAG_SWAP) {
        if (swap_target) {
            XSetWindowBorder(dpy, swap_target->win,
                             (swap_target == focused ? user_config.border_foc_col : user_config.border_ufoc_col));
            swap_clients(drag_client, swap_target);
        }
        tile();
        update_borders();
    }

    XUngrabPointer(dpy, CurrentTime);

    drag_mode = DRAG_NONE;
    drag_client = NULL;
    swap_target = NULL;
}

void hdl_client_msg(XEvent *xev)
{
    /* clickable bar workspace switching */
    if (xev->xclient.message_type == atom_net_current_desktop) {
        int ws = (int)xev->xclient.data.l[0];
        change_workspace(ws);
        return;
    }
    if (xev->xclient.message_type == atom_net_wm_state) {
        long action = xev->xclient.data.l[0];
        Atom target = xev->xclient.data.l[1];
        if (target == atom_net_wm_state_fullscreen) {
            if (action == 1 || action == 2) {
                toggle_fullscreen();
            }
            else if (action == 0 && focused && focused->fullscreen) {
                toggle_fullscreen();
            }
        }
        return;
    }
}

void hdl_config_ntf(XEvent *xev)
{
    if (xev->xconfigure.window == root) {
        update_monitors();
        tile();
        update_borders();
    }
}

void hdl_config_req(XEvent *xev)
{
    XConfigureRequestEvent *e = &xev->xconfigurerequest;
    Client *c = NULL;

    for (int ws = 0; ws < NUM_WORKSPACES && !c; ws++) {
        for (c = workspaces[ws]; c; c = c->next) {
            if (c->win == e->window) {
                break;
            }
        }
    }

    if (!c || c->floating || c->fullscreen) {
        /* allow client to configure itself */
        XWindowChanges wc = {.x = e->x,
                             .y = e->y,
                             .width = e->width,
                             .height = e->height,
                             .border_width = e->border_width,
                             .sibling = e->above,
                             .stack_mode = e->detail};
        XConfigureWindow(dpy, e->window, e->value_mask, &wc);
        return;
    }

    if (c->fixed) {
        return;
    }
}

void hdl_dummy(XEvent *xev)
{
    (void)xev;
}

void hdl_destroy_ntf(XEvent *xev)
{
    Window w = xev->xdestroywindow.window;

    for (int ws = 0; ws < NUM_WORKSPACES; ws++) {
        Client *prev = NULL, *c = workspaces[ws];
        while (c && c->win != w) {
            prev = c;
            c = c->next;
        }
        if (c) {
            if (focused == c) {
                if (c->next) {
                    focused = c->next;
                }
                else if (prev) {
                    focused = prev;
                }
                else {
                    if (ws == current_ws) {
                        focused = NULL;
                    }
                }
            }

            if (!prev) {
                workspaces[ws] = c->next;
            }
            else {
                prev->next = c->next;
            }

            free(c);
            update_net_client_list();
            open_windows--;

            if (ws == current_ws) {
                tile();
                update_borders();

                if (focused) {
                    XSetInputFocus(dpy, focused->win, RevertToPointerRoot, CurrentTime);
                    XRaiseWindow(dpy, focused->win);
                }
            }
            return;
        }
    }
}

void hdl_keypress(XEvent *xev)
{
    KeySym ks = XkbKeycodeToKeysym(dpy, xev->xkey.keycode, 0, 0);
    int mods = clean_mask(xev->xkey.state);

    for (int i = 0; i < user_config.bindsn; i++) {
        Binding *b = &user_config.binds[i];
        if (b->keysym == ks && clean_mask(b->mods) == mods) {
            switch (b->type) {
                case TYPE_CMD:
                    spawn(b->action.cmd);
                    break;

                case TYPE_FUNC:
                    if (b->action.fn) {
                        b->action.fn();
                    }
                    break;
                case TYPE_CWKSP:
                    change_workspace(b->action.ws);
                    update_net_client_list();
                    break;
                case TYPE_MWKSP:
                    move_to_workspace(b->action.ws);
                    update_net_client_list();
                    break;
            }
            return;
        }
    }
}

void swap_clients(Client *a, Client *b)
{
    if (!a || !b || a == b) {
        return;
    }

    Client **head = &workspaces[current_ws];
    Client **pa = head, **pb = head;

    while (*pa && *pa != a) {
        pa = &(*pa)->next;
    }
    while (*pb && *pb != b) {
        pb = &(*pb)->next;
    }

    if (!*pa || !*pb) {
        return;
    }

    /* if next to it swap */
    if (*pa == b && *pb == a) {
        Client *tmp = b->next;
        b->next = a;
        a->next = tmp;
        *pa = b;
        return;
    }

    /* full swap */
    Client *ta = *pa;
    Client *tb = *pb;
    Client *ta_next = ta->next;
    Client *tb_next = tb->next;

    *pa = tb;
    tb->next = ta_next == tb ? ta : ta_next;

    *pb = ta;
    ta->next = tb_next == ta ? tb : tb_next;
}

void hdl_map_req(XEvent *xev)
{
    Window w = xev->xmaprequest.window;
    XWindowAttributes wa;

    if (!XGetWindowAttributes(dpy, w, &wa)) {
        return;
    }

    if (wa.override_redirect || wa.width <= 0 || wa.height <= 0) {
        XMapWindow(dpy, w);
        return;
    }

    /* check if this window is already managed on any workspace */
    for (int ws = 0; ws < NUM_WORKSPACES; ws++) {
        for (Client *c = workspaces[ws]; c; c = c->next) {
            if (c->win == w) {
                if (ws == current_ws) {
                    if (!c->mapped) {
                        XMapWindow(dpy, w);
                        c->mapped = True;
                    }
                    if (user_config.new_win_focus) {
                        focused = c;
                        XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
                        send_wm_take_focus(c->win);
                        if (user_config.warp_cursor) {
                            warp_cursor(c);
                        }
                    }
                    update_borders();
                }
                return;
            }
        }
    }

    Atom type;
    int format;
    unsigned long nitems, after;
    Atom *types = NULL;
    Bool should_float = False;

    if (XGetWindowProperty(dpy, w, atom_wm_window_type, 0, 8, False, XA_ATOM, &type, &format, &nitems, &after,
                           (unsigned char **)&types) == Success &&
        types) {
        Atom dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
        Atom util = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
        Atom dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
        Atom toolbar = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
        Atom splash = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
        Atom popup = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);

        for (unsigned long i = 0; i < nitems; i++) {
            if (types[i] == dock) {
                XFree(types);
                XMapWindow(dpy, w);
                return;
            }
            if (types[i] == util || types[i] == dialog || types[i] == toolbar || types[i] == splash ||
                types[i] == popup) {
                should_float = True;
                break;
            }
        }
        XFree(types);
    }

    if (!should_float) {
        should_float = window_should_float(w);
    }

    if (open_windows == MAXCLIENTS) {
        fprintf(stderr, "sxwm: max clients reached, ignoring map request\n");
        return;
    }

    Client *c = add_client(w, current_ws);
    if (!c) {
        return;
    }

    Window tr;
    if (!should_float && XGetTransientForHint(dpy, w, &tr)) {
        should_float = True;
    }
    XSizeHints sh;
    long sup;
    if (!should_float && XGetWMNormalHints(dpy, w, &sh, &sup) && (sh.flags & PMinSize) && (sh.flags & PMaxSize) &&
        sh.min_width == sh.max_width && sh.min_height == sh.max_height) {
        should_float = True;
        c->fixed = True;
    }

    // New: Override should_float in MONOCLE mode
    if (layout[c->mon] == MONOCLE) {
        should_float = False;
        c->floating = False;
    } else if (should_float || global_floating) {
        c->floating = True;
    }

    /* center floating windows & set border */
    if (c->floating && !c->fullscreen) {
        int w_ = MAX(c->w, 64), h_ = MAX(c->h, 64);
        int mx = mons[c->mon].x, my = mons[c->mon].y;
        int mw = mons[c->mon].w, mh = mons[c->mon].h;
        int x = mx + (mw - w_) / 2, y = my + (mh - h_) / 2;
        c->x = x;
        c->y = y;
        c->w = w_;
        c->h = h_;
        XMoveResizeWindow(dpy, w, x, y, w_, h_);
        XSetWindowBorderWidth(dpy, w, user_config.border_width);
    }

    /* map & borders */
    update_net_client_list();
    if (!global_floating && !c->floating) {
        tile();
    }
    else if (c->floating) {
        XRaiseWindow(dpy, w);
    }

    XMapWindow(dpy, w);
    c->mapped = True;

    if (user_config.new_win_focus) {
        focused = c;
        XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
        send_wm_take_focus(c->win);
        if (user_config.warp_cursor) {
            warp_cursor(c);
        }
    }
    update_borders();
}

void hdl_motion(XEvent *xev)
{
    XMotionEvent *e = &xev->xmotion;

    if ((drag_mode == DRAG_NONE || !drag_client) ||
        (e->time - last_motion_time <= (1000 / (long unsigned int)user_config.motion_throttle))) {
        return;
    }
    last_motion_time = e->time;

    if (drag_mode == DRAG_SWAP) {
        Window root_ret, child;
        int rx, ry, wx, wy;
        unsigned int mask;
        XQueryPointer(dpy, root, &root_ret, &child, &rx, &ry, &wx, &wy, &mask);

        Client *last_swap_target = NULL;
        Client *new_target = NULL;

        for (Client *c = workspaces[current_ws]; c; c = c->next) {
            if (c == drag_client || c->floating) {
                continue;
            }
            if (c->win == child) {
                new_target = c;
                break;
            }
        }

        if (new_target != last_swap_target) {
            if (last_swap_target) {
                XSetWindowBorder(
                    dpy, last_swap_target->win,
                    (last_swap_target == focused ? user_config.border_foc_col : user_config.border_ufoc_col));
            }
            if (new_target) {
                XSetWindowBorder(dpy, new_target->win, user_config.border_swap_col);
            }
            last_swap_target = new_target;
        }

        swap_target = new_target;
        return;
    }

    else if (drag_mode == DRAG_MOVE) {
        int dx = e->x_root - drag_start_x;
        int dy = e->y_root - drag_start_y;
        int nx = drag_orig_x + dx;
        int ny = drag_orig_y + dy;

        int outer_w = drag_client->w + 2 * user_config.border_width;
        int outer_h = drag_client->h + 2 * user_config.border_width;

        if (UDIST(nx, 0) <= user_config.snap_distance) {
            nx = 0;
        }
        else if (UDIST(nx + outer_w, scr_width) <= user_config.snap_distance) {
            nx = scr_width - outer_w;
        }

        if (UDIST(ny, 0) <= user_config.snap_distance) {
            ny = 0;
        }
        else if (UDIST(ny + outer_h, scr_height) <= user_config.snap_distance) {
            ny = scr_height - outer_h;
        }

        if (!drag_client->floating && (UDIST(nx, drag_client->x) > user_config.snap_distance ||
                                       UDIST(ny, drag_client->y) > user_config.snap_distance)) {
            toggle_floating();
        }

        XMoveWindow(dpy, drag_client->win, nx, ny);
        drag_client->x = nx;
        drag_client->y = ny;
    }

    else if (drag_mode == DRAG_RESIZE) {
        int dx = e->x_root - drag_start_x;
        int dy = e->y_root - drag_start_y;
        int nw = drag_orig_w + dx;
        int nh = drag_orig_h + dy;
        drag_client->w = nw < 20 ? 20 : nw;
        drag_client->h = nh < 20 ? 20 : nh;
        XResizeWindow(dpy, drag_client->win, drag_client->w, drag_client->h);
    }
}

void hdl_root_property(XEvent *xev)
{
    XPropertyEvent *e = &xev->xproperty;
    if (e->atom == atom_net_current_desktop) {
        long *val = NULL;
        Atom actual;
        int fmt;
        unsigned long n, after;
        if (XGetWindowProperty(dpy, root, atom_net_current_desktop, 0, 1, False, XA_CARDINAL, &actual, &fmt, &n, &after,
                               (unsigned char **)&val) == Success &&
            val) {
            change_workspace((int)val[0]);
            XFree(val);
        }
    }
    else if (e->atom == atom_wm_strut_partial) {
        update_struts();
        tile();
        update_borders();
    }
}

void hdl_unmap_ntf(XEvent *xev)
{
    if (!in_ws_switch) {
        Window w = xev->xunmap.window;
        for (Client *c = workspaces[current_ws]; c; c = c->next) {
            if (c->win == w) {
                c->mapped = False;
                break;
            }
        }
    }

    update_net_client_list();
    tile();
    update_borders();
}

void update_struts(void)
{
    reserve_left = reserve_right = reserve_top = reserve_bottom = 0;

    Window root_ret, parent_ret, *children;
    unsigned int nchildren;
    if (!XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &nchildren)) {
        return;
    }

    for (unsigned int i = 0; i < nchildren; i++) {
        Window w = children[i];

        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        Atom *types = NULL;

        if (XGetWindowProperty(dpy, w, atom_wm_window_type, 0, 4, False, XA_ATOM, &actual_type, &actual_format, &nitems,
                               &bytes_after, (unsigned char **)&types) != Success ||
            !types) {
            continue;
        }

        Bool is_dock = False;
        for (unsigned long j = 0; j < nitems; j++) {
            if (types[j] == atom_net_wm_window_type_dock) {
                is_dock = True;
                break;
            }
        }
        XFree(types);
        if (!is_dock) {
            continue;
        }

        long *str = NULL;
        Atom actual;
        int sfmt;
        unsigned long len, rem;
        if (XGetWindowProperty(dpy, w, atom_wm_strut_partial, 0, 12, False, XA_CARDINAL, &actual, &sfmt, &len, &rem,
                               (unsigned char **)&str) == Success &&
            str && len >= 4) {
            reserve_left = MAX(reserve_left, str[0]);
            reserve_right = MAX(reserve_right, str[1]);
            reserve_top = MAX(reserve_top, str[2]);
            reserve_bottom = MAX(reserve_bottom, str[3]);
            XFree(str);
        }
        else if (XGetWindowProperty(dpy, w, atom_wm_strut, 0, 4, False, XA_CARDINAL, &actual, &sfmt, &len, &rem,
                                    (unsigned char **)&str) == Success &&
                 str && len == 4) {
            reserve_left = MAX(reserve_left, str[0]);
            reserve_right = MAX(reserve_right, str[1]);
            reserve_top = MAX(reserve_top, str[2]);
            reserve_bottom = MAX(reserve_bottom, str[3]);
            XFree(str);
        }
    }
    XFree(children);
}

void update_workarea(void)
{
    long workarea[4 * MAX_MONITORS];

    for (int i = 0; i < monsn && i < MAX_MONITORS; i++) {
        workarea[i * 4 + 0] = mons[i].x + reserve_left;
        workarea[i * 4 + 1] = mons[i].y + reserve_top;
        workarea[i * 4 + 2] = mons[i].w - reserve_left - reserve_right;
        workarea[i * 4 + 3] = mons[i].h - reserve_top - reserve_bottom;
    }
    XChangeProperty(dpy, root, atom_net_workarea, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)workarea,
                    monsn * 4);
}

void inc_gaps(void)
{
    user_config.gaps++;
    tile();
    update_borders();
}

void init_defaults(void)
{
    default_config.modkey = Mod4Mask;
    default_config.gaps = 10;
    default_config.border_width = 1;
    default_config.border_foc_col = parse_col("#c0cbff");
    default_config.border_ufoc_col = parse_col("#555555");
    default_config.border_swap_col = parse_col("#fff4c0");
    for (int i = 0; i < MAX_MONITORS; i++) {
        default_config.master_width[i] = 50 / 100.0f;
    }

    default_config.motion_throttle = 60;
    default_config.resize_master_amt = 5;
    default_config.resize_stack_amt = 20;
    default_config.snap_distance = 5;
    default_config.bindsn = 0;
    default_config.new_win_focus = True;
    default_config.warp_cursor = True;

    if (backup_binds) {
