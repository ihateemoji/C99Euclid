#define _GNU_SOURCE
#include "euclid.h"

/* define the dimensionality of the GUI */
#define EU_GUI_W 1180
#define EU_GUI_H 680

static unsigned long eu_col(int r, int g, int b) {
    return ((unsigned long)r << 16) |
           ((unsigned long)g << 8) | (unsigned long)b;
}

static void eu_fill(eu_plug_t *p, int x, int y, int w, int h,
                    unsigned long c) {
    XSetForeground(p->dpy, p->gc, c);
    XFillRectangle(p->dpy, p->win, p->gc, x, y, (unsigned)w, (unsigned)h);
}

static void eu_rect(eu_plug_t *p, int x, int y, int w, int h,
                    unsigned long c) {
    XSetForeground(p->dpy, p->gc, c);
    XDrawRectangle(p->dpy, p->win, p->gc, x, y, (unsigned)w, (unsigned)h);
}

static void eu_text(eu_plug_t *p, int x, int y, const char *s,
                    unsigned long c) {
    XSetForeground(p->dpy, p->gc, c);
    XDrawString(p->dpy, p->win, p->gc, x, y, s, (int)strlen(s));
}

static void eu_gui_paint(eu_plug_t *plug) {
    if (!plug->dpy || !plug->win) return;
    int W = plug->gui_w;
    int H = plug->gui_h;
    unsigned long bg = eu_col(24, 26, 30);
    unsigned long surf = eu_col(38, 41, 48);
    unsigned long fg = eu_col(240, 242, 248);
    unsigned long mut = eu_col(95, 100, 110);
    unsigned long acc = eu_col(85, 145, 235);
    unsigned long grid = eu_col(52, 56, 64);
    unsigned long cyan = eu_col(75, 195, 225);
    unsigned long green = eu_col(95, 205, 145);
    unsigned long note_bg = eu_col(48, 52, 60);

    eu_fill(plug, 0, 0, W, H, bg);
    eu_text(plug, 24, 28, "EUCLID RPE", fg);
    eu_text(plug, W - 58, 28, "CLAP", mut);

    char buf[64];
    int top_y = 52;

    /* Top bar */
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

    /* Left panel - tracks */
    int lx = 24;
    int ly = 96;
    eu_fill(plug, lx, ly, 320, 540, surf);
    eu_rect(plug, lx, ly, 320, 540, acc);
    eu_text(plug, lx + 12, ly + 22, "TRACKS", fg);

    int py = ly + 48;
    int cell_h = 20;
    int cell_w = 48;

    for (int t = 0; t < EU_TRACKS; t++) {
        const eu_track_t *tr = &plug->st.tr[t];
        unsigned long tc = tr->mute ? mut : cyan;
        int row_y = py + t * 62;

        /* track row background */
        eu_fill(plug, lx + 8, row_y, 304, 56, grid);
        eu_rect(plug, lx + 8, row_y, 304, 56, tc);

        /* track label + note */
        snprintf(buf, sizeof(buf), "T%d", t+1);
        eu_text(plug, lx + 16, row_y + 16, buf, tc);

        /* NOTE box */
        snprintf(buf, sizeof(buf), "%s", eu_note_name(tr->note, buf+20, 8));
        eu_fill(plug, lx + 52, row_y + 6, 56, 20, note_bg);
        eu_rect(plug, lx + 52, row_y + 6, 56, 20, acc);
        eu_text(plug, lx + 58, row_y + 20, buf, fg);

        /* param cells */
        int cx0 = lx + 100;
        int cy0 = row_y + 30;

        snprintf(buf, sizeof(buf), "ST %d", tr->steps);
        eu_fill(plug, cx0, cy0, cell_w, cell_h, grid);
        eu_text(plug, cx0 + 4, cy0 + 14, buf, fg);

        snprintf(buf, sizeof(buf), "PU %d", tr->pulses);
        eu_fill(plug, cx0 + cell_w + 6, cy0, cell_w, cell_h, grid);
        eu_text(plug, cx0 + cell_w + 10, cy0 + 14, buf, fg);

        snprintf(buf, sizeof(buf), "RO %d", tr->rotate);
        eu_fill(plug, cx0 + 2*(cell_w + 6), cy0, cell_w, cell_h, grid);
        eu_text(plug, cx0 + 2*(cell_w + 6) + 4, cy0 + 14, buf, fg);

        snprintf(buf, sizeof(buf), "V %d", tr->vel);
        eu_fill(plug, cx0 + 3*(cell_w + 6), cy0, cell_w, cell_h, grid);
        eu_text(plug, cx0 + 3*(cell_w + 6) + 6, cy0 + 14, buf, fg);
    }

    /* Right side: concentric rings - bigger area */
    int vx = 360;
    int vy = 96;
    int vw = W - vx - 24;
    int vh = 540;
    eu_fill(plug, vx, vy, vw, vh, surf);
    eu_text(plug, vx + 12, vy + 22, "CONCENTRIC EUCLIDEAN  -  PLAYHEAD", cyan);

    int cx = vx + vw / 2;
    int cy = vy + vh / 2 + 10;
    int base_rad = 42;
    int rad_step = 34;

    int64_t gstep = plug->last_gstep;

    for (int t = 0; t < EU_TRACKS; t++) {
        const eu_track_t *tr = &plug->st.tr[t];
        if (tr->mute) continue;   /* hide disabled rings completely */

        int rad = base_rad + t * rad_step;
        unsigned long tc = cyan;

        XSetForeground(plug->dpy, plug->gc, tc);
        XDrawArc(plug->dpy, plug->win, plug->gc, cx - rad, cy - rad,
                 rad * 2, rad * 2, 0, 360 * 64);

        int cur_step = (gstep >= 0) ? (gstep % tr->steps) : -1;

        for (int s = 0; s < tr->steps; s++) {
            double ang = (2.0 * M_PI * s / tr->steps) - M_PI / 2.0;
            int px = (int)lround(cx + (rad-1) * cos(ang));
            int py = (int)lround(cy + (rad-1) * sin(ang));
            int hit = plug->pat[t][s];
            int is_playhead = (s == cur_step);
            int is_active_now = is_playhead && hit;
            if (is_active_now) {
                /* currently firing step - bright green */
                XSetForeground(plug->dpy, plug->gc, green);
                XFillArc(plug->dpy, plug->win, plug->gc,
                                        px - 9, py - 9, 18, 18, 0, 360 * 64);
                XSetForeground(plug->dpy, plug->gc, fg);
                XDrawArc(plug->dpy, plug->win, plug->gc,
                                        px - 11, py - 11, 22, 22, 0, 360 * 64);
            } else if (hit) {
                XSetForeground(plug->dpy, plug->gc, acc);
                XFillArc(plug->dpy, plug->win, plug->gc,
                                        px - 6, py - 6, 12, 12, 0, 360 * 64);
            } else {
                XSetForeground(plug->dpy, plug->gc, mut);
                XFillArc(plug->dpy, plug->win, plug->gc,
                                        px - 4, py - 4, 8, 8, 0, 360 * 64);
            }
            if (is_playhead) {
                XSetForeground(plug->dpy, plug->gc, fg);
                XDrawArc(plug->dpy, plug->win, plug->gc,
                                        px - 8, py - 8, 16, 16, 0, 360 * 64);
            }
        }
    }

    eu_text(plug, 24, H - 18,
            "Click cells or wheel. Click note box to cycle MIDI note.", mut);
    XFlush(plug->dpy);
}

