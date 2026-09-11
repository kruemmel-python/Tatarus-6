#include "lm_transport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace tatarus::cortex::detail {
namespace {

struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::uint16_t port = 0;
    std::string path;
};

[[nodiscard]] bool isLoopbackHost(std::string_view host) noexcept {
    return host == "127.0.0.1" || host == "localhost" || host == "::1" || host == "[::1]";
}

[[nodiscard]] ParsedUrl parseUrl(std::string_view url) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) throw std::runtime_error("LM Studio URL has no scheme");
    ParsedUrl parsed;
    parsed.scheme = std::string(url.substr(0, schemeEnd));
    std::transform(parsed.scheme.begin(), parsed.scheme.end(), parsed.scheme.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (parsed.scheme != "http" && parsed.scheme != "https") {
        throw std::runtime_error("LM Studio URL must use http or https");
    }

    std::string_view rest = url.substr(schemeEnd + 3U);
    const auto pathStart = rest.find('/');
    std::string_view authority = pathStart == std::string_view::npos ? rest : rest.substr(0, pathStart);
    parsed.path = pathStart == std::string_view::npos ? "/" : std::string(rest.substr(pathStart));
    if (authority.empty()) throw std::runtime_error("LM Studio URL has no host");

    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) throw std::runtime_error("Invalid IPv6 URL host");
        parsed.host = std::string(authority.substr(0, close + 1U));
        if (close + 1U < authority.size()) {
            if (authority[close + 1U] != ':') throw std::runtime_error("Invalid IPv6 URL port");
            const auto portText = authority.substr(close + 2U);
            unsigned port = 0;
            const auto [ptr, ec] = std::from_chars(portText.data(), portText.data() + portText.size(), port);
            if (ec != std::errc{} || ptr != portText.data() + portText.size() || port > 65535U) {
                throw std::runtime_error("Invalid URL port");
            }
            parsed.port = static_cast<std::uint16_t>(port);
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon != std::string_view::npos && authority.find(':') == colon) {
            parsed.host = std::string(authority.substr(0, colon));
            const auto portText = authority.substr(colon + 1U);
            unsigned port = 0;
            const auto [ptr, ec] = std::from_chars(portText.data(), portText.data() + portText.size(), port);
            if (ec != std::errc{} || ptr != portText.data() + portText.size() || port > 65535U) {
                throw std::runtime_error("Invalid URL port");
            }
            parsed.port = static_cast<std::uint16_t>(port);
        } else {
            parsed.host = std::string(authority);
        }
    }

    if (!isLoopbackHost(parsed.host)) {
        throw std::runtime_error("Cortex transport is restricted to loopback LM Studio endpoints");
    }
    if (parsed.port == 0) parsed.port = parsed.scheme == "https" ? 443U : 80U;
    return parsed;
}

#ifndef _WIN32

