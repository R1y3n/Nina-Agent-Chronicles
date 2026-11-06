"""stdcall argument counts (in 32-bit dwords) of the Win32/winmm/winsock/dinput APIs imported by the Nina/LithTech binaries.
A value of 'c' means __cdecl (caller pops the arguments)."""
SIGS = {}
def _a(dll, spec):
    for tok in spec.split():
        n, v = tok.rsplit(':', 1); SIGS[n.lower()] = (dll, 'c' if v == 'c' else int(v))
_a('kernel32', '''CloseHandle:1 CompareStringA:6 CompareStringW:6 CreateDirectoryA:2 CreateEventA:4 CreateFileA:7 CreateMutexA:3 CreateProcessA:10
CreateThread:6 DebugBreak:0 DeleteCriticalSection:1 DuplicateHandle:7 EnterCriticalSection:1 EnumSystemLocalesA:2 ExitProcess:1 ExitThread:1
FatalAppExitA:2 FileTimeToDosDateTime:3 FileTimeToLocalFileTime:2 FileTimeToSystemTime:2 FindClose:1 FindFirstFileA:2 FindNextFileA:2
FindResourceA:3 FlushFileBuffers:1 FormatMessageA:7 FreeEnvironmentStringsA:1 FreeEnvironmentStringsW:1 FreeLibrary:1 GetACP:0 GetCPInfo:2
GetCommandLineA:0 GetCurrentDirectoryA:2 GetCurrentProcess:0 GetCurrentProcessId:0 GetCurrentThread:0 GetCurrentThreadId:0
GetDiskFreeSpaceA:5 GetDriveTypeA:1 GetEnvironmentStrings:0 GetEnvironmentStringsW:0 GetEnvironmentVariableA:3 GetExitCodeProcess:2
GetFileAttributesA:1 GetFileSize:2 GetFileTime:4 GetFileType:1 GetFullPathNameA:4 GetLastError:0 GetLocalTime:1 GetLocaleInfoA:4
GetLocaleInfoW:4 GetModuleFileNameA:3 GetModuleHandleA:1 GetOEMCP:0 GetProcAddress:2 GetProcessHeap:0 GetProfileStringA:5
GetStartupInfoA:1 GetStdHandle:1 GetStringTypeA:5 GetStringTypeW:4 GetSystemDirectoryA:2 GetSystemInfo:1 GetSystemTime:1
GetSystemTimeAsFileTime:1 GetTempFileNameA:4 GetTempPathA:2 GetTickCount:0 GetTimeZoneInformation:1 GetUserDefaultLCID:0 GetVersion:0
GetVersionExA:1 GetWindowsDirectoryA:2 GlobalAlloc:2 GlobalFree:1 GlobalHandle:1 GlobalLock:1 GlobalMemoryStatus:1 GlobalUnlock:1
HeapAlloc:3 HeapCompact:2 HeapCreate:3 HeapDestroy:1 HeapFree:3 HeapReAlloc:4 HeapSize:3 HeapValidate:3 InitializeCriticalSection:1
InterlockedDecrement:1 InterlockedExchange:2 InterlockedIncrement:1 IsBadCodePtr:1 IsBadReadPtr:2 IsBadWritePtr:2 IsValidCodePage:1
IsValidLocale:2 LCMapStringA:6 LCMapStringW:6 LeaveCriticalSection:1 LoadLibraryA:1 LoadModule:2 LoadResource:2 LocalFree:1 LockResource:1
MultiByteToWideChar:6 OpenFile:3 OutputDebugStringA:1 PulseEvent:1 QueryPerformanceCounter:1 QueryPerformanceFrequency:1 RaiseException:4
ReadFile:5 ReleaseMutex:1 ResetEvent:1 ResumeThread:1 RtlUnwind:4 SetConsoleCtrlHandler:2 SetCurrentDirectoryA:1 SetEndOfFile:1
SetEnvironmentVariableA:2 SetErrorMode:1 SetEvent:1 SetFilePointer:4 SetHandleCount:1 SetLastError:1 SetPriorityClass:2 SetStdHandle:2
SetThreadPriority:2 SetUnhandledExceptionFilter:1 SizeofResource:2 Sleep:1 SuspendThread:1 TerminateProcess:2 TlsAlloc:0 TlsFree:1
TlsGetValue:1 TlsSetValue:2 UnhandledExceptionFilter:1 VirtualAlloc:4 VirtualFree:3 VirtualQuery:3 WaitForMultipleObjects:4
WaitForSingleObject:2 WideCharToMultiByte:8 WriteFile:5 lstrcatA:2 lstrcpyA:2 lstrlenA:1''')
_a('user32', '''BeginPaint:2 CallWindowProcA:5 ClientToScreen:2 ClipCursor:1 CreateWindowExA:12 DefWindowProcA:4 DestroyWindow:1 DispatchMessageA:1
EndPaint:2 GetActiveWindow:0 GetAsyncKeyState:1 GetClassInfoA:3 GetClassLongA:2 GetDC:1 GetDesktopWindow:0 GetFocus:0 GetForegroundWindow:0
GetKeyboardState:1 GetMessageA:4 GetSystemMetrics:1 GetTopWindow:1 GetWindow:2 GetWindowLongA:2 GetWindowRect:2 GetWindowThreadProcessId:2
InvalidateRect:3 IsIconic:1 IsWindow:1 KillTimer:2 LoadCursorA:2 LoadIconA:2 LoadImageA:6 LoadStringA:4 MessageBoxA:4 PeekMessageA:5
PostQuitMessage:1 RegisterClassA:1 ReleaseDC:2 SendMessageA:4 SetCursor:1 SetCursorPos:2 SetFocus:1 SetTimer:4 SetWindowLongA:3
SetWindowPos:7 ShowCursor:1 ShowWindow:2 ToAscii:5 TranslateMessage:1 UpdateWindow:1 wsprintfA:c wvsprintfA:3''')
_a('gdi32', '''BitBlt:9 CreateCompatibleBitmap:3 CreateCompatibleDC:1 CreateDIBSection:6 CreateFontA:14 CreatePalette:1 DeleteDC:1 DeleteObject:1
GetDIBits:7 GetDeviceCaps:2 GetNearestPaletteIndex:2 GetStockObject:1 GetSystemPaletteEntries:4 GetSystemPaletteUse:1
GetTextCharacterExtra:1 GetTextExtentPoint32A:4 RealizePalette:1 Rectangle:5 SelectObject:2 SelectPalette:3 SetBkColor:2 SetBkMode:2
SetStretchBltMode:2 SetTextCharacterExtra:2 SetTextColor:2 StretchBlt:11 StretchDIBits:13 TextOutA:5''')
_a('ole32', 'CoCreateInstance:5 CoInitialize:1 CoUninitialize:0')
_a('dinput', 'DirectInputCreateA:4')
_a('advapi32', 'RegCloseKey:1 RegCreateKeyExA:9 RegDeleteKeyA:2 RegDeleteValueA:2 RegQueryValueExA:6 RegSetValueExA:6')
_a('wsock32', '''WSACleanup:0 WSAGetLastError:0 WSAStartup:2 bind:3 closesocket:1 gethostbyname:1 gethostname:2 getsockname:3 htonl:1 htons:1
inet_addr:1 ioctlsocket:3 ntohl:1 ntohs:1 recvfrom:6 sendto:6 setsockopt:5 socket:3''')
_a('winmm', '''auxGetDevCapsA:3 auxGetNumDevs:0 auxGetVolume:2 auxSetVolume:2 mciSendCommandA:4 midiOutClose:1 midiOutLongMsg:3 midiOutOpen:5
midiOutPrepareHeader:3 midiOutReset:1 midiOutShortMsg:2 midiOutUnprepareHeader:3 mixerClose:1 mixerGetControlDetailsA:3
mixerGetLineControlsA:3 mixerGetLineInfoA:3 mixerGetNumDevs:0 mixerOpen:5 mixerSetControlDetails:3 timeBeginPeriod:1 timeEndPeriod:1
timeGetTime:0 timeKillEvent:1 timeSetEvent:5 waveInAddBuffer:3 waveInClose:1 waveInOpen:6 waveInPrepareHeader:3 waveInReset:1
waveInStart:1 waveInUnprepareHeader:3 waveOutClose:1 waveOutGetDevCapsA:3 waveOutGetID:2 waveOutOpen:6 waveOutPrepareHeader:3
waveOutReset:1 waveOutSetVolume:2 waveOutUnprepareHeader:3 waveOutWrite:3''')
_a('kernel32', '''DeleteFileA:1 GetPrivateProfileStringA:6 WritePrivateProfileStringA:5 LocalFileTimeToFileTime:2 MoveFileA:2 RemoveDirectoryA:1
SetFileTime:4 SystemTimeToFileTime:2 WaitForSingleObjectEx:3''')
_a('user32', 'AddFontResourceA:1 RemoveFontResourceA:1 AdjustWindowRect:3 GetClientRect:2 MapWindowPoints:4')
_a('advapi32', 'RegOpenKeyExA:5')
_a('wsock32', '__WSAFDIsSet:2 connect:3 getsockopt:5 inet_ntoa:1 recv:4 select:5 send:4 shutdown:2')
_a('ddraw', 'DirectDrawCreate:3 DirectDrawCreateEx:4 DirectDrawEnumerateA:2')