void eu_gui_redraw(eu_plug_t *plug) {
    if (plug && plug->gui_visible) eu_gui_paint(plug);
}

static void eu_gui_click(eu_plug_t *plug, int x, int y) {
    int top_y = 52;

    /* top bar clicks */
    if (y >= top_y && y <= top_y + 26) {
        if (x >= 24 && x < 142) {
            plug->st.rate = (plug->st.rate + 1) % EU_RATE_COUNT;
        } else if (x >= 152 && x < 252) {
            plug->st.swing = (plug->st.swing + 5) % 80;
        } else if (x >= 262 && x < 354) {
            plug->st.gate = ((plug->st.gate + 5) % 100) + 5;
            if (plug->st.gate > 95) plug->st.gate = 95;
        } else {
            return;
        }
        plug->dirty = 1;
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
            plug->dirty = 1;
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
            } else if (x >= cx0 + cell_w + 20 && x < cx0 + 2*cell_w + 6) {
                tr->pulses = (tr->pulses + 1) % (tr->steps + 1);
            } else if (x >= cx0 + 2*(cell_w + 6) &&
                                            x < cx0 + 2*(cell_w + 6) + 14) {
                tr->rotate = tr->rotate > 0 ? tr->rotate - 1 : tr->steps - 1;
            } else if (x >= cx0 + 2*(cell_w + 6) + 14 &&
                                            x < cx0 + 3*cell_w + 6) {
                tr->rotate = (tr->rotate + 1) % tr->steps;
            } else if (x >= cx0 + 3*(cell_w + 6) &&
                                            x < cx0 + 3*(cell_w + 6) + 14) {
                tr->vel = tr->vel > 1 ? tr->vel - 10 : 127;
            } else if (x >= cx0 + 3*(cell_w + 6) + 14 &&
                                            x < cx0 + 4*cell_w + 6) {
                tr->vel = ((tr->vel + 10) % 127) + 1;
            }
            plug->dirty = 1;
            eu_gui_paint(plug);
            return;
        }

        /* mute toggle on label area - enforce chronological order */
        if (y >= row_y && y < row_y + 26 &&
            x >= lx + 8 && x < lx + 52) {
            if (tr->mute) {
                /* currently muted -> 
                        allow unmuting only if previous is active */
                if (t == 0 || plug->st.tr[t-1].mute == 0) {
                    tr->mute = 0;
                }
            } else {
                /* currently active ->
                        allow muting and force all later tracks off */
                tr->mute = 1;
                for (int k = t + 1; k < EU_TRACKS; k++) {
                    plug->st.tr[k].mute = 1;
                }
            }
            plug->dirty = 1;
            eu_gui_paint(plug);
            return;
        }
    }
}

