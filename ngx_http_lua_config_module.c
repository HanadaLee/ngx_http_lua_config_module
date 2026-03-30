
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
    ngx_http_complex_value_t   *value;       /* complex value */
    ngx_http_complex_value_t   *filter;      /* filter complex value */
    ngx_uint_t                  negative;    /* negative filter */
} ngx_http_lua_config_cmd_t;


typedef struct {
    ngx_str_t                   key;
    ngx_array_t                *cmds;
} ngx_http_lua_config_keyval_t;


typedef struct {
    ngx_str_t                   host;
    ngx_uint_t                  port;
    ngx_uint_t                  level;
    ngx_uint_t                  weight;
    ngx_flag_t                  down;
} ngx_http_lua_upstream_server_t;


typedef struct {
    ngx_str_t                   name;
    ngx_array_t                *servers;   /* array of ngx_http_lua_upstream_server_t */
    ngx_array_t                *keys;      /* array of ngx_http_lua_config_keyval_t */
} ngx_http_lua_upstream_t;


typedef struct {
    ngx_array_t                *keys;      /* array of ngx_keyval_t */
} ngx_http_lua_config_main_conf_t;


typedef struct {
    ngx_array_t                *upstreams; /* array of ngx_http_lua_upstream_t */
    ngx_hash_t                  hash;
} ngx_http_lua_config_srv_conf_t;


typedef struct {
    ngx_array_t                *keys;
    ngx_hash_t                  hash;
    ngx_uint_t                  hash_max_size;
    ngx_uint_t                  hash_bucket_size;
} ngx_http_lua_config_loc_conf_t;


static ngx_int_t ngx_http_lua_config_add_variables(ngx_conf_t *cf);
static char *ngx_http_lua_config_directive(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_lua_init_config_directive(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);

static ngx_int_t ngx_http_lua_config_get_value_internal(ngx_http_request_t *r,
    u_char *name, size_t len, ngx_str_t *value);
static char *ngx_http_lua_upstream_block(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_lua_upstream(ngx_conf_t *cf,
    ngx_command_t *dummy, void *conf);

static void *ngx_http_lua_config_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_lua_config_create_srv_conf(ngx_conf_t *cf);
static char *ngx_http_lua_config_merge_srv_conf(ngx_conf_t *cf, void *parent,
    void *child);
static void *ngx_http_lua_config_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_lua_config_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);

static ngx_int_t ngx_http_lua_config_init(ngx_conf_t *cf);

static int ngx_http_lua_config_create_module(lua_State *L);
static int ngx_http_lua_config_get_config(lua_State *L);
static int ngx_http_lua_config_get_upstream(lua_State *L);
static int ngx_http_lua_get_init_configs(lua_State *L);

static ngx_int_t ngx_http_lua_config_prefix_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);


