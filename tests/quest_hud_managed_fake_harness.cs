using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Dluma.Managed.Bootstrap;
using Dluma.Managed.Sdk;

internal static unsafe class Program
{
    private static int _calls;
    private static bool _unavailable;
    private static string _target = "";
    private static string _text = "";
    private static ulong _token;

    private static void Check(bool condition)
    {
        if (!condition) throw new InvalidOperationException("HUD managed bridge contract failed.");
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Invoke(void* context, uint operation, void* request,
        uint requestSize, void* result, uint resultSize)
    {
        ++_calls;
        var call = (NativeCall*)((byte*)result + sizeof(uint) * 2);
        call->Status = (uint)DlumaStatus.InternalError;
        if (context != (void*)1) return 0;
        if (_unavailable)
        {
            call->Status = (uint)DlumaStatus.Unavailable;
            call->Error->OperationId = operation;
            call->Error->Retryable = 1;
            NativeUtf8.WriteDiagnostic(call->Error->Code.Data, 96, "unsupported_capability");
            NativeUtf8.WriteDiagnostic(call->Error->Message.Data, 384, "HUD is absent.");
            return 0;
        }
        if (operation == 5003 && requestSize == sizeof(NativeQuestHudRequest) &&
            resultSize == sizeof(NativeQuestHudResult))
        {
            var snapshot = (NativeQuestHudResult*)result;
            const string json = "{\"revision\":73,\"header\":{\"text\":\"Café\"},\"objectives\":[]}";
            var bytes = Encoding.UTF8.GetBytes(json);
            if (snapshot->Json.Capacity < (ulong)bytes.Length + 1) return 0;
            bytes.CopyTo(new Span<byte>(snapshot->Json.Data, bytes.Length));
            snapshot->Json.Data[bytes.Length] = 0;
            snapshot->Json.Required = (ulong)bytes.Length + 1;
            snapshot->Revision = 73;
            call->Status = (uint)DlumaStatus.Ok;
        }
        else if (operation == 5004 && requestSize == sizeof(NativeQuestHudOverrideRequest) &&
            resultSize == sizeof(NativeTokenResult))
        {
            var value = (NativeQuestHudOverrideRequest*)request;
            _target = NativeUtf8.Read(value->TargetId);
            _text = NativeUtf8.Read(value->Text);
            _token = value->Token;
            ((NativeTokenResult*)result)->Token = value->Token == 0 ? 1234UL : value->Token;
            call->Status = (uint)DlumaStatus.Ok;
        }
        return call->Status == 0 ? 1 : 0;
    }

    public static int Main()
    {
        var bridge = new NativeBridgeV1
        {
            StructSize = (uint)sizeof(NativeBridgeV1),
            StructVersion = NativeConstants.StructVersion,
            AbiVersion = NativeConstants.AbiVersion,
            Context = (void*)1,
            Invoke = &Invoke,
        };
        var context = new NativeModContext("test.hud", new NativeBridge(&bridge));
        Check(context.Ui.QuestHudSnapshot(new("")).Call.Status == DlumaStatus.OwnerRevoked);
        Check(context.Ui.QuestHudOverride(new("target", "text", 0)).Call.Status == DlumaStatus.OwnerRevoked);
        Check(_calls == 0);
        context.Activate();
        var snapshot = context.Ui.QuestHudSnapshot(new(""));
        Check(snapshot.Call.Succeeded && snapshot.Revision == 73 && snapshot.Json.Contains("Café"));
        var created = context.Ui.QuestHudOverride(new("header:1", "À jour", 0));
        Check(created.Call.Succeeded && created.Token == 1234 && _target == "header:1" && _text == "À jour");
        var updated = context.Ui.QuestHudOverride(new("header:1", "Updated", created.Token));
        Check(updated.Call.Succeeded && updated.Token == created.Token && _token == created.Token);
        _unavailable = true;
        var missing = context.Ui.QuestHudSnapshot(new(""));
        Check(missing.Call.Status == DlumaStatus.Unavailable && missing.Json == "" && missing.Revision == 0);
        Check(missing.Call.Error?.OperationId == 5003 && missing.Call.Error?.Code == "unsupported_capability");
        context.Revoke();
        var before = _calls;
        Check(context.Ui.QuestHudOverride(new("header:1", "x", 1234)).Call.Status == DlumaStatus.OwnerRevoked);
        Check(_calls == before);
        Console.WriteLine("Managed quest HUD bridge passed: marshaling, updates, errors, revocation.");
        return 0;
    }
}
