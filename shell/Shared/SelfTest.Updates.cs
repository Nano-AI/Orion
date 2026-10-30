using Velopack;
using Velopack.Locators;
using Velopack.Sources;

namespace OrionShell;

/// Updates (#389) must never cost a launch: a build Velopack did not install
/// checks nothing, and an installed one that is offline says so and goes on.
static partial class SelfTest {
    static void UpdateChecks() {
        // This run is bin/, as `dotnet run` and every CI selftest are: not installed.
        Check(Updates.Check().Result == Updates.NotInstalled, "updates: a build Velopack did not install checks nothing");

        // Installed (a test locator) but offline (a refused port): an answer, not an exception, and no restart offered.
        var dir = Directory.CreateTempSubdirectory("orion-updates-").FullName;
        try {
            var offline = new UpdateManager(new SimpleWebSource("http://127.0.0.1:9/"), null, new TestVelopackLocator("Orion", "0.6.0", dir, null));
            string said = Updates.Check(offline).Result;
            Check(said.StartsWith(Updates.Failed) && Updates.Ready == null && !Commands.CanRun("restart-to-update"),
                  $"updates: offline is a status line, not a crash ({said})");
        } finally { Directory.Delete(dir, true); }
    }
}
