#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"

#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_hash.h>
#include <lauxlib.h>
#include "ngx_http_lua_api.h"


typedef struct {
    ngx_hash_t               *lua_configs_hash;
    ngx_pool_t               *pool;
    ngx_array_t              *keys;
} ngx_http_lua_config_loc_conf_t;

typedef struct {
    ngx_uint_t                hash_max_size;
    ngx_uint_t                hash_bucket_size;
} ngx_http_lua_config_main_conf_t;


static char *ngx_http_lua_config_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void *ngx_http_lua_config_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_lua_config_init_main_conf(ngx_conf_t *cf, void *conf);
static void *ngx_http_lua_config_create_srv_conf(ngx_conf_t *cf);
static char *ngx_http_lua_config_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child);
static void *ngx_http_lua_config_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_lua_config_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);


static ngx_int_t ngx_http_lua_config_init_module(ngx_conf_t *cf);

static int ngx_http_lua_create_config_module(lua_State *L);
static int ngx_http_lua_get_config_api(lua_State *L);


static ngx_command_t  ngx_http_lua_config_commands[] = {

    { ngx_string("lua_config"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_http_lua_config_directive,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("lua_config_hash_max_size"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_lua_config_main_conf_t, hash_max_size),
      NULL },

    { ngx_string("lua_config_hash_bucket_size"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_lua_config_main_conf_t, hash_bucket_size),
      NULL },

      ngx_null_command
};


static ngx_http_module_t ngx_http_lua_config_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_lua_config_init_module,       /* postconfiguration */
    ngx_http_lua_config_create_main_conf,  /* create main configuration */
    ngx_http_lua_config_init_main_conf,    /* init main configuration */
    ngx_http_lua_config_create_srv_conf,   /* create server configuration */
    ngx_http_lua_config_merge_srv_conf,    /* merge server configuration */
    ngx_http_lua_config_create_loc_conf,   /* create location configuration */
    ngx_http_lua_config_merge_loc_conf     /* merge location configuration */
};


