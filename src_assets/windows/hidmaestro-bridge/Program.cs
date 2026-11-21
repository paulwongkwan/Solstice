// HIDMaestro bridge for Solstice.
//
// Solstice drives virtual gamepads through a small binary protocol on a named
// pipe. This process owns the HIDMaestro SDK: it installs the driver when an
// elevated caller asks for it, creates and destroys controllers per slot,
// submits input state, and forwards rumble and lightbar output back to Solstice.

using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading;
using HIDMaestro;

namespace Solstice.HIDMaestroBridge;

/// <summary>Message types exchanged with Solstice.</summary>
internal enum MessageType
{
    Ack = 0x80,
    Ready = 0x81,
    Rumble = 0x82,
    Led = 0x83,
    Log = 0x84,
    Hello = 1,
    Create = 2,
    Destroy = 3,
    Input = 4,
    Touch = 5,
    Motion = 6,
    Battery = 7,
    Shutdown = 8,
}

/// <summary>Moonlight button masks, as documented by src/platform/common.h.</summary>
[Flags]
internal enum Button : uint
{
    DpadUp = 0x000001,
    DpadDown = 0x000002,
    DpadLeft = 0x000004,
    DpadRight = 0x000008,
    Start = 0x000010,
    Back = 0x000020,
    LeftStick = 0x000040,
    RightStick = 0x000080,
    LeftShoulder = 0x000100,
    RightShoulder = 0x000200,
    Home = 0x000400,
    South = 0x001000,
    East = 0x002000,
    West = 0x004000,
    North = 0x008000,
    Paddle1 = 0x010000,
    Paddle2 = 0x020000,
    Paddle3 = 0x040000,
    Paddle4 = 0x080000,
    Touchpad = 0x100000,
    Misc = 0x200000,
}

/// <summary>Supported virtual controller identities.</summary>
internal enum ProfileKind
{
    Xbox360 = 1,
    XboxOne = 2,
    XboxSeries = 3,
    DualShock4 = 4,
    DualSense = 5,
    SwitchPro = 6,
    SteamController = 7,
}

/// <summary>Per-slot bridge state.</summary>
internal sealed class Slot
{
    public HMController? Controller;
    public HMGamepadState State;
    public ProfileKind Kind;
    public bool Touch0Active;
    public bool Touch1Active;
}

/// <summary>Owns the SDK context and implements the pipe protocol.</summary>
internal sealed class Bridge : IDisposable
{
    private readonly HMContext context = new();
    private readonly Slot[] slots = new Slot[16];
    private readonly Dictionary<HMController, int> slotByController = new();
    private readonly Stream output;
    private readonly object sync = new();
    private readonly object writeGate = new();
    private bool driverInstalled;

    public Bridge(Stream output)
    {
        this.output = output;
        for (int i = 0; i < slots.Length; i++)
        {
            slots[i] = new Slot();
        }

        HMOemNameOverride.RecoverOrphans();
        context.LoadDefaultProfiles();
    }

    /// <summary>Installs the driver when needed and reports whether a controller can be created.</summary>
    public (int Status, string Message) Initialize()
    {
        try
        {
            driverInstalled = context.IsDriverInstalled;
            if (!driverInstalled)
            {
                // Requires elevation; the Solstice service runs as SYSTEM.
                context.InstallDriver();
                driverInstalled = context.IsDriverInstalled;
            }

            return driverInstalled
                ? (0, "HIDMaestro driver is ready")
                : (2, "HIDMaestro driver installation did not complete");
        }
        catch (Exception exception)
        {
            return (3, exception.Message);
        }
    }

    private static string ProfileId(ProfileKind kind) => kind switch
    {
        ProfileKind.Xbox360 => "xbox-360-wired",
        ProfileKind.XboxOne => "xbox-one-s",
        ProfileKind.XboxSeries => "xbox-series-xs",
        ProfileKind.DualShock4 => "dualshock-4-v2",
        ProfileKind.DualSense => "dualsense",
        ProfileKind.SwitchPro => "switch-pro",
        ProfileKind.SteamController => "steam-controller-2",
        _ => "xbox-series-xs",
    };

