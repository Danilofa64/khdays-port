/* host/net/https.h on Windows: WinHTTP, with the system's proxies and certificates. */
#include "https.h"

#include <windows.h>
#include <winhttp.h>

#include <stdlib.h>
#include <string.h>

/* `s` (UTF-8) as a wide string, to give back with free(); NULL if it cannot be made. */
static wchar_t *wide(const char *s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = n > 0 ? (wchar_t *)malloc((size_t)n * sizeof(wchar_t)) : NULL;
    if (w != NULL && MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n) != n) {
        free(w);
        w = NULL;
    }
    return w;
}

/* The `n` wide characters at `s` as a string of their own, to give back with free(). */
static wchar_t *piece(const wchar_t *s, DWORD n)
{
    wchar_t *w = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (w != NULL) {
        if (n > 0) {
            memcpy(w, s, (size_t)n * sizeof(wchar_t));
        }
        w[n] = L'\0';
    }
    return w;
}

char *khdays_https_get(const char *url, const char *agent, int timeout_ms)
{
    wchar_t *wurl = wide(url), *wagent = wide(agent), *host = NULL, *path = NULL;
    HINTERNET session = NULL, connection = NULL, request = NULL;
    URL_COMPONENTS parts;
    DWORD status = 0, status_size = sizeof(status);
    char *body = NULL;
    size_t size = 0;
    int whole = 0;
    if (wurl == NULL || wagent == NULL) {
        goto done;
    }
    ZeroMemory(&parts, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = (DWORD)-1;
    parts.dwUrlPathLength = (DWORD)-1;
    parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl, 0, 0, &parts) || parts.lpszHostName == NULL || parts.lpszUrlPath == NULL) {
        goto done;
    }
    host = piece(parts.lpszHostName, parts.dwHostNameLength);
    /* the path with its query after it: the two follow each other in the address */
    path = parts.dwUrlPathLength + parts.dwExtraInfoLength > 0
               ? piece(parts.lpszUrlPath, parts.dwUrlPathLength + parts.dwExtraInfoLength)
               : piece(L"/", 1);
    if (host == NULL || path == NULL) {
        goto done;
    }
    session =
        WinHttpOpen(wagent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) {
        goto done;
    }
    WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (connection == NULL) {
        goto done;
    }
    request = WinHttpOpenRequest(connection, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (request == NULL) {
        goto done;
    }
    WinHttpAddRequestHeaders(request, L"Accept: application/vnd.github+json", (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) {
        goto done;
    }
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        goto done;
    }
    for (;;) {
        DWORD available = 0, read = 0;
        char *grown;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            goto done;
        }
        if (available == 0) {
            break;
        }
        grown = (char *)realloc(body, size + available + 1);
        if (grown == NULL) {
            goto done;
        }
        body = grown;
        if (!WinHttpReadData(request, body + size, available, &read)) {
            goto done;
        }
        size += read;
    }
    if (body == NULL) {
        body = (char *)malloc(1);
        if (body == NULL) {
            goto done;
        }
    }
    body[size] = '\0';
    whole = 1;
done:
    if (!whole) {
        free(body);
        body = NULL;
    }
    if (request != NULL) {
        WinHttpCloseHandle(request);
    }
    if (connection != NULL) {
        WinHttpCloseHandle(connection);
    }
    if (session != NULL) {
        WinHttpCloseHandle(session);
    }
    free(path);
    free(host);
    free(wagent);
    free(wurl);
    return body;
}
