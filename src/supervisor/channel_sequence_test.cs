// Offline integration check: spawn only PowerShell, never a radio backend.
using System;
using System.IO;

internal static class ChannelSequenceTest
{
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    public static int Main()
    {
        string oldSequence = Environment.GetEnvironmentVariable("AWDL_CHANSEQ");
        string oldFollow = Environment.GetEnvironmentVariable("AWDL_FOLLOW_MASTER_SEQ");
        try
        {
            // Deliberately contaminate the parent. Checking the dictionary alone
            // would miss the old getenv/presence bug in the real child process.
            Environment.SetEnvironmentVariable("AWDL_CHANSEQ", "social");
            Environment.SetEnvironmentVariable("AWDL_FOLLOW_MASTER_SEQ", "1");
            foreach (string mode in new[] { "fixed", "social" })
            {
                var supervisor = new Supervisor(".", "offline", false, 149, 20, null, mode);
                string observed = null;
                string shell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
                                             @"WindowsPowerShell\v1.0\powershell.exe");
                string command = "-NoProfile -NonInteractive -Command \"" +
                    "Write-Output ('seq=' + [Environment]::GetEnvironmentVariable('AWDL_CHANSEQ') +" +
                    " ';follow=' + [Environment]::GetEnvironmentVariable('AWDL_FOLLOW_MASTER_SEQ'));" +
                    "Write-Output 'probe ready'\"";
                var child = new Child("probe", shell, command, supervisor.AwdldEnvironment(),
                    Environment.CurrentDirectory, "probe ready", new LogSink(".", false),
                    delegate(string line) { if (line.StartsWith("seq=")) observed = line; });
                string error;
                try
                {
                    Require(child.Start(out error), error);
                    Require(child.WaitReady(15000, out error), error);
                    Require(observed == "seq=" + (mode == "social" ? "social" : "") + ";follow=",
                            "wrong child environment: " + observed);
                    Require(supervisor.Plan().Contains("chanseq: " + mode), "plan lost mode");
                }
                finally { child.Stop(5000); }
                Console.WriteLine("PASS: " + mode + " child environment, inherited follow removed");
            }
            bool rejected = false;
            try { new Supervisor(".", "offline", false, 149, 20, null, "0"); }
            catch (ArgumentException) { rejected = true; }
            Require(rejected, "invalid mode accepted");
            Console.WriteLine("PASS: invalid mode rejected; no radio process started");
            return 0;
        }
        finally
        {
            Environment.SetEnvironmentVariable("AWDL_CHANSEQ", oldSequence);
            Environment.SetEnvironmentVariable("AWDL_FOLLOW_MASTER_SEQ", oldFollow);
        }
    }
}
