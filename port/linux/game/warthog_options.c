/*
WARTHOG_OPTIONS.C

Server Setup's WARTHOG OPTIONS: the host's tuning of every Warthog for a
network game (the chaingun and rocket Warthogs alike: vehicle tags of the
jeep type named for the Warthog), and shields the stock Warthog lacks.
port/linux/include/halo_warthog_options.h has the record.

- Tuning. When a network game's map loads, every machine scales the
  Warthogs' tags by the host's choice (warthog_options_tags_loaded, as
  pal_tags.c tunes a PAL map's): acceleration, top speed, how sharply and
  fast they steer, how far the passenger turns, and their turrets' rate of
  fire and spread. A client predicts its own vehicle and shots from them, as
  the host checks them, so they must be alike: the host's choice comes in the
  game record, before the map loads. Turret damage and headshots the host
  deals alone (damage.c asks warthog_turret_damage_scale and
  warthog_turret_headshot_scale).
- Shields. Each Warthog has up to three layers of shield points; the
  outermost breaks first, and after a hit they recharge (after the delay)
  from the inside out, each layer at its own rate. Only the host keeps them
  (the clients deal no damage): it sends their points to the clients ten
  times a second for the riders' HUD. While a Warthog has shield points,
  of each kind of damage (bullets, explosives, plasma bullets, plasma
  explosives: warthog_damage_kind) the shields take less (the shield
  damage reduction), the Warthog is thrown less (knockback), and its riders
  are hurt and stunned less (pass-through); of what still reaches a rider,
  RIDER PROTECTION's part is taken by the shields instead.
  - A hit on the Warthog drains the shields, and what they cover does not
    reach its body (warthog_vehicle_absorb, in object_cause_damage's loop).
  - A hit on a rider (a bullet, a vehicle's pass-through, an explosion's
    splash) is reduced (warthog_rider_damage).
  - The knockback and stun the shields took travel with the host's damage
    events (network_damage.c), so a client's replay of them throws its
    predicted Warthog and stuns its player as the host's did: damage.c and
    units.c scale them by warthog_damage_acceleration_scale and
    warthog_damage_stun_scale, which the host sets around each object's
    aftermath and a client from the event.
- The view. A key (controls.vehicle_view) switches the driver's and the
  passenger's seat between first and third person, each remembered for that
  seat (director.c asks warthog_options_view_toggled). It is the local
  player's own; nothing of it is sent.
*/

#include "cseries.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/hud_messaging.h"
#include "items/weapon_definitions.h"
#include "objects/damage.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "units/unit_definitions.h"
#include "units/units.h"
#include "units/vehicle_definitions.h"
#include "units/vehicles.h"

#include "halo_keyboard.h"
#include "halo_warthog_options.h"
#include "network_distributed.h"
#include "warthog_options.h"

#include <stdio.h>
#include <string.h>

/* the platform layer's (port/linux/src) */
void platform_log(char const *format, ...);
long config_integer(char const *name);

/* ---------- constants */

enum
{
	/* the vehicle tag's fields this unit scales (vehicles.c's struct
	vehicle_definition, which it keeps to itself; pal_tags.c has the same) */
	/* (network_objects.c checks vehicle_type's offset too) */
	VEHICLE_TYPE_OFFSET = 0x2F4,
	VEHICLE_MAXIMUM_FORWARD_SPEED_OFFSET = 0x2F8,
	VEHICLE_SPEED_ACCELERATION_OFFSET = 0x300,
	VEHICLE_MAXIMUM_LEFT_TURN_OFFSET = 0x308,
	VEHICLE_MAXIMUM_RIGHT_TURN_OFFSET = 0x30C,
	VEHICLE_TURN_RATE_OFFSET = 0x314,
	/* (vehicles.c's _vehicle_type_human_jeep) */
	VEHICLE_TYPE_HUMAN_JEEP = 1,

	/* damage.c's damage categories */
	DAMAGE_CATEGORY_BULLET = 2,
	DAMAGE_CATEGORY_GRENADE = 3,
	DAMAGE_CATEGORY_HIGH_EXPLOSIVE = 4,
	DAMAGE_CATEGORY_SNIPER = 5,
	DAMAGE_CATEGORY_MELEE = 6,
	DAMAGE_CATEGORY_FLAME = 7,
	DAMAGE_CATEGORY_MOUNTED_WEAPON = 8,
	DAMAGE_CATEGORY_VEHICLE = 9,
	DAMAGE_CATEGORY_PLASMA = 10,
	DAMAGE_CATEGORY_NEEDLE = 11,
	DAMAGE_CATEGORY_SHOTGUN = 12,

