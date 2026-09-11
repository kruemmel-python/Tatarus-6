#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace tatarus::cortex::detail {

struct HttpResponse {
    int status = 0;
    std::string body;
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    [[nodiscard]] virtual HttpResponse get(
        std::string_view url,
        std::uint32_t timeoutMs) const = 0;
    [[nodiscard]] virtual HttpResponse postJson(
        std::string_view url,
        std::string_view body,
        std::uint32_t timeoutMs) const = 0;
};

[[nodiscard]] std::unique_ptr<IHttpTransport> makeNativeHttpTransport();

} // namespace tatarus::cortex::detail
