// The one place a report meets the network, through libcurl as Mans In My House's reports and
// rooms do on its three builds: the system's libcurl.so.4 on Linux, the macOS SDK's on the Mac, and
// a pinned static curl over Windows' own Schannel on Windows (cmake/reports.cmake). Certificates
// and the host name are checked. A build without curl says so and sends nothing.
#include "build_info.hpp"
#include "report/report_internal.hpp"

#include <SDL3/SDL.h>

#ifdef PHANTOMCRASH_NO_CURL

int post_report(const std::string&, const std::string&, double, std::string& error) {
    error = "this build has no HTTP (no curl)";
    return 0;
}

#else
#include <curl/curl.h>

namespace {

std::size_t ignore_body(char*, std::size_t size, std::size_t count, void*) {
    return size * count;
}

} // namespace

int post_report(const std::string& url, const std::string& body, double timeout_seconds,
                std::string& error) {
    static const bool ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    CURL* curl = ready ? curl_easy_init() : nullptr;
    if (!curl) {
        error = "HTTP couldn't start";
        return 0;
    }
    const std::string token = std::string("X-OpenPCrash-Token: ") + report_token;
    const std::string agent =
        std::string("OpenPCrash/") + build_info().version + " (" + SDL_GetPlatform() + ")";
    curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
    headers = curl_slist_append(headers, token.c_str());
    headers = curl_slist_append(headers, "Expect:");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_seconds * 1000));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
#ifdef CURLSSLOPT_REVOKE_BEST_EFFORT
    // Schannel (Windows) fails closed when it can't reach a revocation list
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
#endif
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ignore_body);
    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK) {
        error = curl_easy_strerror(result);
        return 0;
    }
    return static_cast<int>(status);
}

#endif