	MAXIMUM_WARTHOG_TAGS = 16,
	MAXIMUM_WARTHOG_WEAPON_TAGS = 16,
	/* the Warthogs whose shields are kept at once */
	MAXIMUM_SHIELDED_WARTHOGS = 64,
	/* (the host) ticks between sends of the shields */
	SHIELDS_SEND_PERIOD = 3,
	/* shield points in the message, in eighths */
	SHIELD_POINTS_SCALE = 8,
	/* tenths of a second */
	DEFAULT_RECHARGE_DELAY_TENTHS = 30,
	DEFAULT_RECHARGE_TIME_TENTHS = 50,
};

/* the seats the view key switches */
enum
{
	_warthog_seat_driver,
	_warthog_seat_passenger,
	NUMBER_OF_WARTHOG_VIEW_SEATS
};

/* ---------- structures */

struct warthog_shield
{
	/* NONE: free */
	long object_index;
	real points[MAXIMUM_WARTHOG_SHIELD_LAYERS];
	/* the game time of its last hit */
	long hit_time;
};

/* (what goes on the wire: the host's shields, ten times a second) */
struct distributed_warthog_shields
{
	long object_index;
	word points[MAXIMUM_WARTHOG_SHIELD_LAYERS];
	word pad;
};

typedef char distributed_warthog_shields_size_assert[sizeof(struct distributed_warthog_shields) == 12 ? 1 : -1];

/* ---------- globals */

/* the scales of the knockback and stun of the damage being dealt or replayed
(damage.c, units.c): 1 but while a Warthog's shields took some of it */
real warthog_damage_acceleration_scale = 1.0f;
real warthog_damage_stun_scale = 1.0f;

static struct
{
	/* the game's options (all zeros: stock) */
	struct warthog_options options;
	boolean active;
	/* the loaded map's Warthogs */
	long tags[MAXIMUM_WARTHOG_TAGS];
	short tag_count;
	struct warthog_shield shields[MAXIMUM_SHIELDED_WARTHOGS];
	/* each local player's seats seen the other way, and the key last tick */
	boolean view_toggled[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS][NUMBER_OF_WARTHOG_VIEW_SEATS];
	boolean view_key_down;
} warthog_globals;

/* ---------- private code */

/* a percentage of stock (0: stock) as a scale */
static real percent_scale(
	short percent)
{
	return percent > 0 ? (real)percent / 100.0f : 1.0f;
}

/* a reduction's percentage as the scale left */
static real reduction_scale(
	byte percent)
{
	return percent >= 100 ? 0.0f : 1.0f - (real)percent / 100.0f;
}

static short shield_layers(
	void)
{
	return (short)MIN(warthog_globals.options.shield_layers, MAXIMUM_WARTHOG_SHIELD_LAYERS);
}

static real layer_maximum(
	short layer)
{
	return (real)MAX(warthog_globals.options.shield_amount[layer], 0);
}

static boolean shields_on(
	void)
{
	return warthog_globals.active && shield_layers() > 0;
}

static boolean tag_is_warthog(
	long definition_index)
{
	short index;

	for (index = 0; index < warthog_globals.tag_count; index++)
	{
		if (warthog_globals.tags[index] == definition_index)
			return TRUE;
	}
	return FALSE;
}

/* whether the object is one of the loaded map's Warthogs */
static boolean object_is_warthog(
	long object_index)
{
	struct object_datum *object;

	if (object_index == NONE || warthog_globals.tag_count == 0)
		return FALSE;
	object = (struct object_datum *)object_try_and_get_and_verify_type(object_index, _object_mask_vehicle);
	return object && tag_is_warthog(object->definition_index);
}

static void scale_real(
	void *field,
	real scale)
{
	real value;

	csmemcpy(&value, field, sizeof(value));
	value *= scale;
	csmemcpy(field, &value, sizeof(value));
}

