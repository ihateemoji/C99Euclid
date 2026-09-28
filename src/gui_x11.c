/*
 * C99Euclid — X11 GUI (tracks + concentric Euclidean rings)
 *
 * Drawing model
 * -------------
 * Everything is rendered into an offscreen Pixmap (`plug->back`) and
 * copied to the window with a single XCopyArea.  That eliminates the
 * classic full-window-clear flicker at 60 fps.
 *
 * Redraw cadence
 * --------------
 * A CLAP timer (CLAP_EXT_TIMER_SUPPORT) fires every ~16–17 ms (~60 Hz).
 * When the host calls on_timer we paint.  X events (Expose,
 * ConfigureNotify, ButtonPress) are delivered via the X connection fd
 * registered with CLAP_EXT_POSIX_FD_SUPPORT and also trigger a paint
 * when needed.
 *
 * Parent-size sync
 * ----------------
 * On every timer tick we also call eu_sync_size_from_parent().  Some
 * hosts grow their container without sending a ConfigureNotify to the
 * child (typical when dragging the bottom-right corner).  Matching our
 * window to the parent's client size keeps the UI filling the frame
 * instead of leaving a black margin.
 *
 * Layout
 * ------
 *   [ title bar + RATE / SWING / GATE controls ]
 *   [ left panel: 8 track rows (note, steps, pulses, rotate, vel, mute) ]
 *   [ right panel: concentric Euclidean rings with playhead ]
 */

#include "euclid.h"

/* ---- colour helper ----------------------------------------------------- */

static unsigned long eu_col(int r, int g, int b) {
    /* Convert 8-bit RGB components into a 24-bit X11 pixel value.
       Inputs:
         <int> - red component in the range 0..255
         <int> - green component in the range 0..255
         <int> - blue component in the range 0..255
       Returns:
         <unsigned long> - packed colour suitable for
                           XSetForeground / XFillRectangle */
    return ((unsigned long)r << 16) |
           ((unsigned long)g << 8) | (unsigned long)b;
}

/* ---- offscreen destination --------------------------------------------- */

static Drawable eu_dst(eu_plug_t *p) {
    /* Simple fallback to the window if the offscreen pixmap is missing.
       Inputs:
         <*eu_plug_t> - pointer to the plugin instance
       Returns:
         <Drawable> - X11 Drawable to draw into */
    return (p->back != None) ? (Drawable)p->back : (Drawable)p->win;
}

static void eu_ensure_back(eu_plug_t *p) {
    /* Ensure an offscreen pixmap exists and matches the current window size.
       Drawing directly on a window at 60 fps creates nauseating flicker;
       we always paint into the pixmap and blit once.
       Inputs:
         <*eu_plug_t> - pointer to the plugin instance */
    if (!p->dpy || !p->win)
        return;
    /* Already correct size — nothing to do. */
    if (p->back != None && p->back_w == p->gui_w && p->back_h == p->gui_h)
        return;
    /* Size changed (or first allocation): free the old pixmap. */
    if (p->back != None) {
        XFreePixmap(p->dpy, p->back);
        p->back = None;
    }
    if (p->gui_w < 1 || p->gui_h < 1)
        return;
    int depth = DefaultDepth(p->dpy, DefaultScreen(p->dpy));
    p->back = XCreatePixmap(p->dpy, p->win, (unsigned)p->gui_w,
                            (unsigned)p->gui_h, (unsigned)depth);
    p->back_w = p->gui_w;
    p->back_h = p->gui_h;
}

/* ---- primitive drawing (always target eu_dst) -------------------------- */

static void eu_fill(eu_plug_t *p, int x, int y, int w, int h,
                    unsigned long c) {
    /* Draw a solid rectangle.
       Inputs:
         <*eu_plug_t>   - plugin instance
         <int>          - x coordinate of the top-left corner
         <int>          - y coordinate of the top-left corner
         <int>          - width
         <int>          - height
         <unsigned long> - packed colour from eu_col() */
    XSetForeground(p->dpy, p->gc, c);
    XFillRectangle(p->dpy, eu_dst(p), p->gc, x, y, (unsigned)w, (unsigned)h);
}

static void eu_rect(eu_plug_t *p, int x, int y, int w, int h,
                    unsigned long c) {
    /* Draw a one-pixel rectangle outline.
       Inputs:
         <*eu_plug_t>   - plugin instance
         <int>          - x coordinate of the top-left corner
         <int>          - y coordinate of the top-left corner
         <int>          - width
         <int>          - height
         <unsigned long> - packed colour from eu_col() */
    XSetForeground(p->dpy, p->gc, c);
    XDrawRectangle(p->dpy, eu_dst(p), p->gc, x, y, (unsigned)w, (unsigned)h);
}

