/// @file colorimetric_response.h
/// Shared colorimetric response math helpers.
///
/// This header is the Phase 2 extraction point for portable CIE / matrix /
/// geometry primitives used by RGBW today and future RGB / arbitrary-emitter
/// colorimetric solvers.  It intentionally avoids RGBW topology policy:
/// RGW/RBW/BGW routing, W extraction, LP, overdrive, LUT storage, and RGBCCT
/// composition remain in the RGBW-specific files until later phases move them.

#pragma once

#include "fl/math/math.h"
#include "fl/stl/noexcept.h"
#include "fl/stl/stdint.h"

namespace fl {
namespace colorimetric_detail {

// Krystek's approximation for blackbody chromaticity (good 1000K - 15000K).
// Krystek's outputs are (u, v) in the CIE 1960 UCS; convert to xy via the
// standard 1960->xy formulas. Reference: Krystek, "An algorithm to calculate
// correlated color temperature", Color Research & Application, 1985.
inline void cct_to_xy(int cct, float out[2]) FL_NOEXCEPT {
    const float T = static_cast<float>(
        (cct < 1500) ? 1500 : ((cct > 15000) ? 15000 : cct));
    const float T2 = T * T;
    const float u_num = 0.860117757f + 1.54118254e-4f * T + 1.28641212e-7f * T2;
    const float u_den = 1.0f + 8.42420235e-4f * T + 7.08145163e-7f * T2;
    const float v_num = 0.317398726f + 4.22806245e-5f * T + 4.20481691e-8f * T2;
    const float v_den = 1.0f - 2.89741816e-5f * T + 1.61456053e-7f * T2;
    const float u = u_num / u_den;
    const float v = v_num / v_den;
    const float den = 2.0f * u - 8.0f * v + 4.0f;
    out[0] = 3.0f * u / den;
    out[1] = 2.0f * v / den;
}

// Convert CIE 1931 xy plus luminance Y to absolute XYZ.  This is the common
// bridge from measured emitter profiles (xyY) into the linear algebra domain
// used by every solver.  Degenerate y values map to black rather than NaN/inf.
inline void xyY_to_XYZ(float x, float y, float Y, float out[3]) FL_NOEXCEPT {
    if (y < 1e-12f) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    const float inv_y = 1.0f / y;
    out[0] = x * Y * inv_y;
    out[1] = Y;
    out[2] = (1.0f - x - y) * Y * inv_y;
}

// Invert a 3x3 matrix with a direct adjugate/determinant formula. Solvers use
// this for tiny fixed bases such as RGB, RGW, RBW, and BGW; false means the
// chromaticities/luminance data are degenerate enough that the caller should
// avoid using the inverse.
inline bool invert3x3(const float in[3][3], float out[3][3]) FL_NOEXCEPT {
    const float a = in[0][0], b = in[0][1], c = in[0][2];
    const float d = in[1][0], e = in[1][1], f = in[1][2];
    const float g = in[2][0], h = in[2][1], i = in[2][2];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fl::fabs(det) < 1e-20f) {
        return false;
    }
    const float inv_det = 1.0f / det;
    out[0][0] = (e * i - f * h) * inv_det;
    out[0][1] = (c * h - b * i) * inv_det;
    out[0][2] = (b * f - c * e) * inv_det;
    out[1][0] = (f * g - d * i) * inv_det;
    out[1][1] = (a * i - c * g) * inv_det;
    out[1][2] = (c * d - a * f) * inv_det;
    out[2][0] = (d * h - e * g) * inv_det;
    out[2][1] = (b * g - a * h) * inv_det;
    out[2][2] = (a * e - b * d) * inv_det;
    return true;
}

// Multiply a 3x3 matrix by a 3-vector. Kept as a named helper so all response
// paths use the same row-major convention.
inline void matvec3(const float M[3][3], const float v[3], float out[3]) FL_NOEXCEPT {
    out[0] = M[0][0] * v[0] + M[0][1] * v[1] + M[0][2] * v[2];
    out[1] = M[1][0] * v[0] + M[1][1] * v[1] + M[1][2] * v[2];
    out[2] = M[2][0] * v[0] + M[2][1] * v[1] + M[2][2] * v[2];
}

// Return barycentric weights for point `t` relative to triangle A/B/C in CIE
// xy space. The function does not classify containment by itself; callers check
// the returned weights against their own tolerance.
inline bool barycentric_xy(const float t[2], const float A[2], const float B[2],
                           const float C[2], float bary[3]) FL_NOEXCEPT {
    const float v0x = B[0] - A[0], v0y = B[1] - A[1];
    const float v1x = C[0] - A[0], v1y = C[1] - A[1];
    const float v2x = t[0] - A[0], v2y = t[1] - A[1];
    const float d00 = v0x * v0x + v0y * v0y;
    const float d01 = v0x * v1x + v0y * v1y;
    const float d11 = v1x * v1x + v1y * v1y;
    const float d20 = v2x * v0x + v2y * v0y;
    const float d21 = v2x * v1x + v2y * v1y;
    const float den = d00 * d11 - d01 * d01;
    if (fl::fabs(den) < 1e-20f) {
        return false;
    }
    const float inv_den = 1.0f / den;
    const float u = (d11 * d20 - d01 * d21) * inv_den;
    const float v = (d00 * d21 - d01 * d20) * inv_den;
    bary[0] = 1.0f - u - v;
    bary[1] = u;
    bary[2] = v;
    return true;
}

// Quantize a normalized float channel to FastLED's public 8-bit dispatch range
// using round-half-up semantics and saturation at the byte endpoints.
inline u8 quantize_u8(float v) FL_NOEXCEPT {
    const float scaled = v * 255.0f + 0.5f;
    if (scaled <= 0.0f) return 0;
    if (scaled >= 255.0f) return 255;
    return static_cast<u8>(scaled);
}

// Standard CIE primary-matrix construction (#2705). Given source primary
// chromaticities xy_r/g/b and a source white chromaticity xy_w, build the
// 3x3 matrix M such that M * [1,1,1]^T = xyY_to_XYZ(xy_w, 1.0). Columns are
// per-channel scaled XYZ vectors of the primaries at Y=1, with scaling
// chosen so the (1,1,1) input lands at source white in XYZ.
// Returns false if the primary matrix is singular (collinear chromaticities).
inline bool build_source_matrix(const float xy_r[2], const float xy_g[2],
                                const float xy_b[2], const float xy_w[2],
                                float M_out[3][3]) FL_NOEXCEPT {
    float xyz_R[3], xyz_G[3], xyz_B[3], xyz_W[3];
    xyY_to_XYZ(xy_r[0], xy_r[1], 1.0f, xyz_R);
    xyY_to_XYZ(xy_g[0], xy_g[1], 1.0f, xyz_G);
    xyY_to_XYZ(xy_b[0], xy_b[1], 1.0f, xyz_B);
    xyY_to_XYZ(xy_w[0], xy_w[1], 1.0f, xyz_W);

    float P[3][3];
    P[0][0] = xyz_R[0]; P[0][1] = xyz_G[0]; P[0][2] = xyz_B[0];
    P[1][0] = xyz_R[1]; P[1][1] = xyz_G[1]; P[1][2] = xyz_B[1];
    P[2][0] = xyz_R[2]; P[2][1] = xyz_G[2]; P[2][2] = xyz_B[2];

    float P_inv[3][3];
    if (!invert3x3(P, P_inv)) {
        return false;
    }
    float k[3];
    matvec3(P_inv, xyz_W, k);

    M_out[0][0] = k[0] * xyz_R[0]; M_out[0][1] = k[1] * xyz_G[0]; M_out[0][2] = k[2] * xyz_B[0];
    M_out[1][0] = k[0] * xyz_R[1]; M_out[1][1] = k[1] * xyz_G[1]; M_out[1][2] = k[2] * xyz_B[1];
    M_out[2][0] = k[0] * xyz_R[2]; M_out[2][1] = k[1] * xyz_G[2]; M_out[2][2] = k[2] * xyz_B[2];
    return true;
}

// Non-negative least squares for the 3x3 sub-system M.t = b with t >= 0.
// Projected-gradient form matching the reference `_nnls_solve` fallback used
// when scipy is unavailable: 500 iterations at step 0.01.  This is only used
// on rare projection/fallback paths; normal in-hull solves use cached inverses.
inline void nnls3(const float M[3][3], const float b[3],
                  float t_out[3], float* residual_out) FL_NOEXCEPT {
    float t[3] = {0.0f, 0.0f, 0.0f};
    constexpr float kStep = 0.01f;
    constexpr int kIters = 500;
    for (int it = 0; it < kIters; ++it) {
        float r[3];
        r[0] = M[0][0]*t[0] + M[0][1]*t[1] + M[0][2]*t[2] - b[0];
        r[1] = M[1][0]*t[0] + M[1][1]*t[1] + M[1][2]*t[2] - b[1];
        r[2] = M[2][0]*t[0] + M[2][1]*t[1] + M[2][2]*t[2] - b[2];
        float g[3];
        g[0] = M[0][0]*r[0] + M[1][0]*r[1] + M[2][0]*r[2];
        g[1] = M[0][1]*r[0] + M[1][1]*r[1] + M[2][1]*r[2];
        g[2] = M[0][2]*r[0] + M[1][2]*r[1] + M[2][2]*r[2];
        for (int j = 0; j < 3; ++j) {
            float v = t[j] - kStep * g[j];
            t[j] = v > 0.0f ? v : 0.0f;
        }
    }
    if (residual_out != nullptr) {
        float r[3];
        r[0] = M[0][0]*t[0] + M[0][1]*t[1] + M[0][2]*t[2] - b[0];
        r[1] = M[1][0]*t[0] + M[1][1]*t[1] + M[1][2]*t[2] - b[1];
        r[2] = M[2][0]*t[0] + M[2][1]*t[1] + M[2][2]*t[2] - b[2];
        *residual_out = fl::sqrt(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
    }
    t_out[0] = t[0]; t_out[1] = t[1]; t_out[2] = t[2];
}

} // namespace colorimetric_detail
} // namespace fl
