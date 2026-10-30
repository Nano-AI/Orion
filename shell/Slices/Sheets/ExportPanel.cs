using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Threading;
using OrionShell.Library;

namespace OrionShell.Sheets;

/// A port of the Swift app's ExportPanel.swift: the export sheet. Every control moves the
/// byte count, so each one re-measures through a real encode after a pause.
[Panel("sheet-export", Region.Sheet)]
public sealed class ExportPanel : Border {
    readonly ExportSettings _s = ExportSettings.Shared;
    readonly Watermark _mark = Watermark.Shared;
    readonly StackPanel _options = new() { Spacing = Space.Lg, Margin = new Thickness(0, 0, 14, 0) };   // clear of the overlay scrollbar
    readonly TextBlock _dims = Faces.Figure("", Faces.Label, Palette.Dim);
    readonly TextBlock _size = Faces.Figure("", Faces.Label);
    readonly TextBlock _sizeNote = SheetKit.Note("", Palette.Dim);
    readonly ProgressBar _measuring = new() { IsIndeterminate = true, Width = 40, MinHeight = 0, Height = 3, VerticalAlignment = VerticalAlignment.Center };
    IDisposable? _pending;
    int _token;
    uint _sw, _sh;

    /// How many photographs the batch writes (exportSelected): the selection's
    /// export targets when there is a real selection, 0 to export the open one.
    static int Selected => LibraryModel.Shared.HasExplicitSelection ? ExportRun.Targets().Count : 0;
    readonly Border _readout;
    /// Closes the sheet. Set by whatever opened it.
    public Action? Dismiss { get; set; }

