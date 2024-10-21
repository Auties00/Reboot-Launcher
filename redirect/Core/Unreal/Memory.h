// Copyright (c) 2024 Project Nova LLC

#pragma once
#include <Windows.h>
#include <cstdint>

class FMemory
{
public:
	static inline void* (*_Realloc)(void*, size_t, int64_t);

	static void Free(void* Data)
	{
		_Realloc(Data, 0, 0);
	}

	static void* Malloc(size_t Size)
	{
		return _Realloc(0, Size, 0);
	}

	static void* Realloc(void* Data, size_t NewSize)
	{
		return _Realloc(Data, NewSize, 0);
	}

	static void* Memmove(void* Dest, const void* Src, size_t Count)
	{
		return memmove(Dest, Src, Count);
	}

	static int Memcmp(const void* Buf1, const void* Buf2, size_t Count)
	{
		return memcmp(Buf1, Buf2, Count);
	}

	static void* Memset(void* Dest, uint8_t Char, size_t Count)
	{
		return memset(Dest, Char, Count);
	}

	template< class T >
	static void Memset(T& Src, uint8_t ValueToSet)
	{
		Memset(&Src, ValueToSet, sizeof(T));
	}

	static void* Memzero(void* Dest, size_t Count)
	{
		return ZeroMemory(Dest, Count);
	}

	template <class T>
	static void Memzero(T& Src)
	{
		Memzero(&Src, sizeof(T));
	}

	static void* Memcpy(void* Dest, const void* Src, size_t Count)
	{
		return memcpy(Dest, Src, Count);
	}

	template <class T>
	static void Memcpy(T& Dest, const T& Src)
	{
		Memcpy(&Dest, &Src, sizeof(T));
	}

	static void* Calloc(size_t NumElements, size_t ElementSize)
	{
		auto TotalSize = NumElements * ElementSize;
		auto Data = FMemory::Malloc(TotalSize);

		if (!Data)
			return NULL;

		FMemory::Memzero(Data, TotalSize);

		return Data;
	}

	static char* Strdup(const char* Str)
	{
		auto StrLen = strlen(Str) + 1;
		auto StrDup = (char*)FMemory::Malloc(StrLen);

		FMemory::Memcpy(StrDup, Str, StrLen);

		return StrDup;
	}
};