static ngx_command_t  ngx_http_lua_config_commands[] = {

    { ngx_string("lua_config"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE23,
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

    { ngx_string("lua_upstream"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_BLOCK|NGX_CONF_TAKE1,
      ngx_http_lua_upstream_block,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("lua_init_config"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE2,
      ngx_http_lua_init_config_directive,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_lua_config_module_ctx = {
    ngx_http_lua_config_add_variables,     /* preconfiguration */
    ngx_http_lua_config_init,              /* postconfiguration */
    ngx_http_lua_config_create_main_conf,  /* create main configuration */
    NULL,                                  /* init main configuration */
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


static ngx_http_variable_t  ngx_http_lua_config_vars[] = {

    { ngx_string("lua_config_"), NULL, ngx_http_lua_config_prefix_variable,
    0, NGX_HTTP_VAR_NOCACHEABLE|NGX_HTTP_VAR_PREFIX, 0 },

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
    ngx_str_t  *name = (ngx_str_t *) data;

    u_char     *lua_config;
    size_t      len;
    ngx_str_t   value;
    ngx_int_t   rc;

    len = name->len - (sizeof("lua_config_") - 1);
    lua_config = name->data + sizeof("lua_config_") - 1;

    if (len == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    rc = ngx_http_lua_config_get_value_internal(r, lua_config, len, &value);
    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (rc != NGX_OK) {
        v->not_found = 1;
        return NGX_OK;
    }

    v->data = value.data;
    v->len = value.len;
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


static char *
ngx_http_lua_init_config_directive(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    ngx_http_lua_config_main_conf_t  *mcf = conf;

    ngx_str_t                        *value;
    u_char                           *p;
    ngx_keyval_t    *kv;
    ngx_uint_t                        i;

    value = cf->args->elts;

    if (value[1].len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "lua_init_config directive name cannot be empty");
        return NGX_CONF_ERROR;
    }

    for (p = value[1].data; p < value[1].data + value[1].len; p++) {
        if (!((*p >= '0' && *p <= '9')
              || (*p >= 'a' && *p <= 'z')
              || *p == '_'))
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid character in lua_init_config "
                               "directive name \"%V\"", &value[1]);
            return NGX_CONF_ERROR;
        }
    }

    if (mcf->keys == NULL) {
        mcf->keys = ngx_array_create(cf->pool, 4,
                                     sizeof(ngx_keyval_t));
        if (mcf->keys == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    /* check for duplicate key: error on conflict */
    kv = mcf->keys->elts;
    for (i = 0; i < mcf->keys->nelts; i++) {
        if (value[1].len == kv[i].key.len
            && ngx_strncmp(value[1].data, kv[i].key.data, value[1].len) == 0)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "duplicate lua_init_config \"%V\"",
                               &value[1]);
            return NGX_CONF_ERROR;
        }
    }

    kv = ngx_array_push(mcf->keys);
    if (kv == NULL) {
        return NGX_CONF_ERROR;
    }

    kv->key = value[1];
    kv->value = value[2];

    return NGX_CONF_OK;
}


static int
ngx_http_lua_get_init_configs(lua_State *L)
{
    ngx_http_lua_config_main_conf_t  *mcf;
    ngx_keyval_t                     *kv;
    ngx_uint_t                        i;

    mcf = ngx_http_cycle_get_module_main_conf(ngx_cycle,
                                              ngx_http_lua_config_module);

    lua_createtable(L, 0, (mcf != NULL && mcf->keys != NULL)
                           ? (int) mcf->keys->nelts : 0);

    if (mcf == NULL || mcf->keys == NULL) {
        return 1;
    }

    kv = mcf->keys->elts;
    for (i = 0; i < mcf->keys->nelts; i++) {
        lua_pushlstring(L, (char *) kv[i].key.data, kv[i].key.len);
        lua_pushlstring(L, (char *) kv[i].value.data, kv[i].value.len);
        lua_rawset(L, -3);
    }

    return 1;
}


static char *
ngx_http_lua_config_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_lua_config_loc_conf_t *lccf = conf;

    ngx_str_t                      *value;
    u_char                         *p;
    ngx_http_lua_config_keyval_t   *kv;
    ngx_http_lua_config_cmd_t      *lcmd;
    ngx_uint_t                      i;
    ngx_str_t                       s;

    ngx_http_compile_complex_value_t   ccv;

    value = cf->args->elts;

    if (value[1].len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "lua config directive name cannot be empty");
        return NGX_CONF_ERROR;
    }

    for (p = value[1].data; p < value[1].data + value[1].len; p++) {
        if (!((*p >= '0' && *p <= '9')
              || (*p >= 'a' && *p <= 'z')
              || *p == '_'))
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid character in lua config "
                               "directive name \"%V\"", &value[1]);
            return NGX_CONF_ERROR;
        }
    }

    if (lccf->keys == NULL) { 
        lccf->keys = ngx_array_create(cf->pool, 4,
                                      sizeof(ngx_http_lua_config_keyval_t));
        if (lccf->keys == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    kv = lccf->keys->elts;
    for (i = 0; i < lccf->keys->nelts; i++) {
        if (value[1].len == kv[i].key.len &&
            ngx_strncmp(value[1].data, kv[i].key.data, value[1].len) == 0)
        {
            break;
        }
    }

    if (i == lccf->keys->nelts) {
        kv = ngx_array_push(lccf->keys);
        if (kv == NULL) {
            return NGX_CONF_ERROR;
        }

        kv->key = value[1];

        kv->cmds = ngx_array_create(cf->pool, 4,
                                    sizeof(ngx_http_lua_config_cmd_t));
        if (kv->cmds == NULL) {
            return NGX_CONF_ERROR;
        }

    } else {
        kv = &kv[i];
    }

    lcmd = ngx_array_push(kv->cmds);
    if (lcmd == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[2];
    ccv.complex_value = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));
    if (ccv.complex_value == NULL) {
        return NGX_CONF_ERROR;
    }

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    lcmd->value = ccv.complex_value;

    if (cf->args->nelts == 3) {
        lcmd->negative = 0;
        lcmd->filter = NULL;
        return NGX_CONF_OK;
    }

    if (ngx_strncmp(value[3].data, "if=", 3) == 0) {
        s.len = value[3].len - 3;
        s.data = value[3].data + 3;
        lcmd->negative = 0;

    } else if (ngx_strncmp(value[3].data, "if!=", 4) == 0) {
        s.len = value[3].len - 4;
        s.data = value[3].data + 4;
        lcmd->negative = 1;

    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "invalid parameter \"%V\"", &value[3]);
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &s;
    ccv.complex_value = ngx_palloc(cf->pool,
                                sizeof(ngx_http_complex_value_t));
    if (ccv.complex_value == NULL) {
        return NGX_CONF_ERROR;
    }

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    lcmd->filter = ccv.complex_value;

    return NGX_CONF_OK;
}


static void *
ngx_http_lua_config_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_lua_config_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_config_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->keys = NULL;
     */

    return conf;
}


static void *
ngx_http_lua_config_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_lua_config_srv_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_config_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->hash = { NULL };
     *     conf->upstreams = NULL;
     */

    return conf;
}


static char *
ngx_http_lua_config_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_lua_config_srv_conf_t  *prev = parent;
    ngx_http_lua_config_srv_conf_t  *conf = child;

    ngx_hash_init_t                  hash;
    ngx_hash_keys_arrays_t           ha;
    ngx_uint_t                       i, j, found;
    ngx_http_lua_upstream_t         *src, *dst, *us;

    if (prev->upstreams != NULL && prev->hash.buckets == NULL) {
        ngx_memzero(&ha, sizeof(ngx_hash_keys_arrays_t));
        ha.pool = cf->pool;
        ha.temp_pool = cf->temp_pool;

        if (ngx_hash_keys_array_init(&ha, NGX_HASH_SMALL) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        us = prev->upstreams->elts;
        for (i = 0; i < prev->upstreams->nelts; i++) {
            if (ngx_hash_add_key(&ha, &us[i].name, &us[i], 0) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }

        if (ha.keys.nelts > 0) {
            hash.key = ngx_hash_key;
            hash.max_size = 512;
            hash.bucket_size = ngx_align(64, ngx_cacheline_size);
            hash.name = "lua_upstream_hash";
            hash.pool = cf->pool;
            hash.temp_pool = NULL;
            hash.hash = &prev->hash;

            if (ngx_hash_init(&hash, ha.keys.elts, ha.keys.nelts) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }

    if (conf->upstreams == NULL) {
        conf->hash = prev->hash;
        conf->upstreams = prev->upstreams;
        return NGX_CONF_OK;
    }

    if (prev->upstreams && prev->upstreams->nelts != 0) {
        src = prev->upstreams->elts;
        for (i = 0; i < prev->upstreams->nelts; i++) {
            found = 0;

            dst = conf->upstreams->elts;
            for (j = 0; j < conf->upstreams->nelts; j++) {
                if (src[i].name.len == dst[j].name.len
                    && ngx_strcmp(dst[j].name.data, src[i].name.data) == 0)
                {
                    found = 1;
                    break;
                }
            }

            if (found) {
                continue;
            }

            us = ngx_array_push(conf->upstreams);
            if (us == NULL) {
                return NGX_CONF_ERROR;
            }

            *us = src[i];
        }
    }

    ngx_memzero(&ha, sizeof(ngx_hash_keys_arrays_t));
    ha.pool = cf->pool;
    ha.temp_pool = cf->temp_pool;

    if (ngx_hash_keys_array_init(&ha, NGX_HASH_SMALL) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    us = conf->upstreams->elts;
    for (i = 0; i < conf->upstreams->nelts; i++) {
        if (ngx_hash_add_key(&ha, &us[i].name, &us[i], 0) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    if (ha.keys.nelts == 0) {
        return NGX_CONF_OK;
    }

    hash.key = ngx_hash_key;
    hash.max_size = 512;
    hash.bucket_size = ngx_align(64, ngx_cacheline_size);
    hash.name = "lua_upstream_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;
    hash.hash = &conf->hash;

    if (ngx_hash_init(&hash, ha.keys.elts, ha.keys.nelts) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

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

    /*
     * set by ngx_pcalloc():
     *
     *     conf->hash = { NULL };
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
    ngx_hash_keys_arrays_t           ha;
    ngx_uint_t                       i, j, found;
    ngx_http_lua_config_keyval_t    *src, *dst, *kv;
    ngx_http_lua_config_cmd_t       *cmd_src, *cmd_dst;
    ngx_uint_t                       k;

    ngx_conf_merge_uint_value(conf->hash_max_size, prev->hash_max_size, 512);
    ngx_conf_merge_uint_value(conf->hash_bucket_size, prev->hash_bucket_size,
                              ngx_align(64, ngx_cacheline_size));

    if (prev->keys != NULL && prev->hash.buckets == NULL) {
        ngx_memzero(&ha, sizeof(ngx_hash_keys_arrays_t));
        ha.pool = cf->pool;
        ha.temp_pool = cf->temp_pool;

        if (ngx_hash_keys_array_init(&ha, NGX_HASH_SMALL) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        kv = prev->keys->elts;
        for (i = 0; i < prev->keys->nelts; i++) {
            if (ngx_hash_add_key(&ha, &kv[i].key, &kv[i], 0) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }

        if (ha.keys.nelts > 0) {
            hash.key = ngx_hash_key;
            hash.max_size = (prev->hash_max_size != NGX_CONF_UNSET_UINT)
                            ? prev->hash_max_size : 512;
            hash.bucket_size = (prev->hash_bucket_size != NGX_CONF_UNSET_UINT)
                                ? prev->hash_bucket_size
                                : ngx_align(64, ngx_cacheline_size);
            hash.name = "lua_config_hash";
            hash.pool = cf->pool;
            hash.temp_pool = NULL;
            hash.hash = &prev->hash;

            if (ngx_hash_init(&hash, ha.keys.elts, ha.keys.nelts) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }

    if (conf->keys == NULL) {
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
                /* append parent cmds to the end of child's same-name kv */
                cmd_src = src[i].cmds->elts;
                for (k = 0; k < src[i].cmds->nelts; k++) {
                    cmd_dst = ngx_array_push(dst[j].cmds);
                    if (cmd_dst == NULL) {
                        return NGX_CONF_ERROR;
                    }
                    *cmd_dst = cmd_src[k];
                }

                continue;
            }

            kv = ngx_array_push(conf->keys);
            if (kv == NULL) {
                return NGX_CONF_ERROR;
            }

            *kv = src[i];
        }
    }

    ngx_memzero(&ha, sizeof(ngx_hash_keys_arrays_t));
    ha.pool = cf->pool;
    ha.temp_pool = cf->temp_pool;

    if (ngx_hash_keys_array_init(&ha, NGX_HASH_SMALL) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    kv = conf->keys->elts;
    for (i = 0; i < conf->keys->nelts; i++) {
        if (ngx_hash_add_key(&ha, &kv[i].key, &kv[i], 0) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    if (ha.keys.nelts == 0) {
        return NGX_CONF_OK;
    }

    hash.key = ngx_hash_key;
    hash.max_size = conf->hash_max_size;
    hash.bucket_size = conf->hash_bucket_size;
    hash.name = "lua_config_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;
    hash.hash = &conf->hash;

    if (ngx_hash_init(&hash, ha.keys.elts, ha.keys.nelts) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_lua_upstream_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_lua_config_srv_conf_t  *lscf = conf;

    ngx_str_t                       *value;
    u_char                          *p;
    ngx_http_lua_upstream_t         *us;
    ngx_uint_t                       i;
    char                            *rv;
    ngx_conf_t                       save;

    value = cf->args->elts;

    if (value[1].len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "lua_upstream name cannot be empty");
        return NGX_CONF_ERROR;
    }

    for (p = value[1].data; p < value[1].data + value[1].len; p++) {
        if (!((*p >= '0' && *p <= '9')
              || (*p >= 'a' && *p <= 'z')
              || *p == '_'))
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid character in lua_upstream "
                               "name \"%V\"", &value[1]);
            return NGX_CONF_ERROR;
        }
    }

    if (lscf->upstreams == NULL) {
        lscf->upstreams = ngx_array_create(cf->pool, 4,
                                       sizeof(ngx_http_lua_upstream_t));
        if (lscf->upstreams == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    /* check for duplicate name in current context */
    us = lscf->upstreams->elts;
    for (i = 0; i < lscf->upstreams->nelts; i++) {
        if (value[1].len == us[i].name.len
            && ngx_strcmp(value[1].data, us[i].name.data) == 0)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "duplicate lua_upstream \"%V\"", &value[1]);
            return NGX_CONF_ERROR;
        }
    }

    us = ngx_array_push(lscf->upstreams);
    if (us == NULL) {
        return NGX_CONF_ERROR;
    }

    us->name = value[1];

    us->servers = ngx_array_create(cf->pool, 4,
                                   sizeof(ngx_http_lua_upstream_server_t));
    if (us->servers == NULL) {
        return NGX_CONF_ERROR;
    }

    us->keys = ngx_array_create(cf->pool, 4,
                                sizeof(ngx_http_lua_config_keyval_t));
    if (us->keys == NULL) {
        return NGX_CONF_ERROR;
    }

    save = *cf;
    cf->handler = ngx_http_lua_upstream;
    cf->handler_conf = conf;

    rv = ngx_conf_parse(cf, NULL);

    *cf = save;

    return rv;
}


static char *
ngx_http_lua_upstream(ngx_conf_t *cf, ngx_command_t *dummy, void *conf)
{
    ngx_http_lua_config_srv_conf_t  *lscf = conf;
    ngx_http_lua_upstream_t         *us;
    ngx_str_t                       *value;
    ngx_uint_t                       i;
    ngx_http_lua_upstream_server_t  *server;
    ngx_http_lua_config_keyval_t    *kv;
    ngx_http_lua_config_cmd_t       *lcmd;
    ngx_http_compile_complex_value_t ccv;
    ngx_str_t                        s;
    ngx_url_t                        u;
    u_char                          *p;

    value = cf->args->elts;

    us = (ngx_http_lua_upstream_t *) lscf->upstreams->elts
         + (lscf->upstreams->nelts - 1);
    if (us == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "lua_upstream handler called without context");
        return NGX_CONF_ERROR;
    }

    /* parse "server host[:port] [level=N] [weight=N] [down]" */
    if (ngx_strcmp(value[0].data, "server") == 0) {
        if (cf->args->nelts < 2) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid number of arguments in "
                               "\"server\" directive inside lua_upstream");
            return NGX_CONF_ERROR;
        }

        server = ngx_array_push(us->servers);
        if (server == NULL) {
            return NGX_CONF_ERROR;
        }

        server->level = 1;
        server->weight = 1;
        server->down = 0;
        server->port = 0;

        if (ngx_strncasecmp(value[1].data, (u_char *) "unix:", 5) == 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "unix domain socket is not supported "
                               "in lua_upstream server");
            return NGX_CONF_ERROR;
        }

        ngx_memzero(&u, sizeof(ngx_url_t));

        u.url = value[1];
        u.default_port = 0;
        u.no_resolve = 1;

        if (ngx_parse_url(cf->pool, &u) != NGX_OK) {
            if (u.err) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "%s in upstream \"%V\"", u.err, &u.url);
            }

            return NGX_CONF_ERROR;
        }

        server->host = u.host;
        server->port = u.port;

        /* parse optional params: level=N, weight=N, down */
        for (i = 2; i < cf->args->nelts; i++) {
            if (ngx_strncmp(value[i].data, "level=", 6) == 0) {
                server->level = ngx_atoi(value[i].data + 6,
                                         value[i].len - 6);

                if (server->level == (ngx_uint_t) NGX_ERROR) {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "invalid level value \"%V\"",
                                       &value[i]);
                    return NGX_CONF_ERROR;
                }

                continue;
            }

            if (ngx_strncmp(value[i].data, "weight=", 7) == 0) {
                server->weight = ngx_atoi(value[i].data + 7,
                                          value[i].len - 7);

                if (server->weight == (ngx_uint_t) NGX_ERROR) {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "invalid weight value \"%V\"",
                                       &value[i]);
                    return NGX_CONF_ERROR;
                }

                if (server->weight == 0) {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "weight value cannot be zero "
                                       "\"%V\"", &value[i]);
                    return NGX_CONF_ERROR;
                }

                continue;
            }

            if (ngx_strcmp(value[i].data, "down") == 0) {
                server->down = 1;
                continue;
            }

            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid parameter \"%V\" in server directive "
                               "inside lua_upstream", &value[i]);

            return NGX_CONF_ERROR;
        }

        return NGX_CONF_OK;
    }

    /* parse other config items: key [value [if=cond/if!=cond]] */
    if (cf->args->nelts != 2 && cf->args->nelts != 3) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                            "invalid number of the lua_upstream parameters");
        return NGX_CONF_ERROR;
    }

    if (ngx_strcmp(value[0].data, "name") == 0
        || ngx_strcmp(value[0].data, "crc32") == 0)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid directive name in lua_upstream");
        return NGX_CONF_ERROR;
    }

    /* find or create keyval */
    kv = us->keys->elts;
    for (i = 0; i < us->keys->nelts; i++) {
        if (value[0].len == kv[i].key.len
            && ngx_strncmp(value[0].data, kv[i].key.data, value[0].len) == 0)
        {
            break;
        }
    }

    if (i == us->keys->nelts) {
        kv = ngx_array_push(us->keys);
        if (kv == NULL) {
            return NGX_CONF_ERROR;
        }

        kv->key = value[0];

        kv->cmds = ngx_array_create(cf->pool, 4,
                                    sizeof(ngx_http_lua_config_cmd_t));
        if (kv->cmds == NULL) {
            return NGX_CONF_ERROR;
        }

    } else {
        kv = &kv[i];
    }

    lcmd = ngx_array_push(kv->cmds);
    if (lcmd == NULL) {
        return NGX_CONF_ERROR;
    }

    /* compile value */
    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));
    if (ccv.complex_value == NULL) {
        return NGX_CONF_ERROR;
    }

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    lcmd->value = ccv.complex_value;

    if (cf->args->nelts == 2) {
        lcmd->filter = NULL;
        lcmd->negative = 0;
        return NGX_CONF_OK;
    }

    /* parse if=/if!= filter */
    if (ngx_strncmp(value[2].data, "if=", 3) == 0) {
        s.len = value[2].len - 3;
        s.data = value[2].data + 3;
        lcmd->negative = 0;

    } else if (ngx_strncmp(value[2].data, "if!=", 4) == 0) {
        s.len = value[2].len - 4;
        s.data = value[2].data + 4;
        lcmd->negative = 1;

    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid parameter \"%V\" in lua_upstream",
                           &value[2]);
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &s;
    ccv.complex_value = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));

    if (ccv.complex_value == NULL) {
        return NGX_CONF_ERROR;
    }

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    lcmd->filter = ccv.complex_value;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_lua_config_get_value_internal(ngx_http_request_t *r, u_char *name,
    size_t len, ngx_str_t *value)
{
    ngx_http_lua_config_loc_conf_t  *lccf;
    ngx_http_lua_config_keyval_t    *kv;
    ngx_http_lua_config_cmd_t       *cmds;
    ngx_str_t                        s;
    ngx_uint_t                       key, i;

    if (r == NULL) {
        return NGX_DECLINED;
    }

    s.len = len;
    s.data = name;

    lccf = ngx_http_get_module_loc_conf(r, ngx_http_lua_config_module);

    if (lccf == NULL || lccf->keys == NULL || lccf->hash.buckets == NULL) {
        return NGX_DECLINED;
    }

    key = ngx_hash_key(s.data, s.len);

    kv = ngx_hash_find(&lccf->hash, key, s.data, s.len);
    if (kv == NULL) {
        return NGX_DECLINED;
    }

    cmds = kv->cmds->elts;
    for (i = 0; i < kv->cmds->nelts; i++) {
        if (cmds[i].filter) {
            if (ngx_http_complex_value(r, cmds[i].filter, &s)
                != NGX_OK)
            {
                return NGX_ERROR;
            }

            if (s.len == 0
                || (s.len == 1 && s.data[0] == '0'))
            {
                if (!cmds[i].negative) {
                    continue;
                }

            } else {
                if (cmds[i].negative) {
                    continue;
                }
            }
        }

        if (ngx_http_complex_value(r, cmds[i].value, value) != NGX_OK) {
            return NGX_ERROR;
        }

        return NGX_OK;
    }

    return NGX_DECLINED;
}


