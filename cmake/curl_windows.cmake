# libcurl for Windows, where there is none to find: a pinned release built
# static into the game, HTTP and HTTPS only, TLS through Windows' own
# Schannel, so the room service works with nothing extra to ship.
FetchContent_Declare(curl
    URL https://github.com/curl/curl/releases/download/curl-8_22_0/curl-8.22.0.tar.xz
    URL_HASH SHA256=f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
foreach(option BUILD_CURL_EXE BUILD_TESTING BUILD_EXAMPLES BUILD_LIBCURL_DOCS
               BUILD_MISC_DOCS ENABLE_CURL_MANUAL CURL_USE_LIBPSL CURL_USE_LIBSSH2 CURL_USE_LIBSSH
               CURL_ZLIB CURL_BROTLI CURL_ZSTD USE_NGHTTP2 USE_LIBIDN2 USE_WIN32_IDN CURL_USE_OPENSSL)
    set(${option} OFF CACHE BOOL "" FORCE)
endforeach()
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(curl)
