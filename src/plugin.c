#include "euclid.h"

/* Euclid RPE — C99 CLAP Euclidean MIDI sequencer.
   Silent stereo audio + one note output. Place it on a MIDI / instrument
   track immediately before a drum machine or sampler; the plugin emits
   note-ons on Euclidean hits. Incoming notes can be learned onto tracks. */

enum {
    PID_RATE  = 1,
    PID_SWING = 2,
    PID_GATE  = 3,
    PID_LEARN = 4,
    PID_TRACK_BASE = 100,
    F_MUTE = 0,
    F_NOTE = 1,
    F_CH   = 2,
    F_STEPS = 3,
    F_PULSES = 4,
    F_ROT  = 5,
    F_VEL  = 6,
    F_COUNT = 7
};

enum { EU_PARAM_COUNT = 4 + EU_TRACKS * F_COUNT };

static clap_id field_id(int track, int field)
{
    return (clap_id)(PID_TRACK_BASE + track * 10 + field);
}

/* eu_voice_t and eu_plug_t now live in euclid.h */

/* eu_plug_t is now defined in euclid.h (shared with gui_x11.c) */

/* ----------------------------- stream I/O ----------------------------- */

static int write_all(const clap_ostream_t *s, const void *p, uint64_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint64_t off = 0;
    while (off < n) {
        int64_t w = s->write(s, b + off, n - off);
        if (w <= 0) return 0;
        off += (uint64_t)w;
    }
    return 1;
}

static int read_all(const clap_istream_t *s, void *p, uint64_t n)
{
    uint8_t *b = (uint8_t *)p;
    uint64_t off = 0;
    while (off < n) {
        int64_t r = s->read(s, b + off, n - off);
        if (r <= 0) return 0;
        off += (uint64_t)r;
    }
    return 1;
}

/* ----------------------------- parameters ----------------------------- */

static double get_param(const eu_plug_t *plug, clap_id id)
{
    const eu_state_t *st = &plug->st;
    int t, f;
    if (id == PID_RATE)  return (double)st->rate;
    if (id == PID_SWING) return (double)st->swing;
    if (id == PID_GATE)  return (double)st->gate;
    if (id == PID_LEARN) return (double)st->learn;
    if (id >= PID_TRACK_BASE) {
        t = (int)((id - PID_TRACK_BASE) / 10);
        f = (int)((id - PID_TRACK_BASE) % 10);
        if (t >= 0 && t < EU_TRACKS) {
            const eu_track_t *tr = &st->tr[t];
            switch (f) {
            case F_MUTE:   return (double)tr->mute;
            case F_NOTE:   return (double)tr->note;
            case F_CH:     return (double)tr->ch;
            case F_STEPS:  return (double)tr->steps;
            case F_PULSES: return (double)tr->pulses;
            case F_ROT:    return (double)tr->rotate;
            case F_VEL:    return (double)tr->vel;
            default: break;
            }
        }
    }
    return 0.0;
}

static void apply_param(eu_plug_t *plug, clap_id id, double value)
{
    eu_state_t *st = &plug->st;
    int t, f;
    int gen = 1;

    if (id == PID_RATE)       st->rate  = (uint32_t)value;
    else if (id == PID_SWING) st->swing = (uint32_t)value;
    else if (id == PID_GATE)  st->gate  = (uint32_t)value;
    else if (id == PID_LEARN) st->learn = (uint32_t)value;
    else if (id >= PID_TRACK_BASE) {
        t = (int)((id - PID_TRACK_BASE) / 10);
        f = (int)((id - PID_TRACK_BASE) % 10);
        if (t >= 0 && t < EU_TRACKS) {
            eu_track_t *tr = &st->tr[t];
            switch (f) {
            case F_MUTE:   tr->mute   = value >= 0.5 ? 1 : 0; break;
            case F_NOTE:   tr->note   = (uint8_t)value; break;
            case F_CH:     tr->ch     = (uint8_t)value; break;
            case F_STEPS:  tr->steps  = (uint8_t)value; break;
            case F_PULSES: tr->pulses = (uint8_t)value; break;
            case F_ROT:    tr->rotate = (uint8_t)value; break;
            case F_VEL:    tr->vel    = (uint8_t)value; break;
            default: gen = 0; break;
            }
        } else gen = 0;
    } else gen = 0;

    if (gen) {
        eu_clamp(st);
        plug->dirty = 1;
    }
}

