/*
WARTHOG_OPTIONS.H

What the game's sources ask of Server Setup's WARTHOG OPTIONS
(warthog_options.c; the record is port/linux/include/halo_warthog_options.h).
*/

#ifndef __WARTHOG_OPTIONS_H
#define __WARTHOG_OPTIONS_H
#pragma once

struct damage_data;

/* the scales of the knockback (damage.c) and stun (units.c) of the damage
being dealt or replayed: 1 but while a Warthog's shields took some of it */
extern real warthog_damage_acceleration_scale;
extern real warthog_damage_stun_scale;

/* the shields' kind of the damage (_warthog_damage_*), NONE for one they
leave as it is */
short warthog_damage_kind(struct damage_data const *damage, short category);
/* whether the damage is a Warthog's turret's */
boolean warthog_damage_from_turret(struct damage_data const *damage, short category);
/* (the host) the scale of a turret's damage (1: stock) */
real warthog_turret_damage_scale(struct damage_data const *damage, short category);
/* (the host) the scale of a turret's head hit, 0 for the game's own rule */
real warthog_turret_headshot_scale(struct damage_data const *damage, short category);
/* (the host) a hit on the object, if a Warthog with shields: the shields
take what they cover of the damage, and the knockback is scaled */
void warthog_vehicle_absorb(long object_index, struct damage_data const *damage, short category,
	real *total_damage, real *acceleration_scale);
/* (the host) a hit on the object, if a rider of a Warthog with shields: its
damage and stun reduced */
void warthog_rider_damage(long object_index, struct damage_data const *damage, short category,
	real *total_damage, real *stun_scale);
/* whether the unit's local player has its Warthog seat's view switched
(director.c) */
boolean warthog_options_view_toggled(long unit_index);
/* the riders' shield bar (hud.c) */
void warthog_options_draw(short local_player_index);

#endif /* __WARTHOG_OPTIONS_H */