    /// <summary>Creates a controller in a slot.</summary>
    public int Create(int slot, ProfileKind kind)
    {
        if (slot < 0 || slot >= slots.Length)
        {
            return 1;
        }

        try
        {
            lock (sync)
            {
                DisposeSlot(slot);

                HMProfile profile = context.GetProfile(ProfileId(kind))
                    ?? throw new InvalidOperationException($"Profile '{ProfileId(kind)}' is unavailable");

                HMController controller = context.CreateControllerAt(slot, profile);
                controller.OutputReceived += OnOutput;
                slots[slot].Controller = controller;
                slots[slot].Kind = kind;
                slots[slot].State = default;
                slots[slot].Touch0Active = false;
                slots[slot].Touch1Active = false;
                slotByController[controller] = slot;
            }

            return 0;
        }
        catch (Exception exception)
        {
            SendLog(exception.Message);
            return 3;
        }
    }

    /// <summary>Destroys the controller in a slot.</summary>
    public int Destroy(int slot)
    {
        if (slot < 0 || slot >= slots.Length)
        {
            return 1;
        }

        lock (sync)
        {
            DisposeSlot(slot);
        }

        return 0;
    }

    private void DisposeSlot(int slot)
    {
        HMController? controller = slots[slot].Controller;
        slots[slot].Controller = null;
        if (controller == null)
        {
            return;
        }

        controller.OutputReceived -= OnOutput;
        slotByController.Remove(controller);
        controller.Dispose();
    }

    /// <summary>Submits a full gamepad state.</summary>
    public int SubmitInput(int slot, uint buttons, short lx, short ly, short rx, short ry, byte lt, byte rt)
    {
        if (slot < 0 || slot >= slots.Length)
        {
            return 1;
        }

        lock (sync)
        {
            HMController? controller = slots[slot].Controller;
            if (controller == null)
            {
                return 2;
            }

            HMGamepadState state = slots[slot].State;
            state.Buttons = MapButtons(buttons);
            state.Hat = MapHat(buttons);
            state.Axes = HMGamepadStateHelpers.StandardAxes(
                controller.Profile,
                leftStickX: NormalizeAxis(lx),
                leftStickY: NormalizeAxis(ly),
                rightStickX: NormalizeAxis(rx),
                rightStickY: NormalizeAxis(ry),
                leftTrigger: lt / 255f,
                rightTrigger: rt / 255f);

            controller.SubmitState(state);
            slots[slot].State = state;
            return 0;
        }
    }

    /// <summary>Submits a touchpad contact lifecycle event.</summary>
    public int SubmitTouch(int slot, byte contact, byte eventType, ushort x, ushort y, ushort pressure)
    {
        if (slot < 0 || slot >= slots.Length || contact > 1)
        {
            return 1;
        }

        lock (sync)
        {
            HMController? controller = slots[slot].Controller;
            if (controller == null)
            {
                return 2;
            }

            HMGamepadState state = slots[slot].State;

            // Moonlight event codes: hover 0, down 1, up 2, move 3, cancel 4, cancel all 5.
            if (eventType == 5)
            {
                slots[slot].Touch0Active = false;
                slots[slot].Touch1Active = false;
                state.TouchpadFinger0Active = false;
                state.TouchpadFinger1Active = false;
            }
            else
            {
                bool active = eventType is 0 or 1 or 3;
                if (contact == 0)
                {
                    slots[slot].Touch0Active = active;
                    state.TouchpadFinger0Active = active;
                    state.TouchpadFinger0X = x;
                    state.TouchpadFinger0Y = y;
                    state.TouchpadFinger0Pressure = pressure / 65535f;
                }
                else
                {
                    slots[slot].Touch1Active = active;
                    state.TouchpadFinger1Active = active;
                    state.TouchpadFinger1X = x;
                    state.TouchpadFinger1Y = y;
                    state.TouchpadFinger1Pressure = pressure / 65535f;
                }
            }

            controller.SubmitState(state);
            slots[slot].State = state;
            return 0;
        }
    }

