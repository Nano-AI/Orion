// RCD's directional discrimination statistic.
//
// From the reference implementation by Luis Sanz Rodríguez (MIT licensed):
// https://github.com/LuisSR/RCD-Demosaicing
//
// A quadratic form over nine samples along one axis. It is effectively the
// energy of a high-pass filter applied along that direction, so a large value
// means the signal changes fast along it — and interpolation should therefore
// run the *other* way, along the smooth direction. Guessing that wrong is what
// produces zippers and maze artifacts.
//
// The same statistic serves the cardinal axes in step 1 and the diagonals in
// step 4; only the sampling offsets change.

#ifndef ORION_RCD_STAT_SLANG
#define ORION_RCD_STAT_SLANG

static const float kRcdEps   = 1e-5f;
static const float kRcdEpsSq = 1e-10f;

/// Samples are ordered m4, m3, m2, m1, center, p1, p2, p3, p4.
float rcdDirectionStat(float m4, float m3, float m2, float m1, float c0,
                       float p1, float p2, float p3, float p4)
{
    float s =
        - 18.0f * c0 * m1 - 18.0f * c0 * p1
        - 36.0f * c0 * m2 - 36.0f * c0 * p2
        + 18.0f * c0 * m3 + 18.0f * c0 * p3
        -  2.0f * c0 * m4 -  2.0f * c0 * p4
        + 38.0f * c0 * c0

        - 70.0f * m1 * p1
        - 12.0f * m1 * m2 + 24.0f * m1 * p2
        - 38.0f * m1 * m3 + 16.0f * m1 * p3
        + 12.0f * m1 * m4 -  6.0f * m1 * p4
        + 46.0f * m1 * m1

        + 24.0f * p1 * m2 - 12.0f * p1 * p2
        + 16.0f * p1 * m3 - 38.0f * p1 * p3
        -  6.0f * p1 * m4 + 12.0f * p1 * p4
        + 46.0f * p1 * p1

        + 14.0f * m2 * p2 - 12.0f * m2 * p3
        -  2.0f * m2 * m4 +  2.0f * m2 * p4
        + 11.0f * m2 * m2

        - 12.0f * p2 * m3 +  2.0f * p2 * m4
        -  2.0f * p2 * p4 + 11.0f * p2 * p2

        +  2.0f * m3 * p3 -  6.0f * m3 * m4
        + 10.0f * m3 * m3

        -  6.0f * p3 * p4 + 10.0f * p3 * p3

        +  1.0f * m4 * m4 +  1.0f * p4 * p4;

    return max(s, kRcdEpsSq);
}

#endif