static clap_id param_id_at(uint32_t index)
{
    static const clap_id glob[4] = { PID_RATE, PID_SWING, PID_GATE, PID_LEARN };
    if (index < 4) return glob[index];
    index -= 4;
    return field_id((int)(index / F_COUNT), (int)(index % F_COUNT));
}

/* ----------------------------- notes ---------------------------------- */

static void emit_note(const clap_output_events_t *out, uint32_t time,
                      int on, int32_t pitch, double vel, int32_t note_id, int ch)
{
    clap_event_note_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.header.size     = (uint32_t)sizeof(ev);
    ev.header.time     = time;
    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type     = on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF;
    ev.note_id         = note_id;
    ev.port_index      = 0;
    ev.channel         = (int16_t)ch;
    ev.key             = (int16_t)pitch;
    ev.velocity        = vel;
    out->try_push(out, &ev.header);
}

static void all_notes_off(eu_plug_t *plug, const clap_output_events_t *out, uint32_t time)
{
    int i;
    for (i = 0; i < EU_TRACKS; i++) {
        if (plug->voices[i].live) {
            emit_note(out, time, 0, plug->voices[i].pitch, 0.0,
                      plug->voices[i].note_id, plug->voices[i].ch);
            plug->voices[i].live = 0;
        }
    }
}

static void push_param_out(const clap_output_events_t *out, uint32_t time,
                           clap_id id, double value)
{
    clap_event_param_value_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.header.size     = (uint32_t)sizeof(ev);
    ev.header.time     = time;
    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type     = CLAP_EVENT_PARAM_VALUE;
    ev.param_id        = id;
    ev.cookie          = NULL;
    ev.note_id         = -1;
    ev.port_index      = -1;
    ev.channel         = -1;
    ev.key             = -1;
    ev.value           = value;
    out->try_push(out, &ev.header);
}

static void learn_note(eu_plug_t *plug, int32_t key, const clap_output_events_t *out, uint32_t time)
{
    int t;
    if (key < 0) key = 0;
    if (key > 127) key = 127;

    if (plug->st.learn >= EU_LEARN_T1 && plug->st.learn <= EU_LEARN_T8) {
        t = (int)plug->st.learn - 1;
        plug->st.tr[t].note = (uint8_t)key;
        plug->st.tr[t].mute = 0;
        if (out) push_param_out(out, time, field_id(t, F_NOTE), (double)key);
        if (out) push_param_out(out, time, field_id(t, F_MUTE), 0.0);
        plug->dirty = 1;
    } else if (plug->st.learn == EU_LEARN_FILL) {
        t = plug->learn_next % EU_TRACKS;
        plug->st.tr[t].note = (uint8_t)key;
        plug->st.tr[t].mute = 0;
        plug->learn_next = (t + 1) % EU_TRACKS;
        if (out) push_param_out(out, time, field_id(t, F_NOTE), (double)key);
        if (out) push_param_out(out, time, field_id(t, F_MUTE), 0.0);
        plug->dirty = 1;
    }
}

static void consume_in_events(eu_plug_t *plug, const clap_input_events_t *in,
                              const clap_output_events_t *out)
{
    uint32_t n, i;
    if (!in) return;
    n = in->size(in);
    for (i = 0; i < n; i++) {
        const clap_event_header_t *hdr = in->get(in, i);
        if (!hdr || hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
            const clap_event_param_value_t *ev = (const clap_event_param_value_t *)hdr;
            apply_param(plug, ev->param_id, ev->value);
        } else if (hdr->type == CLAP_EVENT_NOTE_ON) {
            const clap_event_note_t *ev = (const clap_event_note_t *)hdr;
            learn_note(plug, ev->key, out, hdr->time);
        } else if (hdr->type == CLAP_EVENT_MIDI) {
            const clap_event_midi_t *ev = (const clap_event_midi_t *)hdr;
            unsigned st = ev->data[0] & 0xF0u;
            if (st == 0x90u && ev->data[2] > 0)
                learn_note(plug, ev->data[1], out, hdr->time);
        }
    }
}