    public ExportPanel() {
        Width = 380;
        Padding = new Thickness(22);
        Background = Palette.Panel;
        HorizontalAlignment = HorizontalAlignment.Left;
        VerticalAlignment = VerticalAlignment.Top;
        SheetKit.Restyle(this);
        (_sw, _sh) = SheetsNative.ImageSize();
        if (_s.CustomWidth == 0) _s.SetCustomWidth(_sw, _sw, _sh);
        // The two numbers that make every control above legible.
        var readout = new DockPanel();
        DockPanel.SetDock(_size, Dock.Right);
        DockPanel.SetDock(_measuring, Dock.Right);
        _measuring.Margin = new Thickness(0, 0, Space.Sm + 2, 0);
        readout.Children.AddRange([_size, _measuring, _dims]);
        _readout = new Border {
            Background = Palette.Raised, CornerRadius = new CornerRadius(Radius.Md), Padding = new Thickness(11, 9), Child = readout,
        };
        Build();

        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Sm + 2, HorizontalAlignment = HorizontalAlignment.Right };
        actions.Children.Add(SheetKit.Button("Cancel", () => Dismiss?.Invoke()));
        // runBatchExport over a selection, else exportFile. The batch reads sidecars: what is owed is written first.
        actions.Children.Add(SheetKit.Button(Selected > 0 ? $"Export {Selected}…" : "Export…", () => {
            bool batch = Selected > 0;
            Dismiss?.Invoke();
            if (batch) { Autosave.Shared.Flush(); ExportRun.ExportAll(); } else ExportRun.ExportOpen();
        }, prominent: true));
        Child = new StackPanel {
            Spacing = Space.Lg,
            // The options scroll on a small window; Cancel and Export stay put.
            Children = { new ScrollViewer { Content = _options, MaxHeight = 540 }, actions },
        };
        Remeasure();
    }

    /// Rebuilt whole on a change that shows or hides a row (format, size).
    void Build() {
        _options.Children.Clear();
        _options.Children.Add(SheetKit.Title(Selected > 0 ? $"Export {Selected} photos" : "Export"));

        var format = new Segmented(ExportSettings.Formats.Select(c => c.Title).ToList(), _s.Format);
        format.Changed += i => { _s.Format = i; Build(); Remeasure(); };
        _options.Children.Add(SheetKit.Row("Format", format));

        if (_s.IsLossy) _options.Children.Add(SheetKit.Row("Quality", QualityControl()));

        _options.Children.Add(SheetKit.Row("Color", NotedMenu(ExportSettings.Spaces, () => _s.Space, i => _s.Space = i)));

        var depth = new Segmented(ExportSettings.Depths.Select(c => c.Title).ToList(), _s.Depth) { IsEnabled = _s.CarriesDepth };
        var depthNote = SheetKit.Note(DepthNote(), Palette.Dim);
        depth.Changed += i => { _s.Depth = i; depthNote.Text = DepthNote(); Remeasure(); };
        _options.Children.Add(SheetKit.Row("Depth", new StackPanel { Spacing = 5, Children = { depth, depthNote } }));

        _options.Children.Add(SheetKit.Row("Metadata", NotedMenu(ExportSettings.Metadatas, () => _s.Metadata, i => _s.Metadata = i)));

        var size = SheetKit.Menu(ExportSettings.Sizes.Select(c => c.Title), _s.Size, i => { _s.Size = i; Build(); Remeasure(); });
        var sizeRow = new StackPanel { Spacing = Space.Sm + 2, Children = { size } };
        if (ExportSettings.Sizes[_s.Size].Code < 0) sizeRow.Children.Add(CustomFields());
        _options.Children.Add(SheetKit.Row("Size", sizeRow));

        // Under Size, because that is what it corrects: a resize softens.
        var sharp = new Segmented(ExportSettings.Sharpenings.Select(c => c.Title).ToList(), _s.Sharpening);
        var sharpNote = SheetKit.Note(ExportSettings.Sharpenings[_s.Sharpening].Note, Palette.Dim);
        sharp.Changed += i => { _s.Sharpening = i; sharpNote.Text = ExportSettings.Sharpenings[i].Note; Remeasure(); };
        _options.Children.Add(SheetKit.Row("Sharpening", new StackPanel { Spacing = 5, Children = { sharp, sharpNote } }));

        _options.Children.Add(SheetKit.Row("Watermark", WatermarkRow()));

        _options.Children.Add(_readout);
        _options.Children.Add(_sizeNote);
        Paint();
    }

    Control QualityControl() {
        var slider = new Slider { Minimum = 0.3, Maximum = 1, Value = _s.Quality, Foreground = Palette.Accent, MinHeight = 0 };
        var value = Faces.Figure("", Faces.Mark, Palette.Dim);
        void Show() => value.Text = _s.IsJxl && _s.Quality >= 1 ? "Lossless" : ((int)(_s.Quality * 100)).ToString();
        slider.ValueChanged += (_, e) => { _s.Quality = e.NewValue; Show(); Remeasure(); };
        Show();
        var scale = new Grid { ColumnDefinitions = new("Auto,*,Auto,*,Auto") };
        var smaller = new TextBlock { Text = "Smaller file", FontSize = Faces.Mark, Foreground = Palette.Dim };
        var better = new TextBlock { Text = "Better detail", FontSize = Faces.Mark, Foreground = Palette.Dim };
        Grid.SetColumn(smaller, 2);
        Grid.SetColumn(better, 4);
        scale.Children.AddRange([value, smaller, better]);
        var panel = new StackPanel { Spacing = 3, Children = { slider, scale } };
        if (_s.IsJxl) panel.Children.Add(SheetKit.Note("At 100 the file is lossless: every pixel kept exactly.", Palette.Dim));
        return panel;
    }

    /// A menu with the chosen item's note under it.
    Control NotedMenu(ExportSettings.Choice[] choices, Func<int> get, Action<int> set) {
        var note = SheetKit.Note(choices[get()].Note, Palette.Dim);
        var menu = SheetKit.Menu(choices.Select(c => c.Title), get(), i => { set(i); note.Text = choices[i].Note; Remeasure(); });
        return new StackPanel { Spacing = 5, Children = { menu, note } };
    }

    string DepthNote() => _s.CarriesDepth ? ExportSettings.Depths[_s.Depth].Note : "JPEG holds eight bits. Choose PNG, TIFF or JPEG XL for more.";

    /// Commits on Return or on leaving the field, not per keystroke: three
    /// wasted encodes and a jumping number while you type "2048".
    Control CustomFields() {
        TextBox Field(string label, uint value, Action<uint> commit) {
            var box = SheetKit.Field(label);
            box.Width = 74;
            box.Text = value.ToString();
            (box.FontFamily, box.FontWeight, box.FontFeatures) = (Faces.Numeral, Avalonia.Media.FontWeight.Medium, Faces.Tabular);
            void Commit() { if (uint.TryParse(box.Text, out uint v) && v > 0) { commit(v); Build(); Remeasure(); } }
            box.KeyDown += (_, e) => { if (e.Key == Avalonia.Input.Key.Enter) Commit(); };
            box.LostFocus += (_, _) => Commit();
            return box;
        }
        var (w, h) = _s.Dimensions(_sw, _sh);
        return new StackPanel {
            Orientation = Orientation.Horizontal, Spacing = Space.Sm,
            Children = {
                Field("Width", w, v => _s.SetCustomWidth(v, _sw, _sh)),
                new TextBlock { Text = "×", Foreground = Palette.Dim, VerticalAlignment = VerticalAlignment.Center },
                Field("Height", h, v => _s.SetCustomHeight(v, _sw, _sh)),
                new TextBlock { Text = "px", FontSize = Faces.Mark, Foreground = Palette.Dim, VerticalAlignment = VerticalAlignment.Center },
            },
        };
    }

    Control WatermarkRow() {
        var toggle = new ToggleSwitch { IsChecked = _mark.Enabled, OnContent = null, OffContent = null, MinWidth = 0, VerticalAlignment = VerticalAlignment.Center };
        var summary = new TextBlock {
            FontSize = TypeSize.Sm, TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center,
        };
        void Show() {
            summary.Text = _mark.Summary;
            summary.Foreground = _mark.Enabled && _mark.IsDrawable ? Palette.Text : Palette.Dim;
        }
        // The mark is detail the encoder spends bytes on, so it moves the size.
        toggle.IsCheckedChanged += (_, _) => { _mark.Enabled = toggle.IsChecked == true; _mark.Save(); Show(); Remeasure(); };
        Show();
        var edit = SheetKit.Button("Edit…", () => EditWatermark(Show));
        var row = new DockPanel();
        DockPanel.SetDock(toggle, Dock.Left);
        DockPanel.SetDock(edit, Dock.Right);
        summary.Margin = new Thickness(Space.Sm + 2, 0);
        row.Children.AddRange([toggle, edit, summary]);
        return row;
    }

    /// The watermark editor, a sheet over this one.
    void EditWatermark(Action refresh) {
        if (TopLevel.GetTopLevel(this) is not Window owner) return;
        var editor = new WatermarkPanel();
        var sheet = new Window { Content = editor, SizeToContent = SizeToContent.WidthAndHeight, CanResize = false, Background = Palette.Panel };
        editor.Dismiss = sheet.Close;
        sheet.Closed += (_, _) => { refresh(); Remeasure(); };
        sheet.ShowDialog(owner);
    }

    void Paint() {
        var (w, h) = _s.Dimensions(_sw, _sh);
        _dims.Text = $"{w} × {h}";
        _size.Text = _failed ? "Size unknown" : _s.SizeText(_sw, _sh);
        _size.Foreground = _s.MeasuredBytes == null ? Palette.Dim : Palette.Text;
        _measuring.IsVisible = _s.MeasuredBytes == null && !_failed && EngineModel.Shared.Handle != 0;
        _sizeNote.Text = _failed ? "The encoded size could not be measured."
            : _s.MeasuredBytes == null ? "Measuring the encoded size…"
            : Selected > 0 ? $"Measured on the open photo. Each of the {Selected} is sized by its own frame and goes to a folder you choose; nothing there is overwritten."
            : $"Encoded size, measured. Color space is {ExportSettings.Spaces[_s.Space].Title}.";
    }

    /// Re-encodes after a pause: a 24 MP JPEG is a sixth of a second, so one
    /// per slider tick would make the slider unusable. The render stays on the
    /// UI thread, which owns the develop graph; the encode does not, because a
    /// 24 MP JPEG XL is seconds of it (#6). One encode at a time: a change made
    /// while one runs is measured when it ends, and only the latest lands.
    /// The watermark's mask is passed when it is on (the snapshot copies it).
    void Remeasure() {
        ++_token;
        _s.MeasuredBytes = null;
        _failed = false;
        Paint();
        _pending?.Dispose();
        _pending = DispatcherTimer.RunOnce(Measure, TimeSpan.FromMilliseconds(260));
    }

    bool _encoding, _again, _failed, _closed;

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnAttachedToVisualTree(e);
        if (!_closed) return;
        _closed = false;
        Remeasure();
    }

    /// Closed: no more renders on the UI thread, and a result still in flight lands nowhere.
    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnDetachedFromVisualTree(e);
        _closed = true;
        _pending?.Dispose();
        _again = false;
        ++_token;
    }

    void Measure() {
        if (_closed) return;
        // ponytail: one in flight, so a dragged slider never stacks 200 MB snapshots and full-core encodes.
        if (_encoding) { _again = true; return; }
        int token = _token;
        nint engine = EngineModel.Shared.Handle;
        var options = _s.Options(_sw, _sh);
        nint snapshot = 0;
        if (engine == 0 || WatermarkRaster.Marked(options, _s.Dimensions(_sw, _sh),
                                                  o => SheetsNative.orion_engine_export_snapshot(engine, in o, out snapshot)) != 0) {
            Land(token, null);
            return;
        }
        _encoding = true;
        // The task frees its snapshot itself, once started, whatever the encode does;
        // so the hand-off frees it only if the task never started.
        Task<long?>? encode = null;
        try {
            encode = Task.Run(() => {
                try { return SheetsNative.orion_export_snapshot_size(snapshot, out ulong b) == 0 ? (long?)b : null; }
                finally { SheetsNative.orion_export_snapshot_free(snapshot); }
            });
            encode.ContinueWith(t => Dispatcher.UIThread.Post(() => {
                _encoding = false;
                Land(token, t.IsCompletedSuccessfully ? t.Result : null);
                if (_again && !_closed) { _again = false; Measure(); }
            }), TaskScheduler.Default);
        } catch {
            if (encode == null) SheetsNative.orion_export_snapshot_free(snapshot);
            _encoding = false;
            Land(token, null);
        }
    }

    /// A measurement, or its failure, if it is still the one the sheet is waiting for.
    void Land(int token, long? bytes) {
        if (token != _token || _closed) return;
        _s.MeasuredBytes = bytes;
        _failed = bytes == null;
        Paint();
    }
}
