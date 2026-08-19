Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Stop-Process -Name "CT0405_ControlPanel" -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

$proc = Start-Process -FilePath "build\bin\Release\CT0405_ControlPanel.exe" -PassThru
Start-Sleep -Milliseconds 1800

$code = @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public class WinCapture {
    [DllImport("user32.dll")]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBmp, uint nFlags);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    public static string Capture(string path) {
        IntPtr hwnd = FindWindow("CT0405_ControlPanel_Class", null);
        if (hwnd == IntPtr.Zero) return "Window not found";
        ShowWindow(hwnd, 9); // SW_RESTORE
        SetForegroundWindow(hwnd);
        System.Threading.Thread.Sleep(400);
        RECT r;
        GetWindowRect(hwnd, out r);
        int w = r.Right - r.Left;
        int h = r.Bottom - r.Top;
        if (w <= 0 || h <= 0) return "Invalid size: " + w + "x" + h;
        using (Bitmap bmp = new Bitmap(w, h)) {
            using (Graphics g = Graphics.FromImage(bmp)) {
                IntPtr hdc = g.GetHdc();
                PrintWindow(hwnd, hdc, 2);
                g.ReleaseHdc(hdc);
            }
            bmp.Save(path, ImageFormat.Png);
        }
        return "Saved " + w + "x" + h + " to " + path;
    }
}
"@

Add-Type -TypeDefinition $code -ReferencedAssemblies System.Drawing

$res = [WinCapture]::Capture("C:\Users\tiaz\.gemini\antigravity-ide\brain\dd580a6b-3599-4e97-999e-129ce8b9f8b0\screenshot.png")
Write-Output $res
