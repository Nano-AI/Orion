using System.Runtime.InteropServices;

namespace OrionShell;

/// native/shim.cpp (and shim_mac.mm), plus the whole engine facade (engine/include/orion/orion.h),
/// all exported from one dylib. A slice may add its own [LibraryImport]s for
/// any orion_* function in its own folder, against EngineModel.Handle.
public static unsafe partial class Native {
    public const string Lib = "orion_shim";

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial nint shim_open_mode(string path, int headless);

    [LibraryImport(Lib)]
    public static partial void shim_close(nint shim);

    /// The OrionEngine* inside, for direct orion_* calls.
    [LibraryImport(Lib)]
    public static partial nint shim_engine(nint shim);

    [LibraryImport(Lib)]
    public static partial int shim_field_count();

    /// Pointer to a static C string; `length` is 1 for a scalar, N for float[N].
    [LibraryImport(Lib)]
    public static partial nint shim_field_name(int i, out int length);

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial float shim_get(nint shim, string field, int index, out int ok);

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int shim_set(nint shim, string field, int index, float value);

    /// 1 = preview graph rendered, 2 = full graph, 0 = failed.
    [LibraryImport(Lib)]
    public static partial int shim_render(nint shim, int preview, out double gpuMs);

    /// 3 = only part of the picture rendered, at screen size (#22); 0 = this
    /// edit needs the whole graph, or failed: render that instead.
    [LibraryImport(Lib)]
    public static partial int shim_render_region(nint shim, float x, float y, float w, float h,
                                                 int outW, int outH, int interacting, out double gpuMs);

    [LibraryImport(Lib)]
    public static partial int shim_read(nint shim, int which, byte* dst, int capacity, out int w, out int h);

    /// The canvas's copy of a render, `level` halvings down, queued on the GPU (#386). Its ticket; 0 = nothing to copy.
    [LibraryImport(Lib)]
    public static partial ulong shim_copy_begin(nint shim, int which, int level, out int w, out int h);

    /// Windows: the copy into the D3D11 texture ANGLE imports by NT handle (#386). 0 = none here; orion_last_error says why.
    [LibraryImport(Lib)]
    public static partial int shim_share(nint shim, int which, int level, out nint handle, out int generation, out int w, out int h);

    /// The newest copy that has landed: dst null asks its size, dst copies it. Its ticket; 0 = none, or no room;
    /// -1 = a copy failed and is gone (orion_last_error).
    [LibraryImport(Lib)]
    public static partial long shim_copy_take(nint shim, int wait, byte* dst, int rowBytes, int capacity, out int w, out int h);

    // native/present.mm: the picture handed to Avalonia on the GPU (SharedPicture).
    [LibraryImport(Lib)]
    public static partial nint present_create();

    [LibraryImport(Lib)]
    public static partial void present_destroy(nint presenter);

    /// The MTLSharedEvent the shim signals once a surface holds a frame.
    [LibraryImport(Lib)]
    public static partial nint present_producer(nint presenter);

    /// The MTLSharedEvent Avalonia signals once it has read one.
    [LibraryImport(Lib)]
    public static partial nint present_consumer(nint presenter);

    [LibraryImport(Lib)]
    public static partial int present_frame(nint presenter, nint shim, int which, int level, out nint surface, out int generation,
                                            out int w, out int h, out ulong value);

    /// A hand-off Avalonia will never signal: frees its surface for the next.
    [LibraryImport(Lib)]
    public static partial void present_release(nint presenter, ulong value);

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int shim_thumbnail(string path, byte* dst, int capacity, out int turns);

    [LibraryImport(Lib)]
    public static partial nint orion_last_error(nint engine);

    [LibraryImport(Lib)]
    public static partial int orion_engine_image_size(nint engine, out uint width, out uint height);

    /// The preview graph's picture region; nonzero status when there is no preview graph.
    [LibraryImport(Lib)]
    public static partial int orion_engine_preview_size(nint engine, out uint width, out uint height);

    [LibraryImport(Lib)]
    public static partial int orion_engine_release_render_memory(nint engine);

    /// One tone-curve channel (0 master, 1-3 R G B) as interleaved x, y; returns the point count.
    [LibraryImport(Lib)]
    public static partial int shim_curve(nint shim, int channel, float* xy, int capacity);

    [LibraryImport(Lib)]
    public static partial int shim_set_curve(nint shim, int channel, float* xy, int count);

    [LibraryImport(Lib)]
    public static partial int shim_auto_enhance(nint shim);

    /// How many OrionMaskComponent fields shim_mask_set knows by name.
    [LibraryImport(Lib)]
    public static partial int shim_mask_field_count();

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int shim_mask_set(nint shim, int row, string field, float value);

    /// `erase` one value per dab (nonzero takes coverage away), or null: every dab paints.
    [LibraryImport(Lib)]
    public static partial int shim_mask_stroke(nint shim, int row, float* xy, float* erase, int dabs);

    /// One row's stroke as the engine holds it; returns the dab count, copies at most `capacity`.
    [LibraryImport(Lib)]
    public static partial int shim_mask_stroke_get(nint shim, int row, float* xy, float* erase, int capacity);

    [LibraryImport(Lib)]
    public static partial int shim_mask_commit(nint shim, int count);

    /// The overlay for the renders that follow, applied inside each one only.
    [LibraryImport(Lib)]
    public static partial void shim_mask_overlay(nint shim, int on, int layer);

}
