/*
WARTHOG_SHIELDS.C (test)

The real shields of port/linux/game/warthog_options.c (Server Setup's
WARTHOG OPTIONS): their layers drained from the outside in and recharged
from the inside out, each at its own rate after the delay; what they take of
a hit on the Warthog and of the knockback; and what they spare its riders.
test_warthog_shields.py takes the record, the constants and the code.
*/

#include "harness.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(n, floor, ceiling) ((n) < (floor) ? (floor) : MIN((n), (ceiling)))
#define csmemset memset
#define TICKS_PER_SECOND 30
#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
#define _object_mask_biped 1

typedef unsigned short word;

#include "config.inc"

/* the fake world: object 1 is a Warthog, object 2 its rider, object 3 a
rider of nothing */
enum { WARTHOG = 1, RIDER = 2, WALKER = 3 };

struct object_datum
{
	struct
	{
		long parent_object_index;
	} object;
};

struct damage_data
{
	int unused;
};

static long game_time;
static struct object_datum objects[4] = { { { NONE } }, { { NONE } }, { { WARTHOG } }, { { NONE } } };

static long game_time_get(void)
{
	return game_time;
}

static boolean object_is_warthog(long object_index)
{
	return object_index == WARTHOG;
}

static boolean host_deals_damage(void)
{
	return TRUE;
}

static void *object_try_and_get_and_verify_type(long object_index, int mask)
{
	(void)mask;
	return object_index == RIDER || object_index == WALKER ? &objects[object_index] : NULL;
}

/* (the test's categories are the kinds themselves) */
static short warthog_damage_kind(struct damage_data const *damage, short category)
{
	(void)damage;
	return category;
}

#include "under_test.inc"

static void options(short layers, short amount0, short amount1, short amount2)
{
	memset(&warthog_globals, 0, sizeof(warthog_globals));
	warthog_globals.active = TRUE;
	warthog_globals.options.shield_layers = (byte)layers;
	warthog_globals.options.shield_amount[0] = amount0;
	warthog_globals.options.shield_amount[1] = amount1;
	warthog_globals.options.shield_amount[2] = amount2;
	{
		short index;

		for (index = 0; index < MAXIMUM_SHIELDED_WARTHOGS; index++)
			warthog_globals.shields[index].object_index = NONE;
	}
	game_time = 1000;
}