ngx_module_t  ngx_http_lua_config_module = {
    NGX_MODULE_V1,
    &ngx_http_lua_config_module_ctx,       /* module context */
    ngx_http_lua_config_commands,          /* module directives */
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



static ngx_int_t
ngx_http_lua_config_init_module(ngx_conf_t *cf)
{
    if (ngx_http_lua_add_package_preload(cf, "ngx.lua_config",
                                         ngx_http_lua_create_config_module)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void *
ngx_http_lua_config_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_lua_config_main_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_config_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->hash_max_size = NGX_CONF_UNSET_UINT;
    conf->hash_bucket_size = NGX_CONF_UNSET_UINT;
    return conf;
}


static char *
ngx_http_lua_config_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_lua_config_main_conf_t *mcf = conf;
    ngx_http_conf_ctx_t             *ctx;
    ngx_http_lua_config_loc_conf_t  *lccf;
    ngx_hash_init_t                  hash_init;


    ctx = (ngx_http_conf_ctx_t *)cf->ctx;
    lccf = ngx_http_conf_get_module_loc_conf(ctx, ngx_http_lua_config_module);

    if (lccf == NULL || lccf->keys == NULL || lccf->keys->nelts == 0) {
        return NGX_CONF_OK;
    }

    if (mcf->hash_max_size == NGX_CONF_UNSET_UINT) {
        mcf->hash_max_size = 2048;
    }
    if (mcf->hash_bucket_size == NGX_CONF_UNSET_UINT) {
        mcf->hash_bucket_size = ngx_align(64, ngx_cacheline_size);
    }

    if (lccf->lua_configs_hash == NULL) {
        lccf->lua_configs_hash = ngx_pcalloc(cf->pool, sizeof(ngx_hash_t));
        if (lccf->lua_configs_hash == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    hash_init.hash = lccf->lua_configs_hash;
    hash_init.key = ngx_hash_key_lc;
    hash_init.max_size = mcf->hash_max_size;
    hash_init.bucket_size = mcf->hash_bucket_size;
    hash_init.name = ngx_string("lua_config_main_hash");
    hash_init.pool = cf->pool;
    hash_init.temp_pool = NULL;

    if (ngx_hash_init(&hash_init, lccf->keys->elts, lccf->keys->nelts) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    lccf->keys = NULL;

    return NGX_CONF_OK;
}


static void *
ngx_http_lua_config_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_lua_config_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_config_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    conf->pool = cf->pool;
    conf->lua_configs_hash = ngx_pcalloc(cf->pool, sizeof(ngx_hash_t));
    if (conf->lua_configs_hash == NULL) {
        return NULL;
    }
    conf->keys = ngx_array_create(cf->pool, 4, sizeof(ngx_hash_key_t));
    if (conf->keys == NULL) {
        return NULL;
    }

    return conf;
}


static char *
ngx_http_lua_config_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_lua_config_loc_conf_t *prev = parent;
    ngx_http_lua_config_loc_conf_t *conf = child;
    ngx_http_lua_config_main_conf_t *mcf;
    ngx_hash_init_t                  hash_init;

    mcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_lua_config_module);

    if (conf->keys == NULL || conf->keys->nelts == 0) {
        conf->lua_configs_hash = prev->lua_configs_hash;
        conf->keys = NULL;
        return NGX_CONF_OK;
    }

    hash_init.hash = conf->lua_configs_hash;
    hash_init.key = ngx_hash_key_lc;
    hash_init.max_size = mcf->hash_max_size;
    hash_init.bucket_size = mcf->hash_bucket_size;
    hash_init.name = ngx_string("lua_config_srv_hash");
    hash_init.pool = cf->pool;
    hash_init.temp_pool = NULL;

    if (ngx_hash_init(&hash_init, conf->keys->elts, conf->keys->nelts) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    conf->keys = NULL;

    return NGX_CONF_OK;
}


static void *
ngx_http_lua_config_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_lua_config_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_config_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    conf->pool = cf->pool;
    conf->lua_configs_hash = ngx_pcalloc(cf->pool, sizeof(ngx_hash_t));
    if (conf->lua_configs_hash == NULL) {
        return NULL;
    }
    conf->keys = ngx_array_create(cf->pool, 4, sizeof(ngx_hash_key_t));
    if (conf->keys == NULL) {
        return NULL;
    }

    return conf;
}


static char *
ngx_http_lua_config_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_lua_config_loc_conf_t  *prev = parent;
    ngx_http_lua_config_loc_conf_t  *conf = child;
    ngx_http_lua_config_main_conf_t *mcf;
    ngx_hash_init_t                  hash_init;

    mcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_lua_config_module);

    if (conf->keys == NULL || conf->keys->nelts == 0) {
        conf->lua_configs_hash = prev->lua_configs_hash;
        conf->keys = NULL;
        return NGX_CONF_OK;
    }

    hash_init.hash = conf->lua_configs_hash;
    hash_init.key = ngx_hash_key_lc;
    hash_init.max_size = mcf->hash_max_size;
    hash_init.bucket_size = mcf->hash_bucket_size;
    hash_init.name = ngx_string("lua_config_loc_hash");
    hash_init.pool = cf->pool;
    hash_init.temp_pool = NULL;

    if (ngx_hash_init(&hash_init, conf->keys->elts, conf->keys->nelts) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    conf->keys = NULL;

    return NGX_CONF_OK;
}


static char *
ngx_http_lua_config_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_lua_config_loc_conf_t *lccf = conf;
    ngx_str_t                      *value;
    ngx_hash_key_t                 *hash_key;

    value = cf->args->elts;

    hash_key = ngx_array_push(lccf->keys);
    if (hash_key == NULL) {
        return NGX_CONF_ERROR;
    }

    hash_key->key.len = value[1].len;
    hash_key->key.data = ngx_pnalloc(cf->pool, hash_key->key.len);
    if (hash_key->key.data == NULL) {
        return NGX_CONF_ERROR;
    }
    ngx_strlow(hash_key->key.data, value[1].data, hash_key->key.len);
    hash_key->hash = ngx_hash_key(hash_key->key.data, hash_key->key.len);

    hash_key->value = ngx_pnalloc(cf->pool, value[2].len + 1);
    if (hash_key->value == NULL) {
        return NGX_CONF_ERROR;
    }
    ngx_memcpy(hash_key->value, value[2].data, value[2].len);
    ((u_char *)hash_key->value)[value[2].len] = '\0';

    return NGX_CONF_OK;
}


