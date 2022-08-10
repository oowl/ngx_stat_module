#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "ngx_stat.h"

#define NGX_MAX_SHARE_MEMORY_POOL_NUM   256

static ngx_int_t ngx_http_stat_handler(ngx_http_request_t *r);
static char *ngx_http_set_stat(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_command_t  ngx_http_status_commands[] = {

    { ngx_string("stat"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS|NGX_CONF_TAKE1,
      ngx_http_set_stat,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_stat_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_stat_module = {
    NGX_MODULE_V1,
    &ngx_http_stat_module_ctx,             /* module context */
    ngx_http_status_commands,              /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


typedef struct {
    ngx_str_t          *name;
    size_t             total_size;
    size_t             free_pages;
    ngx_uint_t         slot;
    size_t             min_shift;
    ngx_slab_stat_t    *stats;
} ngx_http_stat_slab_stat_t;


ngx_uint_t
ngx_http_stat_slab_stat(ngx_pool_t *pool, ngx_http_stat_slab_stat_t *status)
{
    volatile ngx_list_part_t             *part;
    ngx_shm_zone_t              *shm_zone;
    ngx_uint_t                  i, n;
    ngx_slab_pool_t             *shpool;

    part = &ngx_cycle->shared_memory.part;
    shm_zone = part->elts;
    n = 0;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            shm_zone = part->elts;
            i = 0;
        }

        if (n >= NGX_MAX_SHARE_MEMORY_POOL_NUM) {
            return n;
        }

        if (shm_zone[i].shm.addr == NULL) {
            continue;
        }

        shpool = (ngx_slab_pool_t *) shm_zone[i].shm.addr;
        status[n].name = &shm_zone[i].shm.name;
        status[n].total_size = shm_zone[i].shm.size;


        ngx_shmtx_lock(&shpool->mutex);
        status[n].free_pages = shpool->pfree;
        status->min_shift = shpool->min_shift;
        status[n].stats = shpool->stats;
        status[n].slot = ngx_pagesize_shift - shpool->min_shift;
        ngx_shmtx_unlock(&shpool->mutex);
        
        n += 1;
    }
    return n;
}


#define NGX_SLAB_SHM_SIZE               (sizeof("* shared memory: \n") - 1)
#define NGX_SLAB_SHM_FORMAT             "* shared memory: %V\n"
#define NGX_SLAB_SUMMARY_SIZE           \
    (2 * 12 + sizeof("total:(KB)    free:(KB)\n") - 1)
#define NGX_SLAB_SUMMARY_FORMAT         \
    "total:%12z(KB)    free:%12z(KB)\n"
#define NGX_SLAB_SLOT_ENTRY_SIZE        \
    (12 * 5 + sizeof("slot:(Bytes) total: used: reqs: fails:\n") - 1)
#define NGX_SLAB_SLOT_ENTRY_FORMAT      \
    "slot:%12z(Bytes) total:%12z used:%12z reqs:%12z fails:%12z\n"


static ngx_int_t
ngx_http_stat_handler(ngx_http_request_t *r)
{
    size_t                      size;
    ngx_int_t                   rc;
    ngx_buf_t                   *b;
    ngx_chain_t                 out;
    ngx_http_stat_slab_stat_t   status[NGX_MAX_SHARE_MEMORY_POOL_NUM];
    ngx_uint_t                  status_len;
    ngx_slab_stat_t             *stats;

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK) {
        return rc;
    }

    r->headers_out.content_type_len = sizeof("text/plain") - 1;
    ngx_str_set(&r->headers_out.content_type, "text/plain");
    r->headers_out.content_type_lowcase = NULL;

    status_len = ngx_http_stat_slab_stat(r->pool, status);
    
    size = sizeof("SHM SLAB\n") - 1;
    for (ngx_uint_t i = 0; i < status_len; i++) {
        size += NGX_SLAB_SHM_SIZE + status[i].name->len;
        size += NGX_SLAB_SUMMARY_SIZE + status[i].slot * NGX_SLAB_SLOT_ENTRY_SIZE;
    }
    size += 1;

    b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    out.buf = b;
    out.next = NULL;

    b->last = ngx_cpymem(b->last, "SHM SLAB\n",
                        sizeof("SHM SLAB\n") - 1);

    for (ngx_uint_t i = 0; i < status_len; i++) {
        b->last = ngx_sprintf(b->last, NGX_SLAB_SHM_FORMAT, status[i].name);
        b->last = ngx_sprintf(b->last, NGX_SLAB_SUMMARY_FORMAT, status[i].total_size / 1024, status[i].free_pages * ngx_pagesize / 1024);
        stats = status[i].stats;
        for (ngx_uint_t j = 0; j <  status[i].slot; j++) {
            b->last = ngx_sprintf(b->last, NGX_SLAB_SLOT_ENTRY_FORMAT,  1 << (j + status[i].min_shift),
                stats[j].total, stats[j].used, stats[j].reqs, stats[j].fails);
        }
    }

    b->last = ngx_cpymem(b->last, "\n",
                        sizeof("\n") - 1);

    b->memory = 1;
    b->last_buf = (r == r->main) ? 1 : 0;

    r->headers_out.content_length_n += b->last - b->pos;

    ngx_chain_t                 *chain;

#if (NGX_DEBUG_POOL)
    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_stat_palloc_stat_get(r->pool, b) == NGX_ERROR) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    chain = ngx_alloc_chain_link(r->pool);
    chain->buf = b;
    chain->next = NULL;

    out.next = chain;

    r->headers_out.content_length_n += b->last - b->pos;
#endif

    // timer
    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_stat_timer_get(r->pool, b) == NGX_ERROR) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    chain = ngx_alloc_chain_link(r->pool);
    chain->buf = b;
    chain->next = NULL;

    out.next->next = chain;

    r->headers_out.content_length_n += b->last - b->pos;

    // thread pool
    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_stat_thread_pool_get(r->pool, b) == NGX_ERROR) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    chain = ngx_alloc_chain_link(r->pool);
    chain->buf = b;
    chain->next = NULL;

    b->last_buf = 1;
    out.next->next->next = chain;

    r->headers_out.content_length_n += b->last - b->pos;

    r->headers_out.status = NGX_HTTP_OK;

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, &out);
}

static char *
ngx_http_set_stat(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_stat_handler;

    return NGX_CONF_OK;
}