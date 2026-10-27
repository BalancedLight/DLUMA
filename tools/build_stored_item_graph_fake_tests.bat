@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\stored-item-graph-fake mkdir build\stored-item-graph-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\stored-item-graph-fake/" ^
  /Fd"build\stored-item-graph-fake\stored_item_graph_fake_harness.pdb" ^
  /Fe"build\stored-item-graph-fake\stored_item_graph_fake_harness.exe" ^
  tests\stored_item_graph_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\StoredItemDescriptorGraph.cpp ^
  src\fixes\custom_map_asset_isolation\StoredBlueprintOwner.cpp ^
  src\fixes\custom_map_asset_isolation\CurveBorrowBoundary.cpp ^
  /link kernel32.lib || exit /b 1
exit /b 0