static void eu_text(eu_plug_t *p, int x, int y, const char *s,
                    unsigned long c) {
    /* Draw a null-terminated string at the given coordinates.
       Inputs:
         <*eu_plug_t>   - plugin instance
         <int>          - x coordinate of the baseline origin
         <int>          - y coordinate of the baseline origin
         <const char *> - null-terminated string to draw
         <unsigned long> - packed colour from eu_col() */
    XSetForeground(p->dpy, p->gc, c);
    XDrawString(p->dpy, eu_dst(p), p->gc, x, y, s, (int)strlen(s));
}

/* ---- pattern rebuild on GUI edits ------------------------------------ */

static void eu_gui_apply_dirty(eu_plug_t *plug) {
    /* Clamp state, rebuild Euclidean patterns, and mark the host state
       dirty so the change is visible immediately and will be saved.
       Called after every interactive parameter change so the rings and
       hit dots update on the next paint without waiting for the audio
       thread.
       Inputs:
         <*eu_plug_t> - plugin instance */
    eu_clamp(&plug->st);
    euclid_rebuild(&plug->st, plug->pat);
    plug->dirty = 1;   /* keep set so process()/flush also see it */
    if (plug->host_state && plug->host_state->mark_dirty)
        plug->host_state->mark_dirty(plug->host);
}

/* ---- full-frame paint -------------------------------------------------- */

