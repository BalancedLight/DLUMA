"""Copy the production retaining wrapper into an ignored hermetic build input."""
from pathlib import Path
import hashlib

root = Path(__file__).resolve().parents[1]
source = root / "src/fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp"
text = source.read_text()
start = text.index("class RetainingObservationCall {")
end = text.index("\n};", start) + len("\n};")
out = root / "build/content-replay-permit-fake/retaining_observer.generated.h"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(f"/* Production SHA256 {hashlib.sha256(source.read_bytes()).hexdigest()} */\n" + text[start:end])
xui_start = text.index("unsigned char __fastcall HookXuiXmlLoad(")
xui_end = text.index("bool __fastcall HookAudioScriptLoad(", xui_start)
(out.parent / "xui_load_observers.generated.h").write_text(
    f"/* Production SHA256 {hashlib.sha256(source.read_bytes()).hexdigest()} */\n"
    + text[xui_start:xui_end]
)
validator_start = text.index("bool ValidateProductionFactoryReplayAuthorization(")
validator_end = text.index("bool AuthorizeProductionReplayFactory(", validator_start)
(out.parent / "factory_replay_validator.generated.h").write_text(
    f"/* Production SHA256 {hashlib.sha256(source.read_bytes()).hexdigest()} */\n"
    + text[validator_start:validator_end]
)
context_start = text.index("bool ProductionDomainTransactionMatches(")
context_end = text.index("bool ProductionCurveTransactionMatches(", context_start)
(out.parent / "domain_callback_context.generated.h").write_text(
    f"/* Production SHA256 {hashlib.sha256(source.read_bytes()).hexdigest()} */\n"
    + text[context_start:context_end]
)
