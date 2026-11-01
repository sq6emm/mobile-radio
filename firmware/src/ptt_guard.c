#include "ptt_guard.h"
#include <string.h>

void ptt_guard_default_cfg(ptt_guard_cfg_t *cfg)
{
    cfg->tot_ms = 180000;
    cfg->keepalive_ms = 2000;
    cfg->cooldown_ms = 10000;
    cfg->busy_lockout = false;
}

void ptt_guard_init(ptt_guard_t *g, const ptt_guard_cfg_t *cfg)
{
    memset(g, 0, sizeof(*g));
    if (cfg) g->cfg = *cfg; else ptt_guard_default_cfg(&g->cfg);
}

void ptt_guard_set_inhibit(ptt_guard_t *g, bool inhibit, uint32_t now_ms)
{
    g->inhibit = inhibit;
    if (inhibit && g->state == PTT_KEYED) {
        g->state = PTT_DENIED_INHIBIT;
        g->source = PTT_SRC_NONE;
    }
    (void)now_ms;
}

void ptt_guard_set_channel_busy(ptt_guard_t *g, bool busy)
{
    g->channel_busy = busy;
}

ptt_state_t ptt_guard_request(ptt_guard_t *g, ptt_source_t src, bool key, uint32_t now_ms)
{
    if (!key) {
        if (g->state == PTT_KEYED && (g->source == src || src == PTT_SRC_NONE)) {
            g->state = PTT_IDLE;
            g->source = PTT_SRC_NONE;
        } else if (g->state != PTT_KEYED && g->state != PTT_COOLDOWN) {
            g->state = PTT_IDLE;   /* clear a sticky denial on release */
        }
        return g->state;
    }
    if (g->state == PTT_KEYED)
        return PTT_KEYED;          /* already keyed by someone; local button has no priority */
    if (g->state == PTT_COOLDOWN && (int32_t)(now_ms - g->cooldown_until_ms) < 0)
        return PTT_COOLDOWN;
    if (g->inhibit) {
        g->state = PTT_DENIED_INHIBIT;
        return g->state;
    }
    if (g->cfg.busy_lockout && g->channel_busy) {
        g->state = PTT_DENIED_BUSY;
        return g->state;
    }
    g->state = PTT_KEYED;
    g->source = src;
    g->keyed_at_ms = now_ms;
    g->last_keepalive_ms = now_ms;
    return g->state;
}

void ptt_guard_keepalive(ptt_guard_t *g, uint32_t now_ms)
{
    g->last_keepalive_ms = now_ms;
}

bool ptt_guard_tick(ptt_guard_t *g, uint32_t now_ms)
{
    if (g->state == PTT_COOLDOWN && (int32_t)(now_ms - g->cooldown_until_ms) >= 0)
        g->state = PTT_IDLE;
    if (g->state != PTT_KEYED)
        return false;
    if (g->inhibit) {
        g->state = PTT_DENIED_INHIBIT;
        g->source = PTT_SRC_NONE;
        return false;
    }
    if (now_ms - g->keyed_at_ms >= g->cfg.tot_ms) {
        g->state = PTT_COOLDOWN;
        g->source = PTT_SRC_NONE;
        g->cooldown_until_ms = now_ms + g->cfg.cooldown_ms;
        return false;
    }
    if (g->source == PTT_SRC_HOST && now_ms - g->last_keepalive_ms >= g->cfg.keepalive_ms) {
        g->state = PTT_RELEASED_KEEPALIVE;
        g->source = PTT_SRC_NONE;
        return false;
    }
    return true;
}

uint16_t ptt_guard_tx_remaining_s(const ptt_guard_t *g, uint32_t now_ms)
{
    if (g->state != PTT_KEYED)
        return 0;
    uint32_t used = now_ms - g->keyed_at_ms;
    if (used >= g->cfg.tot_ms)
        return 0;
    return (uint16_t)((g->cfg.tot_ms - used + 999) / 1000);
}
