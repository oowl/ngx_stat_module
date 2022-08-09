#include "ngx_stat.h"
#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_palloc.h"

#if (NGX_DEBUG_POOL)
ngx_int_t
ngx_stat_palloc_stat_get(ngx_pool_t *pool, ngx_buf_t *b)
{
    u_char              *p, *unit;
    size_t               size, s, n, cn, ln;
    ngx_uint_t           i;
    ngx_pool_stat_t     *stat;

#define NGX_POOL_PID_SIZE       (NGX_TIME_T_LEN + sizeof("pid:\n") - 1)     /* sizeof pid_t equals time_t */
#define NGX_POOL_PID_FORMAT     "pid:%P\n"
#define NGX_POOL_ENTRY_SIZE     (48 /* func */ + 12 * 4 + sizeof("size: num: cnum: lnum: \n") - 1)
#define NGX_POOL_ENTRY_FORMAT   "size:%12z num:%12z cnum:%12z lnum:%12z %s\n"
#define NGX_POOL_SUMMARY_SIZE   (12 * 4 + sizeof("size: num: cnum: lnum: [SUMMARY]\n\n") - 1)
#define NGX_POOL_SUMMARY_FORMAT "size:%10z%2s num:%12z cnum:%12z lnum:%12z [SUMMARY]\n\n"

    size = NGX_POOL_PID_SIZE + ngx_pool_stats_num * NGX_POOL_ENTRY_SIZE
           + NGX_POOL_SUMMARY_SIZE + sizeof("MEM POOL\n") - 1;
    p = ngx_palloc(pool, size);
    if (p == NULL) {
        return NGX_ERROR;
    }

    b->pos = p;

    p = ngx_cpymem(p, "MEM POOL\n", sizeof("MEM POOL\n") - 1);
    p = ngx_sprintf(p, NGX_POOL_PID_FORMAT, ngx_pid);

    /* lines of entry */

    s = n = cn = ln = 0;

    for (i = 0; i < NGX_POOL_STATS_MAX; i++) {
        for (stat = ngx_pool_stats[i]; stat != NULL; stat = stat->next) {
            p = ngx_snprintf(p, NGX_POOL_ENTRY_SIZE, NGX_POOL_ENTRY_FORMAT,
                             stat->size, stat->num, stat->cnum, stat->lnum,
                             stat->func);
            s += stat->size;
            n += stat->num;
            cn += stat->cnum;
            ln += stat->lnum;
        }
    }

    /* summary line */

    unit = (u_char *) " B";

    if (s > 1024 * 1024) {
        s = s / (1024 * 1024);
        unit = (u_char *) "MB";
    } else if (s > 1024) {
        s = s / 1024;
        unit = (u_char *) "KB";
    }

    p = ngx_snprintf(p, NGX_POOL_SUMMARY_SIZE, NGX_POOL_SUMMARY_FORMAT,
                     s, unit, n, cn, ln);

    b->last = p;
    b->memory = 1;

    return NGX_OK;
}
#endif