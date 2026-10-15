/*
HALO_WARTHOG_OPTIONS.H

Server Setup's WARTHOG OPTIONS (port/linux/game/warthog_options.c): the
host's tuning of every Warthog (the chaingun and rocket Warthogs alike) for
a network game, and the shields it can give them. The host puts its choice
in the game settings record (struct network_game's warthog_options), which
every machine has before the map loads, so all of them tune the Warthog's
tags alike (the clients predict their own vehicles from them) and the host
deals the shields' damage.

Every field's 0 is the stock game's: a record of zeros (a host that sets
none, or a game record cleared) plays as the game does.
*/

#ifndef __HALO_WARTHOG_OPTIONS_H
#define __HALO_WARTHOG_OPTIONS_H

/* ---------- constants */

enum
{
	/* the most shield layers a Warthog has; layer 0 is the outermost, the
	first to break and the last to come back */
	MAXIMUM_WARTHOG_SHIELD_LAYERS = 3,
};

/* what damage the shields tell apart (warthog_options.c's
warthog_damage_kind, by the damage's category and the tag's name) */
enum
{
	_warthog_damage_bullets,
	_warthog_damage_explosives,
	_warthog_damage_plasma_bullets,
	_warthog_damage_plasma_explosives,
	NUMBER_OF_WARTHOG_DAMAGE_KINDS
};

/* what the shields reduce of each kind while they hold */
enum
{
	/* the shield's own drain: how much less of the hit it takes */
	_warthog_effect_shield_damage,
	/* the throw that flips the Warthog */
	_warthog_effect_knockback,
	/* the damage and stun that reach its riders */
	_warthog_effect_pass_through,
	NUMBER_OF_WARTHOG_EFFECTS
};

/* ---------- structures */

/* (what goes on the wire, in the game settings record: every machine has the
same layout, little-endian) */
struct warthog_options
{
	/* percentages of the stock value (0: stock, as 100) */
	short acceleration;
	short top_speed;
	short steering;
	/* 200: half the spread */
	short turret_accuracy;
	short turret_rate_of_fire;
	short turret_damage;
	/* how many times a head hit's damage the turret deals, in percent (0:
	stock, the game's own rule) */
	short turret_headshot;
	/* how far the passenger can turn in their seat, degrees (0: stock; 360:
	all the way round) */
	short passenger_aim_range;
	/* (0: none, the stock Warthog) */
	byte shield_layers;
	/* of the damage that still reaches a rider while the shields hold, the
	percentage the Warthog's shields take for them */
	byte rider_protection;
	/* tenths of a second after a hit before the shields recharge (0: the
	default, warthog_options.c's DEFAULT_RECHARGE_DELAY_TENTHS) */
	short shield_recharge_delay;
	/* each layer's shield points (a player's shields are 75) */
	short shield_amount[MAXIMUM_WARTHOG_SHIELD_LAYERS];
	/* tenths of a second for each layer to fill from empty (0: the default) */
	short shield_recharge_time[MAXIMUM_WARTHOG_SHIELD_LAYERS];
	/* percentage of each effect each kind loses while the shields hold (0:
	none, stock) */
	byte reduction[NUMBER_OF_WARTHOG_DAMAGE_KINDS][NUMBER_OF_WARTHOG_EFFECTS];
	byte pad[4];
};

#define HALO_PORT_WARTHOG_OPTIONS_SIZE 0x30

typedef char warthog_options_size_assert[sizeof(struct warthog_options) == HALO_PORT_WARTHOG_OPTIONS_SIZE ? 1 : -1];

/* ---------- prototypes/WARTHOG_OPTIONS.C */

/* the network game's options, before its map loads (network_game_manager.c);
NULL: stock */
void warthog_options_set(struct warthog_options const *options);
/* the host's own, from config.toml's warthog.* (Server Setup's) */
void warthog_options_from_config(struct warthog_options *options);
/* a map's tags have loaded (cache_files.c): the Warthogs' tuned */
void warthog_options_tags_loaded(void);
/* each tick (game_time.c, after the netcode's): the host's shields recharge
and go to its clients */
void warthog_options_tick(void);
/* (a client) the host's shields */
void warthog_options_handle_shields(void const *entries, short count);
unsigned short warthog_options_shields_entry_size(void);

#endif /* __HALO_WARTHOG_OPTIONS_H */