static void eu_gui_paint(eu_plug_t *plug) {
    /* Draw one complete frame of the UI into the offscreen pixmap,
       then blit it to the window in a single XCopyArea.
       Inputs:
         <*eu_plug_t> - plugin instance */
    if (!plug->dpy || !plug->win) return;
    eu_ensure_back(plug);

    int W = plug->gui_w;
    int H = plug->gui_h;
    /* colours from #defines in euclid.h — edit there for theming */
    unsigned long bg      = eu_col(EU_BG_R, EU_BG_G, EU_BG_B);
    unsigned long surf    = eu_col(EU_SURF_R, EU_SURF_G, EU_SURF_B);
    unsigned long fg      = eu_col(EU_FG_R, EU_FG_G, EU_FG_B);
    unsigned long mut     = eu_col(EU_MUT_R, EU_MUT_G, EU_MUT_B);
    unsigned long acc     = eu_col(EU_ACC_R, EU_ACC_G, EU_ACC_B);
    unsigned long grid    = eu_col(EU_GRID_R, EU_GRID_G, EU_GRID_B);
    unsigned long cyan    = eu_col(EU_CYA_R, EU_CYA_G, EU_CYA_B);
    unsigned long green   = eu_col(EU_GRN_R, EU_GRN_G, EU_GRN_B);
    unsigned long note_bg = eu_col(EU_NOTE_BG_R, EU_NOTE_BG_G, EU_NOTE_BG_B);

    /* Background */
    eu_fill(plug, 0, 0, W, H, bg);
    eu_text(plug, 24, 28, "C99Euclid", fg);

    char buf[64];
    int top_y = 52;

    /* Top bar: RATE / SWING / GATE */
    snprintf(buf, sizeof(buf), "RATE %s", eu_rate_name(plug->st.rate));
    eu_fill(plug, 24, top_y, 118, 26, surf);
    eu_rect(plug, 24, top_y, 118, 26, acc);
    eu_text(plug, 32, top_y + 18, buf, fg);

    snprintf(buf, sizeof(buf), "SWING %u%%", plug->st.swing);
    eu_fill(plug, 152, top_y, 100, 26, surf);
    eu_rect(plug, 152, top_y, 100, 26, acc);
    eu_text(plug, 160, top_y + 18, buf, fg);

    snprintf(buf, sizeof(buf), "GATE %u%%", plug->st.gate);
    eu_fill(plug, 262, top_y, 92, 26, surf);
    eu_rect(plug, 262, top_y, 92, 26, acc);
    eu_text(plug, 270, top_y + 18, buf, fg);

    /* Left panel — tracks */
    int lx = 24;
    int ly = 96;
    eu_fill(plug, lx, ly, 320, 540, surf);
    eu_rect(plug, lx, ly, 320, 540, acc);
    eu_text(plug, lx + 12, ly + 22, "TRACKS", fg);

    int py = ly + 38;
    int cell_h = 20;
    int cell_w = 48;

    for (int t = 0; t < EU_TRACKS; t++) {
        const eu_track_t *tr = &plug->st.tr[t];
        unsigned long tc = tr->mute ? mut : cyan;
        int row_y = py + t * 62;

        /* track row background */
        eu_fill(plug, lx + 8, row_y, 304, 56, grid);
        eu_rect(plug, lx + 8, row_y, 304, 56, tc);

        /* track label */
        snprintf(buf, sizeof(buf), "T%d", t + 1);
        eu_text(plug, lx + 16, row_y + 16, buf, tc);

        /* NOTE box */
        snprintf(buf, sizeof(buf), "%s", eu_note_name(tr->note, buf + 20, 8));
        eu_fill(plug, lx + 52, row_y + 6, 56, 20, note_bg);
        eu_rect(plug, lx + 52, row_y + 6, 56, 20, acc);
        eu_text(plug, lx + 58, row_y + 20, buf, fg);

        /* param cells: ST / PU / RO / V */
        int cx0 = lx + 100;
        int cy0 = row_y + 30;

        snprintf(buf, sizeof(buf), "ST %d", tr->steps);
        eu_fill(plug, cx0, cy0, cell_w, cell_h, grid);
        eu_text(plug, cx0 + 4, cy0 + 14, buf, fg);

        snprintf(buf, sizeof(buf), "PU %d", tr->pulses);
        eu_fill(plug, cx0 + cell_w + 6, cy0, cell_w, cell_h, grid);
        eu_text(plug, cx0 + cell_w + 10, cy0 + 14, buf, fg);

        snprintf(buf, sizeof(buf), "RO %d", tr->rotate);
        eu_fill(plug, cx0 + 2 * (cell_w + 6), cy0, cell_w, cell_h, grid);
        eu_text(plug, cx0 + 2 * (cell_w + 6) + 4, cy0 + 14, buf, fg);

        snprintf(buf, sizeof(buf), "V %d", tr->vel);
        eu_fill(plug, cx0 + 3 * (cell_w + 6), cy0, cell_w, cell_h, grid);
        eu_text(plug, cx0 + 3 * (cell_w + 6) + 6, cy0 + 14, buf, fg);
    }

    /* Right panel — concentric Euclidean rings */
    int vx = 360;
    int vy = 96;
    int vw = W - vx - 24;
    int vh = 540;
    eu_fill(plug, vx, vy, vw, vh, surf);
    eu_text(plug, vx + 12, vy + 22, "ACTIVATED CIRCLES", cyan);

    int cx = vx + vw / 2;
    int cy = vy + vh / 2 + 10;
    int base_rad = 42;
    int rad_step = 27;

    int64_t gstep = plug->last_gstep;

    for (int t = 0; t < EU_TRACKS; t++) {
        const eu_track_t *tr = &plug->st.tr[t];
        if (tr->mute) continue;   /* hide muted rings completely */

        int rad = base_rad + t * rad_step;
        unsigned long tc = cyan;

        XSetForeground(plug->dpy, plug->gc, tc);
        XDrawArc(plug->dpy, eu_dst(plug), plug->gc,
                 cx - rad, cy - rad, rad * 2, rad * 2, 0, 360 * 64);

        int cur_step = (gstep >= 0) ? (int)(gstep % tr->steps) : -1;

        for (int s = 0; s < tr->steps; s++) {
            double ang = (2.0 * M_PI * s / tr->steps) - M_PI / 2.0;
            int px = (int)lround(cx + (rad - 1) * cos(ang));
            int py = (int)lround(cy + (rad - 1) * sin(ang));
            int hit = plug->pat[t][s];
            int is_playhead = (s == cur_step);
            int is_active_now = is_playhead && hit;

            if (is_active_now) {
                /* currently firing step — bright green */
                XSetForeground(plug->dpy, plug->gc, green);
                XFillArc(plug->dpy, eu_dst(plug), plug->gc,
                         px - 9, py - 9, 18, 18, 0, 360 * 64);
                XSetForeground(plug->dpy, plug->gc, fg);
                XDrawArc(plug->dpy, eu_dst(plug), plug->gc,
                         px - 11, py - 11, 22, 22, 0, 360 * 64);
            } else if (hit) {
                XSetForeground(plug->dpy, plug->gc, acc);
                XFillArc(plug->dpy, eu_dst(plug), plug->gc,
                         px - 6, py - 6, 12, 12, 0, 360 * 64);
            } else {
                XSetForeground(plug->dpy, plug->gc, mut);
                XFillArc(plug->dpy, eu_dst(plug), plug->gc,
                         px - 4, py - 4, 8, 8, 0, 360 * 64);
            }
            if (is_playhead) {
                XSetForeground(plug->dpy, plug->gc, fg);
                XDrawArc(plug->dpy, eu_dst(plug), plug->gc,
                         px - 8, py - 8, 16, 16, 0, 360 * 64);
            }
        }
    }

    eu_text(plug, 24, H - 18,
            "Click cells or wheel. Click note box to cycle MIDI note.", mut);

    /* Single blit of the completed frame — no flicker. */
    if (plug->back != None) {
        XCopyArea(plug->dpy, plug->back, plug->win, plug->gc,
                  0, 0, (unsigned)W, (unsigned)H, 0, 0);
    }
    XFlush(plug->dpy);
}

