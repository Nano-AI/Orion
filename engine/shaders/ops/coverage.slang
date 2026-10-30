// Shared by develop_linear and denoise_field: both read a layer's coverage
// from one of the eight fold slots.

/// One layer's coverage. ⚠ A branch rather than an array index: Slang cannot
/// index a set of texture parameters, and the graph is static so which slot a
/// layer uses is only known at render.
///
/// ⚠ `[ForceInline]` is for the WGSL target: slangc 2026.14.1 segfaults
/// compiling a call that passes an element of a parameter-block array (here
/// `p.layerMask[L]`) to a function taking texture parameters, because the WGSL
/// uniform layout rewrites that array (#375). Inlined, it compiles; Metal's
/// output is unchanged in meaning.
[ForceInline]
float coverage(int slot, uint2 p,
               Texture2D<float> m0, Texture2D<float> m1,
               Texture2D<float> m2, Texture2D<float> m3,
               Texture2D<float> m4, Texture2D<float> m5,
               Texture2D<float> m6, Texture2D<float> m7)
{
    if (slot == 0) return m0[p];
    if (slot == 1) return m1[p];
    if (slot == 2) return m2[p];
    if (slot == 3) return m3[p];
    if (slot == 4) return m4[p];
    if (slot == 5) return m5[p];
    if (slot == 6) return m6[p];
    return m7[p];
}