static void tune_weapon(
	long weapon_index)
{
	struct weapon_definition *weapon = weapon_definition_get(weapon_index);
	real rate_of_fire = percent_scale(warthog_globals.options.turret_rate_of_fire);
	real spread = 1.0f / percent_scale(warthog_globals.options.turret_accuracy);
	long index;

	for (index = 0; index < weapon->weapon.triggers.count; index++)
	{
		struct weapon_trigger_definition *trigger = TAG_BLOCK_GET_ELEMENT(&weapon->weapon.triggers, index,
			struct weapon_trigger_definition);

		trigger->initial_rate_of_fire *= rate_of_fire;
		trigger->final_rate_of_fire *= rate_of_fire;
		trigger->projectile_error_inner_cone_angle *= spread;
		trigger->projectile_error_angle_lower_bound *= spread;
		trigger->projectile_error_angle_upper_bound *= spread;
	}
}

static void tune_vehicle(
	long vehicle_index,
	long *weapons,
	short *weapon_count)
{
	byte *vehicle = (byte *)tag_get(VEHICLE_DEFINITION_TAG, vehicle_index);
	struct unit_definition *unit = (struct unit_definition *)vehicle;
	struct warthog_options const *options = &warthog_globals.options;
	real steering = percent_scale(options->steering);
	long index;

	scale_real(vehicle + VEHICLE_SPEED_ACCELERATION_OFFSET, percent_scale(options->acceleration));
	scale_real(vehicle + VEHICLE_MAXIMUM_FORWARD_SPEED_OFFSET, percent_scale(options->top_speed));
	scale_real(vehicle + VEHICLE_MAXIMUM_LEFT_TURN_OFFSET, steering);
	scale_real(vehicle + VEHICLE_MAXIMUM_RIGHT_TURN_OFFSET, steering);
	scale_real(vehicle + VEHICLE_TURN_RATE_OFFSET, steering);

	/* the passenger's seats: neither the driver's nor the gunner's */
	if (options->passenger_aim_range > 0)
	{
		for (index = 0; index < unit->unit.seats.count; index++)
		{
			struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&unit->unit.seats, index, struct unit_seat);

			if (TEST_FLAG(seat->flags, _unit_seat_driver_bit) || TEST_FLAG(seat->flags, _unit_seat_gunner_bit))
				continue;
			if (options->passenger_aim_range >= 360)
			{
				/* (both 0: not clamped, player_control.c) */
				seat->yaw_minimum = 0.0f;
				seat->yaw_maximum = 0.0f;
			}
			else
			{
				real half = DEGREES_TO_RADIANS((real)options->passenger_aim_range) * 0.5f;

				seat->yaw_minimum = -half;
				seat->yaw_maximum = half;
			}
		}
	}

	/* its weapons (the turret), each once however many Warthogs carry it */
	for (index = 0; index < unit->unit.initial_weapons.count; index++)
	{
		struct tag_reference const *weapon = (struct tag_reference const *)
			((byte *)unit->unit.initial_weapons.address + index * 0x24);
		short known;

		if (weapon->index == NONE)
			continue;
		for (known = 0; known < *weapon_count; known++)
		{
			if (weapons[known] == weapon->index)
				break;
		}
		if (known < *weapon_count || *weapon_count >= MAXIMUM_WARTHOG_WEAPON_TAGS)
			continue;
		weapons[(*weapon_count)++] = weapon->index;
		tune_weapon(weapon->index);
	}
}

static struct warthog_shield *shield_find(
	long object_index)
{
	short index;

	for (index = 0; index < MAXIMUM_SHIELDED_WARTHOGS; index++)
	{
		if (warthog_globals.shields[index].object_index == object_index)
			return &warthog_globals.shields[index];
	}
	return NULL;
}

/* (the host) the Warthog's shields, new ones full */
static struct warthog_shield *shield_get(
	long object_index)
{
	struct warthog_shield *shield = shield_find(object_index);
	short layer;

	if (shield)
		return shield;
	shield = shield_find(NONE);
	if (!shield)
		return NULL;
	shield->object_index = object_index;
	shield->hit_time = NONE;
	for (layer = 0; layer < MAXIMUM_WARTHOG_SHIELD_LAYERS; layer++)
		shield->points[layer] = layer < shield_layers() ? layer_maximum(layer) : 0.0f;
	return shield;
}

static real shield_total(
	struct warthog_shield const *shield)
{
	real total = 0.0f;
	short layer;

	for (layer = 0; layer < shield_layers(); layer++)
		total += shield->points[layer];
	return total;
}

