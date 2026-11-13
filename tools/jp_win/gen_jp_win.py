#!/usr/bin/env python3
"""gen_jp_win.py - JP100 v3. Builds runtime/hle_jp_win.inc from hle_jp_win_{a,b,c}.inc + SPEC below
(real handlers + logged failure stubs + the JPW_DEFS table), includes it in runtime/hle_jp.c and makes
rt_fault leave through _Exit(3). Idempotent. Usage: python3 gen_jp_win.py [path/to/me1androd]"""
import os, sys
R = sys.argv[1] if len(sys.argv) > 1 else '/data/work/me1androd'
H = os.path.dirname(os.path.abspath(__file__))
# [dll] then per line:  Name nargs ret [e=SetLastError] [z=i,j: zero the out-pointer args i,j]
#                  or:  Name nargs @handler [nolock]      nargs = stdcall argument dwords (0 for cdecl)
SPEC = '''
[kernel32.dll]
HeapSetInformation 4 1
FlushInstructionCache 3 1
GetUserGeoID 1 244
GetUserDefaultLangID 0 0x409
GetSystemDefaultLangID 0 0x409
IsValidLanguageGroup 2 1
CreateFileMappingW 6 0 e=50
AreFileApisANSI 0 1
LockFileEx 6 1
FindCloseChangeNotification 1 1
FindFirstChangeNotificationW 3 -1 e=50
FindNextChangeNotification 1 0 e=6
PeekNamedPipe 6 0 e=109
CreatePipe 4 0 e=50
ReadConsoleInputA 4 0 e=6
FlushConsoleInputBuffer 1 0 e=6
SetConsoleMode 2 0 e=6
OpenFile 3 -1 e=2
GlobalMemoryStatusEx 1 @jw_GlobalMemoryStatusEx
lstrcmpA 2 @jw_lstrcmpA
lstrcmpW 2 @jw_lstrcmpW
MulDiv 3 @jw_MulDiv
lstrcpynA 3 @jw_lstrcpynA
LoadLibraryExA 3 @jw_LoadLibraryExA nolock
LoadLibraryExW 3 @jw_LoadLibraryExW nolock
CompareStringW 6 @jw_CompareStringW
GetDateFormatA 6 @jw_GetDateFormatA
GetDateFormatW 6 @jw_GetDateFormatW
GetTimeFormatA 6 @jw_GetTimeFormatA
GetTimeFormatW 6 @jw_GetTimeFormatW
GetVolumeInformationW 8 @jw_GetVolumeInformationW
GetSystemDirectoryW 2 @jw_GetSystemDirectoryW
SystemTimeToTzSpecificLocalTime 3 @jw_SystemTimeToTzSpecificLocalTime
GlobalSize 1 @jw_GlobalSize
GetProfileStringW 5 @jw_GetProfileStringW
GetThreadTimes 5 @jw_GetThreadTimes
VirtualProtect 4 @jw_VirtualProtect
FindFirstFileExA 6 @jw_FindFirstFileExA nolock
InterlockedPushEntrySList 2 @jw_InterlockedPushEntrySList nolock
InterlockedPopEntrySList 1 @jw_InterlockedPopEntrySList nolock
CreateWaitableTimerA 3 @jw_CreateWaitableTimerA nolock
SetWaitableTimer 6 @jw_SetWaitableTimer nolock
[advapi32.dll]
RegEnumKeyA 4 259
RegEnumKeyExA 8 259
RegEnumKeyExW 8 259
RegEnumValueW 8 259
RegFlushKey 1 0
RegDeleteValueA 2 2
RegDeleteValueW 2 2
RegDeleteKeyA 2 2
RegDeleteKeyW 2 2
RegCreateKeyExA 9 5 z=7
RegSetValueExA 6 0
RegisterEventSourceA 2 0x0EE70001
ReportEventA 9 @jw_ReportEventA
DeregisterEventSource 1 1
RegQueryInfoKeyW 12 @jw_RegQueryInfoKeyW
GetTokenInformation 5 0 e=6
OpenProcessToken 3 0 e=5 z=2
[comctl32.dll]
#17 0 0
_TrackMouseEvent 1 1
InitCommonControlsEx 1 1
[comdlg32.dll]
PrintDlgExW 1 0x80004001
GetOpenFileNameW 1 0
[gdi32.dll]
GetCharABCWidthsFloatW 4 0
GetOutlineTextMetricsW 3 0
SetWorldTransform 2 0
CreateBitmap 5 0
PtInRegion 3 0
SelectPalette 3 0
RealizePalette 1 0
StartDocW 2 0
CreateRectRgn 4 0
GetRegionData 3 0
CombineRgn 4 0
RestoreDC 2 0
CreatePen 3 0
GetBkMode 1 0
StartPage 1 0
AbortDoc 1 0
EndPage 1 0
EndDoc 1 0
ResetDCW 2 0
SelectClipRgn 2 0
OffsetRgn 3 0
CreateSolidBrush 1 0
CreatePalette 1 0
GetPaletteEntries 4 0
CreateEllipticRgn 4 0
SaveDC 1 0
GetCharABCWidthsW 4 0
GetBitmapBits 3 0
CreateDCA 4 0
GetTextFaceW 3 0
BitBlt 9 0
GetRgnBox 2 0
SelectClipPath 2 0
CreateDCW 4 0
ExtCreatePen 5 0
StrokePath 1 0
FillPath 1 0
BeginPath 1 0
PolyBezierTo 3 0
LineTo 3 0
CloseFigure 1 0
EndPath 1 0
SetPolyFillMode 2 1
GdiFlush 0 1
DPtoLP 3 1
SetGraphicsMode 2 1
EnumFontFamiliesExW 5 1
GetFontData 5 -1
[imm32.dll]
ImmSetCompositionFontW 2 0
ImmGetContext 1 0
ImmReleaseContext 2 1
ImmNotifyIME 4 0
ImmGetCompositionStringW 4 0
ImmAssociateContext 2 0
ImmSetCandidateWindow 2 0
ImmSetCompositionWindow 2 0
ImmGetDefaultIMEWnd 1 0
[oleaut32.dll]
#161 2 0x80029C4A z=1
#162 5 0x8002801D z=4
#420 3 0x80004001 z=2
#9 1 @jw_VariantClear
#8 1 @jw_VariantInit
#277 4 @jw_VarUI4FromStr
#4 2 @jw_SysAllocStringLen
#7 1 @jw_SysStringLen
[ole32.dll]
CoTaskMemFree 1 @jw_CoTaskMemFree
CoTaskMemAlloc 1 @jw_CoTaskMemAlloc
CoTaskMemRealloc 2 @jw_CoTaskMemRealloc
PropVariantClear 1 @jw_PropVariantClear
CLSIDFromString 2 @jw_CLSIDFromString
CLSIDFromProgID 2 0x800401F3
OleIsCurrentClipboard 1 1
OleFlushClipboard 0 0
OleSetClipboard 1 0x800401D0
OleGetClipboard 1 0x800401D0 z=0
CoGetMalloc 2 0x8007000E z=1
CreateStreamOnHGlobal 3 0x8007000E z=2
CoGetClassObject 5 0x80040154 z=4
DoDragDrop 4 0x00040101 z=3
RevokeDragDrop 1 0x80040100
RegisterDragDrop 2 0x80040102
ReleaseStgMedium 1 0
CoLockObjectExternal 3 0
OleUninitialize 0 0
OleInitialize 1 0
OleLockRunning 3 0
[shell32.dll]
SHFileOperationA 1 1
ShellExecuteExA 1 0
ExtractIconExA 5 0
SHGetFileInfoW 5 0
[shlwapi.dll]
PathRemoveFileSpecA 1 @jw_PathRemoveFileSpecA
PathRemoveFileSpecW 1 @jw_PathRemoveFileSpecW
SHGetValueW 6 2
PathCombineW 3 @jw_PathCombineW
PathIsDirectoryA 1 @jw_PathIsDirectoryA nolock
[user32.dll]
GetClassInfoExA 3 0 e=1411
GetClassInfoW 3 0 e=1411
LoadCursorA 2 0x00010003
CreateAcceleratorTableA 2 0
SetWindowTextA 2 @jw_SetWindowTextA
GetWindowTextA 3 @jw_GetTextA0
GetWindowTextLengthA 1 0
GetMenu 1 0
AdjustWindowRectEx 4 1
LoadImageA 6 0
wsprintfA 0 @jw_wsprintfA
CopyRect 2 @jw_CopyRect
RegisterRawInputDevices 3 0
GetRawInputData 5 -1
CallNextHookEx 4 0
DestroyIcon 1 1
UnhookWindowsHookEx 1 1
ScrollWindowEx 8 0
SetWindowsHookExW 4 0x4B000001
SetWindowRgn 3 1
SetParent 2 0
IsZoomed 1 0
SetWindowPlacement 2 1
GetWindowPlacement 2 @jw_GetWindowPlacement nolock
ValidateRgn 2 1
EnableMenuItem 3 -1
GetSystemMenu 2 0
mouse_event 5 0
MessageBeep 1 1
FlashWindowEx 1 0
GetCaretBlinkTime 0 530
SetCaretBlinkTime 1 1
SetDoubleClickTime 1 1
GetDoubleClickTime 0 500
WindowFromPoint 2 0
GetUpdateRect 3 @jw_GetUpdateRect
GetWindowRgn 2 0
GetSysColorBrush 1 0
CreateIconIndirect 1 0
DrawIconEx 9 0
GetIconInfo 2 0
DestroyCursor 1 1
CreateCursor 7 0
CharNextExA 3 @jw_CharNextExA
SetTimer 4 @jw_SetTimer
GetQueueStatus 1 0
KillTimer 2 1
MsgWaitForMultipleObjectsEx 5 @jw_MsgWaitForMultipleObjectsEx nolock
GetKeyboardLayoutList 2 @jw_GetKeyboardLayoutList
GetKeyboardLayout 1 0x04090409
HideCaret 1 1
CreateCaret 4 1
SetCaretPos 2 1
DestroyCaret 0 1
ToUnicode 6 @jw_ToUnicode
ToAscii 5 @jw_ToAscii
TrackPopupMenuEx 6 0
SetMenuItemInfoW 4 0
GetKeyboardState 1 @jw_GetKeyboardState nolock
GetClipboardFormatNameW 3 0
ChangeClipboardChain 2 0
SetClipboardViewer 1 0
GetLastInputInfo 1 @jw_GetLastInputInfo nolock
GetPropW 2 @jw_GetPropW
SetPropW 3 @jw_SetPropW
CallWindowProcW 5 @jw_CallWindowProc nolock
CallWindowProcA 5 @jw_CallWindowProc nolock
DestroyAcceleratorTable 1 1
FillRect 3 1
GetClassNameA 3 @jw_GetTextA0
IsChild 2 0
InvalidateRgn 3 1
MoveWindow 6 1
GetSysColor 1 @jw_GetSysColor
WaitMessage 0 @jw_WaitMessage nolock
CharNextA 1 @jw_CharNextA
GetUserObjectInformationW 5 @jw_GetUserObjectInformationW
GetProcessWindowStation 0 0x5A000001
CharLowerBuffA 2 @jw_CharLowerBuffA
SendMessageA 4 @jw_SendMessageA nolock
RegisterWindowMessageA 1 @jw_RegisterWindowMessageA
RegisterClipboardFormatW 1 @jw_RegisterClipboardFormatW
[version.dll]
GetFileVersionInfoSizeW 2 0 e=1812 z=1
GetFileVersionInfoW 4 0 e=1812
VerQueryValueW 4 0
[winspool.drv]
DeviceCapabilitiesW 5 -1
GetPrinterW 5 0 e=1801
OpenPrinterW 3 0 e=1801 z=1
ClosePrinter 1 1
EnumPrintersW 7 1 z=5,6
EnumFormsW 6 0 e=1801
[winmm.dll]
waveOutGetNumDevs 0 0
waveInGetNumDevs 0 0
waveOutGetDevCapsA 3 2
waveOutGetDevCapsW 3 2
waveInGetDevCapsA 3 2
waveInGetDevCapsW 3 2
waveOutGetPosition 3 6
waveInOpen 6 6 z=0
waveInReset 1 5
waveInClose 1 5
waveInStart 1 5
waveInPrepareHeader 3 5
waveInUnprepareHeader 3 5
waveInAddBuffer 3 5
mciSendCommandA 4 306
[msacm32.dll]
acmStreamPrepareHeader 3 5
acmStreamUnprepareHeader 3 5
acmStreamConvert 3 5
acmStreamSize 4 5
acmStreamOpen 8 512 z=0
acmFormatSuggest 5 512
[wsock32.dll]
#103 5 0 e=10050
#108 1 -1 e=10050
[ws2_32.dll]
WSASocketW 6 -1 e=10050
WSAIoctl 9 -1 e=10050
WSAConnect 7 -1 e=10050
WSASend 7 -1 e=10050
WSASendTo 9 -1 e=10050
WSARecv 7 -1 e=10050
WSARecvFrom 9 -1 e=10050
WSAAccept 5 -1 e=10050
WSAHtons 3 @jw_WSAHtons
WSAHtonl 3 @jw_WSAHtonl
WSANtohs 3 @jw_WSAHtons
WSANtohl 3 @jw_WSAHtonl
freeaddrinfo 1 0
getaddrinfo 4 11001 z=3
#1 3 -1 e=10050
#2 3 -1 e=10050
#5 3 -1 e=10050
#6 3 -1 e=10050
#7 5 -1 e=10050
#10 3 -1 e=10050
#13 2 -1 e=10050
#17 6 -1 e=10050
#18 5 -1 e=10050
#20 6 -1 e=10050
#21 5 -1 e=10050
#22 2 -1 e=10050
#8 1 @jw_htonl
#14 1 @jw_htonl
#15 1 @jw_ntohs
#11 1 @jw_inet_addr
#51 3 0 e=11001
#57 2 @jw_gethostname
#101 4 -1 e=10050
#111 0 @jw_WSAGetLastError
#112 1 @jw_WSASetLastError
#116 0 0
#151 2 @jw_FDIsSet
'''
stubs, defs, seen, dll = [], [], set(), None
for ln in SPEC.splitlines():
    ln = ln.split(';')[0].strip()
    if not ln:
        continue
    if ln.startswith('['):
        dll = ln[1:-1]
        continue
    t = ln.split()
    name, n = t[0], int(t[1])
    key = (dll.lower(), name)
    assert key not in seen, key
    seen.add(key)
    if t[2].startswith('@'):
        defs.append((dll, name, n, t[2][1:], 'HLE_NOLOCK' if 'nolock' in t[3:] else '0'))
        continue
    ret, err, z = int(t[2], 0) & 0xFFFFFFFF, 0, [-1, -1]
    for x in t[3:]:
        if x.startswith('e='):
            err = int(x[2:], 0)
        elif x.startswith('z='):
            z = ([int(v) for v in x[2:].split(',')] + [-1, -1])[:2]
    defs.append((dll, name, n, 'jws_%d' % len(stubs), '0'))
    stubs.append((dll, name, ret, err, z))
