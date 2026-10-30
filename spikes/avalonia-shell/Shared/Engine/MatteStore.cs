using System.Buffers.Binary;
using System.IO.Compression;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace OrionShell;

/// A raster matte saved with its photo: a port of app/MatteStore.swift, and of
/// Engine+Mask.swift's restoreMattes. One sibling PNG per matte,
/// `PHOTO.orion-matte-<uuid>.png`, 16-bit grayscale, linear. Immutable: a new
/// selection mints a new id and a new file, written before the id reaches the
/// edit, so a sidecar never names a file that is not on disk.
///
/// The PNG is written and read here with zlib (System.IO.Compression), not
/// ImageIO, so it runs wherever the shell does. The samples are linear and the
/// file says so (gAMA 1.0), the reading Swift's linear-gray ImageIO path gives;
/// the selftest reads the PNGs the Swift app wrote.
///
/// Not ported: the orphan sweep on open. Its keep-set includes every saved
/// version's and proposal's mattes, and the shell does not read those files
/// yet, so a sweep here would delete mattes a version still names. An orphan
/// costs about 30 KB; a deleted matte costs a finished edit.
public static class MatteStore {
    /// Only a UUID names a matte: an id from a sidecar is never a path (`../x`), so anything else throws.
    public static string PathFor(string photo, string id) => Guid.TryParse(id, out _)
        ? Path.Combine(Path.GetDirectoryName(photo) ?? "", $"{Path.GetFileNameWithoutExtension(photo)}.orion-matte-{id}.png")
        : throw new ArgumentException($"'{id}' is not a matte id", nameof(id));

    /// Writes a matte beside the photo and returns its new id. Throws IOException
    /// (or UnauthorizedAccessException) when it did not land: the caller must not
    /// go on to put an id in the edit that names nothing.
    public static string Write(float[] alpha, int width, int height, string photo) {
        string id = Guid.NewGuid().ToString(), path = PathFor(photo, id), temp = path + ".orion-tmp";
        try {
            File.WriteAllBytes(temp, Encode(alpha, width, height));
            File.Move(temp, path);
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
            try { File.Delete(temp); } catch (Exception) { /* the write already failed; that is the report */ }
            throw;
        }
        return id;
    }

    /// Null for any matte that will not read, whatever the reason: one bad file is
    /// that row's "Missing file", never the whole photo's edit (Session.Restore).
    public static (float[] Alpha, int Width, int Height)? Read(string photo, string id) {
        try { return Decode(File.ReadAllBytes(PathFor(photo, id))); }
        catch (Exception e) {
            Console.Error.WriteLine($"orion: the matte {id} beside {Path.GetFileName(photo)} will not read ({e.Message})");
            return null;
        }
    }

    // MARK: Which matte each engine slot holds

    sealed class Held { public readonly string?[] Slots = new string?[8]; public readonly HashSet<string> Missing = []; }
    static readonly ConditionalWeakTable<Session, Held> HeldBy = new();

    /// Ids of the open rows whose file could not be read: they cover nothing, and the panel says so.
    public static bool IsMissing(Session s, string? id) => id != null && HeldBy.GetOrCreateValue(s).Missing.Contains(id);

    /// Puts each kind-4 row's matte in its engine slot, by its id, and clears
    /// the slots no matte row holds. Only slots whose id changed are touched, so
    /// this is cheap on every push; a reorder, a removal or an undo moves the
    /// rasters with their rows. Pushes the adjustments, does not render. True when a slot changed.
    public static unsafe bool Upload(Session s, IReadOnlyList<MaskShape> rows) {
        var held = HeldBy.GetOrCreateValue(s);
        bool changed = false;
        for (int i = 0; i < held.Slots.Length; i++) {
            string? want = i < rows.Count && rows[i].Kind == 4 ? rows[i].MatteId : null;
            if (held.Slots[i] == want) continue;
            changed = true;
            held.Slots[i] = null;
            if (want != null && Read(s.Path, want) is { } m) {
                fixed (float* a = m.Alpha)
                    if (Native.orion_engine_set_mask_matte(s.Handle, i, a, m.Width, m.Height) == 0) {
                        held.Slots[i] = want;
                        held.Missing.Remove(want);
                        continue;
                    }
            }
            Native.orion_engine_set_mask_matte(s.Handle, i, null, 0, 0);
            if (want != null) {
                held.Missing.Add(want);
                held.Slots[i] = want;   // tried: not re-read at every push until the id changes
            }
        }
        // The engine reads a matte into its mask params when the adjustments are
        // pushed (orion.h: the upload dirties nothing), so push them again.
        if (changed) s.Set("mask_count", s.Get("mask_count"));
        return changed;
    }

    // MARK: PNG, 16-bit grayscale (ISO/IEC 15948:2004)

    static readonly byte[] Signature = [137, 80, 78, 71, 13, 10, 26, 10];