void eu_gui_redraw(eu_plug_t *plug) {
    /* Public entry point used by the DSP side when a parameter changes
       and the GUI should refresh immediately.
       Inputs:
         <*eu_plug_t> - plugin instance */
    if (plug && plug->gui_visible)
        eu_gui_paint(plug);
}

/* ---- interaction ------------------------------------------------------- */

static void eu_gui_click(eu_plug_t *plug, int x, int y) {
    /* Handle left-button click on a control.
       Inputs:
         <*eu_plug_t> - plugin instance
         <int>        - x window coordinate of the click
         <int>        - y window coordinate of the click */
    int top_y = 52;

    /* top bar clicks */
    if (y >= top_y && y <= top_y + 26) {
        if (x >= 24 && x < 142) {
            plug->st.rate = (plug->st.rate + 1) % EU_RATE_COUNT;
        } else if (x >= 152 && x < 252) {
            plug->st.swing = (plug->st.swing + 5) % 105;
        } else if (x >= 262 && x < 354) {
            plug->st.gate = (plug->st.gate + 5) % 105;
        } else {
            return;
        }
        eu_gui_apply_dirty(plug);
        eu_gui_paint(plug);
        return;
    }

    /* track rows */
    int lx = 24, ly = 96;
    int cell_h = 20, cell_w = 48;
    int py = ly + 48;

    for (int t = 0; t < EU_TRACKS; t++) {
        int row_y = py + t * 62;
        eu_track_t *tr = &plug->st.tr[t];

        /* NOTE box click */
        if (y >= row_y + 6 && y < row_y + 26 &&
            x >= lx + 52 && x < lx + 108) {
            tr->note = (tr->note + 1) % 128;
            eu_gui_apply_dirty(plug);
            eu_gui_paint(plug);
            return;
        }

        /* param cells */
        int cy0 = row_y + 30;
        int cx0 = lx + 100;

        if (y >= cy0 && y < cy0 + cell_h) {
            if (x >= cx0 && x < cx0 + 14) {
                tr->steps = tr->steps > 1 ? tr->steps - 1 : 32;
            } else if (x >= cx0 + 14 && x < cx0 + cell_w) {
                tr->steps = ((tr->steps + 1) % 32) + 1;
            } else if (x >= cx0 + cell_w + 6 && x < cx0 + cell_w + 20) {
                tr->pulses = tr->pulses > 0 ? tr->pulses - 1 : tr->steps;
            } else if (x >= cx0 + cell_w + 20 && x < cx0 + 2 * cell_w + 6) {
                tr->pulses = (tr->pulses + 1) % (tr->steps + 1);
            } else if (x >= cx0 + 2 * (cell_w + 6) &&
                       x < cx0 + 2 * (cell_w + 6) + 14) {
                tr->rotate = tr->rotate > 0 ? tr->rotate - 1 : tr->steps - 1;
            } else if (x >= cx0 + 2 * (cell_w + 6) + 14 &&
                       x < cx0 + 3 * cell_w + 6) {
                tr->rotate = (tr->rotate + 1) % tr->steps;
            } else if (x >= cx0 + 3 * (cell_w + 6) &&
                       x < cx0 + 3 * (cell_w + 6) + 14) {
                tr->vel = tr->vel > 1 ? tr->vel - 10 : 127;
            } else if (x >= cx0 + 3 * (cell_w + 6) + 14 &&
                       x < cx0 + 4 * cell_w + 6) {
                tr->vel = ((tr->vel + 10) % 127) + 1;
            }
            eu_gui_apply_dirty(plug);
            eu_gui_paint(plug);
            return;
        }

        /* mute toggle on label area — enforce chronological order */
        if (y >= row_y && y < row_y + 26 &&
            x >= lx + 8 && x < lx + 52) {
            if (tr->mute) {
                /* currently muted allow unmuting only if previous is active */
                if (t == 0 || plug->st.tr[t - 1].mute == 0) {
                    tr->mute = 0;
                }
            } else {
                /* currently active → mute and force all later tracks off */
                tr->mute = 1;
                for (int k = t + 1; k < EU_TRACKS; k++) {
                    plug->st.tr[k].mute = 1;
                }
            }
            eu_gui_apply_dirty(plug);
            eu_gui_paint(plug);
            return;
        }
    }
}