    /// <summary>Submits one accelerometer or gyroscope sample.</summary>
    public int SubmitMotion(int slot, byte kind, int x, int y, int z)
    {
        if (slot < 0 || slot >= slots.Length)
        {
            return 1;
        }

        lock (sync)
        {
            HMController? controller = slots[slot].Controller;
            if (controller == null)
            {
                return 2;
            }

            HMGamepadState state = slots[slot].State;
            if (kind == 1)
            {
                // milli m/s^2 -> g
                state.AccelGX = x / 9806.65f;
                state.AccelGY = y / 9806.65f;
                state.AccelGZ = z / 9806.65f;
            }
            else
            {
                // milli deg/s -> deg/s
                state.GyroDpsX = x / 1000f;
                state.GyroDpsY = y / 1000f;
                state.GyroDpsZ = z / 1000f;
            }

            controller.SubmitState(state);
            slots[slot].State = state;
            return 0;
        }
    }

    /// <summary>Submits the client's battery state.</summary>
    public int SubmitBattery(int slot, byte percent, byte flags)
    {
        if (slot < 0 || slot >= slots.Length)
        {
            return 1;
        }

        lock (sync)
        {
            HMController? controller = slots[slot].Controller;
            if (controller == null)
            {
                return 2;
            }

            HMGamepadState state = slots[slot].State;
            state.BatteryLevel = percent;
            state.BatteryCharging = flags == 3;
            state.BatteryFull = flags == 4;
            controller.SubmitState(state);
            slots[slot].State = state;
            return 0;
        }
    }

    private static float NormalizeAxis(short value) => (value + 32768f) / 65535f;

    private static HMButton MapButtons(uint flags)
    {
        HMButton buttons = HMButton.None;
        if ((flags & (uint)Button.South) != 0) buttons |= HMButton.A;
        if ((flags & (uint)Button.East) != 0) buttons |= HMButton.B;
        if ((flags & (uint)Button.West) != 0) buttons |= HMButton.X;
        if ((flags & (uint)Button.North) != 0) buttons |= HMButton.Y;
        if ((flags & (uint)Button.LeftShoulder) != 0) buttons |= HMButton.LeftBumper;
        if ((flags & (uint)Button.RightShoulder) != 0) buttons |= HMButton.RightBumper;
        if ((flags & (uint)Button.Start) != 0) buttons |= HMButton.Start;
        if ((flags & (uint)Button.Back) != 0) buttons |= HMButton.Back;
        if ((flags & (uint)Button.LeftStick) != 0) buttons |= HMButton.LeftStick;
        if ((flags & (uint)Button.RightStick) != 0) buttons |= HMButton.RightStick;
        if ((flags & (uint)Button.Home) != 0) buttons |= HMButton.Guide;
        if ((flags & (uint)Button.Touchpad) != 0) buttons |= HMButton.Touchpad;
        if ((flags & (uint)Button.Misc) != 0) buttons |= HMButton.Misc1;
        if ((flags & (uint)Button.Paddle1) != 0) buttons |= HMButton.LeftPaddle;
        if ((flags & (uint)Button.Paddle2) != 0) buttons |= HMButton.RightPaddle;
        if ((flags & (uint)Button.Paddle3) != 0) buttons |= HMButton.LeftPaddle2;
        if ((flags & (uint)Button.Paddle4) != 0) buttons |= HMButton.RightPaddle2;
        return buttons;
    }

    private static HMHat MapHat(uint flags)
    {
        bool up = (flags & (uint)Button.DpadUp) != 0;
        bool down = (flags & (uint)Button.DpadDown) != 0;
        bool left = (flags & (uint)Button.DpadLeft) != 0;
        bool right = (flags & (uint)Button.DpadRight) != 0;

        if (up) return left ? HMHat.NorthWest : right ? HMHat.NorthEast : HMHat.North;
        if (down) return left ? HMHat.SouthWest : right ? HMHat.SouthEast : HMHat.South;
        if (left) return HMHat.West;
        if (right) return HMHat.East;
        return HMHat.None;
    }

