/* One HTTP(S) request, for the update check (host/app/updates.c). Each system has its own
 * (https_win.c on Windows). */
#ifndef KHDAYS_HOST_HTTPS_H
#define KHDAYS_HOST_HTTPS_H

/* GETs `url` (http or https) with `agent` as its User-Agent, each step waiting at most
 * `timeout_ms`: the answer's body, NUL-ended, to give back with free(), when the answer is 200 OK;
 * NULL otherwise. */
char *khdays_https_get(const char *url, const char *agent, int timeout_ms);

#endif
