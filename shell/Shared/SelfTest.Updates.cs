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
        bool packaged = Updates.Packaged;
        try {
            var offline = new UpdateManager(new SimpleWebSource("http://127.0.0.1:9/"), null, new TestVelopackLocator("Orion", "0.6.0", dir, null));
            string said = Updates.Check(offline).Result;
            Check(said.StartsWith(Updates.Failed) && Updates.Ready == null && !Commands.CanRun("restart-to-update"),
                  $"updates: offline is a status line, not a crash ({said})");

            // The Store's MSIX: even an installed manager is never asked, and there is nothing to check for.
            // The windows job runs the real package (tools/package-msix.sh).
            Updates.Packaged = true;
            said = Updates.Check(offline).Result;
            Check(said == Updates.FromStore && !Commands.CanRun("check-for-updates"), $"updates: the Store's build leaves updates to the Store ({said})");
        } finally {
            Updates.Packaged = packaged;
            Directory.Delete(dir, true);
        }
    }
}