static void eu_gui_wheel(eu_plug_t *plug, int x, int y, int dir) {
    int top_y = 52;
    /* top bar wheel */
    if (y >= top_y && y <= top_y + 26) {
        if (x >= 24 && x < 142) {
            int r = plug->st.rate + dir;
            if (r < 0) r = EU_RATE_COUNT - 1;
            if (r >= EU_RATE_COUNT) r = 0;
            plug->st.rate = r;
        } else if (x >= 152 && x < 252) {
            int s = plug->st.swing + dir * 5;
            if (s < 0) s = 0;
            if (s > 75) s = 75;
            plug->st.swing = s;
        } else if (x >= 262 && x < 354) {
            int g = plug->st.gate + dir * 5;
            if (g < 10) g = 10;
            if (g > 95) g = 95;
            plug->st.gate = g;
        }
        plug->dirty = 1;
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
            plug->dirty = 1;
            eu_gui_paint(plug);
            return;
        }

        int cy0 = row_y + 30;
        int cx0 = lx + 100;

        if (y >= cy0 && y < cy0 + cell_h) {
            int v;
            if (x >= cx0 && x < cx0 + 14) {
                v = (int)tr->steps + dir;
                if (v<1) v=1;
                if (v>32) v=32;
                tr->steps = (uint8_t)v;
            } else if (x >= cx0 + 14 && x < cx0 + cell_w) {
                v = (int)tr->steps + dir;
                if (v<1) v=1;
                if (v>32) v=32;
                tr->steps = (uint8_t)v;
            } else if (x >= cx0 + cell_w + 6 && x < cx0 + cell_w + 20) {
                v = (int)tr->pulses + dir;
                if (v<0) v=0;
                if (v>tr->steps) v=tr->steps;
                tr->pulses = (uint8_t)v;
            } else if (x >= cx0 + cell_w + 20 && x < cx0 + 2*cell_w + 6) {
                v = (int)tr->pulses + dir;
                if (v<0) v=0;
                if (v>tr->steps) v=tr->steps;
                tr->pulses = (uint8_t)v;
            } else if (x >= cx0 + 2*(cell_w + 6) &&
                                          x < cx0 + 2*(cell_w + 6) + 14) {
                v = (int)tr->rotate + dir;
                if (v<0) v=0;
                if (v>=tr->steps) v=tr->steps-1;
                tr->rotate = (uint8_t)v;
            } else if (x >= cx0 + 2*(cell_w + 6) + 14 &&
                                          x < cx0 + 3*cell_w + 6) {
                v = (int)tr->rotate + dir;
                if (v<0) v=0;
                if (v>=tr->steps) v=tr->steps-1;
                tr->rotate = (uint8_t)v;
            } else if (x >= cx0 + 3*(cell_w + 6) &&
                                          x < cx0 + 3*(cell_w + 6) + 14) {
                v = (int)tr->vel + dir*5;
                if (v<1) v=1;
                if (v>127) v=127;
                tr->vel = (uint8_t)v;
            } else if (x >= cx0 + 3*(cell_w + 6) + 14 &&
                                          x < cx0 + 4*cell_w + 6) {
                v = (int)tr->vel + dir*5;
                if (v<1) v=1;
                if (v>127) v=127;
                tr->vel = (uint8_t)v;
            }
            plug->dirty = 1;
            eu_gui_paint(plug);
            return;
        }
    }
}