static void eu_gui_wheel(eu_plug_t *plug, int x, int y, int dir) {
    /* Handle mouse-wheel (button 4/5) over a control.
       Inputs:
         <*eu_plug_t> - plugin instance
         <int>        - x window coordinate
         <int>        - y window coordinate
         <int>        - direction (+1 up, -1 down) */
    int top_y = 52;
    /* top bar wheel */
    if (y >= top_y && y <= top_y + 26) {
        if (x >= 24 && x < 142) {
            int r = (int)plug->st.rate + dir;
            if (r < 0) r = EU_RATE_COUNT - 1;
            if (r >= EU_RATE_COUNT) r = 0;
            plug->st.rate = (uint32_t)r;
        } else if (x >= 152 && x < 252) {
            int s = (int)plug->st.swing + dir * 5;
            if (s < 0) s = 0;
            if (s > 100) s = 100;
            plug->st.swing = (uint32_t)s;
        } else if (x >= 262 && x < 354) {
            int g = (int)plug->st.gate + dir * 5;
            if (g < 5) g = 5;
            if (g > 100) g = 100;
            plug->st.gate = (uint32_t)g;
        }
        eu_gui_apply_dirty(plug);
        eu_gui_paint(plug);
        return;
    }

    int lx = 24, ly = 96;
    int cell_h = 20, cell_w = 48;
    int py = ly + 48;

    for (int t = 0; t < EU_TRACKS; t++) {
        int row_y = py + t * 62;
        eu_track_t *tr = &plug->st.tr[t];

        /* NOTE wheel */
        if (y >= row_y + 6 && y < row_y + 26 &&
            x >= lx + 52 && x < lx + 108) {
            int n = (int)tr->note + dir;
            if (n < 0) n = 0;
            if (n > 127) n = 127;
            tr->note = (uint8_t)n;
            eu_gui_apply_dirty(plug);
            eu_gui_paint(plug);
            return;
        }

        int cy0 = row_y + 30;
        int cx0 = lx + 100;

        if (y >= cy0 && y < cy0 + cell_h) {
            int v;
            if (x >= cx0 && x < cx0 + cell_w) {
                v = (int)tr->steps + dir;
                if (v < 1) v = 1;
                if (v > 32) v = 32;
                tr->steps = (uint8_t)v;
            } else if (x >= cx0 + cell_w + 6 && x < cx0 + 2 * cell_w + 6) {
                v = (int)tr->pulses + dir;
                if (v < 0) v = 0;
                if (v > tr->steps) v = tr->steps;
                tr->pulses = (uint8_t)v;
            } else if (x >= cx0 + 2 * (cell_w + 6) &&
                       x < cx0 + 3 * cell_w + 6) {
                v = (int)tr->rotate + dir;
                if (v < 0) v = 0;
                if (v >= tr->steps) v = tr->steps - 1;
                tr->rotate = (uint8_t)v;
            } else if (x >= cx0 + 3 * (cell_w + 6) &&
                       x < cx0 + 4 * cell_w + 6) {
                v = (int)tr->vel + dir * 5;
                if (v < 1) v = 1;
                if (v > 127) v = 127;
                tr->vel = (uint8_t)v;
            }
            eu_gui_apply_dirty(plug);
            eu_gui_paint(plug);
            return;
        }
    }
}

/* ---- CLAP GUI + POSIX FD + Timer implementation ------------------------ */

static bool eu_gui_is_api_supported(const clap_plugin_t *p,
                                    const char *api, bool f) {
    /* Report whether the requested windowing API is supported.
       Only X11 is implemented.
       Inputs:
         <*clap_plugin_t> - plugin instance
         <const char *>   - requested windowing API string
         <bool>           - whether a floating window is requested
       Returns:
         <bool> - true if the API is supported */
    (void)p; (void)f;
    return api && strcmp(api, CLAP_WINDOW_API_X11) == 0;
}

static bool eu_gui_get_preferred_api(const clap_plugin_t *p,
                                     const char **api, bool *f) {
    /* Prefer embedded X11.
       Inputs:
         <*clap_plugin_t> - plugin instance
         <const char **>  - receives the preferred API string
         <bool *>         - receives whether floating is preferred
       Returns:
         <bool> - true */
    (void)p;
    *api = CLAP_WINDOW_API_X11;
    *f = false;
    return true;
}