class SocketHandle {
public:
    explicit SocketHandle(int fd = -1) noexcept : fd_(fd) {}
    ~SocketHandle() { if (fd_ >= 0) ::close(fd_); }
    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;
    SocketHandle(SocketHandle&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    [[nodiscard]] int get() const noexcept { return fd_; }
private:
    int fd_;
};

[[nodiscard]] std::string decodeChunked(std::string_view body) {
    std::string output;
    std::size_t pos = 0;
    while (true) {
        const auto lineEnd = body.find("\r\n", pos);
        if (lineEnd == std::string_view::npos) throw std::runtime_error("Malformed chunked HTTP response");
        const auto sizeText = body.substr(pos, lineEnd - pos);
        const auto semicolon = sizeText.find(';');
        const auto hexText = semicolon == std::string_view::npos ? sizeText : sizeText.substr(0, semicolon);
        std::size_t chunkSize = 0;
        const auto [ptr, ec] = std::from_chars(hexText.data(), hexText.data() + hexText.size(), chunkSize, 16);
        if (ec != std::errc{} || ptr != hexText.data() + hexText.size()) {
            throw std::runtime_error("Malformed HTTP chunk size");
        }
        pos = lineEnd + 2U;
        if (chunkSize == 0U) return output;
        if (pos + chunkSize + 2U > body.size()) throw std::runtime_error("Truncated chunked HTTP response");
        output.append(body.substr(pos, chunkSize));
        pos += chunkSize;
        if (body.substr(pos, 2U) != "\r\n") throw std::runtime_error("Malformed HTTP chunk terminator");
        pos += 2U;
    }
}

[[nodiscard]] HttpResponse socketRequest(
    std::string_view method,
    std::string_view url,
    std::string_view requestBody,
    std::uint32_t timeoutMs) {
    const ParsedUrl parsed = parseUrl(url);
    if (parsed.scheme != "http") {
        throw std::runtime_error("HTTPS LM Studio transport requires Windows WinHTTP in this build");
    }

    std::string hostForDns = parsed.host;
    if (hostForDns.size() >= 2U && hostForDns.front() == '[' && hostForDns.back() == ']') {
        hostForDns = hostForDns.substr(1U, hostForDns.size() - 2U);
    }
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* raw = nullptr;
    const std::string portText = std::to_string(parsed.port);
    const int gai = ::getaddrinfo(hostForDns.c_str(), portText.c_str(), &hints, &raw);
    if (gai != 0 || raw == nullptr) throw std::runtime_error("Cannot resolve LM Studio loopback endpoint");

    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(raw, &freeaddrinfo);
    int connected = -1;
    for (addrinfo* current = addresses.get(); current != nullptr; current = current->ai_next) {
        const int fd = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (fd < 0) continue;
        timeval timeout{};
        timeout.tv_sec = static_cast<long>(timeoutMs / 1000U);
        timeout.tv_usec = static_cast<long>((timeoutMs % 1000U) * 1000U);
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (::connect(fd, current->ai_addr, current->ai_addrlen) == 0) {
            connected = fd;
            break;
        }
        ::close(fd);
    }
    if (connected < 0) throw std::runtime_error("Cannot connect to LM Studio on loopback");
    SocketHandle socket(connected);

    std::string request;
    request.reserve(512U + requestBody.size());
    request.append(method);
    request.push_back(' ');
    request.append(parsed.path);
    request.append(" HTTP/1.1\r\nHost: ");
    request.append(parsed.host);
    request.push_back(':');
    request.append(std::to_string(parsed.port));
    request.append("\r\nAccept: application/json\r\nConnection: close\r\n");
    if (method == "POST") {
        request.append("Content-Type: application/json\r\nContent-Length: ");
        request.append(std::to_string(requestBody.size()));
        request.append("\r\n");
    }
    request.append("\r\n");
    request.append(requestBody);

    std::size_t sent = 0;
    while (sent < request.size()) {
        const auto count = ::send(socket.get(), request.data() + sent, request.size() - sent, 0);
        if (count <= 0) throw std::runtime_error("Failed sending request to LM Studio");
        sent += static_cast<std::size_t>(count);
    }

    std::string response;
    std::array<char, 8192> buffer{};
    while (true) {
        const auto count = ::recv(socket.get(), buffer.data(), buffer.size(), 0);
        if (count == 0) break;
        if (count < 0) throw std::runtime_error("Failed reading response from LM Studio");
        response.append(buffer.data(), static_cast<std::size_t>(count));
        if (response.size() > 8U * 1024U * 1024U) throw std::runtime_error("LM Studio HTTP response exceeds 8 MiB");
    }

    const auto headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) throw std::runtime_error("Malformed LM Studio HTTP response");
    const auto statusEnd = response.find("\r\n");
    if (statusEnd == std::string::npos) throw std::runtime_error("Malformed HTTP status line");
    const std::string_view statusLine(response.data(), statusEnd);
    const auto firstSpace = statusLine.find(' ');
    if (firstSpace == std::string_view::npos || firstSpace + 4U > statusLine.size()) {
        throw std::runtime_error("Malformed HTTP status line");
    }
    int status = 0;
    const auto statusText = statusLine.substr(firstSpace + 1U, 3U);
    const auto [statusPtr, statusEc] = std::from_chars(
        statusText.data(), statusText.data() + statusText.size(), status);
    if (statusEc != std::errc{} || statusPtr != statusText.data() + statusText.size()) {
        throw std::runtime_error("Malformed HTTP status code");
    }

    const std::string headers = response.substr(0, headerEnd + 2U);
    std::string body = response.substr(headerEnd + 4U);
    std::string lowerHeaders = headers;
    std::transform(lowerHeaders.begin(), lowerHeaders.end(), lowerHeaders.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowerHeaders.find("transfer-encoding: chunked") != std::string::npos) {
        body = decodeChunked(body);
    }
    return HttpResponse{status, std::move(body)};
}

