// Copyright (c) 2024 Project Nova LLC

#pragma once
#include "../framework.h"

class FCurlHttpRequest
{
private:
	void** VTable;

public:

	FString GetURL()
	{
		FString Result;
		return ((FString& (*)(FCurlHttpRequest*, FString&))(*VTable))(this, Result);
	}

	void SetURL(FString URL)
	{
		((void (*)(FCurlHttpRequest*, FString&))(VTable[10]))(this, URL);
	}
};

namespace Sinum
{
	static bool (*_ProcessRequest)(FCurlHttpRequest*);
	static bool ProcessRequestHook(FCurlHttpRequest* Request);

	void Init();
}