o = ['/* hle_jp_win.inc - GENERATED by tools/jp_win/gen_jp_win.py (JP100 v3); edit the parts/spec there. */']
for p in ('hle_jp_win_a.inc', 'hle_jp_win_b.inc', 'hle_jp_win_c.inc'):
    o.append(open(os.path.join(H, p)).read())
o.append('/* ---------------- logged stubs: fixed result, optional SetLastError, zeroed out-pointers ---------------- */')
o.append('typedef struct { const char* dll; const char* name; uint32_t ret, err; int z1, z2; } JwStub;')
o.append('static const JwStub jw_stubs[%d] = {' % len(stubs))
for d, nm, r, e, z in stubs:
    o.append('    { "%s", "%s", 0x%08Xu, %du, %d, %d },' % (d, nm, r, e, z[0], z[1]))
o.append('};')
o.append('static volatile int jw_stub_seen[%d];' % len(stubs))
o.append(r'''static void jw_stub(CPU* c, int i) {
    const JwStub* s = &jw_stubs[i];
    if (!jw_stub_seen[i]) { jw_stub_seen[i] = 1; fprintf(stderr, "[hle] stub %s!%s -> 0x%x\n", s->dll, s->name, s->ret); }
    if (s->z1 >= 0 && ARG(s->z1)) wr32(ARG(s->z1), 0);
    if (s->z2 >= 0 && ARG(s->z2)) wr32(ARG(s->z2), 0);
    if (s->err) hle_set_last_error(c, s->err);
    RET(s->ret);
}''')
o += ['static void jws_%d(CPU* c) { jw_stub(c, %d); }' % (i, i) for i in range(len(stubs))]
o.append('#define JPW_DEFS \\')
o += ['    { "%s", "%s", %d, %s, %s }, \\' % d for d in defs]
o.append('    /* end of JPW_DEFS */')
open(os.path.join(R, 'runtime/hle_jp_win.inc'), 'w').write('\n'.join(o) + '\n')

p = os.path.join(R, 'runtime/hle_jp.c'); s = open(p).read()
if 'hle_jp_win.inc' not in s:
    a, b = '#include "hle_jp_d3dx.inc"\n', 'const HleDef hle_jp_defs[] = {\n'
    assert s.count(a) == 1 and s.count(b) == 1
    s = s.replace(a, a + '#include "hle_jp_win.inc"   /* v3: the remaining Win32 imports */\n').replace(b, b + '    JPW_DEFS\n')
    open(p, 'w').write(s)
p = os.path.join(R, 'runtime/rt.c'); s = open(p).read()
if '_Exit(3)' not in s:
    assert s.count('    exit(3);') == 1
    s = s.replace('    exit(3);', '    fflush(NULL); _Exit(3);   /* v3: no atexit handlers / static destructors while guest threads run */')
    open(p, 'w').write(s)
print('hle_jp_win.inc: %d entries (%d handlers, %d stubs)' % (len(defs), len(defs) - len(stubs), len(stubs)))
