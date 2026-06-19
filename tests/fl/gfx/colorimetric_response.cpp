// ok cpp include
// Tests for shared colorimetric response helpers. These cover topology-neutral
// math extracted from RGBW so future RGB/RGBCCT solvers can reuse it safely.

#include "test.h"

#include "fl/gfx/colorimetric_response.h"
#include "fl/stl/static_assert.h"

using namespace fl;
using namespace fl::colorimetric_response;


FL_TEST_CASE("colorimetric_response: xyY to XYZ round trip") {
    float xyz[3];
    xyY_to_XYZ(0.5f, 0.3f, 1.0f, xyz);
    FL_CHECK_CLOSE(xyz[1], 1.0f, 1e-5f);
    const float sum = xyz[0] + xyz[1] + xyz[2];
    FL_CHECK_CLOSE(xyz[0] / sum, 0.5f, 1e-5f);
    FL_CHECK_CLOSE(xyz[1] / sum, 0.3f, 1e-5f);
}


FL_TEST_CASE("colorimetric_response: invert3x3 identity") {
    float I[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float Iinv[3][3];
    FL_CHECK(invert3x3(I, Iinv));
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            FL_CHECK_CLOSE(Iinv[i][j], I[i][j], 1e-6f);
        }
    }
}


FL_TEST_CASE("colorimetric_response: invert3x3 rejects singular matrix") {
    float M[3][3] = {{1, 2, 3}, {1, 2, 3}, {4, 5, 6}};
    float Minv[3][3];
    FL_CHECK(!invert3x3(M, Minv));
}


