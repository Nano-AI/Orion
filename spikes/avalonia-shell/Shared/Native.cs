using System.Runtime.InteropServices;

namespace OrionShell;

/// native/shim.mm, plus the whole engine facade (engine/include/orion/orion.h),
/// all exported from one dylib. A slice may add its own [LibraryImport]s for
/// any orion_* function in its own folder, against EngineModel.Handle.
public static unsafe partial class Native {
    public const string Lib = "orion_shim";

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial nint shim_open(string path);

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

    [LibraryImport(Lib)]
    public static partial int shim_read(nint shim, int which, byte* dst, int capacity, out int w, out int h);

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int shim_thumbnail(string path, byte* dst, int capacity, out int turns);

    [LibraryImport(Lib)]
    public static partial nint orion_last_error(nint engine);

    [LibraryImport(Lib)]
    public static partial int orion_engine_image_size(nint engine, out uint width, out uint height);
}
