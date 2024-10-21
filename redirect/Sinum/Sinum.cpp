// Copyright (c) 2024 Project Nova LLC

#include "Sinum.h"

bool Sinum::ProcessRequestHook(FCurlHttpRequest* Request)
{
    std::wstring URL(Request->GetURL().c_str());
    size_t PathIndex = URL.find(L"ol.epicgames.com");

    if (PathIndex != std::wstring::npos)
    {
        auto Path = URL.substr(PathIndex + 16);
        auto NewURL = Constants::API_URL + Path;

        Request->SetURL(NewURL.c_str());
    }

    return _ProcessRequest(Request);
}

void Sinum::Init()
{
    auto StringRef = Memcury::Scanner::FindStringRef(Constants::ProcessRequest);
    if (StringRef.IsValid())
    {
        _ProcessRequest = StringRef
            .ScanFor({ 0x48, 0x81, 0xEC }, false)
            .ScanFor({ 0x40 }, false)
            .GetAs<decltype(_ProcessRequest)>();
    }
    else
    {
        _ProcessRequest = Memcury::Scanner::FindStringRef(Constants::ProcessRequest_C2)
            .ScanFor({ 0x4C, 0x8B, 0xDC }, false)
            .GetAs<decltype(_ProcessRequest)>();
    }

    *Memcury::Scanner::FindPointerRef(_ProcessRequest)
        .GetAs<void**>() = ProcessRequestHook;
}