static bool eu_gui_create(const clap_plugin_t *plugin,
                          const char *api, bool f) {
    /* CLAP GUI extension – create the X11 window and associated
       resources.  Opens the default X display, creates a simple
       window of the default size, obtains a graphics context,
       selects the required event masks and registers the X11
       file descriptor with the host so that events can be
       processed from the main thread.  A ~60 Hz CLAP timer is
       also registered for continuous playhead animation.
       Inputs:
         <*clap_plugin_t> - plugin instance
         <const char *>   - requested windowing API (must be X11)
         <bool>           - ignored (always embedded)
       Returns:
         <bool> - true on success, false if the display cannot
                  be opened or the API is not X11 */
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    (void)f;
    if (api && strcmp(api, CLAP_WINDOW_API_X11) != 0) return false;
    plug->dpy = XOpenDisplay(NULL);
    if (!plug->dpy) return false;
    int screen = DefaultScreen(plug->dpy);
    Window root = RootWindow(plug->dpy, screen);
    plug->gui_w = EU_GUI_W;
    plug->gui_h = EU_GUI_H;
    plug->back = None;
    plug->back_w = plug->back_h = 0;
    plug->win = XCreateSimpleWindow(plug->dpy, root, 0, 0,
                            (unsigned)plug->gui_w, (unsigned)plug->gui_h, 0,
                                    eu_col(EU_BG_R, EU_BG_G, EU_BG_B),
                                    eu_col(EU_BG_R, EU_BG_G, EU_BG_B));
    /* Ask the X server to keep a backing store when the window is mapped. */
    {
        XSetWindowAttributes wa;
        wa.backing_store = WhenMapped;
        XChangeWindowAttributes(plug->dpy, plug->win, CWBackingStore, &wa);
    }
    plug->gc = XCreateGC(plug->dpy, plug->win, 0, NULL);
    XSelectInput(plug->dpy, plug->win,
                 ExposureMask | ButtonPressMask | StructureNotifyMask);
    plug->xfd = ConnectionNumber(plug->dpy);
    if (plug->host_fd && plug->host_fd->register_fd)
        plug->host_fd->register_fd(plug->host, plug->xfd, CLAP_POSIX_FD_READ);

    /*
     * ~60 Hz CLAP timer.  The host calls our on_timer callback, which
     * is the primary place we continuous-redraw from.  period_ms = 16
     * is accepted by hosts that allow ≥30 Hz; many will round to 16–17.
     */
    plug->timer_id = CLAP_INVALID_ID;
    if (plug->host_timer && plug->host_timer->register_timer) {
        clap_id id = CLAP_INVALID_ID;
        if (plug->host_timer->register_timer(plug->host, 16, &id)) {
            plug->timer_id = id;
        }
    }

    eu_ensure_back(plug);
    plug->gui_created = 1;
    plug->gui_visible = 0;
    return true;
}

static void eu_gui_destroy(const clap_plugin_t *plugin) {
    /* CLAP GUI extension – destroy the X11 window and free all
       resources.  Unregisters the CLAP timer and X11 fd, frees
       the GC / pixmap, destroys the window and closes the
       display.  All fields are reset.
       Inputs:
         <*clap_plugin_t> - plugin instance */
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (plug->host_timer && plug->host_timer->unregister_timer &&
        plug->timer_id != CLAP_INVALID_ID) {
        plug->host_timer->unregister_timer(plug->host, plug->timer_id);
        plug->timer_id = CLAP_INVALID_ID;
    }
    if (plug->host_fd && plug->host_fd->unregister_fd) {
        if (plug->xfd >= 0) {
            plug->host_fd->unregister_fd(plug->host, plug->xfd);
        }
    }
    if (plug->back != None && plug->dpy) {
        XFreePixmap(plug->dpy, plug->back);
        plug->back = None;
    }
    if (plug->gc && plug->dpy) {
        XFreeGC(plug->dpy, plug->gc);
        plug->gc = NULL;
    }
    if (plug->win && plug->dpy) {
        XDestroyWindow(plug->dpy, plug->win);
        plug->win = 0;
    }
    if (plug->dpy) {
        XCloseDisplay(plug->dpy);
        plug->dpy = NULL;
    }
    plug->gui_created = 0;
    plug->gui_visible = 0;
    plug->xfd = -1;
}

static bool eu_gui_set_scale(const clap_plugin_t *p, double s) {
    /* Scale factor is currently ignored; always succeed.
       Inputs:
         <*clap_plugin_t> - plugin instance
         <double>         - requested scale factor
       Returns:
         <bool> - true */
    (void)p; (void)s;
    return true;
}

static bool eu_gui_get_size(const clap_plugin_t *plugin,
                            uint32_t *w, uint32_t *h) {
    /* Report the current window size.
       Inputs:
         <*clap_plugin_t> - plugin instance
         <uint32_t *>     - receives width
         <uint32_t *>     - receives height
       Returns:
         <bool> - true */
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    *w = (uint32_t)plug->gui_w;
    *h = (uint32_t)plug->gui_h;
    return true;
}