/* ----------------------------- lifecycle ------------------------------ */

static void rebuild_if_dirty(eu_plug_t *plug)
{
    if (!plug->dirty) return;
    eu_clamp(&plug->st);
    euclid_rebuild(&plug->st, plug->pat);
    plug->dirty = 0;
    if (plug->host_state && plug->host_state->mark_dirty)
        plug->host_state->mark_dirty(plug->host);
}

static bool eu_init(const clap_plugin_t *plugin)
{
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    plug->host_log = (const clap_host_log_t *)
        plug->host->get_extension(plug->host, CLAP_EXT_LOG);
    plug->host_params = (const clap_host_params_t *)
        plug->host->get_extension(plug->host, CLAP_EXT_PARAMS);
    plug->host_state = (const clap_host_state_t *)
        plug->host->get_extension(plug->host, CLAP_EXT_STATE);
    plug->host_gui = (const clap_host_gui_t *)
        plug->host->get_extension(plug->host, CLAP_EXT_GUI);
    plug->host_fd = (const clap_host_posix_fd_support_t *)
        plug->host->get_extension(plug->host, CLAP_EXT_POSIX_FD_SUPPORT);
    euclid_rebuild(&plug->st, plug->pat);
    plug->dirty = 0;
    plug->last_gstep = INT64_MIN;
    return true;
}

static void eu_destroy(const clap_plugin_t *plugin)
{
    free(plugin->plugin_data);
}

static bool eu_activate(const clap_plugin_t *plugin, double sr,
                        uint32_t min_frames, uint32_t max_frames)
{
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    (void)min_frames; (void)max_frames;
    plug->sr = sr > 1.0 ? sr : 44100.0;
    plug->active = 1;
    plug->abs_sample = 0;
    plug->last_gstep = INT64_MIN;
    memset(plug->voices, 0, sizeof(plug->voices));
    return true;
}

static void eu_deactivate(const clap_plugin_t *plugin)
{
    ((eu_plug_t *)plugin->plugin_data)->active = 0;
}

static bool eu_start_processing(const clap_plugin_t *plugin)
{
    ((eu_plug_t *)plugin->plugin_data)->processing = 1;
    return true;
}

static void eu_stop_processing(const clap_plugin_t *plugin)
{
    ((eu_plug_t *)plugin->plugin_data)->processing = 0;
}

static void eu_reset(const clap_plugin_t *plugin)
{
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    memset(plug->voices, 0, sizeof(plug->voices));
    plug->abs_sample = 0;
    plug->last_gstep = INT64_MIN;
}

static void eu_on_main_thread(const clap_plugin_t *plugin)
{
    (void)plugin;
}

/* ----------------------------- ports ---------------------------------- */

static uint32_t note_ports_count(const clap_plugin_t *plugin, bool is_input)
{
    (void)plugin;
    /* 1 in (MIDI learn) + 1 out (Euclidean triggers). */
    return 1;
    (void)is_input;
}

static bool note_ports_get(const clap_plugin_t *plugin, uint32_t index,
                           bool is_input, clap_note_port_info_t *info)
{
    (void)plugin;
    if (index != 0) return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    snprintf(info->name, sizeof(info->name), "%s",
             is_input ? "Learn In" : "Trig Out");
    return true;
}

static const clap_plugin_note_ports_t s_note_ports = {
    .count = note_ports_count,
    .get   = note_ports_get
};

static uint32_t audio_ports_count(const clap_plugin_t *plugin, bool is_input)
{
    (void)plugin;
    return is_input ? 0 : 1;
}