static int
ngx_http_lua_config_get_config(lua_State *L)
{
    ngx_http_request_t    *r;
    u_char                *name_data;
    size_t                 name_len;
    ngx_str_t              value;
    ngx_int_t              rc;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "exactly one argument expected");
    }

    name_data = (u_char *) luaL_checklstring(L, 1, &name_len);

    r = ngx_http_lua_get_request(L);

    rc = ngx_http_lua_config_get_value_internal(r, name_data, name_len, &value);
    if (rc == NGX_OK) {
        lua_pushlstring(L, (char *) value.data, value.len);

    } else {
        lua_pushnil(L);
    }

    return 1;
}


static int
ngx_http_lua_upstream_key_cmp(const void *a, const void *b)
{
    const ngx_http_lua_config_keyval_t  *ka = a;
    const ngx_http_lua_config_keyval_t  *kb = b;

    ngx_uint_t  min_len;
    ngx_int_t   rc;

    min_len = ngx_min(ka->key.len, kb->key.len);
    rc = ngx_strncmp(ka->key.data, kb->key.data, min_len);
    if (rc != 0) {
        return rc;
    }

    return (int) ka->key.len - (int) kb->key.len;
}


static int
ngx_http_lua_config_get_upstream(lua_State *L)
{
    ngx_http_request_t              *r;
    ngx_http_lua_config_srv_conf_t  *lscf;
    ngx_http_lua_upstream_t         *us;
    ngx_http_lua_upstream_server_t  *servers;
    ngx_http_lua_config_keyval_t    *kv, *sorted_kv;
    ngx_http_lua_config_cmd_t       *cmds;
    ngx_str_t                        val;
    ngx_uint_t                       key, i, j;
    u_char                          *name_data;
    size_t                           name_len;
    uint32_t                         crc;
    u_char                           crc_str[8];
    size_t                           crc_str_len;

    u_char                           num_buf[NGX_INT_T_LEN];
    size_t                           num_len;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "exactly one argument expected");
    }

    name_data = (u_char *) luaL_checklstring(L, 1, &name_len);

    r = ngx_http_lua_get_request(L);
    if (r == NULL) {
        lua_pushnil(L);
        return 1;
    }

    lscf = ngx_http_get_module_srv_conf(r, ngx_http_lua_config_module);
    if (lscf == NULL || lscf->upstreams == NULL
        || lscf->hash.buckets == NULL)
    {
        lua_pushnil(L);
        return 1;
    }

    key = ngx_hash_key(name_data, name_len);
    us = ngx_hash_find(&lscf->hash, key, name_data, name_len);
    if (us == NULL) {
        lua_pushnil(L);
        return 1;
    }

    /* incremental crc32 computation */
    ngx_crc32_init(crc);

    /* start with name */
    ngx_crc32_update(&crc, us->name.data, us->name.len);

    servers = us->servers->elts;

    /* create result table */
    lua_createtable(L, 0, 4 + us->keys->nelts);

    /* name */
    lua_pushlstring(L, (char *) us->name.data, us->name.len);
    lua_setfield(L, -2, "name");

    /* servers */
    lua_createtable(L, us->servers->nelts, 0);

    for (i = 0; i < us->servers->nelts; i++) {
        lua_createtable(L, 0, 5);

        lua_pushlstring(L, (char *) servers[i].host.data, servers[i].host.len);
        lua_setfield(L, -2, "host");

        lua_pushinteger(L, servers[i].port);
        lua_setfield(L, -2, "port");

        lua_pushinteger(L, servers[i].level);
        lua_setfield(L, -2, "level");

        lua_pushinteger(L, servers[i].weight);
        lua_setfield(L, -2, "weight");

        lua_pushboolean(L, servers[i].down);
        lua_setfield(L, -2, "down");

        lua_rawseti(L, -2, i + 1);

        /* crc: |host:port:level:weight:down */
        ngx_crc32_update(&crc, (u_char *) "|", 1);
        ngx_crc32_update(&crc, servers[i].host.data, servers[i].host.len);
        ngx_crc32_update(&crc, (u_char *) ":", 1);
        num_len = ngx_sprintf(num_buf, "%ui", servers[i].port) - num_buf;
        ngx_crc32_update(&crc, num_buf, num_len);
        ngx_crc32_update(&crc, (u_char *) ":", 1);
        num_len = ngx_sprintf(num_buf, "%ui", servers[i].level) - num_buf;
        ngx_crc32_update(&crc, num_buf, num_len);
        ngx_crc32_update(&crc, (u_char *) ":", 1);
        num_len = ngx_sprintf(num_buf, "%ui", servers[i].weight) - num_buf;
        ngx_crc32_update(&crc, num_buf, num_len);
        ngx_crc32_update(&crc, (u_char *) ":", 1);
        ngx_crc32_update(&crc, servers[i].down ? (u_char *) "1"
                                               : (u_char *) "0", 1);
    }

    lua_setfield(L, -2, "servers");

    /* sort keys alphabetically for crc and output */
    kv = us->keys->elts;

    sorted_kv = ngx_palloc(r->pool,
                           us->keys->nelts
                                       * sizeof(ngx_http_lua_config_keyval_t));
    if (sorted_kv == NULL) {
        return luaL_error(L, "memory allocation failed");
    }

    ngx_memcpy(sorted_kv, kv,
               us->keys->nelts * sizeof(ngx_http_lua_config_keyval_t));

    ngx_qsort(sorted_kv, us->keys->nelts,
              sizeof(ngx_http_lua_config_keyval_t),
              ngx_http_lua_upstream_key_cmp);

    /* process config keys */
    for (i = 0; i < us->keys->nelts; i++) {
        cmds = sorted_kv[i].cmds->elts;

        /* find first matching value (filter logic from lua_config) */
        for (j = 0; j < sorted_kv[i].cmds->nelts; j++) {
            if (cmds[j].filter) {
                if (ngx_http_complex_value(r, cmds[j].filter, &val)
                    != NGX_OK)
                {
                    return luaL_error(L, "failed to evaluate filter");
                }

                if (val.len == 0
                    || (val.len == 1 && val.data[0] == '0'))
                {
                    if (!cmds[j].negative) {
                        continue;
                    }

                } else {
                    if (cmds[j].negative) {
                        continue;
                    }
                }
            }

            /* matched */
            if (ngx_http_complex_value(r, cmds[j].value, &val)
                != NGX_OK)
            {
                return luaL_error(L, "failed to evaluate value");
            }

            lua_pushlstring(L, (char *) val.data, val.len);

            /* crc: |key=value */
            ngx_crc32_update(&crc, (u_char *) "|", 1);
            ngx_crc32_update(&crc, sorted_kv[i].key.data,
                                sorted_kv[i].key.len);
            ngx_crc32_update(&crc, (u_char *) "=", 1);
            ngx_crc32_update(&crc, val.data, val.len);

            lua_setfield(L, -2, (char *) sorted_kv[i].key.data);
            break;
        }
    }

    /* compute crc32 */
    ngx_crc32_final(crc);
    crc_str_len = ngx_sprintf(crc_str, "%08xD", crc) - crc_str;

    lua_pushlstring(L, (char *) crc_str, crc_str_len);
    lua_setfield(L, -2, "crc32");

    return 1;
}


static int
ngx_http_lua_config_create_module(lua_State *L)
{
    /* ngx.lua_config */

    lua_createtable(L, 0, 3);

    lua_pushcfunction(L, ngx_http_lua_config_get_config);
    lua_setfield(L, -2, "get");

    lua_pushcfunction(L, ngx_http_lua_config_get_upstream);
    lua_setfield(L, -2, "get_upstream");

    lua_pushcfunction(L, ngx_http_lua_get_init_configs);
    lua_setfield(L, -2, "get_init_configs");

    return 1;
}