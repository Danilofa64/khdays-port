/* host/net/https.h on UWP: IXMLHTTPRequest2 (msxml6), the asynchronous HTTP
 * client UWP apps get, with the system's proxies and certificate stores. */
#include "https.h"

#include <windows.h>
#include <msxml6.h>
#include <objidl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

/* `s` (UTF-8) as a wide string, to give back with free(); NULL if it cannot be made. */
wchar_t *wide(const char *s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = n > 0 ? (wchar_t *)malloc((size_t)n * sizeof(wchar_t)) : NULL;
    if (w != NULL && MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n) != n) {
        free(w);
        w = NULL;
    }
    return w;
}

/* One GET's events: the answer's headers, its body's stream, an error. */
class Callback : public IXMLHTTPRequest2Callback {
  public:
    Callback()
    {
        headers = CreateEventW(NULL, TRUE, FALSE, NULL);
        done = CreateEventW(NULL, TRUE, FALSE, NULL);
    }
    ~Callback()
    {
        if (headers != NULL) {
            CloseHandle(headers);
        }
        if (done != NULL) {
            CloseHandle(done);
        }
    }

    /* IUnknown */
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG left = InterlockedDecrement(&refs);
        if (left == 0) {
            delete this;
        }
        return left;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override
    {
        if (out == NULL) {
            return E_POINTER;
        }
        if (id == __uuidof(IUnknown)) {
            *out = static_cast<IUnknown *>(this);
        } else if (id == __uuidof(IXMLHTTPRequest2Callback)) {
            *out = static_cast<IXMLHTTPRequest2Callback *>(this);
        } else {
            *out = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    /* IXMLHTTPRequest2Callback */
    HRESULT STDMETHODCALLTYPE OnRedirect(IXMLHTTPRequest2 *, const WCHAR *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnHeadersAvailable(IXMLHTTPRequest2 *, DWORD code, const WCHAR *) override
    {
        status = code;
        SetEvent(headers);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDataAvailable(IXMLHTTPRequest2 *, ISequentialStream *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnResponseReceived(IXMLHTTPRequest2 *, ISequentialStream *body) override
    {
        /* the stream only lives through the callback: keep it, to read it after */
        stream = body;
        if (stream != NULL) {
            stream->AddRef();
        }
        SetEvent(done);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnError(IXMLHTTPRequest2 *, HRESULT code) override
    {
        error = code;
        SetEvent(headers);
        SetEvent(done);
        return S_OK;
    }

    volatile LONG refs = 1;
    HANDLE headers;
    HANDLE done;
    DWORD status = 0;
    HRESULT error = S_OK;
    ISequentialStream *stream = NULL;
};

/* One GET, answered. `out` holds the callback and the body's stream; 1 when the answer is 200 OK,
 * the stream then readable; 0 otherwise, everything closed. */
struct Answer {
    Callback *waiter;
    ISequentialStream *stream;
};

int request_open(const char *url, const char *agent, int timeout_ms, Answer &out)
{
    out.waiter = NULL;
    out.stream = NULL;
    wchar_t *wurl = wide(url);
    wchar_t *wagent = wide(agent);
    int made = 0;
    if (wurl != NULL && wagent != NULL) {
        Callback *waiter = new Callback();
        IXMLHTTPRequest2 *xhr = NULL;
        if (SUCCEEDED(CoCreateInstance(CLSID_FreeThreadedXMLHTTP60, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&xhr))) &&
            xhr != NULL) {
            if (SUCCEEDED(xhr->Open(L"GET", wurl, waiter, NULL, NULL, NULL, NULL)) &&
                SUCCEEDED(xhr->SetRequestHeader(L"User-Agent", wagent)) && SUCCEEDED(xhr->Send(NULL, NULL)) &&
                WaitForSingleObject(waiter->headers, (DWORD)timeout_ms) == WAIT_OBJECT_0 &&
                WaitForSingleObject(waiter->done, (DWORD)timeout_ms) == WAIT_OBJECT_0 && waiter->error == S_OK &&
                waiter->status == 200 && waiter->stream != NULL) {
                out.waiter = waiter;
                waiter->AddRef();
                out.stream = waiter->stream;
                made = 1;
            }
            if (made == 0) {
                /* an answer that never came, or a code that is not 200: the request stops */
                xhr->Abort();
            }
            xhr->Release();
        }
        waiter->Release(); /* the caller's reference is left when the answer was made */
    }
    free(wurl);
    free(wagent);
    return made;
}

} /* namespace */

char *khdays_https_get(const char *url, const char *agent, int timeout_ms)
{
    char *body = NULL;
    const HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    const int mine = co == S_OK || co == S_FALSE;
    Answer got;
    if (request_open(url, agent, timeout_ms, got)) {
        char chunk[65536];
        ULONG piece;
        size_t room = 0, have = 0;
        while (got.stream->Read(chunk, sizeof(chunk), &piece) == S_OK && piece > 0) {
            if (have + (size_t)piece + 1 > room) {
                size_t next = room ? room * 2 : 65536;
                while (have + (size_t)piece + 1 > next) {
                    next *= 2;
                }
                char *grown = (char *)realloc(body, next);
                if (grown == NULL) {
                    free(body);
                    body = NULL;
                    break;
                }
                body = grown;
                room = next;
            }
            memcpy(body + have, chunk, piece);
            have += piece;
        }
        if (body != NULL) {
            body[have] = '\0';
        }
    }
    if (got.stream != NULL) {
        got.stream->Release();
    }
    if (got.waiter != NULL) {
        got.waiter->Release();
    }
    if (mine) {
        CoUninitialize();
    }
    return body;
}

int khdays_https_download(const char *url, const char *agent, const char *path, int timeout_ms,
                          KhdaysDownloadProgress progress, void *context)
{
    int made = 0;
    wchar_t *wpath = wide(path);
    const HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    const int mine = co == S_OK || co == S_FALSE;
    Answer got;
    if (wpath != NULL && request_open(url, agent, timeout_ms, got)) {
        HANDLE file = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            char chunk[65536];
            ULONG piece;
            long long done = 0;
            int ok = 1;
            while (ok && got.stream->Read(chunk, sizeof(chunk), &piece) == S_OK && piece > 0) {
                DWORD written;
                ok = WriteFile(file, chunk, piece, &written, NULL) && (DWORD)piece == written;
                if (ok) {
                    done += piece;
                    ok = progress == NULL || progress(context, done) == 0;
                }
            }
            CloseHandle(file);
            made = ok;
        }
    }
    if (!made && wpath != NULL) {
        DeleteFileW(wpath);
    }
    if (got.stream != NULL) {
        got.stream->Release();
    }
    if (got.waiter != NULL) {
        got.waiter->Release();
    }
    free(wpath);
    if (mine) {
        CoUninitialize();
    }
    return made;
}