static bool audio_ports_get(const clap_plugin_t *plugin, uint32_t index,
                            bool is_input, clap_audio_port_info_t *info)
{
    (void)plugin;
    if (is_input || index != 0) return false;
    info->id = 0;
    snprintf(info->name, sizeof(info->name), "Silent");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

static const clap_plugin_audio_ports_t s_audio_ports = {
    .count = audio_ports_count,
    .get   = audio_ports_get
};

/* ----------------------------- params ext ----------------------------- */

static uint32_t params_count(const clap_plugin_t *plugin)
{
    (void)plugin;
    return EU_PARAM_COUNT;
}

static bool params_info(const clap_plugin_t *plugin, uint32_t index, clap_param_info_t *info)
{
    clap_id id;
    int t, f;
    (void)plugin;
    if (index >= EU_PARAM_COUNT) return false;
    memset(info, 0, sizeof(*info));
    id = param_id_at(index);
    info->id = id;
    info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_ENUM;
    info->cookie = NULL;
    snprintf(info->module, sizeof(info->module), "Clock");

    if (id == PID_RATE) {
        snprintf(info->name, sizeof(info->name), "Rate");
        info->min_value = 0; info->max_value = EU_RATE_COUNT - 1; info->default_value = EU_RATE_1_16;
        return true;
    }
    if (id == PID_SWING) {
        snprintf(info->name, sizeof(info->name), "Swing");
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = 0; info->max_value = 75; info->default_value = 0;
        return true;
    }
    if (id == PID_GATE) {
        snprintf(info->name, sizeof(info->name), "Gate %%");
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = 5; info->max_value = 95; info->default_value = 40;
        return true;
    }
    if (id == PID_LEARN) {
        snprintf(info->name, sizeof(info->name), "MIDI Learn");
        info->min_value = 0; info->max_value = EU_LEARN_FILL; info->default_value = 0;
        return true;
    }

    t = (int)((id - PID_TRACK_BASE) / 10);
    f = (int)((id - PID_TRACK_BASE) % 10);
    snprintf(info->module, sizeof(info->module), "Track %d", t + 1);
    switch (f) {
    case F_MUTE:
        snprintf(info->name, sizeof(info->name), "T%d Mute", t + 1);
        info->min_value = 0; info->max_value = 1;
        info->default_value = (t >= 5) ? 1 : 0;
        break;
    case F_NOTE:
        snprintf(info->name, sizeof(info->name), "T%d Note", t + 1);
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = 0; info->max_value = 127;
        info->default_value = (t == 0) ? 36 : (t == 1) ? 38 : (t == 2) ? 42 : 60;
        break;
    case F_CH:
        snprintf(info->name, sizeof(info->name), "T%d Channel", t + 1);
        info->min_value = 0; info->max_value = 15; info->default_value = 0;
        break;
    case F_STEPS:
        snprintf(info->name, sizeof(info->name), "T%d Steps", t + 1);
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = 1; info->max_value = EU_MAX_STEPS; info->default_value = 16;
        break;
    case F_PULSES:
        snprintf(info->name, sizeof(info->name), "T%d Triggers", t + 1);
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = 0; info->max_value = EU_MAX_STEPS; info->default_value = 4;
        break;
    case F_ROT:
        snprintf(info->name, sizeof(info->name), "T%d Rotate", t + 1);
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = 0; info->max_value = EU_MAX_STEPS - 1; info->default_value = 0;
        break;
    case F_VEL:
        snprintf(info->name, sizeof(info->name), "T%d Velocity", t + 1);
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = 1; info->max_value = 127; info->default_value = 100;
        break;
    default:
        return false;
    }
    return true;
}

static bool params_get_value(const clap_plugin_t *plugin, clap_id id, double *out)
{
    *out = get_param((const eu_plug_t *)plugin->plugin_data, id);
    return true;
}

static bool params_value_to_text(const clap_plugin_t *plugin, clap_id id,
                                 double value, char *out, uint32_t cap)
{
    int v = (int)value;
    int f;
    char nbuf[8];
    (void)plugin;
    if (cap == 0) return false;
    if (id == PID_RATE) {
        snprintf(out, cap, "%s", eu_rate_name((uint32_t)v));
    } else if (id == PID_SWING) {
        snprintf(out, cap, "%d%%", v);
    } else if (id == PID_GATE) {
        snprintf(out, cap, "%d%%", v);
    } else if (id == PID_LEARN) {
        if (v == 0) snprintf(out, cap, "Off");
        else if (v == EU_LEARN_FILL) snprintf(out, cap, "Fill");
        else snprintf(out, cap, "Track %d", v);
    } else if (id >= PID_TRACK_BASE) {
        f = (int)((id - PID_TRACK_BASE) % 10);
        if (f == F_MUTE) snprintf(out, cap, "%s", v ? "Mute" : "On");
        else if (f == F_NOTE) snprintf(out, cap, "%s", eu_note_name((uint8_t)v, nbuf, sizeof nbuf));
        else if (f == F_CH) snprintf(out, cap, "Ch %d", v + 1);
        else snprintf(out, cap, "%d", v);
    } else {
        snprintf(out, cap, "%d", v);
    }
    return true;
}

static bool params_text_to_value(const clap_plugin_t *plugin, clap_id id,
                                 const char *text, double *out)
{
    (void)plugin; (void)id;
    if (!text) return false;
    *out = strtod(text, NULL);
    return true;
}

static void params_flush(const clap_plugin_t *plugin,
                         const clap_input_events_t *in,
                         const clap_output_events_t *out)
{
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    consume_in_events(plug, in, out);
    rebuild_if_dirty(plug);
}

static const clap_plugin_params_t s_params = {
    .count         = params_count,
    .get_info      = params_info,
    .get_value     = params_get_value,
    .value_to_text = params_value_to_text,
    .text_to_value = params_text_to_value,
    .flush         = params_flush
};

/* ----------------------------- state ---------------------------------- */

static bool state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream)
{
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    return write_all(stream, &plug->st, sizeof(plug->st)) ? true : false;
}

