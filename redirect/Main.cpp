// Copyright (c) 2024 Project Nova LLC

#include "framework.h"

static void Main()
{
    Sleep(7500);

    Core::Init();
    Sinum::Init();
}

bool DllMain(HMODULE hModule, DWORD dwReason, void* lpReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        Windows::Thread::Create(Main);
    }

    return true;
}