class NativeHttpTransport final : public IHttpTransport {
public:
    HttpResponse get(std::string_view url, std::uint32_t timeoutMs) const override {
        return socketRequest("GET", url, {}, timeoutMs);
    }
    HttpResponse postJson(
        std::string_view url,
        std::string_view body,
        std::uint32_t timeoutMs) const override {
        return socketRequest("POST", url, body, timeoutMs);
    }
};

#else

[[nodiscard]] std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) throw std::runtime_error("Cannot convert LM Studio URL to UTF-16");
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
            result.data(), required) != required) {
        throw std::runtime_error("Cannot convert LM Studio URL to UTF-16");
    }
    return result;
}

class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET handle = nullptr) noexcept : handle_(handle) {}
    ~WinHttpHandle() { if (handle_) WinHttpCloseHandle(handle_); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    [[nodiscard]] HINTERNET get() const noexcept { return handle_; }
private:
    HINTERNET handle_;
};

[[nodiscard]] std::runtime_error winHttpError(std::string_view operation) {
    const DWORD error = GetLastError();
    return std::runtime_error(
        std::string(operation) + " failed (WinHTTP error "
        + std::to_string(static_cast<unsigned long>(error)) + ")");
}

[[nodiscard]] HttpResponse winHttpRequest(
    std::string_view method,
    std::string_view url,
    std::string_view body,
    std::uint32_t timeoutMs) {
    const ParsedUrl parsed = parseUrl(url);
    std::string host = parsed.host;
    if (host.size() >= 2U && host.front() == '[' && host.back() == ']') {
        host = host.substr(1U, host.size() - 2U);
    }
    const auto hostWide = widen(host);
    const auto pathWide = widen(parsed.path);
    const auto methodWide = widen(method);

    WinHttpHandle session(WinHttpOpen(
        L"TATARUS-Cortex/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session.get()) throw winHttpError("WinHttpOpen");
    const int timeout = static_cast<int>(std::min<std::uint32_t>(timeoutMs, 120000U));
    WinHttpSetTimeouts(session.get(), timeout, timeout, timeout, timeout);

    WinHttpHandle connection(WinHttpConnect(
        session.get(), hostWide.c_str(), parsed.port, 0));
    if (!connection.get()) throw winHttpError("WinHttpConnect");

    const DWORD flags = parsed.scheme == "https" ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle request(WinHttpOpenRequest(
        connection.get(), methodWide.c_str(), pathWide.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request.get()) throw winHttpError("WinHttpOpenRequest");

    const std::wstring headers = method == "POST"
        ? L"Content-Type: application/json\r\nAccept: application/json\r\n"
        : L"Accept: application/json\r\n";
    const BOOL sent = WinHttpSendRequest(
        request.get(), headers.c_str(), static_cast<DWORD>(-1L),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!sent) throw winHttpError("WinHttpSendRequest");
    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        throw winHttpError("WinHttpReceiveResponse");
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(
            request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        throw winHttpError("WinHttpQueryHeaders");
    }

    std::string responseBody;
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            throw winHttpError("WinHttpQueryDataAvailable");
        }
        if (available == 0) break;
        if (responseBody.size() + available > 8U * 1024U * 1024U) {
            throw std::runtime_error("LM Studio HTTP response exceeds 8 MiB");
        }
        const std::size_t oldSize = responseBody.size();
        responseBody.resize(oldSize + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), responseBody.data() + oldSize, available, &read)) {
            throw winHttpError("WinHttpReadData");
        }
        responseBody.resize(oldSize + read);
    }
    return HttpResponse{static_cast<int>(status), std::move(responseBody)};
}

class NativeHttpTransport final : public IHttpTransport {
public:
    HttpResponse get(std::string_view url, std::uint32_t timeoutMs) const override {
        return winHttpRequest("GET", url, {}, timeoutMs);
    }
    HttpResponse postJson(
        std::string_view url,
        std::string_view body,
        std::uint32_t timeoutMs) const override {
        return winHttpRequest("POST", url, body, timeoutMs);
    }
};

#endif

} // namespace

std::unique_ptr<IHttpTransport> makeNativeHttpTransport() {
    return std::make_unique<NativeHttpTransport>();
}

} // namespace tatarus::cortex::detail
