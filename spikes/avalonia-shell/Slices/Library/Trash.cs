using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Library;

/// OrionApp.swift's trash alert and Library.swift's `moveToTrash`. Nothing
/// moves until "Move to Trash" is pressed, and nothing is ever deleted: a file
/// goes to the system's Trash or stays where it is.
static partial class Trash {
    /// `confirmTrash` / `confirmTrashRejected`: the counted question, over the window.
    public static void Confirm(IReadOnlyList<LibraryModel.Photo> photos) {
        if (photos.Count == 0 || App.Window is not { } owner) return;
        Dialog(photos).ShowDialog(owner);
    }

    /// The alert, built apart from showing it so the selftest can press its buttons
    /// (with a `move` that records instead of moving).
    public static Window Dialog(IReadOnlyList<LibraryModel.Photo> photos, Func<string, string?>? move = null) {
        var paths = photos.Select(p => p.Path).ToList();
        var window = new Window {
            Title = Question(photos), SizeToContent = SizeToContent.WidthAndHeight, CanResize = false,
            Background = Palette.Panel, Tag = "trash-confirmation",
        };
        Button Choice(string title, bool destructive, Action act) {
            var b = new Button { Content = title, MinWidth = 96, HorizontalContentAlignment = HorizontalAlignment.Center };
            if (destructive) b.Foreground = Palette.Reject;
            b.Click += (_, _) => { window.Close(); act(); };
            return b;
        }
        window.Content = new StackPanel {
            Width = 320, Margin = new Thickness(22), Spacing = Space.Md,
            Children = {
                new TextBlock { Text = Question(photos), FontWeight = FontWeight.SemiBold, TextWrapping = TextWrapping.Wrap },
                new TextBlock { Text = Message, Foreground = Palette.Dim, TextWrapping = TextWrapping.Wrap },
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Sm, HorizontalAlignment = HorizontalAlignment.Right,
                                 Margin = new Thickness(0, Space.Sm, 0, 0), Children = {
                    Choice("Cancel", false, () => { }),
                    Choice("Move to Trash", true, () => {
                        if (LibraryFiles.Trash(paths, move ?? Move) is { } complaint) EngineModel.Shared.Say(complaint);
                    }),
                } },
            },
        };
        return window;
    }

    /// `trashQuestion`: counted from the list the button acts on.
    public static string Question(IReadOnlyList<LibraryModel.Photo> photos) =>
        photos.Count == 1 ? $"Move {photos[0].Name} to the Trash?" : $"Move {photos.Count} photos to the Trash?";

    public const string Message = "Their edits go with them: sidecars, saved versions and masks. Recover from the Trash in Finder.";

    /// One file to the system's Trash: null when it went, else why not.
    public static string? Move(string path) {
        try {
            if (OperatingSystem.IsMacOS()) return MacTrash(path);
            if (OperatingSystem.IsWindows()) {
                Microsoft.VisualBasic.FileIO.FileSystem.DeleteFile(path, Microsoft.VisualBasic.FileIO.UIOption.OnlyErrorDialogs,
                                                                   Microsoft.VisualBasic.FileIO.RecycleOption.SendToRecycleBin);
                return null;
            }
            // stub(#315): .NET has no trash on Linux; the freedesktop.org Trash spec is the port. Never a delete instead.
            return "moving to the Trash is not built on this system";
        } catch (Exception e) {
            return e.Message;
        }
    }

    // NSFileManager.trashItemAtURL:resultingItemURL:error:, through the Objective-C runtime.
    const string ObjC = "/usr/lib/libobjc.A.dylib";
    [LibraryImport(ObjC, StringMarshalling = StringMarshalling.Utf8)] private static partial nint objc_getClass(string name);
    [LibraryImport(ObjC, StringMarshalling = StringMarshalling.Utf8)] private static partial nint sel_registerName(string name);
    [LibraryImport(ObjC, EntryPoint = "objc_msgSend")] private static partial nint Send(nint receiver, nint selector);
    [LibraryImport(ObjC, EntryPoint = "objc_msgSend", StringMarshalling = StringMarshalling.Utf8)]
    private static partial nint SendString(nint receiver, nint selector, string value);
    [LibraryImport(ObjC, EntryPoint = "objc_msgSend")] private static partial nint Send(nint receiver, nint selector, nint arg);
    [LibraryImport(ObjC, EntryPoint = "objc_msgSend")]
    [return: MarshalAs(UnmanagedType.I1)]
    private static partial bool SendTrash(nint receiver, nint selector, nint url, nint resulting, out nint error);

    static string? MacTrash(string path) {
        nint text = SendString(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), path);
        nint url = Send(objc_getClass("NSURL"), sel_registerName("fileURLWithPath:"), text);
        nint manager = Send(objc_getClass("NSFileManager"), sel_registerName("defaultManager"));
        if (SendTrash(manager, sel_registerName("trashItemAtURL:resultingItemURL:error:"), url, 0, out nint error)) return null;
        nint reason = error == 0 ? 0 : Send(Send(error, sel_registerName("localizedDescription")), sel_registerName("UTF8String"));
        return Marshal.PtrToStringUTF8(reason) ?? "the Trash refused it";
    }
}