static bool state_load(const clap_plugin_t *plugin, const clap_istream_t *stream)
{
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    eu_state_t tmp;
    if (!read_all(stream, &tmp, sizeof(tmp))) return false;
    if (!eu_state_valid(&tmp)) return false;
    plug->st = tmp;
    eu_clamp(&plug->st);
    euclid_rebuild(&plug->st, plug->pat);
    plug->dirty = 0;
    if (plug->host_params && plug->host_params->rescan)
        plug->host_params->rescan(plug->host, CLAP_PARAM_RESCAN_VALUES);
    return true;
}

static const clap_plugin_state_t s_state = {
    .save = state_save,
    .load = state_load
};

/* ----------------------------- process -------------------------------- */

static void silence(const clap_process_t *process)
{
    uint32_t p, c;
    for (p = 0; p < process->audio_outputs_count; p++) {
        clap_audio_buffer_t *buf = process->audio_outputs + p;
        for (c = 0; c < buf->channel_count; c++) {
            if (buf->data32 && buf->data32[c])
                memset(buf->data32[c], 0, process->frames_count * sizeof(float));
        }
    }
}

typedef struct {
    uint32_t time;
    int on;
    int32_t pitch;
    int32_t vel;
    int32_t id;
    int ch;
} midiev_t;

static int ev_cmp(const void *a, const void *b)
{
    const midiev_t *x = (const midiev_t *)a;
    const midiev_t *y = (const midiev_t *)b;
    if (x->time < y->time) return -1;
    if (x->time > y->time) return 1;
    if (x->on != y->on) return x->on - y->on; /* offs before ons */
    return 0;
}

