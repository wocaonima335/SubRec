using System;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

// WASAPI sync marker. Usage: marker2.exe <intervalSec> <totalSec> <logFile>
// Each marker logs QPC times so the analysis can subtract the beacon's own skew:
//   beep_qpc   when the first tone sample reaches the render mix (queue time + padding)
//   flash_qpc  after the white frame was composed (DwmFlush returned)
//   beacon_delay_ms = (beep_qpc - flash_qpc) in ms (positive: beep after flash)
class Marker2
{
    [DllImport("kernel32.dll")] static extern bool QueryPerformanceCounter(out long value);
    [DllImport("kernel32.dll")] static extern bool QueryPerformanceFrequency(out long value);
    [DllImport("dwmapi.dll")] static extern int DwmFlush();

    static long Qpc() { long v; QueryPerformanceCounter(out v); return v; }

    const int CLSCTX_ALL = 0x17;
    const int S_OK = 0;
    static readonly Guid IID_IAudioClient = new Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2");
    static readonly Guid IID_IAudioRenderClient = new Guid("F294ACFC-3146-4483-A7BF-ADDCA7C260E2");
    static readonly Guid KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = new Guid("00000003-0000-0010-8000-00AA00389B71");
    static readonly Guid KSDATAFORMAT_SUBTYPE_PCM = new Guid("00000001-0000-0010-8000-00AA00389B71");

    enum EDataFlow { eRender = 0, eCapture = 1, eAll = 2 }
    enum ERole { eConsole = 0, eMultimedia = 1, eCommunications = 2 }
    enum AudioClientShareMode { Shared = 0, Exclusive = 1 }
    [Flags] enum AudioClientStreamFlags : uint
    {
        None = 0, EventCallback = 0x00040000, NoPersist = 0x80000000
    }