/* takes up to amount from the shields, the outermost layer first; what it took */
static real shield_drain(
	struct warthog_shield *shield,
	real amount)
{
	real taken = 0.0f;
	short layer;

	for (layer = 0; layer < shield_layers() && amount > 0.0f; layer++)
	{
		real part = MIN(amount, shield->points[layer]);

		shield->points[layer] -= part;
		amount -= part;
		taken += part;
	}
	if (taken > 0.0f)
		shield->hit_time = game_time_get();
	return taken;
}

static void shield_recharge(
	struct warthog_shield *shield)
{
	struct warthog_options const *options = &warthog_globals.options;
	long delay = (options->shield_recharge_delay > 0 ? options->shield_recharge_delay :
		DEFAULT_RECHARGE_DELAY_TENTHS) * TICKS_PER_SECOND / 10;
	short layer;

	if (shield->hit_time != NONE && game_time_get() - shield->hit_time < delay)
		return;
	/* the innermost layer not full */
	for (layer = shield_layers() - 1; layer >= 0; layer--)
	{
		real maximum = layer_maximum(layer);

		if (shield->points[layer] < maximum)
		{
			long tenths = options->shield_recharge_time[layer] > 0 ? options->shield_recharge_time[layer] :
				DEFAULT_RECHARGE_TIME_TENTHS;

			shield->points[layer] += maximum * 10.0f / ((real)tenths * TICKS_PER_SECOND);
			if (shield->points[layer] > maximum)
				shield->points[layer] = maximum;
			return;
		}
	}
}

static boolean host_deals_damage(
	void)
{
	return game_connection() == _game_connection_network_server;
}

static void shields_send(
	void)
{
	struct
	{
		struct distributed_message_header header;
		struct distributed_warthog_shields entries[MAXIMUM_SHIELDED_WARTHOGS];
	} message;
	short limit = MIN(MAXIMUM_SHIELDED_WARTHOGS, DATAGRAM_ENTRIES(struct distributed_warthog_shields));
	short count = 0;
	short index;

	for (index = 0; index < MAXIMUM_SHIELDED_WARTHOGS && count < limit; index++)
	{
		struct warthog_shield const *shield = &warthog_globals.shields[index];
		struct distributed_warthog_shields *entry = &message.entries[count];
		short layer;

		if (shield->object_index == NONE)
			continue;
		csmemset(entry, 0, sizeof(*entry));
		entry->object_index = shield->object_index;
		for (layer = 0; layer < MAXIMUM_WARTHOG_SHIELD_LAYERS; layer++)
		{
			real points = PIN(shield->points[layer], 0.0f, 65535.0f / SHIELD_POINTS_SCALE);

			entry->points[layer] = (word)(points * SHIELD_POINTS_SCALE + 0.5f);
		}
		count++;
	}
	if (count > 0)
	{
		distributed_send(&message, _distributed_message_warthog_shields, count,
			(word)(sizeof(message.header) + count * sizeof(message.entries[0])), _distributed_to_clients);
	}
}

/* the seat the local player's unit rides in a Warthog, NONE for neither of
the view's */
static short warthog_view_seat(
	long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct unit_definition *definition;
	struct unit_seat *seat;

	if (!unit || !object_is_warthog(unit->object.parent_object_index) || unit->unit.parent_seat_index == NONE)
		return NONE;
	definition = unit_definition_get(object_get(unit->object.parent_object_index)->definition_index);
	if (unit->unit.parent_seat_index < 0 || unit->unit.parent_seat_index >= definition->unit.seats.count)
		return NONE;
	seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, unit->unit.parent_seat_index, struct unit_seat);
	if (TEST_FLAG(seat->flags, _unit_seat_gunner_bit))
		return NONE;
	return TEST_FLAG(seat->flags, _unit_seat_driver_bit) ? _warthog_seat_driver : _warthog_seat_passenger;
}

static void view_key_tick(
	void)
{
	boolean down = (halo_keyboard_actions(0) & (1UL << HALO_KEYBOARD_VEHICLE_VIEW)) != 0;
	long player_index = local_player_get_player_index(0);

	if (down && !warthog_globals.view_key_down && player_index != NONE)
	{
		short seat = warthog_view_seat(player_get(player_index)->unit_index);

		if (seat != NONE)
			warthog_globals.view_toggled[0][seat] = !warthog_globals.view_toggled[0][seat];
	}
	warthog_globals.view_key_down = down;
}

/* ---------- public code */