static bool eu_gui_can_resize(const clap_plugin_t *p) {
    /* Report whether the GUI can be resized by the host.
       Inputs:
         <*clap_plugin_t> - plugin instance
       Returns:
         <bool> - true */
    (void)p;
    return true;
}

static bool eu_gui_get_resize_hints(const clap_plugin_t *p,
                                    clap_gui_resize_hints_t *h) {
    /* Fill in preferred resize behaviour.
       Inputs:
         <*clap_plugin_t>         - plugin instance
         <*clap_gui_resize_hints_t> - structure to fill
       Returns:
         <bool> - true */
    (void)p;
    h->can_resize_horizontally = true;
    h->can_resize_vertically = true;
    h->preserve_aspect_ratio = true;
    h->aspect_ratio_width = 820;
    h->aspect_ratio_height = 560;
    return true;
}

static bool eu_gui_adjust_size(const clap_plugin_t *p,
                               uint32_t *w, uint32_t *h) {
    /* Clamp a proposed size to the minimum dimensions.
       Inputs:
         <*clap_plugin_t> - plugin instance
         <uint32_t *>     - width (clamped in place)
         <uint32_t *>     - height (clamped in place)
       Returns:
         <bool> - true */
    (void)p;
    if (*w < 560) *w = 560;
    if (*h < 360) *h = 360;
    return true;
}

static bool eu_gui_set_size(const clap_plugin_t *plugin,
                            uint32_t w, uint32_t h) {
    /* Apply a new window size, recreate the offscreen pixmap if
       needed, and force a full repaint.
       Inputs:
         <*clap_plugin_t> - plugin instance
         <uint32_t>       - new width
         <uint32_t>       - new height
       Returns:
         <bool> - true */
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (w < 560) w = 560;
    if (h < 360) h = 360;
    plug->gui_w = (int)w;
    plug->gui_h = (int)h;
    if (plug->dpy && plug->win) {
        XResizeWindow(plug->dpy, plug->win, w, h);
        eu_ensure_back(plug);
        eu_gui_paint(plug);
    }
    return true;
}

static bool eu_gui_set_parent(const clap_plugin_t *plugin,
                              const clap_window_t *window) {
    /* Embed the plugin window into a host parent.
       Inputs:
         <*clap_plugin_t>  - plugin instance
         <*clap_window_t>  - host parent window
       Returns:
         <bool> - true on success */
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (!window || !plug->dpy ||
            strcmp(window->api, CLAP_WINDOW_API_X11) != 0)
        return false;
    XReparentWindow(plug->dpy, plug->win, (Window)window->x11, 0, 0);
    XMapWindow(plug->dpy, plug->win);
    XFlush(plug->dpy);
    return true;
}

static bool eu_gui_set_transient(const clap_plugin_t *p,
                                 const clap_window_t *w) {
    /* Set a transient-for parent (unused).
       Inputs:
         <*clap_plugin_t> - plugin instance
         <*clap_window_t> - transient parent
       Returns:
         <bool> - true */
    (void)p; (void)w;
    return true;
}

static void eu_gui_suggest_title(const clap_plugin_t *plugin,
                                 const char *title) {
    /* Suggest a window title to the X server.
       Inputs:
         <*clap_plugin_t> - plugin instance
         <const char *>   - suggested title string */
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (plug->dpy && plug->win && title)
        XStoreName(plug->dpy, plug->win, title);
}

static bool eu_gui_show(const clap_plugin_t *plugin) {
    /* Make the plugin window visible and paint an initial frame.
       Inputs:
         <*clap_plugin_t> - plugin instance
       Returns:
         <bool> - true on success */
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (!plug->dpy || !plug->win) return false;
    XMapWindow(plug->dpy, plug->win);
    plug->gui_visible = 1;
    eu_gui_paint(plug);
    return true;
}

static bool eu_gui_hide(const clap_plugin_t *plugin) {
    /* Hide the plugin window.
       Inputs:
         <*clap_plugin_t> - plugin instance
       Returns:
         <bool> - true */
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (plug->dpy && plug->win) XUnmapWindow(plug->dpy, plug->win);
    plug->gui_visible = 0;
    return true;
}

