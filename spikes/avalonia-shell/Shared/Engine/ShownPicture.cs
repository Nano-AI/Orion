using Avalonia.Media.Imaging;

namespace OrionShell;

/// Which graph made a picture: the shim's 1 and 2 (shim_render), 0 for none.
public enum Graph { None = 0, Preview = 1, Full = 2 }

/// The one picture the canvas shows. Everything that wants a picture on screen
/// goes through Show; the canvas reads Bitmap and repaints on Changed. Stage 1a
/// feeds it cached and preview-first pictures; #317 makes it a shared texture.
public sealed class ShownPicture {
    public Bitmap? Bitmap { get; private set; }
    public Graph Graph { get; private set; }
    /// After every Show, including the same bitmap with new pixels.
    public event Action? Changed;

    public void Show(Bitmap bitmap, Graph graph) {
        (Bitmap, Graph) = (bitmap, graph);
        Changed?.Invoke();
    }
}