void warthog_options_set(
	struct warthog_options const *options)
{
	short index;

	csmemset(&warthog_globals.options, 0, sizeof(warthog_globals.options));
	warthog_globals.active = options != NULL;
	if (options)
	{
		struct warthog_options *kept = &warthog_globals.options;
		short layer;
		short kind;
		short effect;

		/* (the host's, bounded as its menus bound them: a host's record is
		untrusted) */
		*kept = *options;
		kept->acceleration = (short)PIN(kept->acceleration, 0, 1000);
		kept->top_speed = (short)PIN(kept->top_speed, 0, 1000);
		kept->steering = (short)PIN(kept->steering, 0, 1000);
		kept->turret_accuracy = (short)PIN(kept->turret_accuracy, 0, 1000);
		kept->turret_rate_of_fire = (short)PIN(kept->turret_rate_of_fire, 0, 1000);
		kept->turret_damage = (short)PIN(kept->turret_damage, 0, 1000);
		kept->turret_headshot = (short)PIN(kept->turret_headshot, 0, 1000);
		kept->passenger_aim_range = (short)PIN(kept->passenger_aim_range, 0, 360);
		kept->shield_layers = (byte)MIN(kept->shield_layers, MAXIMUM_WARTHOG_SHIELD_LAYERS);
		kept->rider_protection = (byte)MIN(kept->rider_protection, 100);
		kept->shield_recharge_delay = (short)PIN(kept->shield_recharge_delay, 0, 600);
		for (layer = 0; layer < MAXIMUM_WARTHOG_SHIELD_LAYERS; layer++)
		{
			kept->shield_amount[layer] = (short)PIN(kept->shield_amount[layer], 0, 5000);
			kept->shield_recharge_time[layer] = (short)PIN(kept->shield_recharge_time[layer], 0, 6000);
		}
		for (kind = 0; kind < NUMBER_OF_WARTHOG_DAMAGE_KINDS; kind++)
		{
			for (effect = 0; effect < NUMBER_OF_WARTHOG_EFFECTS; effect++)
				kept->reduction[kind][effect] = (byte)MIN(kept->reduction[kind][effect], 100);
		}
	}
	for (index = 0; index < MAXIMUM_SHIELDED_WARTHOGS; index++)
		warthog_globals.shields[index].object_index = NONE;
}

void warthog_options_from_config(
	struct warthog_options *options)
{
	static char const *const kinds[NUMBER_OF_WARTHOG_DAMAGE_KINDS] =
	{
		"bullets", "explosives", "plasma_bullets", "plasma_explosives"
	};
	static char const *const effects[NUMBER_OF_WARTHOG_EFFECTS] =
	{
		"shield", "knockback", "pass_through"
	};
	char name[64];
	short kind;
	short effect;
	short layer;

	csmemset(options, 0, sizeof(*options));
	options->acceleration = (short)PIN(config_integer("warthog.acceleration"), 0, 1000);
	options->top_speed = (short)PIN(config_integer("warthog.top_speed"), 0, 1000);
	options->steering = (short)PIN(config_integer("warthog.steering"), 0, 1000);
	options->passenger_aim_range = (short)PIN(config_integer("warthog.passenger_aim_range"), 0, 360);
	options->turret_accuracy = (short)PIN(config_integer("warthog.turret_accuracy"), 0, 1000);
	options->turret_rate_of_fire = (short)PIN(config_integer("warthog.turret_rate_of_fire"), 0, 1000);
	options->turret_damage = (short)PIN(config_integer("warthog.turret_damage"), 0, 1000);
	options->turret_headshot = (short)PIN(config_integer("warthog.turret_headshot"), 0, 1000);
	options->shield_layers = (byte)PIN(config_integer("warthog.shield_layers"), 0, MAXIMUM_WARTHOG_SHIELD_LAYERS);
	options->rider_protection = (byte)PIN(config_integer("warthog.rider_protection"), 0, 100);
	options->shield_recharge_delay = (short)PIN(config_integer("warthog.shield_recharge_delay") * 10, 0, 600);
	for (layer = 0; layer < MAXIMUM_WARTHOG_SHIELD_LAYERS; layer++)
	{
		snprintf(name, sizeof(name), "warthog.shield_amount_%d", layer + 1);
		options->shield_amount[layer] = (short)PIN(config_integer(name), 0, 5000);
		snprintf(name, sizeof(name), "warthog.shield_recharge_%d", layer + 1);
		options->shield_recharge_time[layer] = (short)PIN(config_integer(name) * 10, 0, 6000);
	}
	for (kind = 0; kind < NUMBER_OF_WARTHOG_DAMAGE_KINDS; kind++)
	{
		for (effect = 0; effect < NUMBER_OF_WARTHOG_EFFECTS; effect++)
		{
			snprintf(name, sizeof(name), "warthog.%s_%s", kinds[kind], effects[effect]);
			options->reduction[kind][effect] = (byte)PIN(config_integer(name), 0, 100);
		}
	}
}