static void eu_sync_size_from_parent(eu_plug_t *plug) {
    /* If the host grows its container without resizing our child window
       (common when dragging the bottom-right corner), match our window to
       the parent's client size so the UI fills the frame instead of leaving
       a black margin.
       Inputs:
         <*eu_plug_t> - plugin instance */
    if (!plug->dpy || !plug->win) return;

    Window root = 0, parent = 0, *kids = NULL;
    unsigned int nkids = 0;
    if (!XQueryTree(plug->dpy, plug->win, &root, &parent, &kids, &nkids))
        return;
    if (kids)
        XFree(kids);

    /* Still under root — not reparented, nothing to sync. */
    if (!parent || parent == root)
        return;

    Window r;
    int x, y;
    unsigned int pw, ph, bw, depth;
    if (!XGetGeometry(plug->dpy, parent, &r, &x, &y, &pw, &ph, &bw, &depth))
        return;

    int nw = (int)pw;
    int nh = (int)ph;
    if (nw < 560) nw = 560;
    if (nh < 360) nh = 360;

    /* Already matching — avoid a redundant XResizeWindow every frame. */
    if (nw == plug->gui_w && nh == plug->gui_h)
        return;

    plug->gui_w = nw;
    plug->gui_h = nh;
    XResizeWindow(plug->dpy, plug->win, (unsigned)nw, (unsigned)nh);
    /* Pixmap will be recreated on the next paint via eu_ensure_back(). */
}

static void eu_gui_on_fd(const clap_plugin_t *plugin, int fd,
                         clap_posix_fd_flags_t flags) {
    /* CLAP POSIX FD support – process pending X11 events on the
       connection fd.  Timer-driven redraw is handled separately by
       eu_gui_on_timer (CLAP_EXT_TIMER_SUPPORT).  Drains the X event
       queue.  Expose / ConfigureNotify trigger a full repaint;
       ButtonPress is routed to the interaction handlers.
       Inputs:
         <*clap_plugin_t>        - plugin instance
         <int>                   - file descriptor that became readable
         <clap_posix_fd_flags_t> - event flags */
    (void)flags;
    (void)fd;
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (!plug->dpy) return;

    XEvent ev;
    bool need_redraw = false;
    while (XPending(plug->dpy)) {
        XNextEvent(plug->dpy, &ev);
        if (ev.type == Expose) {
            need_redraw = true;
        } else if (ev.type == ButtonPress) {
            if (ev.xbutton.button == 4 || ev.xbutton.button == 5)
                eu_gui_wheel(plug, ev.xbutton.x, ev.xbutton.y,
                             ev.xbutton.button == 4 ? 1 : -1);
            else if (ev.xbutton.button == 1)
                eu_gui_click(plug, ev.xbutton.x, ev.xbutton.y);
        } else if (ev.type == ConfigureNotify) {
            int nw = ev.xconfigure.width;
            int nh = ev.xconfigure.height;
            if (nw < 560) nw = 560;
            if (nh < 360) nh = 360;
            if (nw != plug->gui_w || nh != plug->gui_h) {
                plug->gui_w = nw;
                plug->gui_h = nh;
            }
            need_redraw = true;
        }
    }
    if (plug->dirty) {
        /* rebuild already done by eu_gui_apply_dirty on edit; just paint */
        need_redraw = true;
    }
    if (need_redraw)
        eu_gui_paint(plug);
}

static void eu_gui_on_timer(const clap_plugin_t *plugin, clap_id timer_id) {
    /* CLAP timer-support – periodic redraw callback (~60 Hz).
       Syncs window size from the host parent (if embedded) and
       paints when the GUI is visible so the playhead animates
       smoothly.
       Inputs:
         <*clap_plugin_t> - plugin instance
         <clap_id>        - id of the timer that fired */
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (timer_id != plug->timer_id)
        return;
    if (plug->gui_visible && plug->dpy) {
        eu_sync_size_from_parent(plug);
        eu_gui_paint(plug);
    }
}

/* Static table of CLAP GUI extension entry points. */
const clap_plugin_gui_t eu_gui_ext = {
    .is_api_supported  = eu_gui_is_api_supported,
    .get_preferred_api = eu_gui_get_preferred_api,
    .create            = eu_gui_create,
    .destroy           = eu_gui_destroy,
    .set_scale         = eu_gui_set_scale,
    .get_size          = eu_gui_get_size,
    .can_resize        = eu_gui_can_resize,
    .get_resize_hints  = eu_gui_get_resize_hints,
    .adjust_size       = eu_gui_adjust_size,
    .set_size          = eu_gui_set_size,
    .set_parent        = eu_gui_set_parent,
    .set_transient     = eu_gui_set_transient,
    .suggest_title     = eu_gui_suggest_title,
    .show              = eu_gui_show,
    .hide              = eu_gui_hide
};

/* Static table of CLAP POSIX file-descriptor support. */
const clap_plugin_posix_fd_support_t eu_posix_fd_ext = {
    .on_fd = eu_gui_on_fd
};

/* Static table of CLAP timer-support.
   Host-driven periodic timer used for continuous ~60 Hz GUI refresh. */
const clap_plugin_timer_support_t eu_timer_ext = {
    .on_timer = eu_gui_on_timer
};