    // IID_IMMDeviceEnumerator (the BCDE0395 GUID below is the coclass CLSID).
    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceEnumerator
    {
        [PreserveSig] int EnumAudioEndpoints(EDataFlow dataFlow, uint stateMask, out IntPtr devices);
        [PreserveSig] int GetDefaultAudioEndpoint(EDataFlow dataFlow, ERole role, out IMMDevice endpoint);
        [PreserveSig] int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IMMDevice device);
        [PreserveSig] int RegisterEndpointNotificationCallback(IntPtr client);
        [PreserveSig] int UnregisterEndpointNotificationCallback(IntPtr client);
    }

    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDevice
    {
        [PreserveSig] int Activate(ref Guid iid, uint clsctx, IntPtr activationParams,
            [MarshalAs(UnmanagedType.Interface)] out IAudioClient audioClient);
        [PreserveSig] int OpenPropertyStore(uint access, out IntPtr properties);
        [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
        [PreserveSig] int GetState(out uint state);
    }

    [ComImport, Guid("1CB9AD4C-DBFA-4C32-B178-C2F568A703B2"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioClient
    {
        [PreserveSig] int Initialize(AudioClientShareMode shareMode, AudioClientStreamFlags streamFlags,
            long hnsBufferDuration, long hnsPeriodicity, IntPtr format, IntPtr sessionGuid);
        [PreserveSig] int GetBufferSize(out uint numBufferFrames);
        [PreserveSig] int GetStreamLatency(out long hnsLatency);
        [PreserveSig] int GetCurrentPadding(out uint numPaddingFrames);
        [PreserveSig] int IsFormatSupported(AudioClientShareMode shareMode, IntPtr format, out IntPtr closestMatch);
        [PreserveSig] int GetMixFormat(out IntPtr format);
        [PreserveSig] int GetDevicePeriod(out long defaultPeriod, out long minimumPeriod);
        [PreserveSig] int Start();
        [PreserveSig] int Stop();
        [PreserveSig] int Reset();
        [PreserveSig] int SetEventHandle(IntPtr eventHandle);
        [PreserveSig] int GetService(ref Guid riid, [MarshalAs(UnmanagedType.Interface)] out IAudioRenderClient service);
    }

    [ComImport, Guid("F294ACFC-3146-4483-A7BF-ADDCA7C260E2"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioRenderClient
    {
        [PreserveSig] int GetBuffer(uint numFramesRequested, out IntPtr data);
        [PreserveSig] int ReleaseBuffer(uint numFramesWritten, uint flags);
    }

    struct Timing
    {
        public uint PaddingFrames;
        public uint BufferFrames;
        public long StreamLatency100ns;
        public int SampleRate;
        public long QueueQpc;   // QPC when the padding was read, right before the tone was written
        public double QueueDelaySeconds { get { return (double)PaddingFrames / SampleRate; } }
        public double StreamLatencySeconds { get { return StreamLatency100ns / 10000000.0; } }
    }

    sealed class WasapiTone : IDisposable
    {
        IAudioClient client;
        IAudioRenderClient render;
        IntPtr formatPtr;
        uint bufferFrames;
        long streamLatency;
        int sampleRate, channels, bits, blockAlign;
        bool isFloat;
        int toneFrames, tonePosition;
        bool tonePending;
        bool started;

        public int SampleRate { get { return sampleRate; } }

        public WasapiTone()
        {
            IMMDeviceEnumerator enumerator = null;
            IMMDevice device = null;
            try
            {
                enumerator = (IMMDeviceEnumerator)new MMDeviceEnumerator();
                Check(enumerator.GetDefaultAudioEndpoint(EDataFlow.eRender, ERole.eConsole, out device), "GetDefaultAudioEndpoint");
                Guid iid = IID_IAudioClient;
                Check(device.Activate(ref iid, CLSCTX_ALL, IntPtr.Zero, out client), "IMMDevice.Activate");
                Check(client.GetMixFormat(out formatPtr), "GetMixFormat");
                ReadFormat(formatPtr);
                // Shared timer-polled stream with approximately a 20 ms endpoint buffer.
                Check(client.Initialize(AudioClientShareMode.Shared, AudioClientStreamFlags.None,
                    200000, 0, formatPtr, IntPtr.Zero), "IAudioClient.Initialize");
                Check(client.GetBufferSize(out bufferFrames), "GetBufferSize");
                Check(client.GetStreamLatency(out streamLatency), "GetStreamLatency");
                Guid riid = IID_IAudioRenderClient;
                Check(client.GetService(ref riid, out render), "GetService(IAudioRenderClient)");
                toneFrames = Math.Max(1, sampleRate / 10);
                Check(client.Start(), "IAudioClient.Start");
                started = true;
                Pump(); // Prime the continuously running stream with silence.
            }
            catch
            {
                Dispose();
                throw;
            }
            finally
            {
                if (device != null) Marshal.ReleaseComObject(device);
                if (enumerator != null) Marshal.ReleaseComObject(enumerator);
            }
        }

        static void Check(int hr, string operation)
        {
            if (hr < 0) Marshal.ThrowExceptionForHR(hr);
        }

        void ReadFormat(IntPtr p)
        {
            ushort tag = (ushort)Marshal.ReadInt16(p, 0);
            channels = (ushort)Marshal.ReadInt16(p, 2);
            sampleRate = Marshal.ReadInt32(p, 4);
            blockAlign = (ushort)Marshal.ReadInt16(p, 12);
            bits = (ushort)Marshal.ReadInt16(p, 14);
            isFloat = tag == 3;
            if (tag == 0xFFFE)
            {
                // WAVEFORMATEXTENSIBLE: valid bits at +18, channel mask +20, subtype GUID +24.
                Guid subtype = (Guid)Marshal.PtrToStructure(new IntPtr(p.ToInt64() + 24), typeof(Guid));
                isFloat = subtype == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
                tag = subtype == KSDATAFORMAT_SUBTYPE_PCM ? (ushort)1 : tag;
            }
            if ((!isFloat && tag != 1) || (isFloat && bits != 32) || (!isFloat && bits != 16))
                throw new NotSupportedException("WASAPI mix format must be float32 or int16 PCM");
            if (channels <= 0 || sampleRate <= 0 || blockAlign <= 0)
                throw new InvalidDataException("Invalid WASAPI mix format");
        }

        public Timing QueueBeep()
        {
            uint padding;
            Check(client.GetCurrentPadding(out padding), "GetCurrentPadding");
            long queueQpc = Qpc();
            tonePending = true;
            return new Timing { PaddingFrames = padding, BufferFrames = bufferFrames,
                StreamLatency100ns = streamLatency, SampleRate = sampleRate, QueueQpc = queueQpc };
        }

        public void Pump()
        {
            if (render == null) return;
            uint padding;
            Check(client.GetCurrentPadding(out padding), "GetCurrentPadding");
            uint available = bufferFrames > padding ? bufferFrames - padding : 0;
            if (available == 0) return;
            IntPtr data;
            Check(render.GetBuffer(available, out data), "IAudioRenderClient.GetBuffer");
            try
            {
                if (tonePending) { tonePending = false; tonePosition = 0; }
                byte[] bytes = new byte[checked((int)((long)available * blockAlign))];
                for (uint f = 0; f < available; f++)
                {
                    double value = 0.0;
                    if (tonePosition < toneFrames)
                    {
                        int i = tonePosition++;
                        double env = Math.Min(1.0, Math.Min(i, toneFrames - 1 - i) / (sampleRate / 1000.0));
                        value = 0.5 * env * Math.Sin(2.0 * Math.PI * 1000.0 * i / sampleRate);
                    }
                    else if (tonePosition == toneFrames) tonePosition++;
                    int off = (int)f * blockAlign;
                    for (int ch = 0; ch < channels; ch++)
                    {
                        int at = off + ch * (bits / 8);
                        if (isFloat)
                        {
                            byte[] b = BitConverter.GetBytes((float)value);
                            Buffer.BlockCopy(b, 0, bytes, at, 4);
                        }
                        else
                        {
                            short s = (short)Math.Max(short.MinValue, Math.Min(short.MaxValue, value * 32767.0));
                            bytes[at] = (byte)s;
                            bytes[at + 1] = (byte)(s >> 8);
                        }
                    }
                }
                Marshal.Copy(bytes, 0, data, bytes.Length);
            }
            finally { Check(render.ReleaseBuffer(available, 0), "IAudioRenderClient.ReleaseBuffer"); }
        }

        public void Dispose()
        {
            try { if (started && client != null) client.Stop(); } catch { }
            started = false;
            if (render != null) { Marshal.ReleaseComObject(render); render = null; }
            if (client != null) { Marshal.ReleaseComObject(client); client = null; }
            if (formatPtr != IntPtr.Zero) { Marshal.FreeCoTaskMem(formatPtr); formatPtr = IntPtr.Zero; }
        }
    }

    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    class MMDeviceEnumerator { }

    [STAThread]
    static void Main(string[] args)
    {
        int interval = int.Parse(args[0], CultureInfo.InvariantCulture);
        int total = int.Parse(args[1], CultureInfo.InvariantCulture);
        string logPath = args[2];
        var audio = new WasapiTone();
        var form = new Form { FormBorderStyle = FormBorderStyle.None, StartPosition = FormStartPosition.Manual,
            Bounds = Screen.PrimaryScreen.Bounds, TopMost = true, BackColor = Color.Black, ShowInTaskbar = false };
        var writer = new StreamWriter(logPath, true) { AutoFlush = true };
        var sw = System.Diagnostics.Stopwatch.StartNew();
        long qpcFrequency;
        QueryPerformanceFrequency(out qpcFrequency);
        writer.WriteLine("start_utc=" + DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture) +
            " interval=" + interval + " total=" + total + " qpc_freq=" + qpcFrequency);
        int count = 0;
        double next = 3.0;
        var timer = new System.Windows.Forms.Timer { Interval = 5 };
        var offTimer = new System.Windows.Forms.Timer { Interval = 200 };
        form.Opacity = 0.01; form.Show();
        offTimer.Tick += delegate
        {
            offTimer.Stop();
            form.Opacity = 0.01; // Set opacity before black to avoid the transient black frame.
            form.BackColor = Color.Black;
        };
        timer.Tick += delegate
        {
            audio.Pump();
            double now = sw.Elapsed.TotalSeconds;
            if (now >= total)
            {
                writer.WriteLine("end elapsed=" + now.ToString("F3", CultureInfo.InvariantCulture));
                writer.Close(); timer.Stop(); offTimer.Stop(); audio.Dispose(); Application.Exit(); return;
            }
            if (now >= next)
            {
                next += interval; count++;
                double before = sw.Elapsed.TotalSeconds;
                Timing t = audio.QueueBeep();
                audio.Pump(); // Put the tone in the next available render buffer now.
                form.BackColor = Color.White; form.Opacity = 1.0; form.Refresh();
                // Wait for the composition that contains the white frame; its return time
                // is the best user-mode estimate of when the flash reached the desktop.
                int dwm = DwmFlush();
                long flashQpc = Qpc();
                // The tone's first sample is written behind PaddingFrames already queued.
                long beepQpc = t.QueueQpc + (long)((double)t.PaddingFrames * qpcFrequency / t.SampleRate);
                double beaconDelayMs = (beepQpc - flashQpc) * 1000.0 / qpcFrequency;
                offTimer.Start();
                writer.WriteLine("marker " + count + " elapsed=" + before.ToString("F3", CultureInfo.InvariantCulture) +
                    " utc=" + DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture) +
                    " flash_after=" + (sw.Elapsed.TotalSeconds - before).ToString("F4", CultureInfo.InvariantCulture) +
                    " padding=" + t.PaddingFrames + " latency=" + t.StreamLatencySeconds.ToString("F6", CultureInfo.InvariantCulture) +
                    " padding_frames=" + t.PaddingFrames + " queue_delay=" + t.QueueDelaySeconds.ToString("F6", CultureInfo.InvariantCulture) +
                    " stream_latency=" + t.StreamLatencySeconds.ToString("F6", CultureInfo.InvariantCulture) +
                    " buffer_frames=" + t.BufferFrames +
                    " beep_qpc=" + beepQpc + " flash_qpc=" + flashQpc + " dwm_flush=" + dwm +
                    " beacon_delay_ms=" + beaconDelayMs.ToString("F3", CultureInfo.InvariantCulture));
            }
        };
        timer.Start();
        try { Application.Run(form); }
        finally { try { writer.Close(); } catch { } audio.Dispose(); }
    }
}
