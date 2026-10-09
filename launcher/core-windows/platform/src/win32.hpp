#pragma once

// The single Windows include for this package; nothing else pulls in the SDK.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <aclapi.h>
#include <dpapi.h>
#include <iphlpapi.h>
#include <objbase.h>
#include <oleauto.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <taskschd.h>
#include <userenv.h>
#include <wbemidl.h>
#include <wincred.h>
#include <winternl.h>

#if defined(__MINGW32__)
// mingw-w64 declares no GetAddrInfoExCancel; ws2_32 has exported it since Windows 8.
extern "C" INT WSAAPI GetAddrInfoExCancel(LPHANDLE lpHandle);
#endif
