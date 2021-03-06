/*
 * Copyright (c) 2021 Soar Qin<soarchin@gmail.com>
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include "pipehost.h"

#include <json.hpp>

#include <windows.h>
#include <string>
#include <cstdio>

namespace d2mapapi {

PipedChildProcess::~PipedChildProcess() {
    if (process) {
        CloseHandle(HANDLE(process));
        CloseHandle(HANDLE(childStdoutRd));
        CloseHandle(HANDLE(childStdinWr));
    }
}

bool PipedChildProcess::start(const wchar_t *filename, const wchar_t *parameters) {
    SECURITY_ATTRIBUTES saAttr = {
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };
    if (!CreatePipe((HANDLE*)&childStdoutRd, (HANDLE*)&childStdoutWr, &saAttr, 0) ||
        !CreatePipe((HANDLE*)&childStdinRd, (HANDLE*)&childStdinWr, &saAttr, 0)) {
        return false;
    }

    SetHandleInformation(HANDLE(childStdoutRd), HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(HANDLE(childStdinWr), HANDLE_FLAG_INHERIT, 0);
    PROCESS_INFORMATION piProcInfo = {};
    STARTUPINFOW siStartInfo = {.cb = sizeof(STARTUPINFOW), .dwFlags = STARTF_USESTDHANDLES, .hStdInput = childStdinRd, .hStdOutput = childStdoutWr, .hStdError = childStdoutWr};
    BOOL bSuccess = FALSE;

    wchar_t cmdLine[1024];
    wsprintfW(cmdLine, L"\"%s\" \"%s\"", filename, parameters);
    bSuccess = CreateProcessW(nullptr, cmdLine, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &siStartInfo, &piProcInfo);
    process = piProcInfo.hProcess;

    if (!bSuccess) {
        return false;
    }
    CloseHandle(piProcInfo.hThread);
    CloseHandle(HANDLE(childStdoutWr));
    CloseHandle(HANDLE(childStdinRd));
    int ret;
    if (!readPipe(&ret, 4)) {
        errMsg_ = "failed to read from child process!";
        return false;
    }
    if (ret == 0) { return true; }
    uint32_t len;
    readPipe(&len, 4);
    std::string str;
    str.resize(len);
    readPipe(str.data(), len);
    try {
        auto j = nlohmann::json::parse(str);
        errMsg_ = j["error"];
    } catch(const std::exception &e) {
        errMsg_ = e.what();
    }
    return false;
}

bool PipedChildProcess::writePipe(const void *data, size_t size) {
    DWORD dwWritten;
    return WriteFile(HANDLE(childStdinWr), data, DWORD(size), &dwWritten, nullptr);
}

bool PipedChildProcess::readPipe(void *data, size_t size) {
    DWORD dwRead;
    return ReadFile(HANDLE(childStdoutRd), data, DWORD(size), &dwRead, nullptr);
}

std::string PipedChildProcess::queryMapRaw(uint32_t seed, uint8_t difficulty, uint32_t levelId, bool generatePathData) {
    struct Req {
        uint32_t seed;
        uint32_t difficulty;
        uint32_t levelId;
    };
    Req req = {.seed = seed, .difficulty = difficulty, .levelId = levelId | ((generatePathData ? 1u : 0u) << 16)};
    uint32_t size;
    if (!writePipe(&req, sizeof(uint32_t) * 3)) { return ""; }
    if (!readPipe(&size, sizeof(size))) { return ""; }
    std::string str;
    str.resize(size);
    if (!readPipe(str.data(), str.length())) { return ""; }
    return std::move(str);
}

CollisionMap *PipedChildProcess::queryMap(uint32_t seed, uint8_t difficulty, uint32_t levelId, bool generatePathData) {
    auto str = queryMapRaw(seed, difficulty, levelId, generatePathData);
    if (str.empty()) { return nullptr; }
    try {
        return new CollisionMap(str);
    } catch(...) {
        return nullptr;
    }
}

}
