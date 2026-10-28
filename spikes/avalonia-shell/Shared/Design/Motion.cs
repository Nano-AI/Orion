using System.Runtime.InteropServices;
using Avalonia.Animation.Easings;

namespace OrionShell;

/// design/DESIGN.md's Motion section as values. A spring is named by its
/// response: the period of the undamped spring in seconds, not a duration
/// (Spring is critically damped, so it settles when it settles).
public static partial class Motion {
    /// Something travelling: a value gliding to one set from outside a drag, a
    /// thumb springing back from past an end. 90% there in 185 ms, within 1% at 315 ms; the rest is under a pixel.
    public const double Settle = 0.3;
    /// Feedback under the pointer (hover lift, press): 95% there in 115 ms.
    public const double Touch = 0.15;
    /// What a Settle spring becomes under reduced motion: an opacity cross-fade.
    public static readonly TimeSpan Fade = TimeSpan.FromMilliseconds(120);
    /// A state mark arriving (the edited dot), and leaving faster.
    public static readonly TimeSpan Enter = TimeSpan.FromMilliseconds(160), Exit = TimeSpan.FromMilliseconds(100);
    /// The strong ease-out (Emil Kowalski's) for the transitions that are not springs.
    public static readonly Easing EaseOut = new SplineEasing(0.23, 1, 0.32, 1);

    /// Past an end, a drag moves the thing less the further it goes and never
    /// more than `limit`: Apple's rubber band, constant 0.55.
    public static double Band(double past, double limit) => past * limit * 0.55 / (limit + 0.55 * Math.Abs(past));

    /// Set by a test; null asks the system.
    public static bool? Forced;

    /// Reduce motion is on: macOS Accessibility › Display › Reduce motion, or
    /// Windows' "Show animations" off. Read as each animation starts, so a change
    /// applies to the next one.
    public static bool Reduced => Forced ?? System();

    internal static bool System() {
        try {
            if (OperatingSystem.IsMacOS()) {
                // Nil until AppKit is loaded (headless runs), and a message to nil answers NO.
                nint workspace = Send(objc_getClass("NSWorkspace"), sel_registerName("sharedWorkspace"));
                return SendBool(workspace, sel_registerName("accessibilityDisplayShouldReduceMotion"));
            }
            if (OperatingSystem.IsWindows()) return SystemParametersInfo(SpiGetClientAreaAnimation, 0, out bool animate, 0) && !animate;
        } catch (Exception e) when (e is DllNotFoundException or EntryPointNotFoundException) { }
        // ponytail: no Linux reading; GNOME's enable-animations is one desktop's. Add with a Linux build.
        return false;
    }

    // Through the Objective-C runtime, as Trash.cs reaches NSFileManager.
    const string ObjC = "/usr/lib/libobjc.A.dylib";
    [LibraryImport(ObjC, StringMarshalling = StringMarshalling.Utf8)] private static partial nint objc_getClass(string name);
    [LibraryImport(ObjC, StringMarshalling = StringMarshalling.Utf8)] private static partial nint sel_registerName(string name);
    [LibraryImport(ObjC, EntryPoint = "objc_msgSend")] private static partial nint Send(nint receiver, nint selector);
    [LibraryImport(ObjC, EntryPoint = "objc_msgSend")]
    [return: MarshalAs(UnmanagedType.U1)] private static partial bool SendBool(nint receiver, nint selector);

    const uint SpiGetClientAreaAnimation = 0x1042;
    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SystemParametersInfo(uint action, uint param, [MarshalAs(UnmanagedType.Bool)] out bool value, uint winIni);
}
