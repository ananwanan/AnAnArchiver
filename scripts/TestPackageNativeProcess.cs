// Test-only launcher: .NET Process.Start does not provide lpApplicationName for
// executables beyond MAX_PATH. Explicit CreateProcessW preserves registered paths.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace AnAnArchiver.PackageValidation
{
    public sealed class NativeProcess : IDisposable
    {
        private IntPtr handle;
        private Process diagnosticProcess;
        public int Id { get; private set; }

        [StructLayout(LayoutKind.Sequential)]
        private struct SecurityAttributes
        {
            public int Length;
            public IntPtr Descriptor;
            [MarshalAs(UnmanagedType.Bool)] public bool Inherit;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct StartupInfo
        {
            public int Size;
            public IntPtr Reserved, Desktop, Title;
            public int X, Y, Width, Height, XChars, YChars, Fill, Flags;
            public short Show, ReservedBytes;
            public IntPtr ReservedData, Input, Output, Error;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct ProcessInformation
        {
            public IntPtr Process, Thread;
            public int ProcessId, ThreadId;
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool CreateProcessW(string application, StringBuilder commandLine,
            IntPtr processSecurity, IntPtr threadSecurity, [MarshalAs(UnmanagedType.Bool)] bool inherit,
            uint flags, IntPtr environment, string workingDirectory, ref StartupInfo startup,
            out ProcessInformation process);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
            ref SecurityAttributes security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetExitCodeProcess(IntPtr process, out int exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool TerminateProcess(IntPtr process, uint exitCode);
        [DllImport("kernel32.dll")]
        private static extern bool CloseHandle(IntPtr handle);

        private NativeProcess(ProcessInformation information)
        {
            handle = information.Process;
            Id = information.ProcessId;
            CloseHandle(information.Thread);
            try { diagnosticProcess = Process.GetProcessById(Id); }
            catch (ArgumentException) { /* A short command may already have exited. */ }
        }

        // Windows CRT argument escaping: double backslashes before a quote and
        // before the closing quote, while preserving all other characters.
        public static string QuoteArgument(string argument)
        {
            if (argument == null || argument.IndexOf('\0') >= 0)
                throw new ArgumentException("A process argument cannot contain NUL.");
            var quoted = new StringBuilder("\"");
            int slashes = 0;
            foreach (char character in argument)
            {
                if (character == '\\') { slashes++; continue; }
                if (character == '"') quoted.Append('\\', slashes * 2 + 1).Append('"');
                else quoted.Append('\\', slashes).Append(character);
                slashes = 0;
            }
            return quoted.Append('\\', slashes * 2).Append('"').ToString();
        }

        private static SafeFileHandle OpenInheritedFile(string path, bool input, ref SecurityAttributes attributes)
        {
            var file = CreateFileW(path, input ? 0x80000000u : 0x40000000u, 3,
                ref attributes, input ? 3u : 2u, 0x80, IntPtr.Zero);
            if (file.IsInvalid)
            {
                int error = Marshal.GetLastWin32Error();
                file.Dispose();
                throw new Win32Exception(error, "Opening inherited standard handle failed: " + path);
            }
            return file;
        }

        public static NativeProcess Start(ProcessStartInfo information, string stdoutPath, string stderrPath)
        {
            var command = new StringBuilder(QuoteArgument(information.FileName));
            foreach (string argument in information.ArgumentList) command.Append(' ').Append(QuoteArgument(argument));
            if (command.Length >= 32767) throw new ArgumentException("Process command line is too long.");
            var names = new List<string>(information.Environment.Keys);
            names.Sort(StringComparer.OrdinalIgnoreCase);
            var environmentText = new StringBuilder();
            foreach (string name in names)
            {
                string value = information.Environment[name];
                if (value != null) environmentText.Append(name).Append('=').Append(value).Append('\0');
            }
            environmentText.Append('\0');
            IntPtr environment = Marshal.StringToHGlobalUni(environmentText.ToString());
            var attributes = new SecurityAttributes { Length = Marshal.SizeOf<SecurityAttributes>(), Inherit = true };
            try
            {
                // Files avoid pipe deadlocks and remain usable by the GUI CRT.
                // NUL provides a valid stdin even when the host has no console.
                using (var input = OpenInheritedFile("NUL", true, ref attributes))
                using (var output = OpenInheritedFile(stdoutPath, false, ref attributes))
                using (var error = OpenInheritedFile(stderrPath, false, ref attributes))
                {
                    var startup = new StartupInfo {
                        Size = Marshal.SizeOf<StartupInfo>(), Flags = 0x100,
                        Input = input.DangerousGetHandle(), Output = output.DangerousGetHandle(), Error = error.DangerousGetHandle()
                    };
                    ProcessInformation process;
                    if (!CreateProcessW(information.FileName, command, IntPtr.Zero, IntPtr.Zero, true,
                        0x400u | 0x08000000u, environment, information.WorkingDirectory, ref startup, out process))
                        throw new Win32Exception(Marshal.GetLastWin32Error(), "Explicit CreateProcessW failed: " + information.FileName);
                    return new NativeProcess(process);
                }
            }
            finally { Marshal.FreeHGlobal(environment); }
        }

        public bool WaitForExit(int milliseconds)
        {
            uint result = WaitForSingleObject(handle, checked((uint)milliseconds));
            if (result == 0) return true;
            if (result == 0x102) return false;
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Waiting for native process failed.");
        }
        public int ExitCode
        {
            get
            {
                int code;
                if (!GetExitCodeProcess(handle, out code)) throw new Win32Exception(Marshal.GetLastWin32Error());
                return code;
            }
        }
        public void Refresh()
        {
            if (diagnosticProcess == null) throw new InvalidOperationException("The process already exited.");
            diagnosticProcess.Refresh();
        }
        public ProcessModuleCollection Modules
        {
            get
            {
                if (diagnosticProcess == null) throw new InvalidOperationException("The process already exited.");
                return diagnosticProcess.Modules;
            }
        }
        public void Kill(bool entireProcessTree)
        {
            try
            {
                if (diagnosticProcess != null) { diagnosticProcess.Kill(entireProcessTree); WaitForExit(5000); return; }
            }
            catch (Win32Exception) { }
            catch (InvalidOperationException) { if (WaitForExit(0)) return; }
            if (!TerminateProcess(handle, 0xffffffff)) throw new Win32Exception(Marshal.GetLastWin32Error());
            WaitForExit(5000);
        }
        public void Dispose()
        {
            if (diagnosticProcess != null) { diagnosticProcess.Dispose(); diagnosticProcess = null; }
            if (handle != IntPtr.Zero) { CloseHandle(handle); handle = IntPtr.Zero; }
        }
    }
}
