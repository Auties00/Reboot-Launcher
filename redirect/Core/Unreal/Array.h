// Copyright (c) 2024 Project Nova LLC

#pragma once
#include <functional>
#include "Memory.h"

template <typename T>
class TArray
{
	friend class FString;

	T* Data;
	int32_t NumElements;
	int32_t MaxElements;

public:

	inline TArray()
	{
		Data = nullptr;
		NumElements = 0;
		MaxElements = 0;
	};

	inline void Free()
	{
		FMemory::Free(Data);
		Data = nullptr;
		NumElements = 0;
		MaxElements = 0;
	}

	inline void Reset()
	{
		Free();
	}

	inline auto GetData()
	{
		return Data;
	}

	inline int GetCount() const
	{
		return NumElements;
	}

	inline int Num() const
	{
		return NumElements;
	}

	inline auto& Get(const int Index)
	{
		return Data[Index];
	}

	inline auto& First()
	{
		return Get(0);
	}

	inline auto GetRef(const int Index, int Size = sizeof(T))
	{
		return (T*)((uint8_t*)Data + (Index * Size));
	}

	inline T& operator[](int i)
	{
		return Get(i);
	};

	inline const T& operator[](int i) const
	{
		return Get(i);
	};

	inline bool Remove(const int Index, int Size = sizeof(T))
	{
		if (Index < NumElements)
		{
			if (Index != NumElements - 1)
				Get(Index) = Get(NumElements - 1);

			--NumElements;

			return true;
		}
		return false;
	};

	inline bool Any(std::function<bool(T)> Func)
	{
		for (int i = 0; i < NumElements; ++i)
		{
			if (Func(Get(i)))
				return true;
		}
		return false;
	}

	inline T Select(std::function<bool(T)> Func)
	{
		for (int i = 0; i < NumElements; ++i)
		{
			if (Func(Get(i)))
				return Get(i);
		}

		return NULL;
	}

	inline void ForEach(std::function<void(T)> Func)
	{
		for (int i = 0; i < NumElements; ++i)
		{
			Func(Get(i));
		}
	}

	inline int Count(std::function<bool(T)> Func)
	{
		int Num = 0;

		for (int i = 0; i < NumElements; ++i)
		{
			if (Func(Get(i)))
				Num++;
		}
		return Num;
	}

	inline int Find(const T& Item)
	{
		for (int i = 0; i < NumElements; i++)
		{
			if (this->operator[](i) == Item)
				return i;
		}

		return -1;
	}
};