void warthog_options_tags_loaded(
	void)
{
	struct tag_iterator iterator;
	long weapons[MAXIMUM_WARTHOG_WEAPON_TAGS];
	short weapon_count = 0;
	long tag_index;

	warthog_globals.tag_count = 0;
	/* (only a network game has the host's options; any other plays stock) */
	if (game_connection() != _game_connection_network_server &&
		game_connection() != _game_connection_network_client)
	{
		warthog_options_set(NULL);
	}
	tag_iterator_new(&iterator, VEHICLE_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE &&
		warthog_globals.tag_count < MAXIMUM_WARTHOG_TAGS)
	{
		byte const *vehicle = (byte const *)tag_get(VEHICLE_DEFINITION_TAG, tag_index);
		short type;
		char const *name = tag_get_name(tag_index);

		csmemcpy(&type, vehicle + VEHICLE_TYPE_OFFSET, sizeof(type));
		if (type != VEHICLE_TYPE_HUMAN_JEEP || !name || !strstr(name, "warthog"))
			continue;
		warthog_globals.tags[warthog_globals.tag_count++] = tag_index;
		if (warthog_globals.active)
			tune_vehicle(tag_index, weapons, &weapon_count);
	}
	if (warthog_globals.active)
	{
		platform_log("warthog options: %d Warthogs and %d of their weapons tuned, %d shield layers",
			(int)warthog_globals.tag_count, (int)weapon_count, (int)shield_layers());
	}
}

void warthog_options_tick(
	void)
{
	view_key_tick();
	if (!shields_on() || !host_deals_damage())
		return;
	{
		struct object_iterator iterator;
		short index;

		/* the shields of Warthogs gone are let go */
		for (index = 0; index < MAXIMUM_SHIELDED_WARTHOGS; index++)
		{
			struct warthog_shield *shield = &warthog_globals.shields[index];

			if (shield->object_index != NONE && !object_is_warthog(shield->object_index))
				shield->object_index = NONE;
		}
		object_iterator_new(&iterator, _object_mask_vehicle, 0);
		while (object_iterator_next(&iterator))
		{
			struct warthog_shield *shield;

			if (!object_is_warthog(iterator.index))
				continue;
			shield = shield_get(iterator.index);
			if (shield)
				shield_recharge(shield);
		}
		if (game_time_get() % SHIELDS_SEND_PERIOD == 0)
			shields_send();
	}
}

void warthog_options_handle_shields(
	void const *entries,
	short count)
{
	struct distributed_warthog_shields const *shields = (struct distributed_warthog_shields const *)entries;
	short index;

	if (!shields_on())
		return;
	/* (the host's list is all of them: those it left out are gone) */
	for (index = 0; index < MAXIMUM_SHIELDED_WARTHOGS; index++)
		warthog_globals.shields[index].object_index = NONE;
	for (index = 0; index < count && index < MAXIMUM_SHIELDED_WARTHOGS; index++)
	{
		struct warthog_shield *shield = &warthog_globals.shields[index];
		short layer;

		if (!distributed_object_index_valid(shields[index].object_index) ||
			!object_is_warthog(shields[index].object_index))
		{
			continue;
		}
		shield->object_index = shields[index].object_index;
		for (layer = 0; layer < MAXIMUM_WARTHOG_SHIELD_LAYERS; layer++)
		{
			real points = (real)shields[index].points[layer] / SHIELD_POINTS_SCALE;

			shield->points[layer] = layer < shield_layers() ? MIN(points, layer_maximum(layer)) : 0.0f;
		}
	}
}

unsigned short warthog_options_shields_entry_size(
	void)
{
	return sizeof(struct distributed_warthog_shields);
}

