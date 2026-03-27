
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
    ngx_array_t                *keys;
    ngx_hash_t                  hash;
    ngx_uint_t                  hash_max_size;
    ngx_uint_t                  hash_bucket_size;
} ngx_http_lua_config_loc_conf_t;


static ngx_int_t ngx_http_lua_config_add_variables(ngx_conf_t *cf);
static char *ngx_http_lua_config_directive(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void *ngx_http_lua_config_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_lua_config_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);

static ngx_int_t ngx_http_lua_config_get_value_internal(ngx_http_request_t *r,
    u_char *name, size_t len, ngx_str_t *value);

static ngx_int_t ngx_http_lua_config_init(ngx_conf_t *cf);
static int ngx_http_lua_config_create_module(lua_State *L);
static int ngx_http_lua_config_get_config(lua_State *L);
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
                               "invalid character '%c' in lua config "
                               "directive name \"%V\"", *p, &value[1]);
            return NGX_CONF_ERROR;
        }
    }

    if (lccf->keys == NULL) { 
        lccf->keys = ngx_array_create(cf->pool, 4, sizeof(ngx_http_lua_config_keyval_t));
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

    rc = ngx_http_lua_config_get_value_internal(r, name_data, name_len,
                                                &value);
    if (rc == NGX_OK) {
        lua_pushlstring(L, (char *) value.data, value.len);

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