    /// Values clamp to 0…1: the kernel saturates anyway, and 1.2 means covered, not a wrap to a hole.
    public static byte[] Encode(float[] alpha, int width, int height) {
        if (width <= 0 || height <= 0 || alpha.Length < width * height) throw new ArgumentException("matte size");
        var raw = new byte[height * (1 + width * 2)];
        for (int y = 0, o = 0; y < height; y++) {
            raw[o++] = 0;   // filter: none
            for (int x = 0; x < width; x++, o += 2)
                BinaryPrimitives.WriteUInt16BigEndian(raw.AsSpan(o), (ushort)Math.Round(Math.Clamp(alpha[y * width + x], 0, 1) * 65535));
        }
        var z = new MemoryStream();
        using (var zs = new ZLibStream(z, CompressionLevel.Optimal, leaveOpen: true)) zs.Write(raw);
        var png = new MemoryStream();
        png.Write(Signature);
        var ihdr = new byte[13];
        BinaryPrimitives.WriteInt32BigEndian(ihdr, width);
        BinaryPrimitives.WriteInt32BigEndian(ihdr.AsSpan(4), height);
        (ihdr[8], ihdr[9]) = (16, 0);   // 16 bits, grayscale; compression, filter, interlace 0
        Chunk(png, "IHDR", ihdr);
        var gamma = new byte[4];
        BinaryPrimitives.WriteInt32BigEndian(gamma, 100000);   // gAMA 1.0: the samples are linear
        Chunk(png, "gAMA", gamma);
        Chunk(png, "IDAT", z.ToArray());
        Chunk(png, "IEND", []);
        return png.ToArray();
    }

    /// Grayscale, 8 or 16 bits, not interlaced: what either app writes. Null for anything else.
    public static (float[] Alpha, int Width, int Height)? Decode(byte[] png) {
        if (png.Length < 8 || !png.AsSpan(0, 8).SequenceEqual(Signature)) return null;
        int width = 0, height = 0, depth = 0;
        var idat = new MemoryStream();
        for (int at = 8; at + 12 <= png.Length;) {
            int n = BinaryPrimitives.ReadInt32BigEndian(png.AsSpan(at));
            if (n < 0 || at + 12 + n > png.Length) return null;
            var type = System.Text.Encoding.ASCII.GetString(png, at + 4, 4);
            var data = png.AsSpan(at + 8, n);
            if (type == "IHDR") {
                if (n < 13) return null;
                (width, height, depth) = (BinaryPrimitives.ReadInt32BigEndian(data), BinaryPrimitives.ReadInt32BigEndian(data[4..]), data[8]);
                if (data[9] != 0 || data[12] != 0 || depth is not (8 or 16)) return null;
            } else if (type == "IDAT") idat.Write(data);
            else if (type == "IEND") break;
            at += 12 + n;
        }
        if (width <= 0 || height <= 0 || (long)width * height > 1 << 26) return null;
        int bpp = depth / 8, stride = width * bpp;
        var raw = new byte[height * (stride + 1)];
        try {
            idat.Position = 0;
            using var zs = new ZLibStream(idat, CompressionMode.Decompress);
            zs.ReadExactly(raw);
        } catch (Exception e) when (e is InvalidDataException or EndOfStreamException) { return null; }
        var row = new byte[stride];
        var prev = new byte[stride];
        var alpha = new float[width * height];
        for (int y = 0; y < height; y++) {
            int filter = raw[y * (stride + 1)];
            for (int i = 0; i < stride; i++) {
                int a = i >= bpp ? row[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
                int predicted = filter switch { 0 => 0, 1 => a, 2 => b, 3 => (a + b) / 2, 4 => Paeth(a, b, c), _ => -1 };
                if (predicted < 0) return null;
                row[i] = (byte)(raw[y * (stride + 1) + 1 + i] + predicted);
            }
            for (int x = 0; x < width; x++)
                alpha[y * width + x] = depth == 16 ? BinaryPrimitives.ReadUInt16BigEndian(row.AsSpan(x * 2)) / 65535f : row[x] / 255f;
            (row, prev) = (prev, row);
        }
        return (alpha, width, height);
    }

    static int Paeth(int a, int b, int c) {
        int p = a + b - c, pa = Math.Abs(p - a), pb = Math.Abs(p - b), pc = Math.Abs(p - c);
        return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
    }

    static void Chunk(Stream png, string type, byte[] data) {
        Span<byte> word = stackalloc byte[4];
        BinaryPrimitives.WriteInt32BigEndian(word, data.Length);
        png.Write(word);
        var typed = new byte[4 + data.Length];
        System.Text.Encoding.ASCII.GetBytes(type, typed);
        data.CopyTo(typed, 4);
        png.Write(typed);
        BinaryPrimitives.WriteUInt32BigEndian(word, Crc(typed));
        png.Write(word);
    }

    static readonly uint[] CrcTable = Enumerable.Range(0, 256).Select(n => {
        uint c = (uint)n;
        for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
        return c;
    }).ToArray();

    static uint Crc(byte[] bytes) {
        uint c = 0xFFFFFFFF;
        foreach (byte b in bytes) c = CrcTable[(c ^ b) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFF;
    }
}

public static unsafe partial class Native {
    [LibraryImport(Lib)]
    public static partial int orion_engine_set_mask_matte(nint engine, int component, float* alpha, int width, int height);

    [LibraryImport(Lib)]
    public static partial int orion_engine_max_matte_size(nint engine, out uint w, out uint h);

    [LibraryImport(Lib)]
    public static partial int orion_engine_quarter_turns(nint engine, out int turns);
}
