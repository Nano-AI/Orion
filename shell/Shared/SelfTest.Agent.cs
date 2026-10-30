using System.Text;
using System.Text.Json.Nodes;
using OrionShell.Agent;

namespace OrionShell;

/// `Orion --agent`'s pure halves, without a photograph: the refusals the
/// parser owes, the edit merge's validation, the histogram arithmetic, and
/// commit keeping a rating. Ported from the retired Swift app's
/// ViewportTests+Agent.swift and +AgentSync.swift (#380); check-agent drives
/// the same verbs end to end through the MCP server.
static partial class SelfTest {
    static partial void AgentChecks() {
        // Each refused as a usage error naming what was wrong, before any file is touched.
        (string[] Args, string Names, string What)[] refusals = [
            (["--agent", "proxy", "/a.arw", "--max", "512", "--out", "/o.jpg", "--sate", "/s.json"], "sate", "an option the verb does not take"),
            (["--agent", "faces", "/a.arw", "--max", "512"], "max", "faces takes no --max"),
            (["--agent", "state", "/a.arw", "--out", "/o.json"], "out", "state takes no --out"),
            (["--agent", "state"], "usage", "state with no photograph"),
            (["--agent", "matte", "/a.arw"], "subject|person|sky", "matte without --kind"),
            (["--agent", "matte", "/a.arw", "--kind", "dog"], "subject|person|sky", "an unknown --kind"),
            (["--agent", "matte", "/a.arw", "--kind", "sky", "--out", "/o"], "out", "matte takes no --out"),
            (["--agent", "sync", "/a.arw"], "--targets", "sync without --targets"),
            (["--agent", "sync", "/a.arw", "--targets", "/t", "--groups", "light,tone"], "tone", "an unknown sync group"),
            (["--agent", "sync", "/a.arw", "--targets", "/t", "--groups", "colour"], "colour", "the legacy colour spelling as a group"),
            (["--agent", "sync", "/a.arw", "--targets", "/t", "--mode", "apply"], "propose or commit", "a bad sync --mode"),
            (["--agent", "keys", "extra"], "keys", "keys with an argument"),
        ];
        foreach (var (args, naming, what) in refusals) {
            string? said = null;
            try { AgentCli.Dispatch(args); } catch (AgentCli.Usage e) { said = e.Message; } catch (Exception e) { said = "wrong error: " + e.Message; }
            Check(said != null && said.Contains(naming) && !said.StartsWith("wrong error"), $"agent: {what} is a usage error naming '{naming}' ({said})");
        }
        Check(AgentCli.ParseRegion("0.25,0.1,0.5,0.4") == new AgentCli.Region(0.25, 0.1, 0.5, 0.4), "agent: --region parses x,y,w,h");
        foreach (var bad in new[] { "0.5,0.5,0.6,0.1", "-0.1,0,0.5,0.5", "0,0,0,0.5", "0,0,0.5" }) {
            bool refused = false;
            try { AgentCli.ParseRegion(bad); } catch (AgentCli.Usage) { refused = true; }
            Check(refused, $"agent: --region {bad} is refused, never clamped");
        }

        // The merge: what lands, what is refused, and with what words.
        byte[] blank = DevelopJson.Encode(DevelopJson.Defaults());
        JsonObject Merge(string edits, byte[]? on = null, string? photo = null) => AgentComposite.MergeEdits(on ?? blank, Encoding.UTF8.GetBytes(edits), photo).State;
        string Refusal(string edits, string? photo = null) {
            try { Merge(edits, photo: photo); return "accepted"; }
            catch (AgentCli.Usage e) { return e.Message; }
            catch (AgentCli.UnknownKeys e) { return "unknown " + string.Join(",", e.Keys); }
        }
        var (one, changed) = AgentComposite.MergeEdits(blank, "{\"exposureEv\": 1.0}"u8.ToArray(), null);
        var rest = (JsonObject)one.DeepClone();
        rest.Remove("exposureEv");
        var was = DevelopJson.Decode(blank)!;
        was.Remove("exposureEv");
        Check(DevelopJson.Float(one["exposureEv"]) == 1 && changed.SequenceEqual(["exposureEv"]) && JsonNode.DeepEquals(rest, was),
              "agent: an edit lands, is named changed, and moves nothing else");
        Check(Refusal("{\"exposure\": 1.0}") == "unknown exposure", "agent: an invented key is refused by name");
        Check(Refusal("{\"temperatureK\": 150}") == "temperatureK 150 is outside 2000…12000 (kelvin, absolute not a delta)",
              "agent: temperatureK 150 is refused as absolute, not a delta");
        Check(Refusal("{\"tint\": 15}").Contains("tint 15 is outside -1…1") && DevelopJson.Float(Merge("{\"tint\": 0.4}")["tint"]) == 0.4f,
              "agent: tint is held to -1…1, and 0.4 lands");
        Check(DevelopJson.Float(Merge("{\"temperatureK\": 2000}")["temperatureK"]) == 2000 && DevelopJson.Float(Merge("{\"temperatureK\": 12000}")["temperatureK"]) == 12000,
              "agent: a range's exact ends are accepted");
        Check(DevelopJson.Int(Merge("{\"rotateQuarters\": 7}")["rotateQuarters"]) == 7, "agent: a key with no product range takes any value");
        Check(DevelopJson.Floats((JsonArray)Merge("{\"gradeShadow\": [0.2, -0.1, 0.3]}")["gradeShadow"]!)!.SequenceEqual([0.2f, -0.1f, 0.3f]),
              "agent: a well-formed grade wheel lands exactly");
        Check(Refusal("{\"gradeMidtone\": [0.9, 0.9, 0]}").Contains("unit disc"), "agent: a grade wheel outside the unit disc is refused");
        Check(Refusal("{\"layers\": [{\"exposureEv\": 1.0}]}") == "layers[0] is missing 'blacks'", "agent: a partial layer is refused naming the field and index");
        var layer = DevelopJson.DefaultLayer();
        layer["exposureEv"] = 1.5; layer["contrast"] = -0.5; layer["tint"] = -0.2;
        var landed = Merge(new JsonObject { ["layers"] = new JsonArray(layer.DeepClone()) }.ToJsonString());
        Check(landed["layers"] is JsonArray { Count: 1 } ls && JsonNode.DeepEquals(ls[0], layer), "agent: a complete layer lands exactly, replacing");
        layer["exposureEv"] = 4;
        string local = Refusal(new JsonObject { ["layers"] = new JsonArray(layer.DeepClone()) }.ToJsonString());
        Check(local.Contains("-3") && !local.Contains("-5"), $"agent: a layer's exposure is held to the local -3…3, not the global range ({local})");
        string curve = "{\"curve\": {\"master\":[{\"x\":0,\"y\":0},{\"x\":1.2,\"y\":1}],\"red\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}],"
                       + "\"green\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}],\"blue\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}]}}";
        string curveWhy = Refusal(curve);
        Check(curveWhy.Contains("master") && curveWhy.Contains("outside 0…1"), $"agent: a curve point past 1 is refused naming the channel ({curveWhy})");
        var good = Merge(curve.Replace("1.2", "0.5").Replace("{\"x\":0.5,\"y\":1}", "{\"x\":0.5,\"y\":0.7},{\"x\":1,\"y\":1}"));
        Check(((JsonArray)good["curve"]!["master"]!).Count == 3, "agent: a valid three-point curve lands");

        // Legacy layer tint (#290): the base converts, the agent's own value never does.
        var legacy = DevelopJson.Defaults();
        var old = DevelopJson.DefaultLayer();
        old["tint"] = 0.2;
        legacy["layers"] = new JsonArray(old);
        legacy.Remove("localTintAxis");
        byte[] legacyBytes = Encoding.UTF8.GetBytes(legacy.ToJsonString());
        var sent = DevelopJson.DefaultLayer();
        sent["tint"] = 0.05;
        var (overLegacy, overChanged) = AgentComposite.MergeEdits(legacyBytes, Encoding.UTF8.GetBytes(new JsonObject { ["layers"] = new JsonArray(sent) }.ToJsonString()), null);
        Check(DevelopJson.Float(overLegacy["layers"]![0]!["tint"]) == 0.05f && DevelopJson.Int(overLegacy["localTintAxis"]) == 1 && overChanged.SequenceEqual(["layers"]),
              "agent: a layer tint sent over a legacy base is not negated");
        var converted = Merge("{\"exposureEv\": 0.3}", legacyBytes);
        Check(DevelopJson.Float(converted["layers"]![0]!["tint"]) == -0.2f && DevelopJson.Int(converted["localTintAxis"]) == 1,
              "agent: a legacy base's own layer tint converts to the current sign");
        legacy["localTintAxis"] = 1;
        Check(DevelopJson.Float(Merge("{\"exposureEv\": 0.3}", Encoding.UTF8.GetBytes(legacy.ToJsonString()))["layers"]![0]!["tint"]) == 0.2f,
              "agent: a base stating axis 1 keeps its layer tint");
        legacy.Remove("process");
        var (oldBase, oldChanged) = AgentComposite.MergeEdits(Encoding.UTF8.GetBytes(legacy.ToJsonString()), "{\"process\": 2, \"localTintAxis\": 1}"u8.ToArray(), null);
        Check(DevelopJson.Int(oldBase["process"]) == 2 && oldChanged.SequenceEqual(["localTintAxis", "process"]), "agent: a key an old base lacks is still a key");

        // Mask rows: kind 7 does not exist; kind 4 needs its matte file beside the photo.
        string dir = Directory.CreateTempSubdirectory("orion-agent-").FullName;
        try {
            string photo = Path.Combine(dir, "photo.ARW");
            var row = DevelopJson.DefaultMask();
            row["kind"] = 7;
            string kind7 = Refusal(new JsonObject { ["maskComponents"] = new JsonArray(row.DeepClone()) }.ToJsonString(), photo);
            Check(kind7.Contains("kind 7") && kind7.Contains("outside 1…6"), $"agent: mask kind 7 is refused ({kind7})");
            row["kind"] = 4;
            row["matteId"] = "6f9c2d0e-4c1a-4b7e-9d1f-00000000a9e1";
            string rowJson = new JsonObject { ["maskComponents"] = new JsonArray(row.DeepClone()) }.ToJsonString();
            string missing = Refusal(rowJson, photo);
            Check(missing.Contains("does not exist"), $"agent: a kind-4 row with no matte file is refused ({missing})");
            File.WriteAllBytes(MatteStore.PathFor(photo, "6f9c2d0e-4c1a-4b7e-9d1f-00000000a9e1"), [0]);
            Check(Refusal(rowJson, photo) == "accepted", "agent: a kind-4 row whose matte file exists is accepted");

            // The keys' own layer example passes the validation it documents.
            var example = AgentKeys.Json().OfType<JsonObject>().First(k => k["name"]?.GetValue<string>() == "layers")["example"]!.DeepClone();
            Check(Refusal(new JsonObject { ["layers"] = example }.ToJsonString()) == "accepted", "agent: the keys' layers example round-trips through the merge");

            // Commit writes develop through the sidecar merge: a rating already on disk survives (the shipped bug).
            File.WriteAllBytes(photo, []);
            new Sidecar { Rating = 5 }.Write(photo);
            string state = Path.Combine(dir, "state.json");
            File.WriteAllText(state, "{\"exposureEv\": 1.2}");
            AgentCli.Commit(photo, state);
            var after = Sidecar.Read(photo);
            Check(after?.Rating == 5 && after.Develop != null && DevelopJson.Float(DevelopJson.Decode(after.Develop)!["exposureEv"]) == 1.2f,
                  "agent: commit writes the edit and keeps the rating already there");
        } finally {
            Directory.Delete(dir, recursive: true);
        }

        var keys = AgentKeys.Json().OfType<JsonObject>().ToList();
        var names = keys.Select(k => k["name"]!.GetValue<string>()).ToList();
        var temperature = keys.First(k => k["name"]!.GetValue<string>() == "temperatureK");
        Check(names.SequenceEqual(names.Order(StringComparer.Ordinal)) && names.ToHashSet().SetEquals(AgentKeys.Editable)
              && temperature["unit"]?.GetValue<string>() == "kelvin" && temperature["absolute"]?.GetValue<bool>() == true
              && temperature["min"]!.GetValue<double>() >= 1000,
              "agent: keys is sorted, names exactly what the merge allows, and marks temperatureK absolute kelvin");

        uint[] histogram = new uint[128 * 3];
        histogram[127] = 1000;
        var (low, high, mean) = AgentCli.Histogram(histogram, 128);
        Check(high[0] == 1 && low[0] == 0 && mean[0] == 1, "agent: all mass in the last bin is clipped high with its mean at the top");

        Check(AgentSync.KeysOf("light").SequenceEqual(Sheets.SyncSettings.Keys(1))
              && AgentSync.KeysOf("masks").SequenceEqual(["maskComponents", "layers", "maskRefine", "maskSpace", "localTintAxis"])
              && new[] { "whiteBalance", "light", "color", "curve", "detail", "effects", "masks" }.SelectMany(AgentSync.KeysOf).All(DevelopJson.Known.Contains),
              "agent: sync's groups are the product's keys, every one a sidecar key");
        string? Targets(string text) {
            try { return string.Join(",", AgentSync.ParseTargets(Encoding.UTF8.GetBytes(text))); } catch (AgentCli.Usage) { return null; }
        }
        Check(Targets("[\"/a.arw\", \"/b.arw\"]") == "/a.arw,/b.arw" && Targets("[]") == null && Targets("[\"a.arw\"]") == null
              && Targets("[\"/a.arw\", 3]") == null && Targets("not json") == null,
              "agent: sync targets are a non-empty array of absolute paths");
    }
}
