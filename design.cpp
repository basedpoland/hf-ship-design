#undef NDEBUG
#include "part.hpp"
#include "part-list.hpp"
#include "state.hpp"
#include "cmdline.hpp"
#include "osdefs.hpp"
#include "log.hpp"

#include "getopt.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace hf::design {

bool report(const state& st, const cmdline& params, int k);

static bool add_gun(state& st, const cmdline& params, const char* str)
{
    char buf[128 + 2] = { 'g', '_', '\0' };
    if (strlen(str) >= sizeof(buf))
        return false;
    int count = 0;
    int ret = sscanf(str, "%d:%127s", &count, buf+2);
    buf[sizeof(buf)-1] = '\0';
    if (ret != 2 || count <= 0)
    {
        ERR("wrong gun specification -- '%s'", str);
        params.seek_help();
        return false;
    }

    const part& p = maybe_part(buf);
    if (p == null_part)
    {
        ERR("no such gun -- '%s'", buf + 2);
        return false;
    }
    if (p.ammo >= 0)
    {
        ERR("part not a gun -- '%s'", str);
        return false;
    }
    add_part(st, p, count);
    int ammo = -p.ammo * count;
    int ammo_big = ammo / 2, ammo_small = ammo % 2;
    add_part(st, ammo_2x2, ammo_big);
    add_part(st, ammo_1x2, ammo_small);

    return true;
}

void add_fixed(state& st, const cmdline& params)
{
    constexpr int min_for_single_piece = 4;

    if (params.fixed_engine_count < min_for_single_piece ||
        params.fixed_engine_count % 2 != 0)
    {
        add_part(st, chassis_2, 2);
        add_part_(st, chassis_1, 2, area_mode::disabled);
    }
    else
    {
        add_part(st, e_d30s, params.fixed_engine_count);
        add_part(st, chassis_2, 1); // gear connected to corner piece
        add_part_(st, chassis_2, 5, area_mode::disabled); // connected to other gear
        add_part_(st, chassis_1, 2, area_mode::disabled); // small legs for landing stability
    }
}

void add_fuel(state& st, const cmdline& params)
{
    assert(st.fuel_flow > 1e-6f);
    // TODO big tank usage
    int num_tanks = (int)std::ceil(st.fuel_flow * params.min_combat_time / tank_1x2.fuel);
    int sneaky_tanks = std::min(st.sneaky_corners_left / 2, num_tanks); // use the cornerless 2x2 pieces to stick in extra tanks
    num_tanks -= sneaky_tanks;
    st.sneaky_corners_left -= sneaky_tanks*2;
    assert(sneaky_tanks >= 0); assert(num_tanks >= 0);
    add_part(st, tank_1x2, num_tanks);
    add_part_(st, tank_1x2, sneaky_tanks, area_mode::disabled);
    add_part_(st, h_05, sneaky_tanks*2, area_mode::disabled);

    add_part(st, fire, params.num_extinguishers);
}

void add_power(state& st)
{
    float power = -st.power;
    assert(power > 1e-6f);
    float x = std::fmod(power, pwr_2x2.power);
    if (x <= 2*pwr_1x2.power) // they weigh less than the full generator
    {
        int small_gens = x > pwr_1x2.power ? 2 : 1;
        add_part(st, pwr_1x2, small_gens);
        power = std::max(0.f, power - pwr_1x2.power*small_gens);
    }
    int big_gens = (int)std::ceil((power + 1e-6f) / pwr_2x2.power);
    add_part(st, pwr_2x2, big_gens);
}

void add_armor(state& st, const cmdline& params)
{
    if (params.armor_layers < 1e-6f)
        return;

    float circumference = std::sqrt((float)st.area) * 4;
    const part* static_engines[] = { &e_d30s };
    for (const auto* part : static_engines)
    {
        const auto& hull = part_to_hull(*part);
        int sz = std::abs(hull.size);
        assert(hull != null_part);
        assert(hull != h_null);
        assert(sz >= 1);
        circumference -= std::sqrt((float)sz);
    }
    assert(circumference > 0);
    int num_armor = (int)std::ceil(circumference);
    add_part(st, arm_1x1, num_armor);
}

void do_search(const state& st_, const cmdline& params)
{
    int num_designs = 0;
    // there are only two vectoring engine types, so simply:
    for (int i = 1; i <= params.max_engines; i++)
    {
        for (int j = 0; j <= i; j++)
        {
            int num_d30 = j, num_nk25 = i-j;
            state st = st_;
            add_part(st, e_d30, num_d30);
            add_part(st, e_nk25, num_nk25);
            add_fuel(st, params);
            add_power(st);
            add_armor(st, params);

            if (report(st, params, num_designs) && ++num_designs >= params.num_matches)
                return;
        }
    }

    if (num_designs == 0)
        fprintf(stderr, "error: no designs could be generated within the constraints.\n");
}

extern "C" int main(int argc, char** argv)
{
#ifdef _WIN32
    if (const char* c = strrchr(argv[0], '.'); c && *c)
        argv[0][c - argv[0]] = '\0';
    argv[0] = std::max(argv[0], strrchr(argv[0], '\\')+1);
#endif
    argv[0] = std::max(argv[0], strrchr(argv[0], '/')+1);

    try {
        if (argc < 2)
            cmdline::usage(argv[0]);

        state st;
        auto params = cmdline::parse_options(argc, argv);
        add_fixed(st, params);
        if (musl_optind == argc)
            cmdline::usage(argv[0]);
        for (int i = musl_optind; i < argc; i++)
            if (!add_gun(st, params, argv[i]))
            {
                INFO("Try '%s -G' to list supported guns.", params.argv[0]);
                params.terminate(EX_USAGE);
            }
        do_search(st, params);
        return 0;
    } catch (const exit_status& x) {
        return x.code;
    }
}

} // namespace hf::design
