init DPLAYX_LibMain

  1 stdcall DirectPlayCreate(ptr ptr ptr) DirectPlayCreate
  2 stdcall DirectPlayEnumerateA(ptr ptr) DirectPlayEnumerateA
  3 stdcall DirectPlayEnumerateW(ptr ptr) DirectPlayEnumerateW
  4 stdcall DirectPlayLobbyCreateA(ptr ptr ptr ptr long) DirectPlayLobbyCreateA
  5 stdcall DirectPlayLobbyCreateW(ptr ptr ptr ptr long) DirectPlayLobbyCreateW
  6 extern gdwDPlaySPRefCount gdwDPlaySPRefCount
  9 stdcall DirectPlayEnumerate(ptr ptr) DirectPlayEnumerateA
  10 stdcall DllCanUnloadNow() DPLAYX_DllCanUnloadNow
  11 stdcall DllGetClassObject(ptr ptr ptr) DPLAYX_DllGetClassObject
