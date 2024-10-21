// Copyright (c) 2024 Project Nova LLC

#pragma once

namespace Constants
{
	constexpr auto API_URL = L"http://localhost:3551";

	constexpr auto ProcessRequest = L"Could not set libcurl options for easy handle, processing HTTP request failed. Increase verbosity for additional information.";
	constexpr auto ProcessRequest_C2 = L"STAT_FCurlHttpRequest_ProcessRequest";
	constexpr auto URLOffset = L"ProcessRequest failed. URL '%s' is not a valid HTTP request. %p";
	constexpr auto Realloc = L"AbilitySystem.Debug.NextTarget";
}