    /// <summary>Forwards rumble and lightbar requests from the virtual device to Solstice.</summary>
    private void OnOutput(HMController controller, HMOutputPacket packet)
    {
        int slot;
        lock (sync)
        {
            if (!slotByController.TryGetValue(controller, out slot))
            {
                return;
            }
        }

        (ushort Low, ushort High, ushort LeftTrigger, ushort RightTrigger, byte? Red, byte? Green, byte? Blue) decoded = packet.Source switch
        {
            HMOutputSource.XInput => DecodeXInput(packet.Data.Span),
            HMOutputSource.HidOutput => DecodeHidOutput(slots[slot].Kind, packet.ReportId, packet.Data.Span),
            _ => default,
        };

        SendAck(MessageType.Rumble, slot, 0, decoded.Low, decoded.High, decoded.LeftTrigger, decoded.RightTrigger);
        if (decoded.Red is { } red && decoded.Green is { } green && decoded.Blue is { } blue)
        {
            SendLed(slot, red, green, blue);
        }
    }

    private static (ushort, ushort, ushort, ushort, byte?, byte?, byte?) DecodeXInput(ReadOnlySpan<byte> data)
    {
        // XINPUT_VIBRATION: left motor, right motor, then the two impulse triggers.
        if (data.Length < 4)
        {
            return default;
        }

        ushort left = BinaryPrimitives.ReadUInt16LittleEndian(data);
        ushort right = BinaryPrimitives.ReadUInt16LittleEndian(data[2..]);
        ushort leftTrigger = data.Length >= 8 ? BinaryPrimitives.ReadUInt16LittleEndian(data[4..]) : (ushort)0;
        ushort rightTrigger = data.Length >= 8 ? BinaryPrimitives.ReadUInt16LittleEndian(data[6..]) : (ushort)0;
        return (left, right, leftTrigger, rightTrigger, null, null, null);
    }

    private static (ushort, ushort, ushort, ushort, byte?, byte?, byte?) DecodeHidOutput(ProfileKind kind, byte reportId, ReadOnlySpan<byte> data)
    {
        switch (kind)
        {
            case ProfileKind.DualShock4 when reportId == 0x05 && data.Length >= 9:
                return (ToUInt16(data[5]), ToUInt16(data[4]), 0, 0, data[6], data[7], data[8]);
            case ProfileKind.DualSense when reportId == 0x02 && data.Length >= 48:
                return (
                    ToUInt16(data[4]),
                    ToUInt16(data[3]),
                    0,
                    0,
                    data[45],
                    data[46],
                    data[47]);
            default:
                return default;
        }
    }

    private static ushort ToUInt16(byte value) => (ushort)(value << 8);

    private void SendLog(string message)
    {
        byte[] text = Encoding.UTF8.GetBytes(message);
        Write(MessageType.Log, 0, text);
    }

    public void SendAck(MessageType type, int slot, int status, ushort low = 0, ushort high = 0, ushort leftTrigger = 0, ushort rightTrigger = 0)
    {
        Span<byte> payload = stackalloc byte[20];
        BinaryPrimitives.WriteInt32LittleEndian(payload, slot);
        BinaryPrimitives.WriteInt32LittleEndian(payload[4..], status);
        BinaryPrimitives.WriteUInt16LittleEndian(payload[8..], low);
        BinaryPrimitives.WriteUInt16LittleEndian(payload[10..], high);
        BinaryPrimitives.WriteUInt16LittleEndian(payload[12..], leftTrigger);
        BinaryPrimitives.WriteUInt16LittleEndian(payload[14..], rightTrigger);
        Write(type, 0, payload[..16].ToArray());
    }

    private void SendLed(int slot, byte red, byte green, byte blue)
    {
        byte[] payload = new byte[8];
        BinaryPrimitives.WriteInt32LittleEndian(payload, slot);
        payload[4] = red;
        payload[5] = green;
        payload[6] = blue;
        Write(MessageType.Led, 0, payload);
    }

    /// <summary>Writes one framed message to Solstice.</summary>
    private void Write(MessageType type, int reserved, byte[] payload)
    {
        Span<byte> header = stackalloc byte[8];
        BinaryPrimitives.WriteInt32LittleEndian(header, (int)type);
        BinaryPrimitives.WriteInt32LittleEndian(header[4..], payload.Length);

        lock (writeGate)
        {
            output.Write(header);
            output.Write(payload);
            output.Flush();
        }
    }