static clap_process_status eu_process(const clap_plugin_t *plugin,
                                      const clap_process_t *process)
{
    eu_plug_t *plug = (eu_plug_t *)plugin->plugin_data;
    uint32_t frames = process->frames_count;
    int playing = 1;
    double beats = -1.0;
    double spb, start, end, sp_step;
    int64_t g_begin, g_end, gs;
    midiev_t evs[256];
    int nev = 0;
    int t;

    consume_in_events(plug, process->in_events, process->out_events);
    rebuild_if_dirty(plug);
    silence(process);

    if (process->transport) {
        const clap_event_transport_t *tr = process->transport;
        if (tr->flags & CLAP_TRANSPORT_HAS_TEMPO) {
            if (tr->tempo > 1.0) plug->tempo = tr->tempo;
        }
        playing = (tr->flags & CLAP_TRANSPORT_IS_PLAYING) ? 1 : 0;
        if (tr->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE)
            beats = (double)tr->song_pos_beats / (double)CLAP_BEATTIME_FACTOR;
    }
    if (plug->tempo < 1.0) plug->tempo = 120.0;
    if (plug->sr < 1.0) plug->sr = 44100.0;

    if (!playing) {
        all_notes_off(plug, process->out_events, 0);
        plug->abs_sample += (int64_t)frames;
        plug->last_gstep = INT64_MIN;
        return CLAP_PROCESS_CONTINUE;
    }

    spb = eu_steps_per_beat(plug->st.rate);
    sp_step = (plug->sr * 60.0 / plug->tempo) / spb;
    if (sp_step < 1.0) sp_step = 1.0;

    if (beats >= 0.0) start = beats * spb;
    else              start = (double)plug->abs_sample / sp_step;
    end = start + (double)frames / sp_step;

    /* pending note-offs from earlier blocks */
    for (t = 0; t < EU_TRACKS; t++) {
        eu_voice_t *v = &plug->voices[t];
        if (!v->live) continue;
        if (v->off_abs >= plug->abs_sample &&
            v->off_abs < plug->abs_sample + (int64_t)frames) {
            if (nev < 256) {
                evs[nev].time  = (uint32_t)(v->off_abs - plug->abs_sample);
                evs[nev].on    = 0;
                evs[nev].pitch = v->pitch;
                evs[nev].vel   = 0;
                evs[nev].id    = v->note_id;
                evs[nev].ch    = v->ch;
                nev++;
            }
            v->live = 0;
        }
    }

    g_begin = (int64_t)ceil(start - 1e-12);
    g_end   = (int64_t)ceil(end   - 1e-12);
    if (g_begin < 0) g_begin = 0;

    for (gs = g_begin; gs < g_end; gs++) {
        double when = (double)gs;
        double frac;
        int32_t samp;
        int gate_samp;

        if (gs == plug->last_gstep) continue;

        /* delay odd steps for swing (classic 16th swing) */
        if (plug->st.swing > 0 && (gs & 1))
            when += ((double)plug->st.swing / 100.0) * 0.5;

        frac = when - start;
        samp = (int32_t)(frac * sp_step + 0.5);
        if (samp < 0) samp = 0;
        if ((uint32_t)samp >= frames) continue;

        gate_samp = (int)(sp_step * ((double)plug->st.gate / 100.0));
        if (gate_samp < 1) gate_samp = 1;

        for (t = 0; t < EU_TRACKS; t++) {
            const eu_track_t *tr = &plug->st.tr[t];
            int steps, idx;
            int32_t nid;
            if (tr->mute) continue;
            steps = tr->steps;
            if (steps < 1) continue;
            idx = (int)(gs % (int64_t)steps);
            if (idx < 0) idx += steps;
            if (!plug->pat[t][idx]) continue;
            if (nev >= 250) break;

            /* retrigger: off previous voice on this track first */
            if (plug->voices[t].live) {
                evs[nev].time  = (uint32_t)samp;
                evs[nev].on    = 0;
                evs[nev].pitch = plug->voices[t].pitch;
                evs[nev].vel   = 0;
                evs[nev].id    = plug->voices[t].note_id;
                evs[nev].ch    = plug->voices[t].ch;
                nev++;
                plug->voices[t].live = 0;
            }

            nid = plug->next_note_id++;
            evs[nev].time  = (uint32_t)samp;
            evs[nev].on    = 1;
            evs[nev].pitch = tr->note;
            evs[nev].vel   = tr->vel;
            evs[nev].id    = nid;
            evs[nev].ch    = tr->ch;
            nev++;

            if ((uint32_t)(samp + gate_samp) < frames) {
                if (nev < 256) {
                    evs[nev].time  = (uint32_t)(samp + gate_samp);
                    evs[nev].on    = 0;
                    evs[nev].pitch = tr->note;
                    evs[nev].vel   = 0;
                    evs[nev].id    = nid;
                    evs[nev].ch    = tr->ch;
                    nev++;
                }
            } else {
                plug->voices[t].live     = 1;
                plug->voices[t].pitch    = tr->note;
                plug->voices[t].note_id  = nid;
                plug->voices[t].ch       = tr->ch;
                plug->voices[t].off_abs  = plug->abs_sample + (int64_t)samp + (int64_t)gate_samp;
            }
        }
        plug->last_gstep = gs;
        plug->dirty = 1;
    }

    if (nev > 1) qsort(evs, (size_t)nev, sizeof(evs[0]), ev_cmp);
    for (t = 0; t < nev; t++) {
        double vel = evs[t].on ? (evs[t].vel / 127.0) : 0.0;
        emit_note(process->out_events, evs[t].time, evs[t].on,
                  evs[t].pitch, vel, evs[t].id, evs[t].ch);
    }

    plug->abs_sample += (int64_t)frames;
    return CLAP_PROCESS_CONTINUE;
}

