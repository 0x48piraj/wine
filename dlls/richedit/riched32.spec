name	riched32
type	win32
init	RICHED32_LibMain

import	user32.dll
import	kernel32.dll
import	ntdll.dll

debug_channels (richedit)

2 stdcall DllGetVersion (ptr) RICHED32_DllGetVersion