FL_TEST_CASE("colorimetric_response: matvec3 basic product") {
    float M[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    float v[3] = {1, 0, 0};
    float out[3];
    matvec3(M, v, out);
    FL_CHECK_CLOSE(out[0], 1.0f, 1e-6f);
    FL_CHECK_CLOSE(out[1], 4.0f, 1e-6f);
    FL_CHECK_CLOSE(out[2], 7.0f, 1e-6f);
}


FL_TEST_CASE("colorimetric_response: barycentric inside outside") {
    const float A[2] = {0.0f, 0.0f};
    const float B[2] = {1.0f, 0.0f};
    const float C[2] = {0.0f, 1.0f};
    float bary[3];
    const float inside[2] = {0.25f, 0.25f};
    FL_CHECK(barycentric_xy(inside, A, B, C, bary));
    FL_CHECK(bary[0] >= 0.0f);
    FL_CHECK(bary[1] >= 0.0f);
    FL_CHECK(bary[2] >= 0.0f);
    FL_CHECK_CLOSE(bary[0] + bary[1] + bary[2], 1.0f, 1e-5f);

    const float outside[2] = {1.0f, 1.0f};
    FL_CHECK(barycentric_xy(outside, A, B, C, bary));
    FL_CHECK(bary[0] < 0.0f);
}


FL_TEST_CASE("colorimetric_response: cct_to_xy known Planckian points") {
    float xy[2];
    cct_to_xy(6500, xy);
    FL_CHECK_CLOSE(xy[0], 0.3135f, 0.005f);
    FL_CHECK_CLOSE(xy[1], 0.3237f, 0.005f);

    cct_to_xy(2700, xy);
    FL_CHECK_CLOSE(xy[0], 0.4600f, 0.005f);
    FL_CHECK_CLOSE(xy[1], 0.4107f, 0.005f);
}


FL_TEST_CASE("colorimetric_response: quantize_u8 edges") {
    FL_CHECK(quantize_u8(-1.0f) == 0);
    FL_CHECK(quantize_u8(0.0f) == 0);
    FL_CHECK(quantize_u8(0.5f) == 128);
    FL_CHECK(quantize_u8(1.0f) == 255);
    FL_CHECK(quantize_u8(2.0f) == 255);
}


FL_TEST_CASE("colorimetric_response: build_source_matrix white") {
    const float xy_r[2] = {0.6400f, 0.3300f};
    const float xy_g[2] = {0.3000f, 0.6000f};
    const float xy_b[2] = {0.1500f, 0.0600f};
    const float xy_w[2] = {0.31272f, 0.32903f};
    float M[3][3];
    FL_CHECK(build_source_matrix(xy_r, xy_g, xy_b, xy_w, M));
    const float one[3] = {1.0f, 1.0f, 1.0f};
    float white[3];
    matvec3(M, one, white);

    float expected[3];
    xyY_to_XYZ(xy_w[0], xy_w[1], 1.0f, expected);
    FL_CHECK_CLOSE(white[0], expected[0], 1e-4f);
    FL_CHECK_CLOSE(white[1], expected[1], 1e-4f);
    FL_CHECK_CLOSE(white[2], expected[2], 1e-4f);
}


FL_TEST_CASE("colorimetric_response: nnls identity nonnegative") {
    float I[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float b[3] = {1.0f, 2.0f, 3.0f};
    float t[3];
    float residual = 0.0f;
    nnls3(I, b, t, &residual);
    FL_CHECK_CLOSE(t[0], 1.0f, 0.03f);
    FL_CHECK_CLOSE(t[1], 2.0f, 0.06f);
    FL_CHECK_CLOSE(t[2], 3.0f, 0.09f);
    FL_CHECK(residual < 0.2f);
}


FL_TEST_CASE("colorimetric_response: hermite basis endpoints") {
    float h[4];
    hermite_basis(0.0f, h);
    FL_CHECK_CLOSE(h[0], 1.0f, 1e-6f);
    FL_CHECK_CLOSE(h[1], 0.0f, 1e-6f);
    FL_CHECK_CLOSE(h[2], 0.0f, 1e-6f);
    FL_CHECK_CLOSE(h[3], 0.0f, 1e-6f);

    hermite_basis(1.0f, h);
    FL_CHECK_CLOSE(h[0], 0.0f, 1e-6f);
    FL_CHECK_CLOSE(h[1], 1.0f, 1e-6f);
    FL_CHECK_CLOSE(h[2], 0.0f, 1e-6f);
    FL_CHECK_CLOSE(h[3], 0.0f, 1e-6f);
}


FL_TEST_CASE("colorimetric_response: LUT constants and EmitterProfile") {
    FL_STATIC_ASSERT(kLutQ == 4096, "lut q");
    FL_STATIC_ASSERT(kLutStrideBilinear == 4, "bilinear stride");
    FL_STATIC_ASSERT(kLutStrideHermite == 12, "hermite stride");
    EmitterProfile p{};
    FL_CHECK_CLOSE(p.xy_r[0], 0.0f, 1e-6f);
    FL_CHECK_CLOSE(p.lum_r, 0.0f, 1e-6f);
}


FL_TEST_CASE("colorimetric_response: EmitterCache direct emitter fallback") {
    EmitterProfile p{};
    p.xy_r[0] = 0.6400f; p.xy_r[1] = 0.3300f;
    p.xy_g[0] = 0.3000f; p.xy_g[1] = 0.6000f;
    p.xy_b[0] = 0.1500f; p.xy_b[1] = 0.0600f;
    p.lum_r = 0.25f;
    p.lum_g = 0.50f;
    p.lum_b = 0.125f;
    // input_xy_w.y intentionally left zero: direct emitter-drive fallback.

    EmitterCache cache;
    FL_CHECK(build_emitter_cache(&p, &cache));
    FL_CHECK(!cache.has_source_space);

    float xyz[3];
    source_rgb_to_XYZ(cache, 1.0f, 0.0f, 0.0f, xyz);
    FL_CHECK_CLOSE(xyz[0], cache.P_R[0], 1e-6f);
    FL_CHECK_CLOSE(xyz[1], cache.P_R[1], 1e-6f);
    FL_CHECK_CLOSE(xyz[2], cache.P_R[2], 1e-6f);
}


FL_TEST_CASE("colorimetric_response: EmitterCache source matrix path") {
    EmitterProfile p{};
    p.xy_r[0] = 0.6400f; p.xy_r[1] = 0.3300f;
    p.xy_g[0] = 0.3000f; p.xy_g[1] = 0.6000f;
    p.xy_b[0] = 0.1500f; p.xy_b[1] = 0.0600f;
    p.lum_r = 1.0f;
    p.lum_g = 1.0f;
    p.lum_b = 1.0f;
    p.input_xy_r[0] = 0.6400f; p.input_xy_r[1] = 0.3300f;
    p.input_xy_g[0] = 0.3000f; p.input_xy_g[1] = 0.6000f;
    p.input_xy_b[0] = 0.1500f; p.input_xy_b[1] = 0.0600f;
    p.input_xy_w[0] = 0.31272f; p.input_xy_w[1] = 0.32903f;

    EmitterCache cache;
    FL_CHECK(build_emitter_cache(&p, &cache));
    FL_CHECK(cache.has_source_space);

    float xyz[3];
    source_rgb_to_XYZ(cache, 1.0f, 1.0f, 1.0f, xyz);
    float white[3];
    xyY_to_XYZ(p.input_xy_w[0], p.input_xy_w[1], 1.0f, white);
    FL_CHECK_CLOSE(xyz[0], white[0], 1e-4f);
    FL_CHECK_CLOSE(xyz[1], white[1], 1e-4f);
    FL_CHECK_CLOSE(xyz[2], white[2], 1e-4f);
}
