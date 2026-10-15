"""The real shields of warthog_options.c (Server Setup's WARTHOG OPTIONS): their layers drained from the outside in
and recharged from the inside out after the delay, what they take of a hit on the Warthog and of its knockback, and
what they spare its riders."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure  # noqa: E402

SOURCE = "port/linux/game/warthog_options.c"
CASES = ["drain-outer-first", "absorb-covers-hit", "absorb-overflow", "immune", "recharge-inside-out",
         "rider-pass-through-and-protection", "rider-protection-limited", "no-shields-stock"]
FUNCTIONS = ("percent_scale", "reduction_scale", "shield_layers", "layer_maximum", "shields_on", "shield_find",
             "shield_get", "shield_total", "shield_drain", "shield_recharge", "warthog_vehicle_absorb",
             "warthog_rider_damage")

# a fault in warthog_options.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "drained-inside-out": (("for (layer = 0; layer < shield_layers() && amount > 0.0f; layer++)",
                            "for (layer = shield_layers() - 1; layer >= 0 && amount > 0.0f; layer--)"),
                           "shield_drain", "drain-outer-first"),
    "recharged-outside-in": (("for (layer = shield_layers() - 1; layer >= 0; layer--)",
                              "for (layer = 0; layer < shield_layers(); layer++)"),
                             "shield_recharge", "recharge-inside-out"),
    "no-recharge-delay": (("if (shield->hit_time != NONE && game_time_get() - shield->hit_time < delay)",
                           "if (0)"), "shield_recharge", "recharge-inside-out"),
    "overflow-lost": (("*total_damage *= 1.0f - available / needed;", "*total_damage = 0.0f;"),
                      "warthog_vehicle_absorb", "absorb-overflow"),
    "knockback-ignored": (("*acceleration_scale *= reduction_scale", "(void)reduction_scale"),
                          "warthog_vehicle_absorb", "absorb-covers-hit"),
    "protection-unbounded": (("soaked = shield_drain(shield, *total_damage * protection);",
                              "soaked = *total_damage * protection;\n\tshield_drain(shield, soaked);"),
                             "warthog_rider_damage", "rider-protection-limited"),
    "stun-unscaled": (("*stun_scale *= through * (1.0f - protection);", "(void)through;"),
                      "warthog_rider_damage", "rider-pass-through-and-protection"),
}


def generated(fault=None, faulty_function=None):
    source = read(SOURCE)
    header = read("port/linux/include/halo_warthog_options.h")
    config = ""
    for enum in re.findall(r"^enum\n\{.*?^\};\n", header, re.M | re.S):
        config += enum
    config += structure(header, "warthog_options") + "\n"
    for name in ("MAXIMUM_WARTHOG_TAGS", "MAXIMUM_SHIELDED_WARTHOGS", "DEFAULT_RECHARGE_DELAY_TENTHS",
                 "DEFAULT_RECHARGE_TIME_TENTHS"):
        config += f"#define {name} {constant(source, name)}\n"
    match = re.search(r"^enum\n\{\n\t_warthog_seat_driver.*?^\};\n", source, re.M | re.S)
    assert match, "the view's seats not found in warthog_options.c"
    config += match.group(0)
    config += structure(source, "warthog_shield") + "\n"
    match = re.search(r"^static struct\n\{\n\t/\* the game's options.*?^\} warthog_globals;\n", source, re.M | re.S)
    assert match, "warthog_globals not found in warthog_options.c"
    config += match.group(0)
    text = ""
    for name in FUNCTIONS:
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("warthog_shields", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("warthog_shields", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"warthog_options.c with a fault ({control}) passed '{case}': the test cannot see it"