/* CLAP GUI + POSIX FD implementation */
static bool eu_gui_is_api_supported(const clap_plugin_t *p,
                                            const char *api, bool f) {
    (void)p; (void)f; return api && strcmp(api, CLAP_WINDOW_API_X11) == 0;
}
static bool eu_gui_get_preferred_api(const clap_plugin_t *p,
                                            const char **api, bool *f) {
    (void)p; *api = CLAP_WINDOW_API_X11; *f = false; return true;
}
static bool eu_gui_create(const clap_plugin_t *plugin,
                                    const char *api, bool f) {
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    (void)f;
    if (api && strcmp(api, CLAP_WINDOW_API_X11) != 0) return false;
    plug->dpy = XOpenDisplay(NULL);
    if (!plug->dpy) return false;
    int screen = DefaultScreen(plug->dpy);
    Window root = RootWindow(plug->dpy, screen);
    plug->gui_w = EU_GUI_W;
    plug->gui_h = EU_GUI_H;
    plug->win = XCreateSimpleWindow(plug->dpy, root, 0, 0,
                            (unsigned)plug->gui_w, (unsigned)plug->gui_h, 0,
                                    eu_col(18,18,22), eu_col(18,18,22));
    plug->gc = XCreateGC(plug->dpy, plug->win, 0, NULL);
    XSelectInput(plug->dpy, plug->win,
                 ExposureMask | ButtonPressMask | StructureNotifyMask);
    plug->xfd = ConnectionNumber(plug->dpy);
    if (plug->host_fd && plug->host_fd->register_fd)
        plug->host_fd->register_fd(plug->host, plug->xfd, CLAP_POSIX_FD_READ);

    /* timer for smooth playhead animation */
    plug->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (plug->timer_fd >= 0) {
        struct itimerspec ts;
        ts.it_interval.tv_sec = 0;
        ts.it_interval.tv_nsec = 66666666; /* ~15 Hz - calmer */
        ts.it_value = ts.it_interval;
        timerfd_settime(plug->timer_fd, 0, &ts, NULL);
        if (plug->host_fd && plug->host_fd->register_fd)
            plug->host_fd->register_fd(plug->host, plug->timer_fd,
                                                CLAP_POSIX_FD_READ);
    }

    plug->gui_created = 1;
    plug->gui_visible = 0;
    return true;
}
static void eu_gui_destroy(const clap_plugin_t *plugin) {
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (plug->host_fd && plug->host_fd->unregister_fd && plug->xfd >= 0)
        plug->host_fd->unregister_fd(plug->host, plug->xfd);
    if (plug->host_fd && plug->host_fd->unregister_fd && plug->timer_fd >= 0)
        plug->host_fd->unregister_fd(plug->host, plug->timer_fd);
    if (plug->timer_fd >= 0) { close(plug->timer_fd); plug->timer_fd = -1; }
    if (plug->gc && plug->dpy) XFreeGC(plug->dpy, plug->gc);
    if (plug->win && plug->dpy) XDestroyWindow(plug->dpy, plug->win);
    if (plug->dpy) XCloseDisplay(plug->dpy);
    plug->dpy = NULL; plug->win = 0; plug->gc = 0;
    plug->gui_created = 0; plug->gui_visible = 0; plug->xfd = -1;
}
static bool eu_gui_set_scale(const clap_plugin_t *p, double s){
    (void)p;
    (void)s;
    return false;
}
static bool eu_gui_get_size(const clap_plugin_t *plugin,
                                            uint32_t *w, uint32_t *h) {
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    *w = (uint32_t)plug->gui_w; *h = (uint32_t)plug->gui_h; return true;
}
static bool eu_gui_can_resize(const clap_plugin_t *p){(void)p;return true;}
static bool eu_gui_get_resize_hints(const clap_plugin_t *p,
                                        clap_gui_resize_hints_t *h){
    (void)p;
    h->can_resize_horizontally=true;
    h->can_resize_vertically=true;
    h->preserve_aspect_ratio=true;
    h->aspect_ratio_width=820;
    h->aspect_ratio_height=560;
    return true;
}
static bool eu_gui_adjust_size(const clap_plugin_t *p,
                                    uint32_t *w, uint32_t *h){
    (void)p;
    if(*w<560)*w=560;
    if(*h<360)*h=360;
    return true;
}
static bool eu_gui_set_size(const clap_plugin_t *plugin,
                                        uint32_t w, uint32_t h) {
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    plug->gui_w = (int)w; plug->gui_h = (int)h;
    if (plug->dpy && plug->win) {
        XResizeWindow(plug->dpy, plug->win, w, h);
        eu_gui_paint(plug); 
    }
    return true;
}
static bool eu_gui_set_parent(const clap_plugin_t *plugin,
                                        const clap_window_t *window) {
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (!window || !plug->dpy ||
            strcmp(window->api, CLAP_WINDOW_API_X11) != 0) return false;
    XReparentWindow(plug->dpy, plug->win, (Window)window->x11, 0, 0);
    XMapWindow(plug->dpy, plug->win); XFlush(plug->dpy); return true;
}
static bool eu_gui_set_transient(const clap_plugin_t *p,
                    const clap_window_t *w) {
    (void)p;
    (void)w;
    return true;
}
static void eu_gui_suggest_title(const clap_plugin_t *plugin,
                                                    const char *title) {
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (plug->dpy && plug->win && title) {
        XStoreName(plug->dpy, plug->win, title);
    }
}
static bool eu_gui_show(const clap_plugin_t *plugin) {
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (!plug->dpy || !plug->win) return false;
    XMapWindow(plug->dpy, plug->win);
    plug->gui_visible = 1;
    eu_gui_paint(plug);
    return true;
}
static bool eu_gui_hide(const clap_plugin_t *plugin) {
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (plug->dpy && plug->win) XUnmapWindow(plug->dpy, plug->win);
    plug->gui_visible = 0; return true;
}
static void eu_gui_on_fd(const clap_plugin_t *plugin, int fd,
                                            clap_posix_fd_flags_t flags) {
    (void)fd; (void)flags;
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    if (!plug->dpy) return;

    /* drain timer */
    if (plug->timer_fd >= 0 && fd == plug->timer_fd) {
        uint64_t expirations;
        while (read(plug->timer_fd, &expirations, sizeof(expirations)) > 0) {}
        if (plug->gui_visible) eu_gui_paint(plug);
        return;
    }

    XEvent ev;
    bool need_redraw = false;
    while (XPending(plug->dpy)) {
        XNextEvent(plug->dpy, &ev);
        if (ev.type == Expose) need_redraw = true;
        else if (ev.type == ButtonPress) {
            if (ev.xbutton.button == 4 || ev.xbutton.button == 5)
                eu_gui_wheel(plug, ev.xbutton.x, ev.xbutton.y,
                                                ev.xbutton.button==4?1:-1);
            else if (ev.xbutton.button == 1)
                eu_gui_click(plug, ev.xbutton.x, ev.xbutton.y);
        } else if (ev.type == ConfigureNotify) {
            plug->gui_w = ev.xconfigure.width;
            plug->gui_h = ev.xconfigure.height;
            need_redraw = true;
        }
    }
    if (plug->dirty) {
        plug->dirty = 0;
        need_redraw = true;
    }
    if (need_redraw) eu_gui_paint(plug);
}

const clap_plugin_gui_t eu_gui_ext = {
    .is_api_supported = eu_gui_is_api_supported,
    .get_preferred_api = eu_gui_get_preferred_api,
    .create = eu_gui_create,
    .destroy = eu_gui_destroy,
    .set_scale = eu_gui_set_scale,
    .get_size = eu_gui_get_size,
    .can_resize = eu_gui_can_resize,
    .get_resize_hints = eu_gui_get_resize_hints,
    .adjust_size = eu_gui_adjust_size,
    .set_size = eu_gui_set_size,
    .set_parent = eu_gui_set_parent,
    .set_transient = eu_gui_set_transient,
    .suggest_title = eu_gui_suggest_title,
    .show = eu_gui_show,
    .hide = eu_gui_hide
};

const clap_plugin_posix_fd_support_t eu_posix_fd_ext = {
    .on_fd = eu_gui_on_fd
};
