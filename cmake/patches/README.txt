Fixes to the pinned commits, applied when deps.cmake fetches them
(cmake/apply_patch.cmake). The gubsy and gview ones are for building on
Windows and harmless elsewhere. Drop each once its library has the fix and the
pin moves past it.

gubsy-windows.patch  a loop variable named `interface`, which <rpc.h>
                     (through <iphlpapi.h>) defines as a macro
gview-windows.patch  std::filesystem::path::c_str() is wchar_t on Windows
