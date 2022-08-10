#include "ngx_stat.h"
#include <ngx_config.h>
#include <ngx_core.h>
#include <execinfo.h>
#include <ngx_event_timer.h>


#define NGX_TIME_STAT_MAX_FUNC_NAME 128

static void
ngx_stat_timer_traversal(ngx_array_t *array, ngx_rbtree_node_t *root)
{
    ngx_rbtree_node_t              **node;

    if (array != NULL && root != NULL
        && root != ngx_event_timer_rbtree.sentinel)
    {
        ngx_stat_timer_traversal(array, root->left);
        node = ngx_array_push(array);
        if (node == NULL) {
            return;
        }
        *node = (ngx_rbtree_node_t *) root;
        ngx_stat_timer_traversal(array, root->right);
    }
}

ngx_int_t
ngx_stat_timer_get(ngx_pool_t *pool, ngx_buf_t *b)
{
    u_char              *p;
    size_t               size;
    ngx_uint_t           i, n;
    ngx_event_t         *ev;
    ngx_array_t         *array;
    ngx_msec_int_t       timer;
    ngx_rbtree_node_t   *root;
    ngx_rbtree_node_t  **nodes, *node;

#define NGX_TIMER_TITLE_SIZE     (sizeof(NGX_TIMER_TITLE_FORMAT) - 1 + NGX_TIME_T_LEN + NGX_INT_T_LEN)     /* sizeof pid_t equals time_t */
#define NGX_TIMER_TITLE_FORMAT   "pid:%P\n"                  \
                                 "timer:%ui\n"

#define NGX_TIMER_ENTRY_SIZE     (sizeof(NGX_TIMER_ENTRY_FORMAT) - 1 + \
                                  NGX_INT_T_LEN * 2 + NGX_PTR_SIZE * 4 + 256 /* func name */)
#define NGX_TIMER_ENTRY_FORMAT  "------------- [%ui] --------\n"\
                                "    timers[i]: %p\n"          \
                                "        timer: %ui\n"          \
                                "           ev: %p\n"           \
                                "         data: %p\n"           \
                                "      handler: %p\n"           \
                                "       action: %s\n"
#define NGX_TIMER_ENTRY_FUNC_FORMAT  " handler_func: %pF \n"
#define NGX_TIMER_ENTRY_FUNC_SIZE   (sizeof(NGX_TIMER_ENTRY_FORMAT) + NGX_TIME_STAT_MAX_FUNC_NAME)

    root = ngx_event_timer_rbtree.root;

    array = ngx_array_create(pool, 10, sizeof(ngx_rbtree_node_t **));
    if (array == NULL) {
        return NGX_ERROR;
    }

    ngx_stat_timer_traversal(array, root);

    n = array->nelts;

    size = NGX_TIMER_TITLE_SIZE + n * NGX_TIMER_ENTRY_SIZE  + n * NGX_TIMER_ENTRY_FUNC_SIZE + sizeof("TIMER\n") -1;
    p = ngx_palloc(pool, size);
    if (p == NULL) {
        ngx_array_destroy(array);
        return NGX_ERROR;
    }

    b->pos = p;

    p = ngx_cpymem(p, "TIMER\n", sizeof("TIMER\n") - 1);
    p = ngx_sprintf(p, NGX_TIMER_TITLE_FORMAT, ngx_pid, n);

    nodes = (ngx_rbtree_node_t **) array->elts;

    for (i = 0; i < n; i++) {
        node = nodes[i]; /* node: timer */
        ev = (ngx_event_t *) ((char *) node - (intptr_t)&((ngx_event_t *) 0x0)->timer);

         /* entry format of timer and ev */

        timer = (ngx_msec_int_t) (node->key - ngx_current_msec);

        p = ngx_snprintf(p, NGX_TIMER_ENTRY_SIZE, NGX_TIMER_ENTRY_FORMAT,
                         i, node, timer, ev, ev->data, ev->handler,
                         (ev->log->action != NULL) ? ev->log->action : "");
        p = ngx_snprintf(p, NGX_TIMER_ENTRY_FUNC_SIZE, NGX_TIMER_ENTRY_FUNC_FORMAT, ev->handler);
    }

    ngx_array_destroy(array);

    p = ngx_cpymem(p, "\n", sizeof("\n") - 1);

    b->last = p;
    b->memory = 1;

    return NGX_OK;
}