static char *
ngx_http_lua_config_get_value_internal(ngx_http_request_t *r, u_char *name, size_t len)
{
    ngx_http_lua_config_loc_conf_t  *lccf = NULL;
    ngx_str_t                        s_name;
    u_char                           lowcase_name[NGX_MAX_CONF_ERR_LEN];

    s_name.len = len;
    s_name.data = lowcase_name;
    ngx_strlow(s_name.data, name, len);

    if (r) {
        lccf = ngx_http_get_module_loc_conf(r, ngx_http_lua_config_module);
        if (lccf && lccf->lua_configs_hash) {
            char *ret = (char *)ngx_hash_value(lccf->lua_configs_hash,
                                                 ngx_hash_key(s_name.data, s_name.len),
                                                 s_name.data, s_name.len);
            if (ret) {
                return ret;
            }
        }

        lccf = ngx_http_get_module_srv_conf(r, ngx_http_lua_config_module);
        if (lccf && lccf->lua_configs_hash) {
            char *ret = (char *)ngx_hash_value(lccf->lua_configs_hash,
                                                 ngx_hash_key(s_name.data, s_name.len),
                                                 s_name.data, s_name.len);
            if (ret) {
                return ret;
            }
        }
    }

    ngx_http_conf_ctx_t             *conf_ctx;

    if (r) {
        conf_ctx = (ngx_http_conf_ctx_t *)ngx_http_get_module_main_conf(r, ngx_http_core_module);
        if (conf_ctx && conf_ctx->main_conf) {
             lccf = ngx_http_conf_get_module_loc_conf(conf_ctx, ngx_http_lua_config_module);
             if (lccf && lccf->lua_configs_hash) {
                 char *ret = (char *)ngx_hash_value(lccf->lua_configs_hash,
                                                      ngx_hash_key(s_name.data, s_name.len),
                                                      s_name.data, s_name.len);
                 if (ret) {
                     return ret;
                 }
             }
        }

    } else {
        conf_ctx = (ngx_http_conf_ctx_t *)ngx_cycle->conf_ctx->http;
        if (conf_ctx && conf_ctx->main_conf) {
            lccf = ngx_http_conf_get_module_loc_conf(conf_ctx, ngx_http_lua_config_module);
            if (lccf && lccf->lua_configs_hash) {
                char *ret = (char *)ngx_hash_value(lccf->lua_configs_hash,
                                                    ngx_hash_key(s_name.data, s_name.len),
                                                    s_name.data, s_name.len);
                if (ret) {
                    return ret;
                }
            }
        }
    }

    return NULL;
}


static int
ngx_http_lua_get_config_api(lua_State *L)
{
    ngx_http_request_t          *r;
    u_char                      *name_data;
    size_t                       name_len;
    char                        *value;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "expecting 1 argument (name), but got %d", lua_gettop(L));
    }

    name_data = (u_char *)luaL_checklstring(L, 1, &name_len);

    r = ngx_http_lua_get_request(L);

    value = ngx_http_lua_config_get_value_internal(r, name_data, name_len);

    if (value) {
        lua_pushlstring(L, value, ngx_strlen(value));

    } else {
        lua_pushnil(L);
    }

    return 1;
}


static int
ngx_http_lua_create_config_module(lua_State *L)
{
    lua_createtable(L, 0, 1);

    lua_pushcfunction(L, ngx_http_lua_get_config_api);
    lua_setfield(L, -2, "get");

    return 1;
}