/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/render.h"
#include "../test.h"

static void fractional_vectors_keep_green_precision_and_round_once(void) {
    CHECK(cupid_brighten(0, 1) == 0x0842);
    CHECK(cupid_brighten(0, 8) == 0x4210);
    CHECK(cupid_brighten(0x0010, 8) == 0x4218);
    CHECK(cupid_brighten(0, 9) == 0x4631);
    CHECK(cupid_brighten(0x8000, 9) == 0x4651);
    CHECK(cupid_darken(1, 8) == 1);
    CHECK(cupid_darken(1, 9) == 0);
    CHECK(cupid_darken(0x001F, 8) == 0x0010);
    CHECK(cupid_darken(0x7C00, 8) == 0x4000);
    CHECK(cupid_darken(0x03E0, 8) == 0x01E0);
    CHECK(cupid_darken(0x0020, 8) == 0);
    CHECK(cupid_darken(0x8020, 8) == 0x0020);
    CHECK(cupid_color_mix(0, 0, 8, 1) == 1);
    CHECK(cupid_color_mix(4, 1, 4, 1) == 1);
    CHECK(cupid_color_mix(8, 0x001F, 8, 0x7C00) == 0x4010);
    CHECK(cupid_color_mix(8, 0x0020, 0, 0) == 0);
    CHECK(cupid_color_mix(8, 0x8020, 0, 0) == 0x0020);
    CHECK(cupid_color_mix(0, 0, 8, 0x8020) == 0x0020);
    CHECK(cupid_color_mix(16, 0xFFFF, 16, 0xFFFF) == 0x7FFF);
}

static void complete_color_endpoints_and_coefficient_saturation(void) {
    const unsigned clipped[] = {16, 17, 31};
    for (uint32_t color = 0; color < 65536; color++) {
        CHECK(cupid_brighten(color, 0) == (color & 0x7FFF));
        CHECK(cupid_darken(color, 0) == (color & 0x7FFF));
        CHECK(cupid_color_mix(16, color, 0, 0xFFFF) == (color & 0x7FFF));
        CHECK(cupid_color_mix(0, 0xFFFF, 16, color) == (color & 0x7FFF));
        for (unsigned i = 0; i < sizeof(clipped) / sizeof(*clipped); i++) {
            CHECK(cupid_brighten(color, (int)clipped[i]) == 0x7FFF);
            CHECK(cupid_darken(color, (int)clipped[i]) == 0);
            CHECK(cupid_color_mix((int)clipped[i], color, 0, 0xFFFF) == (color & 0x7FFF));
            CHECK(cupid_color_mix(0, 0xFFFF, (int)clipped[i], color) == (color & 0x7FFF));
        }
    }
}

static void red_and_blue_have_identical_effect_precision(void) {
    for (unsigned first = 0; first < 32; first++) {
        for (unsigned factor = 0; factor < 32; factor++) {
            CHECK((cupid_brighten(first, (int)factor) & 31)
                == ((cupid_brighten(first << 10, (int)factor) >> 10) & 31));
            CHECK((cupid_darken(first, (int)factor) & 31)
                == ((cupid_darken(first << 10, (int)factor) >> 10) & 31));
        }
        for (unsigned second = 0; second < 32; second++) {
            for (unsigned eva = 0; eva < 32; eva++) {
                for (unsigned evb = 0; evb < 32; evb++) {
                    CHECK((cupid_color_mix((int)eva, first, (int)evb, second) & 31)
                        == ((cupid_color_mix((int)eva, first << 10, (int)evb, second << 10) >> 10) & 31));
                }
            }
        }
    }
}

int main(void) {
    RUN(fractional_vectors_keep_green_precision_and_round_once);
    RUN(complete_color_endpoints_and_coefficient_saturation);
    RUN(red_and_blue_have_identical_effect_precision);
    return 0;
}