static int near(real a, real b)
{
	return a - b < 0.01f && b - a < 0.01f;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct damage_data damage = { 0 };
	struct warthog_shield *shield;
	real total;
	real scale;
	long tick;

	/* a new Warthog's shields are full; a hit takes the outermost first */
	CASE("drain-outer-first")
	{
		options(2, 100, 50, 0);
		shield = shield_get(WARTHOG);
		CHECK(shield && near(shield->points[0], 100.0f) && near(shield->points[1], 50.0f), "full at first");
		CHECK(near(shield_drain(shield, 120.0f), 120.0f), "took all of it");
		CHECK(near(shield->points[0], 0.0f) && near(shield->points[1], 30.0f),
			"outer %f inner %f, not 0 and 30", shield->points[0], shield->points[1]);
		CHECK(near(shield_drain(shield, 100.0f), 30.0f), "took only what was left");
	}
	/* the shields cover a hit, at its reduction, and cut the knockback */
	CASE("absorb-covers-hit")
	{
		options(1, 100, 0, 0);
		warthog_globals.options.reduction[_warthog_damage_bullets][_warthog_effect_shield_damage] = 50;
		warthog_globals.options.reduction[_warthog_damage_bullets][_warthog_effect_knockback] = 75;
		total = 40.0f;
		scale = 1.0f;
		warthog_vehicle_absorb(WARTHOG, &damage, _warthog_damage_bullets, &total, &scale);
		CHECK(near(total, 0.0f), "%f reached the body", total);
		CHECK(near(scale, 0.25f), "knockback scale %f, not 0.25", scale);
		CHECK(near(shield_find(WARTHOG)->points[0], 80.0f), "shield %f, not 80", shield_find(WARTHOG)->points[0]);
	}
	/* what the shields cannot cover reaches the body */
	CASE("absorb-overflow")
	{
		options(1, 10, 0, 0);
		total = 40.0f;
		scale = 1.0f;
		warthog_vehicle_absorb(WARTHOG, &damage, _warthog_damage_explosives, &total, &scale);
		CHECK(near(total, 30.0f), "%f reached the body, not 30", total);
		CHECK(near(shield_find(WARTHOG)->points[0], 0.0f), "shield not emptied");
		/* ... and with them empty, the game's own knockback */
		total = 40.0f;
		scale = 1.0f;
		warthog_globals.options.reduction[_warthog_damage_explosives][_warthog_effect_knockback] = 100;
		warthog_vehicle_absorb(WARTHOG, &damage, _warthog_damage_explosives, &total, &scale);
		CHECK(near(total, 40.0f) && near(scale, 1.0f), "empty shields still took %f and scaled %f", 40.0f - total, scale);
	}
	/* a kind the shields are immune to takes none of their points */
	CASE("immune")
	{
		options(1, 50, 0, 0);
		warthog_globals.options.reduction[_warthog_damage_plasma_bullets][_warthog_effect_shield_damage] = 100;
		total = 500.0f;
		scale = 1.0f;
		warthog_vehicle_absorb(WARTHOG, &damage, _warthog_damage_plasma_bullets, &total, &scale);
		CHECK(near(total, 0.0f) && near(shield_find(WARTHOG)->points[0], 50.0f), "immune shields lost points");
	}
	/* after the delay, the innermost layer not full recharges first, at its
	own rate; none before the delay */
	CASE("recharge-inside-out")
	{
		options(3, 30, 30, 30);
		warthog_globals.options.shield_recharge_delay = 10;	/* a second */
		warthog_globals.options.shield_recharge_time[2] = 10;	/* a second to fill */
		warthog_globals.options.shield_recharge_time[1] = 20;
		shield = shield_get(WARTHOG);
		shield_drain(shield, 90.0f);
		for (tick = 0; tick < TICKS_PER_SECOND - 1; tick++, game_time++)
			shield_recharge(shield);
		CHECK(near(shield_total(shield), 0.0f), "recharged %f before the delay", shield_total(shield));
		game_time++;
		for (tick = 0; tick < TICKS_PER_SECOND; tick++, game_time++)
			shield_recharge(shield);
		CHECK(near(shield->points[2], 30.0f) && near(shield->points[1], 0.0f) && near(shield->points[0], 0.0f),
			"after a second: %f %f %f, not 0 0 30", shield->points[0], shield->points[1], shield->points[2]);
		for (tick = 0; tick < TICKS_PER_SECOND; tick++, game_time++)
			shield_recharge(shield);
		CHECK(near(shield->points[1], 15.0f) && near(shield->points[0], 0.0f),
			"the middle layer at its own rate: %f, not 15", shield->points[1]);
	}
	/* a rider: the pass-through reduction, then RIDER PROTECTION's part taken
	by the shields, and the stun scaled by both */
	CASE("rider-pass-through-and-protection")
	{
		options(1, 100, 0, 0);
		warthog_globals.options.reduction[_warthog_damage_bullets][_warthog_effect_pass_through] = 50;
		warthog_globals.options.rider_protection = 50;
		total = 100.0f;
		scale = 1.0f;
		warthog_rider_damage(RIDER, &damage, _warthog_damage_bullets, &total, &scale);
		CHECK(near(total, 25.0f), "rider took %f, not 25", total);
		CHECK(near(scale, 0.25f), "stun scale %f, not 0.25", scale);
		CHECK(near(shield_find(WARTHOG)->points[0], 75.0f), "the shields took %f, not 25",
			100.0f - shield_find(WARTHOG)->points[0]);
	}
	/* the shields take for a rider only what points they have */
	CASE("rider-protection-limited")
	{
		options(1, 10, 0, 0);
		warthog_globals.options.rider_protection = 100;
		total = 100.0f;
		scale = 1.0f;
		warthog_rider_damage(RIDER, &damage, _warthog_damage_explosives, &total, &scale);
		CHECK(near(total, 90.0f), "rider took %f, not 90", total);
		/* nobody else's rider is spared */
		total = 100.0f;
		warthog_rider_damage(WALKER, &damage, _warthog_damage_explosives, &total, &scale);
		CHECK(near(total, 100.0f), "a rider of nothing took %f", total);
	}
	/* no shields: the stock game */
	CASE("no-shields-stock")
	{
		options(0, 100, 100, 100);
		warthog_globals.options.reduction[_warthog_damage_bullets][_warthog_effect_knockback] = 100;
		warthog_globals.options.rider_protection = 100;
		total = 40.0f;
		scale = 1.0f;
		warthog_vehicle_absorb(WARTHOG, &damage, _warthog_damage_bullets, &total, &scale);
		CHECK(near(total, 40.0f) && near(scale, 1.0f), "no shields changed the hit");
		warthog_rider_damage(RIDER, &damage, _warthog_damage_bullets, &total, &scale);
		CHECK(near(total, 40.0f) && near(scale, 1.0f), "no shields spared the rider");
	}
	return 0;
}
