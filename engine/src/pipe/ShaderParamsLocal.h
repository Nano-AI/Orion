/*  Host-side mirrors of the local operators' shader parameter blocks:
 *  Local Laplacian clarity, dehaze and exposure fusion.
 *
 *  Split out of ShaderParams.h, which had passed the 1000-line limit, along
 *  the same seam as DevelopLocal.cpp. The same rule holds: each struct MUST
 *  match its Slang twin byte for byte, and the static_asserts are the check.
 *  Included by ShaderParams.h, so nothing needs to include this directly.
 */

#pragma once

#include <cstdint>

namespace orion::pipe::params {

/// Local Laplacian filters — Paris et al. 2011, Aubry et al. 2014.
/// research/local-laplacian.md. Six kernels share these; every one of them
/// mirrors a struct in the matching shaders/llf_*.slang.
struct LlfLuma {
    std::uint32_t size[2];
    float         lo;         // window floor, log2 units
    float         invRange;   // 1 / (hi - lo)
};
static_assert(sizeof(LlfLuma) == 16);

/// Shared by llfDown and llfDownPacked — the same halving, one channel or four.
struct LlfDown {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
};
static_assert(sizeof(LlfDown) == 16);

struct LlfRemap {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    float         gamma0;      // first of this texture's four
    float         gammaStep;
    float         sigmaR;
    float         alpha;
    float         noiseLo;     // Paris et al. section 5.2, tau's bounds
    float         noiseHi;
    float         _pad[2];
};
static_assert(sizeof(LlfRemap) == 48);

struct LlfRemapH {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    float         gamma0;
    float         gammaStep;
    float         sigmaR;
    float         alpha;
    float         noiseLo;
    float         noiseHi;
    /// The frame's measured noise in working luminance and the multiple of
    /// it the guard starts at; all zero is Paris et al.'s constant guard.
    /// research/high-iso.md §3, `llf::noiseBand`.
    float         noiseA;
    float         noiseB;
    float         floorK;
    float         windowLoEv;
    float         windowEv;
    float         _pad;
};
static_assert(sizeof(LlfRemapH) == 64);

struct LlfCollapse {
    std::uint32_t size[2];
    std::uint32_t coarseSize[2];
    float         gammaStep;
    std::int32_t  gammaCount;
    float         _pad[2];
};
static_assert(sizeof(LlfCollapse) == 32);

/// The finest level recomputes the remapping instead of reading it, so it
/// carries the remapping's own parameters as well.
struct LlfCollapse0 {
    std::uint32_t size[2];
    std::uint32_t coarseSize[2];
    float         gammaStep;
    std::int32_t  gammaCount;
    float         sigmaR;
    float         alpha;
    float         noiseLo;
    float         noiseHi;
    float         noiseA;       // as LlfRemapH
    float         noiseB;
    float         floorK;
    float         windowLoEv;
    float         windowEv;
    float         _pad;
};
static_assert(sizeof(LlfCollapse0) == 64);

struct LlfApply {
    std::uint32_t size[2];
    float         evPerUnit;
    float         maxEv;
};
static_assert(sizeof(LlfApply) == 16);

/// Dehaze — He, Sun & Tang's dark channel prior. research/dehaze.md.
struct alignas(16) DehazeChan {
    std::uint32_t size[2];
    float         _pad[2];
    float         airlight[4];   // A_c, w unused
};
static_assert(sizeof(DehazeChan) == 32);

struct DehazeRank {
    std::uint32_t size[2];
    std::int32_t  radius;
    std::int32_t  horizontal;
    std::int32_t  maximum;
    std::int32_t  _pad[3];
};
static_assert(sizeof(DehazeRank) == 32);

struct DehazePeak {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    std::int32_t  scale;
    std::int32_t  _pad[3];
};
static_assert(sizeof(DehazePeak) == 32);

struct DehazePrep {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    std::int32_t  scale;
    float         omega;
    float         lo;
    float         invRange;
};
static_assert(sizeof(DehazePrep) == 32);

struct BoxBlur4 {
    std::uint32_t size[2];
    std::int32_t  radius;
    std::int32_t  horizontal;
};
static_assert(sizeof(BoxBlur4) == 16);

struct DehazeAb {
    std::uint32_t size[2];
    float         epsilon;
    float         _pad;
};
static_assert(sizeof(DehazeAb) == 16);

struct alignas(16) DehazeRecover {
    std::uint32_t size[2];
    std::uint32_t coeffSize[2];
    float         t0;
    float         lo;
    float         invRange;
    float         _pad;
    float         airlight[4];
};
static_assert(sizeof(DehazeRecover) == 48);

/// Simulated exposure fusion — Hessel & Morel. research/exposure-fusion.md.
/// Mirrors FusePlan in shaders/ops/fuse_ops.slang.
struct FusePlanBlock {
    std::int32_t images;
    std::int32_t bright;
    std::int32_t dark;
    std::int32_t span;
    float        alpha;
    float        beta;
    float        lambda;
    float        sigma;
};
static_assert(sizeof(FusePlanBlock) == 32);

struct FuseProxy {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    std::int32_t  scale;
    float         slope;
    float         _pad[2];
};
static_assert(sizeof(FuseProxy) == 32);

struct FuseSplit {
    std::uint32_t size[2];
    std::int32_t  base;
    std::int32_t  weights;
    FusePlanBlock plan;
    float         epsilon;
    float         _pad[3];
};
static_assert(sizeof(FuseSplit) == 64);

struct FuseBlend {
    std::uint32_t size[2];
    std::uint32_t coarseSize[2];
    std::int32_t  images;
    std::int32_t  residual;
    float         _pad[2];
};
static_assert(sizeof(FuseBlend) == 32);

struct FuseApply {
    std::uint32_t size[2];
    std::uint32_t proxySize[2];
    float         slope;
    float         strength;
    float         maxGain;
    float         _pad;
};
static_assert(sizeof(FuseApply) == 32);

}  // namespace orion::pipe::params
