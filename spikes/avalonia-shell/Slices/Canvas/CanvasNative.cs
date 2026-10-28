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

    // Two things Avalonia's events drop that ImageCanvas.swift reads from AppKit:
    // whether a scroll came from a trackpad, and the Reduce Motion setting.
    const string ObjC = "/usr/lib/libobjc.A.dylib";
    [LibraryImport(ObjC, StringMarshalling = StringMarshalling.Utf8)] private static partial nint objc_getClass(string name);
    [LibraryImport(ObjC, StringMarshalling = StringMarshalling.Utf8)] private static partial nint sel_registerName(string name);
    [LibraryImport(ObjC, EntryPoint = "objc_msgSend")] private static partial nint Send(nint receiver, nint selector);
    [LibraryImport(ObjC, EntryPoint = "objc_msgSend")] private static partial ulong SendWord(nint receiver, nint selector);
    [LibraryImport(ObjC, EntryPoint = "objc_msgSend")] [return: MarshalAs(UnmanagedType.U1)] private static partial bool SendBool(nint receiver, nint selector);

    /// A window on AppKit (not --selftest's headless one), where NSApp exists.
    static bool Cocoa => OperatingSystem.IsMacOS()
        && Avalonia.Application.Current?.ApplicationLifetime is Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime;

    static nint Shared(string cls, string getter) => Send(objc_getClass(cls), sel_registerName(getter));

    /// `NSEvent.hasPreciseScrollingDeltas` of the scroll being handled, which
    /// Avalonia's native view divides away (by 50 or 5) before C# sees it. Read
    /// from `NSApp.currentEvent`: Avalonia dispatches the wheel event while
    /// AppKit is still handling it. Null off AppKit or if it is not a scroll.
    public static bool? PreciseScroll() =>
        Current() is var (e, type) && type == ScrollWheel ? SendBool(e, sel_registerName("hasPreciseScrollingDeltas")) : null;

    const ulong ScrollWheel = 22, Magnify = 30, Ended = 8, Cancelled = 16;   // NSEventType, NSEventPhase

    static (nint Event, ulong Type)? Current() {
        if (!Cocoa) return null;
        nint e = Send(Shared("NSApplication", "sharedApplication"), sel_registerName("currentEvent"));
        return e == 0 ? null : (e, SendWord(e, sel_registerName("type")));
    }

    /// The trackpad gesture being handled has ended: the fingers lifted with no
    /// momentum to follow, or the momentum ran out. A mouse wheel's notch has no
    /// phase, and off AppKit there is none: null, and the caller waits instead.
    /// (`momentumPhase` is asked of scroll events only: AppKit throws on others.)
    public static bool? GestureEnded() {
        if (Current() is not var (e, type) || type is not (ScrollWheel or Magnify)) return null;
        ulong phase = SendWord(e, sel_registerName("phase")), momentum = type == ScrollWheel ? SendWord(e, sel_registerName("momentumPhase")) : 0;
        if (phase == 0 && momentum == 0) return null;
        return (phase & (Ended | Cancelled)) != 0 && momentum == 0 || (momentum & (Ended | Cancelled)) != 0;
    }

    /// System Settings › Accessibility › Display › Reduce motion. Avalonia 12 exposes none (#320).
    public static bool ReduceMotion =>
        Cocoa && SendBool(Shared("NSWorkspace", "sharedWorkspace"), sel_registerName("accessibilityDisplayShouldReduceMotion"));
}