short warthog_damage_kind(
	struct damage_data const *damage,
	short category)
{
	char const *name;

	switch (category)
	{
	case DAMAGE_CATEGORY_BULLET:
	case DAMAGE_CATEGORY_SNIPER:
	case DAMAGE_CATEGORY_SHOTGUN:
	case DAMAGE_CATEGORY_MOUNTED_WEAPON:
		return _warthog_damage_bullets;
	case DAMAGE_CATEGORY_PLASMA:
	case DAMAGE_CATEGORY_NEEDLE:
		return _warthog_damage_plasma_bullets;
	case DAMAGE_CATEGORY_GRENADE:
	case DAMAGE_CATEGORY_HIGH_EXPLOSIVE:
	case DAMAGE_CATEGORY_FLAME:
		/* (the Covenant's explosions by their tags' names: the plasma
		grenade's, the fuel rod's, the Banshee's) */
		name = tag_get_name(damage->definition_index);
		if (name && (strstr(name, "plasma") || strstr(name, "fuel rod") || strstr(name, "fuel_rod") ||
			strstr(name, "banshee")))
		{
			return _warthog_damage_plasma_explosives;
		}
		return _warthog_damage_explosives;
	default:
		/* (melee, collisions, falling: as the stock game has them) */
		return NONE;
	}
}

boolean warthog_damage_from_turret(
	struct damage_data const *damage,
	short category)
{
	struct unit_datum *owner;

	if (!warthog_globals.active || damage->owner_object_index == NONE ||
		category == DAMAGE_CATEGORY_MELEE || category == DAMAGE_CATEGORY_GRENADE ||
		category == DAMAGE_CATEGORY_VEHICLE)
	{
		return FALSE;
	}
	/* (the Warthog's own while someone mans its turret: not, say, its
	explosion once it is empty) */
	if (object_is_warthog(damage->owner_object_index))
		return unit_get(damage->owner_object_index)->unit.gunner_object_index != NONE;
	/* (a gunner's own: the unit in a Warthog's gunner seat) */
	owner = unit_try_and_get(damage->owner_object_index);
	if (owner && object_is_warthog(owner->object.parent_object_index) && owner->unit.parent_seat_index != NONE)
	{
		struct unit_definition *definition =
			unit_definition_get(object_get(owner->object.parent_object_index)->definition_index);

		if (owner->unit.parent_seat_index < definition->unit.seats.count)
		{
			struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, owner->unit.parent_seat_index,
				struct unit_seat);

			return TEST_FLAG(seat->flags, _unit_seat_gunner_bit);
		}
	}
	return FALSE;
}

real warthog_turret_damage_scale(
	struct damage_data const *damage,
	short category)
{
	if (warthog_globals.options.turret_damage <= 0 || !warthog_damage_from_turret(damage, category))
		return 1.0f;
	return percent_scale(warthog_globals.options.turret_damage);
}

real warthog_turret_headshot_scale(
	struct damage_data const *damage,
	short category)
{
	if (warthog_globals.options.turret_headshot <= 0 || !game_engine_running() ||
		!warthog_damage_from_turret(damage, category))
	{
		return 0.0f;
	}
	return (real)warthog_globals.options.turret_headshot / 100.0f;
}

void warthog_vehicle_absorb(
	long object_index,
	struct damage_data const *damage,
	short category,
	real *total_damage,
	real *acceleration_scale)
{
	struct warthog_shield *shield;
	short kind;
	real available;
	real needed;
	real scale;

	if (!shields_on() || !host_deals_damage() || !object_is_warthog(object_index))
		return;
	shield = shield_get(object_index);
	if (!shield || shield_total(shield) <= 0.0f)
		return;
	kind = warthog_damage_kind(damage, category);
	/* (melee, collisions, falls: as the stock game has them) */
	if (kind == NONE)
		return;
	scale = reduction_scale(warthog_globals.options.reduction[kind][_warthog_effect_shield_damage]);
	*acceleration_scale *= reduction_scale(warthog_globals.options.reduction[kind][_warthog_effect_knockback]);
	if (*total_damage <= 0.0f)
		return;
	/* the shields cover the hit (its reduced part drained) as far as they
	have points; the rest reaches the body */
	needed = *total_damage * scale;
	if (needed <= 0.0f)
	{
		shield->hit_time = game_time_get();
		*total_damage = 0.0f;
		return;
	}
	available = shield_total(shield);
	if (available >= needed)
	{
		shield_drain(shield, needed);
		*total_damage = 0.0f;
	}
	else
	{
		shield_drain(shield, available);
		*total_damage *= 1.0f - available / needed;
	}
}

