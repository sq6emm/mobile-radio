/*
 * USB-C Power Delivery role policy for the battery pack.
 *
 * Goal state with a phone attached: the pack is the power SOURCE (it charges
 * the phone) and the data UFP (the phone is the USB host and talks to the
 * radio). Type-C does not give that combination by itself, so this policy
 * asks the PD controller for PR_Swap / DR_Swap as required, backs off on
 * rejection, and hands power back when the battery reaches the reserve level.
 *
 * It is a pure state machine (no I/O) so that it can be unit tested; the port
 * layer translates actions into TPS25751 / FUSB302 register writes.
 */
#ifndef PD_POLICY_H
#define PD_POLICY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { PD_POWER_SINK = 0, PD_POWER_SOURCE = 1 } pd_power_role_t;
typedef enum { PD_DATA_UFP = 0, PD_DATA_DFP = 1 } pd_data_role_t;

typedef enum {
    PD_EV_ATTACHED,        /* roles established (Type-C or PD explicit contract) */
    PD_EV_DETACHED,
    PD_EV_PR_SWAP_RESULT,  /* accepted = swap completed, power role now flipped */
    PD_EV_DR_SWAP_RESULT,
    PD_EV_PARTNER_REQUESTED_PR_SWAP, /* partner wants to flip power role */
} pd_event_kind_t;

typedef struct {
    pd_event_kind_t kind;
    pd_power_role_t power_role;   /* our role (ATTACHED) */
    pd_data_role_t  data_role;    /* our role (ATTACHED) */
    bool partner_pd;              /* partner speaks USB PD */
    bool partner_drp;             /* partner advertises dual-role power (a phone, not a charger) */
    bool accepted;                /* swap results */
} pd_event_t;

typedef enum {
    PD_ACT_NONE = 0,
    PD_ACT_REQUEST_PR_SWAP,
    PD_ACT_REQUEST_DR_SWAP,
    PD_ACT_ACCEPT_PR_SWAP,
    PD_ACT_REJECT_PR_SWAP,
    PD_ACT_SET_SINK_LIMIT_MA,  /* arg = input current limit when sinking */
    PD_ACT_ENABLE_SOURCE,      /* arg = max power in W to advertise */
    PD_ACT_DISABLE_SOURCE,
} pd_action_kind_t;

typedef struct {
    pd_action_kind_t kind;
    uint32_t arg;
} pd_action_t;

typedef struct {
    bool     allow_source;       /* user setting: charge the phone */
    bool     allow_sink;         /* user setting: let the port charge the pack */
    uint8_t  reserve_soc_pct;    /* stop sourcing below this */
    uint8_t  resume_soc_pct;     /* resume sourcing above this (hysteresis) */
    uint8_t  max_source_w;
    uint32_t swap_retry_ms;      /* back-off after a rejected swap */
    uint8_t  swap_max_attempts;
    uint32_t trickle_sink_ma;    /* what we draw from a phone when we must sink */
} pd_policy_cfg_t;

typedef struct {
    pd_policy_cfg_t cfg;
    bool attached, partner_pd, partner_drp;
    pd_power_role_t power_role;
    pd_data_role_t  data_role;
    bool pr_swap_pending, dr_swap_pending;
    uint8_t pr_attempts, dr_attempts;
    uint32_t next_swap_ms;
    bool source_paused_low_batt;
    uint8_t soc_pct;
} pd_policy_t;

void pd_policy_default_cfg(pd_policy_cfg_t *cfg);
void pd_policy_init(pd_policy_t *p, const pd_policy_cfg_t *cfg);

/* Feed an event; returns the immediate action (may be PD_ACT_NONE). */
pd_action_t pd_policy_event(pd_policy_t *p, const pd_event_t *ev, uint32_t now_ms);

/* Call periodically (e.g. 1 Hz) with the fuel-gauge state of charge. */
pd_action_t pd_policy_tick(pd_policy_t *p, uint32_t now_ms, uint8_t soc_pct);

/* True when the pack is in its goal configuration (source + UFP). */
bool pd_policy_goal_reached(const pd_policy_t *p);

#endif
