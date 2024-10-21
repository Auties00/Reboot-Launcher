// Copyright (c) 2024 Project Nova LLC

#pragma once
#include "../framework.h"

namespace Windows
{
	namespace Thread
	{
		static HANDLE Create(void* Routine, void* Param = NULL)
		{
			return CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Routine, Param, 0, NULL);
		}
	}
}