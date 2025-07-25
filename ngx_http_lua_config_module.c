
/*
 * Copyright (C) Hanada
 */


#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include <ngx_core.h>
#include <ngx_http.h>
#include <lauxlib.h>
#include "ngx_http_lua_api.h"


ngx_module_t  ngx_http_lua_config_module;


typedef struct {
    ngx_array_t              *keys;
    ngx_hash_keys_arrays_t   *hash_keys;
    ngx_hash_t                hash;
    ngx_uint_t                hash_max_size;
    ngx_uint_t                hash_bucket_size;
} ngx_http_lua_config_loc_conf_t;


static ngx_int_t ngx_http_lua_config_add_variables(ngx_conf_t *cf);
static char *ngx_http_lua_config_directive(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void *ngx_http_lua_config_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_lua_config_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);

static ngx_int_t ngx_http_lua_config_init(ngx_conf_t *cf);

static int ngx_http_lua_config_create_module(lua_State *L);
static int ngx_http_lua_config_get_config(lua_State *L);
static ngx_int_t ngx_http_lua_config_prefix_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);


static ngx_command_t  ngx_http_lua_config_commands[] = {

    { ngx_string("lua_config"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_http_lua_config_directive,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("lua_config_hash_max_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_config_loc_conf_t, hash_max_size),
      NULL },

    { ngx_string("lua_config_hash_bucket_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_config_loc_conf_t, hash_bucket_size),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_lua_config_module_ctx = {
    ngx_http_lua_config_add_variables,     /* preconfiguration */
    ngx_http_lua_config_init,              /* postconfiguration */
    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */
    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */
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


static ngx_http_variable_t  ngx_http_lua_config_vars[] = {

    { ngx_string("lua_config_"), NULL, ngx_http_lua_config_prefix_variable,
        0, NGX_HTTP_VAR_PREFIX, 0 },

      ngx_http_null_variable
};


static ngx_int_t
ngx_http_lua_config_add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t  *var, *v;

    for (v = ngx_http_lua_config_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_config_prefix_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_str_t *name = (ngx_str_t *) data;

    ngx_http_lua_config_loc_conf_t  *lccf;

    u_char         *lua_config;
    size_t          len;
    ngx_keyval_t   *kv;
    ngx_uint_t      key;

    len = name->len - (sizeof("lua_config_") - 1);
    lua_config = name->data + sizeof("lua_config_") - 1;

    if (len == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    lccf = ngx_http_get_module_loc_conf(r, ngx_http_lua_config_module);

    if (lccf == NULL || lccf->keys == NULL || lccf->hash.buckets == NULL) {
        v->not_found = 1;
        return NGX_OK;
    }

    key = ngx_hash_key(lua_config, len);

    kv = ngx_hash_find(&lccf->hash, key, lua_config, len);
    if (kv == NULL) {
        v->not_found = 1;
        return NGX_OK;
    }

    v->data = ngx_pnalloc(r->pool, kv->value.len);
    if (v->data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(v->data, kv->value.data, kv->value.len);

    v->len = kv->value.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_config_init(ngx_conf_t *cf)
{
    if (ngx_http_lua_add_package_preload(cf, "ngx.lua_config",
                                         ngx_http_lua_config_create_module)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void *
ngx_http_lua_config_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_lua_config_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_config_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->hash = { NULL };
     *     conf->hash_keys = NULL;
     *     conf->keys = NULL;
     */

    conf->hash_max_size = NGX_CONF_UNSET_UINT;
    conf->hash_bucket_size = NGX_CONF_UNSET_UINT;

    return conf;
}


static char *
ngx_http_lua_config_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_lua_config_loc_conf_t  *prev = parent;
    ngx_http_lua_config_loc_conf_t  *conf = child;

    ngx_hash_init_t                  hash;
    ngx_uint_t                       i, j, found;
    ngx_keyval_t                    *src, *dst, *kv;

    /* init hash in http {} context to inherit it in all servers */
    if (prev->keys && prev->keys->nelts > 0 && prev->hash.buckets == NULL) {
        ngx_conf_init_uint_value(prev->hash_max_size, 512);
        ngx_conf_init_uint_value(prev->hash_bucket_size,
                                 ngx_align(64, ngx_cacheline_size));

        hash.key = ngx_hash_key;
        hash.max_size = prev->hash_max_size;
        hash.bucket_size = prev->hash_bucket_size;
        hash.name = "lua_config_hash";
        hash.pool = cf->pool;

        if (prev->hash_keys->keys.nelts) {
            hash.hash = &prev->hash;
            hash.temp_pool = NULL;

            if (ngx_hash_init(&hash, prev->hash_keys->keys.elts,
                              prev->hash_keys->keys.nelts) != NGX_OK)
            {
                return NGX_CONF_ERROR;
            }
        }

        prev->hash_keys = NULL;
    }

    ngx_conf_merge_uint_value(conf->hash_max_size, prev->hash_max_size, 512);
    ngx_conf_merge_uint_value(conf->hash_bucket_size, prev->hash_bucket_size,
                              ngx_align(64, ngx_cacheline_size));

    if (conf->hash_keys == NULL) {
        conf->hash = prev->hash;
        conf->keys = prev->keys;
        return NGX_CONF_OK;
    }
    
    if (prev->keys && prev->keys->nelts != 0) {
        src = prev->keys->elts;
        for (i = 0; i < prev->keys->nelts; i++) { 
            found = 0;

            dst = conf->keys->elts;
            for (j = 0; j < conf->keys->nelts; j++) {
                if (src[i].key.len == dst[j].key.len
                    && ngx_strcmp(dst[j].key.data, src[i].key.data) == 0)
                {
                    found = 1;
                    break;
                }
            }

            if (found) {
                continue;
            }

            kv = ngx_array_push(conf->keys);
            if (kv == NULL) {
                return NGX_CONF_ERROR;
            }

            *kv = src[i];

            if (ngx_hash_add_key(conf->hash_keys, &kv->key, kv, 0) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }

    hash.key = ngx_hash_key;
    hash.max_size = conf->hash_max_size;
    hash.bucket_size = conf->hash_bucket_size;
    hash.name = "lua_config_hash";
    hash.pool = cf->pool;

    if (conf->hash_keys->keys.nelts) {
        hash.hash = &conf->hash;
        hash.temp_pool = NULL;

        if (ngx_hash_init(&hash, conf->hash_keys->keys.elts,
                          conf->hash_keys->keys.nelts) != NGX_OK)
        {
            return NGX_CONF_ERROR;
        }
    }

    conf->hash_keys = NULL;

    return NGX_CONF_OK;
}


static char *
ngx_http_lua_config_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_lua_config_loc_conf_t *lccf = conf;

    ngx_str_t             *value;
    u_char                *p;
    ngx_keyval_t          *kv;
    ngx_uint_t             i;

    value = cf->args->elts;

    if (value[1].len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "lua config name cannot be empty");
        return NGX_CONF_ERROR;
    }

    for (p = value[1].data; p < value[1].data + value[1].len; p++) {
        if (!((*p >= '0' && *p <= '9')
              || (*p >= 'a' && *p <= 'z')
              || *p == '_'))
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid character '%c' in lua config "
                               "name \"%V\"", *p, &value[1]);
            return NGX_CONF_ERROR;
        }
    }

    if (lccf->keys == NULL) { 
        lccf->keys = ngx_array_create(cf->pool, 4, sizeof(ngx_keyval_t));
        if (lccf->keys == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    for (i = 0; i < lccf->keys->nelts; i++) {
        kv = lccf->keys->elts;
        if (value[1].len == kv[i].key.len &&
            ngx_strncmp(value[1].data, kv[i].key.data, value[1].len) == 0)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "duplicate config name \"%s\"",
                               value[1].data);
            return NGX_CONF_ERROR;
        }
    }

    kv = ngx_array_push(lccf->keys);
    if (kv == NULL) {
        return NGX_CONF_ERROR;
    }

    kv->key = value[1];
    kv->value = value[2];

    if (lccf->hash_keys == NULL) {
        lccf->hash_keys = ngx_pcalloc(cf->temp_pool,
            sizeof(ngx_hash_keys_arrays_t));
        if (lccf->hash_keys == NULL) {
            return NGX_CONF_ERROR;
        }

        lccf->hash_keys->pool = cf->pool;
        lccf->hash_keys->temp_pool = cf->pool;

        if (ngx_hash_keys_array_init(lccf->hash_keys, NGX_HASH_SMALL)
            != NGX_OK)
        {
            return NGX_CONF_ERROR;
        }
    }

    if (ngx_hash_add_key(lccf->hash_keys, &kv->key, kv, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_lua_config_get_value_internal(ngx_http_request_t *r, u_char *name, size_t len)
{
    ngx_http_lua_config_loc_conf_t  *lccf;
    ngx_str_t                        s_name;
    ngx_keyval_t                    *kv;
    ngx_uint_t                       key;

    s_name.len = len;
    s_name.data = name;

    if (r) {
        lccf = ngx_http_get_module_loc_conf(r, ngx_http_lua_config_module);

    } else {
        lccf = ngx_http_cycle_get_module_main_conf(ngx_cycle,
                                                   ngx_http_lua_config_module);
    }

    if (lccf == NULL || lccf->keys == NULL || lccf->hash.buckets == NULL) {
        return NULL;
    }

    key = ngx_hash_key(s_name.data, s_name.len);

    kv = ngx_hash_find(&lccf->hash, key, s_name.data, s_name.len);
    if (kv == NULL) {
        return NULL;
    }

    return (char *) kv->value.data;
}


static int
ngx_http_lua_config_get_config(lua_State *L)
{
    ngx_http_request_t    *r;
    u_char                *name_data;
    size_t                 name_len;
    char                  *value;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "exactly one argument expected");
    }

    name_data = (u_char *) luaL_checklstring(L, 1, &name_len);

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
ngx_http_lua_config_create_module(lua_State *L)
{
    /* ngx.lua_config */

    lua_createtable(L, 0, 1);

    lua_pushcfunction(L, ngx_http_lua_config_get_config);
    lua_setfield(L, -2, "get");

    return 1;
}