using System.Runtime.InteropServices;

namespace MusicBitMaker;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Contains("--self-test", StringComparer.OrdinalIgnoreCase))
            return PianoLayout.SelfTest() ? 0 : 1;

        ApplicationConfiguration.Initialize();

        try
        {
            Application.Run(new PianoForm());
            return 0;
        }
        catch (Exception exception)
        {
            MessageBox.Show(
                $"피아노를 시작하지 못했습니다.\n\n{exception.Message}",
                "MusicBitMaker",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return 1;
        }
    }
}

internal static class PianoLayout
{
    internal sealed record Note(Keys Key, string Shortcut, string Name, int Midi);

    internal static readonly Note[] Notes =
    [
        new(Keys.A, "A", "도", 60),
        new(Keys.S, "S", "레", 62),
        new(Keys.D, "D", "미", 64),
        new(Keys.F, "F", "파", 65),
        new(Keys.G, "G", "솔", 67),
        new(Keys.H, "H", "라", 69),
        new(Keys.J, "J", "시", 71),
        new(Keys.K, "K", "도", 72)
    ];

    internal static bool SelfTest() =>
        Notes.Select(note => note.Midi).SequenceEqual([60, 62, 64, 65, 67, 69, 71, 72]) &&
        Notes.Select(note => note.Key).Distinct().Count() == Notes.Length;
}

internal sealed class PianoForm : Form
{
    private readonly MidiPiano piano = new();
    private readonly Dictionary<Keys, PianoKey> keys;
    private readonly HashSet<Keys> pressedKeys = [];
    private readonly HashSet<int> mouseNotes = [];

    internal PianoForm()
    {
        Text = "MusicBitMaker - Piano";
        ClientSize = new Size(920, 430);
        MinimumSize = new Size(720, 360);
        StartPosition = FormStartPosition.CenterScreen;
        BackColor = Color.FromArgb(22, 24, 31);
        ForeColor = Color.White;
        KeyPreview = true;

        var title = new Label
        {
            AutoSize = true,
            Text = "PIANO",
            Font = new Font("Segoe UI", 28, FontStyle.Bold),
            ForeColor = Color.White,
            Location = new Point(28, 22)
        };

        var help = new Label
        {
            AutoSize = true,
            Text = "건반을 클릭하거나 키보드 A S D F G H J K 로 연주하세요",
            Font = new Font("Segoe UI", 11),
            ForeColor = Color.FromArgb(165, 170, 184),
            Location = new Point(31, 78)
        };

        var keyboard = new TableLayoutPanel
        {
            Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
            BackColor = Color.FromArgb(12, 14, 19),
            ColumnCount = PianoLayout.Notes.Length,
            Location = new Point(28, 120),
            Padding = new Padding(8),
            RowCount = 1,
            Size = new Size(864, 280)
        };

        for (var index = 0; index < PianoLayout.Notes.Length; index++)
        {
            keyboard.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f / PianoLayout.Notes.Length));
            var note = PianoLayout.Notes[index];
            var key = new PianoKey(note);
            key.MouseDown += (_, eventArgs) =>
            {
                if (eventArgs.Button != MouseButtons.Left || !mouseNotes.Add(note.Midi))
                    return;

                key.Capture = true;
                Press(key);
            };
            key.MouseUp += (_, _) =>
            {
                if (!mouseNotes.Remove(note.Midi))
                    return;

                key.Capture = false;
                Release(key);
            };
            keyboard.Controls.Add(key, index, 0);
        }

        keys = keyboard.Controls.Cast<PianoKey>().ToDictionary(key => key.Note.Key);
        Controls.AddRange([title, help, keyboard]);

        KeyDown += HandleKeyDown;
        KeyUp += HandleKeyUp;
        Deactivate += (_, _) => ReleaseAll();
    }

    private void HandleKeyDown(object? sender, KeyEventArgs e)
    {
        if (!keys.TryGetValue(e.KeyCode, out var key) || !pressedKeys.Add(e.KeyCode))
            return;

        Press(key);
        e.SuppressKeyPress = true;
    }

    private void HandleKeyUp(object? sender, KeyEventArgs e)
    {
        if (!keys.TryGetValue(e.KeyCode, out var key) || !pressedKeys.Remove(e.KeyCode))
            return;

        Release(key);
        e.SuppressKeyPress = true;
    }

    private void Press(PianoKey key)
    {
        if (!key.IsPressed)
            piano.NoteOn(key.Note.Midi);

        key.IsPressed = true;
    }

    private void Release(PianoKey key)
    {
        if (pressedKeys.Contains(key.Note.Key) || mouseNotes.Contains(key.Note.Midi))
            return;

        piano.NoteOff(key.Note.Midi);
        key.IsPressed = false;
    }

    private void ReleaseAll()
    {
        foreach (var key in keys.Values.Where(key => key.IsPressed))
        {
            piano.NoteOff(key.Note.Midi);
            key.IsPressed = false;
            key.Capture = false;
        }

        pressedKeys.Clear();
        mouseNotes.Clear();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            ReleaseAll();
            piano.Dispose();
        }

        base.Dispose(disposing);
    }
}

