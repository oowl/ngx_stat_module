#include "ngx_stat.h"
#include <ngx_config.h>
#include <ngx_core.h>
#include <execinfo.h>
#include <ngx_module.h>
#include <ngx_thread_pool.h>

extern ngx_module_t ngx_thread_pool_module;

#if (NGX_THREADS)
#include <pthread.h>
typedef pthread_mutex_t  ngx_thread_mutex_t;
typedef pthread_cond_t  ngx_thread_cond_t;

typedef struct {
    ngx_array_t               pools;
} ngx_thread_pool_conf_t;


typedef struct {
    ngx_thread_task_t        *first;
    ngx_thread_task_t       **last;
} ngx_thread_pool_queue_t;

struct ngx_thread_pool_s {
    ngx_thread_mutex_t        mtx;
    ngx_thread_pool_queue_t   queue;
    ngx_int_t                 waiting;
    ngx_thread_cond_t         cond;

    ngx_log_t                *log;

    ngx_str_t                 name;
    ngx_uint_t                threads;
    ngx_int_t                 max_queue;

    u_char                   *file;
    ngx_uint_t                line;
};

ngx_int_t
ngx_stat_thread_pool_get(ngx_pool_t *pool, ngx_buf_t *b)
{
    ngx_uint_t                i;
    ngx_thread_pool_t       **tpp;
    ngx_thread_pool_conf_t   *tcf;
    ngx_uint_t                size;
    u_char                   *p;

#define NGX_THREAD_POOL_PID_SIZE       (NGX_TIME_T_LEN + sizeof("pid:\n") - 1)     /* sizeof pid_t equals time_t */
#define NGX_THREAD_POOL_PID_FORMAT     "pid:%P\n"
#define NGX_THREAD_POOL_ENTRY_SIZE     (12 * 3 + sizeof("max_queue: threads: waiting:  \n") - 1 + 1)
#define NGX_THREAD_POOL_ENTRY_FORMAT   "max_queue:%12z threads:%12z waiting:%12z %V\n"

    tcf = (ngx_thread_pool_conf_t *) ngx_cycle->conf_ctx[ngx_thread_pool_module.index];

    size = 1 + sizeof("THREAD POOL\n") - 1 + NGX_THREAD_POOL_PID_SIZE;
    tpp = tcf->pools.elts;
    for (i = 0; i < tcf->pools.nelts; i++) {
        size += tpp[i]->name.len + NGX_THREAD_POOL_ENTRY_SIZE;
    }

    p = ngx_palloc(pool, size);
    if (p == NULL) {
        return NGX_ERROR;
    }

    b->pos = p;

    p = ngx_cpymem(p, "THREAD POOL\n", sizeof("THREAD POOL\n") - 1);
    p = ngx_sprintf(p, NGX_THREAD_POOL_PID_FORMAT, ngx_pid);

    for (i = 0; i < tcf->pools.nelts; i++) {
        p = ngx_snprintf(p, NGX_THREAD_POOL_ENTRY_SIZE + tpp[i]->name.len, NGX_THREAD_POOL_ENTRY_FORMAT,
                        tpp[i]->max_queue, tpp[i]->threads, tpp[i]->waiting, &tpp[i]->name);
    }

    p = ngx_cpymem(p, "\n", sizeof("\n") - 1);

    b->last = p;
    b->memory = 1;

    return NGX_OK;

}
#endif