    /// <summary>Reports the handshake result.</summary>
    public void SendReady(int status, string message)
    {
        byte[] text = Encoding.UTF8.GetBytes(message);
        byte[] payload = new byte[12 + text.Length];
        BinaryPrimitives.WriteInt32LittleEndian(payload, status);
        BinaryPrimitives.WriteInt32LittleEndian(payload[4..], driverInstalled ? 1 : 0);
        BinaryPrimitives.WriteInt32LittleEndian(payload[8..], text.Length);
        text.CopyTo(payload, 12);
        Write(MessageType.Ready, 0, payload);
    }

    public void Dispose()
    {
        lock (sync)
        {
            for (int slot = 0; slot < slots.Length; slot++)
            {
                DisposeSlot(slot);
            }
        }

        context.Dispose();
    }
}

internal static class Program
{
    private static int Main(string[] args)
    {
        string? pipeName = null;
        for (int i = 0; i < args.Length - 1; i++)
        {
            if (args[i] == "--pipe")
            {
                pipeName = args[i + 1];
            }
        }

        if (string.IsNullOrEmpty(pipeName))
        {
            Console.Error.WriteLine("usage: HIDMaestroBridge --pipe <name>");
            return 64;
        }

        using var server = new NamedPipeServerStream(
            pipeName,
            PipeDirection.InOut,
            1,
            PipeTransmissionMode.Byte,
            PipeOptions.None);

        server.WaitForConnection();
        using var bridge = new Bridge(server);
        var (status, message) = bridge.Initialize();
        bridge.SendReady(status, message);

        using var reader = new BinaryReader(server, Encoding.UTF8, leaveOpen: true);
        while (true)
        {
            MessageType type;
            int length;
            try
            {
                type = (MessageType)reader.ReadInt32();
                length = reader.ReadInt32();
            }
            catch (EndOfStreamException)
            {
                break;
            }

            if (length < 0 || length > 4096)
            {
                break;
            }

            byte[] payload = reader.ReadBytes(length);
            if (payload.Length != length)
            {
                break;
            }

            int result = type switch
            {
                MessageType.Create => bridge.Create(
                    BinaryPrimitives.ReadInt32LittleEndian(payload),
                    (ProfileKind)BinaryPrimitives.ReadInt32LittleEndian(payload.AsSpan(4))),
                MessageType.Destroy => bridge.Destroy(BinaryPrimitives.ReadInt32LittleEndian(payload)),
                MessageType.Input => bridge.SubmitInput(
                    BinaryPrimitives.ReadInt32LittleEndian(payload),
                    BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(4)),
                    BinaryPrimitives.ReadInt16LittleEndian(payload.AsSpan(8)),
                    BinaryPrimitives.ReadInt16LittleEndian(payload.AsSpan(10)),
                    BinaryPrimitives.ReadInt16LittleEndian(payload.AsSpan(12)),
                    BinaryPrimitives.ReadInt16LittleEndian(payload.AsSpan(14)),
                    payload[16],
                    payload[17]),
                MessageType.Touch => bridge.SubmitTouch(
                    BinaryPrimitives.ReadInt32LittleEndian(payload),
                    payload[4],
                    payload[5],
                    BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(6)),
                    BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(8)),
                    BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(10))),
                MessageType.Motion => bridge.SubmitMotion(
                    BinaryPrimitives.ReadInt32LittleEndian(payload),
                    payload[4],
                    BinaryPrimitives.ReadInt32LittleEndian(payload.AsSpan(8)),
                    BinaryPrimitives.ReadInt32LittleEndian(payload.AsSpan(12)),
                    BinaryPrimitives.ReadInt32LittleEndian(payload.AsSpan(16))),
                MessageType.Battery => bridge.SubmitBattery(
                    BinaryPrimitives.ReadInt32LittleEndian(payload),
                    payload[4],
                    payload[5]),
                MessageType.Shutdown => -1,
                _ => 1,
            };

            if (result == -1)
            {
                break;
            }

            if (type != MessageType.Input)
            {
                bridge.SendAck(MessageType.Ack, BinaryPrimitives.ReadInt32LittleEndian(payload), result);
            }
        }

        return 0;
    }
}