internal sealed class PianoKey : Label
{
    private bool isPressed;

    internal PianoLayout.Note Note { get; }

    internal bool IsPressed
    {
        get => isPressed;
        set
        {
            isPressed = value;
            BackColor = value ? Color.FromArgb(111, 86, 255) : Color.FromArgb(247, 247, 250);
            ForeColor = value ? Color.White : Color.FromArgb(29, 31, 40);
        }
    }

    internal PianoKey(PianoLayout.Note note)
    {
        Note = note;
        AccessibleDescription = $"피아노 {note.Name}, 키보드 {note.Shortcut} 키";
        AccessibleName = $"{note.Name} 건반";
        AccessibleRole = AccessibleRole.PushButton;
        AutoSize = false;
        BackColor = Color.FromArgb(247, 247, 250);
        Dock = DockStyle.Fill;
        Font = new Font("Segoe UI", 13, FontStyle.Bold);
        ForeColor = Color.FromArgb(29, 31, 40);
        Cursor = Cursors.Hand;
        Margin = new Padding(4, 4, 4, 10);
        Padding = new Padding(0, 0, 0, 18);
        Text = $"{note.Name}\n\n{note.Shortcut}";
        TextAlign = ContentAlignment.BottomCenter;
    }
}

internal sealed class MidiPiano : IDisposable
{
    private IntPtr handle;

    internal MidiPiano()
    {
        const uint midiMapper = uint.MaxValue;
        var result = midiOutOpen(out handle, midiMapper, IntPtr.Zero, IntPtr.Zero, 0);
        if (result != 0)
            throw new InvalidOperationException($"Windows MIDI 장치를 열 수 없습니다. 오류 코드: {result}");

        Send(0xC0); // Acoustic Grand Piano
    }

    internal void NoteOn(int note) => Send(0x90 | note << 8 | 100 << 16);

    internal void NoteOff(int note) => Send(0x80 | note << 8);

    private void Send(int message)
    {
        if (handle != IntPtr.Zero)
            midiOutShortMsg(handle, message);
    }

    public void Dispose()
    {
        if (handle == IntPtr.Zero)
            return;

        Send(0xB0 | 123 << 8); // All notes off
        midiOutClose(handle);
        handle = IntPtr.Zero;
    }

    [DllImport("winmm.dll")]
    private static extern uint midiOutOpen(
        out IntPtr handle,
        uint deviceId,
        IntPtr callback,
        IntPtr instance,
        uint flags);

    [DllImport("winmm.dll")]
    private static extern uint midiOutShortMsg(IntPtr handle, int message);

    [DllImport("winmm.dll")]
    private static extern uint midiOutClose(IntPtr handle);
}
