#ifndef PERLITE_H
#define PERLITE_H

#include <stddef.h>
#include <stdint.h>

#if defined(PERLITE_STATIC)
#define PERLITE_API
#elif defined(_WIN32)
#if defined(PERLITE_BUILD_SHARED)
#define PERLITE_API __declspec(dllexport)
#else
#define PERLITE_API __declspec(dllimport)
#endif
#else
#define PERLITE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct perlite_server perlite_server;
typedef struct perlite_conn perlite_conn;

typedef struct perlite_options {

    const char* data_dir;

    const char* config_json;

    int loopback_only;
} perlite_options;

typedef void (*perlite_log_fn)(int level, const char* text, void* user);

PERLITE_API uint32_t perlite_abi_version(void);
PERLITE_API void perlite_set_log(perlite_log_fn fn, void* user);

PERLITE_API perlite_server* perlite_create(const perlite_options* options);
PERLITE_API const char* perlite_last_error(void);

PERLITE_API int perlite_start(perlite_server* server);

PERLITE_API int perlite_run_async(perlite_server* server);

PERLITE_API int perlite_is_running(perlite_server* server);

PERLITE_API void perlite_tick(perlite_server* server);
PERLITE_API void perlite_stop(perlite_server* server);
PERLITE_API void perlite_destroy(perlite_server* server);
PERLITE_API void perlite_command(perlite_server* server, const char* line);

PERLITE_API perlite_conn* perlite_connect_loopback(perlite_server* server);
PERLITE_API int perlite_conn_send(perlite_conn* conn, const uint8_t* data, size_t size);

PERLITE_API int perlite_conn_receive(perlite_conn* conn, uint8_t* buf, size_t capacity, size_t* size);
PERLITE_API int perlite_conn_alive(perlite_conn* conn);
PERLITE_API void perlite_conn_close(perlite_conn* conn);

#ifdef __cplusplus
}
#endif

#endif
