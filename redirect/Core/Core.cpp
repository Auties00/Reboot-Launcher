// Copyright (c) 2024 Project Nova LLC

#include "Core.h"

void Core::Init()
{
	FMemory::_Realloc = Memcury::Scanner::FindStringRef(Constants::Realloc)
		.ScanFor({ Memcury::ASM::MNEMONIC::CALL })
		.RelativeOffset(1)
		.GetAs<decltype(FMemory::_Realloc)>();
}