/* ----------------------------- factory -------------------------------- */

static const void *eu_get_extension(const clap_plugin_t *plugin, const char *id)
{
    (void)plugin;
    if (!strcmp(id, CLAP_EXT_NOTE_PORTS))  return &s_note_ports;
    if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &s_audio_ports;
    if (!strcmp(id, CLAP_EXT_PARAMS))      return &s_params;
    if (!strcmp(id, CLAP_EXT_STATE))       return &s_state;
    if (!strcmp(id, CLAP_EXT_GUI))         return &eu_gui_ext;
    if (!strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT)) return &eu_posix_fd_ext;
    return NULL;
}

static const char *s_features[] = {
    CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_UTILITY,
    NULL
};

static const clap_plugin_descriptor_t s_desc = {
    .clap_version = CLAP_VERSION_INIT,
    .id          = "com.euclid.rpe",
    .name        = "Euclid RPE",
    .vendor      = "Euclid",
    .url         = "",
    .manual_url  = "",
    .support_url = "",
    .version     = "1.0.0",
    .description = "C99 Euclidean MIDI sequencer. Place before a drum machine.",
    .features    = s_features
};

static const clap_plugin_t *eu_create(const clap_host_t *host)
{
    eu_plug_t *plug = (eu_plug_t *)calloc(1, sizeof(eu_plug_t));
    if (!plug) return NULL;
    eu_state_default(&plug->st);
    plug->host = host;
    plug->sr = 44100.0;
    plug->tempo = 120.0;
    plug->last_gstep = INT64_MIN;
    plug->plugin.desc = &s_desc;
    plug->plugin.plugin_data = plug;
    plug->plugin.init = eu_init;
    plug->plugin.destroy = eu_destroy;
    plug->plugin.activate = eu_activate;
    plug->plugin.deactivate = eu_deactivate;
    plug->plugin.start_processing = eu_start_processing;
    plug->plugin.stop_processing = eu_stop_processing;
    plug->plugin.reset = eu_reset;
    plug->plugin.process = eu_process;
    plug->plugin.get_extension = eu_get_extension;
    plug->plugin.on_main_thread = eu_on_main_thread;
    return &plug->plugin;
}

static uint32_t factory_count(const clap_plugin_factory_t *f)
{
    (void)f;
    return 1;
}

static const clap_plugin_descriptor_t *factory_desc(const clap_plugin_factory_t *f, uint32_t index)
{
    (void)f;
    if (index != 0) return NULL;
    return &s_desc;
}

static const clap_plugin_t *factory_create(const clap_plugin_factory_t *f,
                                           const clap_host_t *host,
                                           const char *plugin_id)
{
    (void)f;
    if (!host || !plugin_id) return NULL;
    if (strcmp(plugin_id, s_desc.id) != 0) return NULL;
    if (host->clap_version.major < 1) return NULL;
    return eu_create(host);
}

static const clap_plugin_factory_t s_factory = {
    .get_plugin_count      = factory_count,
    .get_plugin_descriptor = factory_desc,
    .create_plugin         = factory_create
};

static bool entry_init(const char *plugin_path)
{
    (void)plugin_path;
    return true;
}

static void entry_deinit(void) {}

static const void *entry_get_factory(const char *factory_id)
{
    if (!strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) return &s_factory;
    return NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init         = entry_init,
    .deinit       = entry_deinit,
    .get_factory  = entry_get_factory
};
