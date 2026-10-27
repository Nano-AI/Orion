using System.Runtime.InteropServices;
using Avalonia;

namespace OrionShell.CanvasSlice;

/// The orion_* calls the canvas makes that EngineModel does not (SLICES.md rule 10),
/// each wrapped so a closed engine answers with a neutral value instead of a crash.
static partial class CanvasNative {
    [LibraryImport(Native.Lib)]
    private static partial int orion_engine_frame_size(nint engine, out uint w, out uint h);

    [LibraryImport(Native.Lib)]
    private static partial int orion_engine_image_size(nint engine, out uint w, out uint h);

    [LibraryImport(Native.Lib)]
    private static partial int orion_engine_display_map(nint engine, [Out] float[] toDisplay, [Out] float[] toFrame);

    [LibraryImport(Native.Lib)]
    private static partial int orion_engine_sample(nint engine, float u, float v, [Out] float[] display, [Out] float[] scene);

    static nint Engine => EngineModel.Shared.Handle;

    /// The frame's width over its height: the turned, uncropped sensor picture
    /// (Engine+Geometry.swift `frameAspect`). 1 when nothing is open.
    public static double FrameAspect =>
        Engine != 0 && orion_engine_frame_size(Engine, out uint w, out uint h) == 0 && w > 0 && h > 0 ? (double)w / h : 1;

    /// The full-resolution output size, which a preview bitmap is a quarter of.
    public static PixelSize ImageSize =>
        Engine != 0 && orion_engine_image_size(Engine, out uint w, out uint h) == 0 ? new PixelSize((int)w, (int)h) : default;

    /// The engine's own frame ↔ display map (Engine+Geometry.swift `frameDisplayMap`).
    public static FrameDisplayMap DisplayMap() {
        var toDisplay = new float[9];
        var toFrame = new float[9];
        if (Engine == 0 || orion_engine_display_map(Engine, toDisplay, toFrame) != 0) return FrameDisplayMap.Identity;
        return new FrameDisplayMap(toDisplay.Select(f => (double)f).ToArray(), toFrame.Select(f => (double)f).ToArray());
    }

    /// The rendered colour and the scene colour at a displayed point (Engine+Render.swift `sample`).
    public static (float[] Display, float[] Scene)? Sample(Point uv) {
        var display = new float[3];
        var scene = new float[3];
        if (Engine == 0 || orion_engine_sample(Engine, (float)uv.X, (float)uv.Y, display, scene) != 0) return null;
        return (display, scene);
    }
}
