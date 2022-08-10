#include <ngx_config.h>
#include <ngx_core.h>

#if (NGX_DEBUG_POOL)
ngx_int_t
ngx_stat_palloc_stat_get(ngx_pool_t *pool, ngx_buf_t *b);
#endif

ngx_int_t
ngx_stat_timer_get(ngx_pool_t *pool, ngx_buf_t *b);

ngx_int_t
ngx_stat_thread_pool_get(ngx_pool_t *pool, ngx_buf_t *b);