void warthog_rider_damage(
	long object_index,
	struct damage_data const *damage,
	short category,
	real *total_damage,
	real *stun_scale)
{
	struct object_datum *rider;
	struct warthog_shield *shield;
	short kind;
	real through;
	real protection;
	real soaked;

	if (!shields_on() || !host_deals_damage())
		return;
	rider = (struct object_datum *)object_try_and_get_and_verify_type(object_index, _object_mask_biped);
	if (!rider || !object_is_warthog(rider->object.parent_object_index))
		return;
	shield = shield_get(rider->object.parent_object_index);
	if (!shield || shield_total(shield) <= 0.0f)
		return;
	kind = warthog_damage_kind(damage, category);
	/* (melee, collisions, falls: as the stock game has them) */
	if (kind == NONE)
		return;
	through = reduction_scale(warthog_globals.options.reduction[kind][_warthog_effect_pass_through]);
	protection = (real)MIN(warthog_globals.options.rider_protection, 100) / 100.0f;
	*stun_scale *= through * (1.0f - protection);
	if (*total_damage <= 0.0f)
		return;
	*total_damage *= through;
	/* of what is left, RIDER PROTECTION's part the shields take, as far as
	they have points */
	soaked = shield_drain(shield, *total_damage * protection);
	*total_damage -= soaked;
	if (*total_damage < 0.0f)
		*total_damage = 0.0f;
}

boolean warthog_options_view_toggled(
	long unit_index)
{
	short local_player_index;
	struct unit_datum *unit = unit_try_and_get(unit_index);
	short seat;

	if (!unit || unit->unit.player_index == NONE)
		return FALSE;
	seat = warthog_view_seat(unit_index);
	if (seat == NONE)
		return FALSE;
	for (local_player_index = 0; local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; local_player_index++)
	{
		if (local_player_get_player_index(local_player_index) == unit->unit.player_index)
			return warthog_globals.view_toggled[local_player_index][seat];
	}
	return FALSE;
}

void warthog_options_draw(
	short local_player_index)
{
	long player_index = local_player_get_player_index(local_player_index);
	struct unit_datum *unit;
	struct warthog_shield const *shield;
	long font_index;
	struct font_header *font;
	real_argb_color color;
	rectangle2d bounds;
	wchar_t text[96];
	short length = 0;
	short height;
	short layer;
	boolean depleted = TRUE;

	if (!shields_on() || player_index == NONE)
		return;
	unit = unit_try_and_get(player_get(player_index)->unit_index);
	if (!unit || !object_is_warthog(unit->object.parent_object_index))
		return;
	shield = shield_find(unit->object.parent_object_index);
	if (!shield)
		return;
	font_index = hud_get_font_index();
	if (font_index == NONE)
		return;

	/* WARTHOG SHIELD [||||||||  ][||||||||||] : each layer's bar of ten,
	the outermost first */
	{
		static wchar_t const label[] = L"WARTHOG SHIELD  ";
		short index;

		for (index = 0; label[index] && length < NUMBEROF(text) - 1; index++)
			text[length++] = label[index];
	}
	for (layer = 0; layer < shield_layers(); layer++)
	{
		real maximum = layer_maximum(layer);
		short filled = maximum > 0.0f ? (short)PIN(shield->points[layer] * 10.0f / maximum + 0.5f, 0.0f, 10.0f) : 0;
		short bar;

		if (shield->points[layer] > 0.0f)
			depleted = FALSE;
		if (length + 13 >= NUMBEROF(text))
			break;
		text[length++] = L'[';
		for (bar = 0; bar < 10; bar++)
			text[length++] = bar < filled ? L'|' : L' ';
		text[length++] = L']';
	}
	text[length] = 0;

	font = font_definition_get(font_index);
	height = (short)(font->ascending_height + font->descending_height);
	bounds = render.camera.window_bounds;
	bounds.y0 = (short)(bounds.y1 - 4 * height - 48);
	bounds.y1 = (short)(bounds.y1 - 3 * height - 48);
	color = depleted ? *global_real_argb_red : *global_real_argb_lightblue;
	/* draw mode 2: centered */
	draw_string_set_draw_mode(font_index, NONE, 2, 0, &color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
}
