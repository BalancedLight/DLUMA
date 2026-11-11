#ifndef DLUMA_HUD_RESOLVER_GATE_H
#define DLUMA_HUD_RESOLVER_GATE_H

/* All operations are called under the backend's lock. */
typedef struct DlumaHudResolverGate {
    unsigned active, sealed, reset_pending, retired;
} DlumaHudResolverGate;
static int dluma_hud_gate_enter(DlumaHudResolverGate* gate,int cleanup)
{ if(gate->retired || gate->active || ((gate->sealed || gate->reset_pending) && !cleanup)) return 0; gate->active=1; return 1; }
static int dluma_hud_gate_reset(DlumaHudResolverGate* gate)
{ if(gate->active) { gate->reset_pending=1; return 0; } return 1; }
static int dluma_hud_gate_seal(DlumaHudResolverGate* gate)
{ gate->sealed=1; return !gate->active; }
static int dluma_hud_gate_take_reset(DlumaHudResolverGate* gate)
{ int pending=gate->reset_pending!=0; gate->reset_pending=0; return pending; }
static void dluma_hud_gate_leave(DlumaHudResolverGate* gate)
{ gate->active=0; }
static int dluma_hud_gate_retire(DlumaHudResolverGate* gate)
{ if(!gate->sealed || gate->active) return 0; gate->retired=1; return 1; }
static int dluma_hud_gate_can_commit(const DlumaHudResolverGate* gate)
{ return gate->active && !gate->sealed && !gate->reset_pending; }
#endif
