# ngx_http_lua_config_module

# Name
`ngx_http_lua_config_module` allows defining key-value configuration items in Nginx configuration, which can then be retrieved in Lua modules via the `ngx.lua_config` API or as nginx variables. This enables unified management and passing of configuration information from the Nginx configuration layer to Lua logic.

# Table of Content

- [ngx\_http\_lua\_config\_module](#ngx_http_lua_config_module)
- [Name](#name)
- [Table of Content](#table-of-content)
- [Status](#status)
- [Synopsis](#synopsis)
- [Installation](#installation)
- [Directives](#directives)
    - [`lua_config`](#lua_config)
    - [`lua_config_hash_max_size`](#lua_config_hash_max_size)
    - [`lua_config_hash_bucket_size`](#lua_config_hash_bucket_size)
    - [`lua_upstream`](#lua_upstream)
- [Variables](#variables)
    - [`$lua_config_name`](#lua_config_name)
- [Lua API](#lua-api)
    - [`ngx.lua_config.get(key)`](#ngxlua_configgetkey)
    - [`ngx.lua_config.get_upstream(name)`](#ngxlua_configget_upstreamname)
- [Author](#author)
- [License](#license)

# Status
This Nginx module is currently considered experimental. Issues and PRs are welcome if you encounter any problems.

# Synopsis

```nginx
http {
    lua_config server_id my_server_id;

    server {
        listen 80;
        server_name example.com;

        # Local configuration overrides parent configuration
        lua_config server_id my_server_id_A;

        lua_upstream backend {
            server 127.0.0.1:8080 weight=5;
            server 10.0.0.2:8080 level=1;
            keepalive_timeout 60s;
        }

        location /api {
            # Local configuration
            lua_config api_version v1.0;

            # Access via Nginx variables
            add_header X-Env $lua_config_environment;
            add_header X-Server-Region $lua_config_server_region;
            add_header X-Server-Id $lua_config_server_id; # "my_server_id_A"
            add_header X-Api-Version $lua_config_api_version;

            content_by_lua_block {
                -- Access via Lua API
				local lua_config = require "ngx.lua_config"
                local env = lua_config.get("environment")
                local region = lua_config.get("server_region")
                local server_id = lua_config.get("server_id") -- "my_server_id_A"
                local api_version = lua_config.get("api_version")

                ngx.log(ngx.INFO, "Env: ", env, ", Region: ", region, ", Server ID: ", server_id, ", API Version: ", api_version)

                -- Access upstream config
                local up = lua_config.get_upstream("backend")
                if up then
                    ngx.log(ngx.INFO, "Backend CRC32: ", up.crc32)
                end

                ngx.say("OK")
            }
        }

        location /another {
            # In another location, server_id will be my_server_id_A
            # api_version is not defined here, so it will be nil
            content_by_lua_block {
                local server_id = ngx.lua_config.get("server_id")
                local api_version = ngx.lua_config.get("api_version")
                ngx.log(ngx.INFO, "Server ID: ", server_id, ", API Version: ", api_version or "nil")
                ngx.say("OK")
            }
        }
    }
}
```

# Installation
To use this module, configure your Nginx branch with `--add-module=/path/to/ngx_http_lua_config_module`.

# Directives

### `lua_config`

**Syntax:** `lua_config key value [if=condition];`

**Default:** `-`

**Context:** `http`, `server`, `location`

Defines a key-value configuration item.
*   `key`: The key name, only allowed to contain lowercase letters, numbers, and underscores. The same key cannot be defined repeatedly within the same `context`.
*   `value`: The key's value, an arbitrary string. it can contain variables.
*   `if`: enables conditional value. If the `condition` evaluates to “0” or an empty string, the subsequent definition of `key` will be evaluated. If none of the definitions are met, the Lua code will return `nil`.

**Example:**

```nginx
lua_config data_source primary;
lua_config set_header $arg_test if=$arg_test;
lua_config cache_timeout 300s;
```

### `lua_config_hash_max_size`

**Syntax:** `lua_config_hash_max_size number;`

**Default:** `lua_config_hash_max_size 512;`

**Context:** `http`, `server`, `location`

Sets the maximum size of the hash table for storing `lua_config` key-value pairs.

### `lua_config_hash_bucket_size`

**Syntax:** `lua_config_hash_bucket_size number;`

**Default:** `lua_config_hash_bucket_size 32|64|128;`

**Context:** `http`, `server`, `location`

Sets the bucket size of the hash table for `lua_config` items. The default value depends on the processor's cache line size. Details on setting up hash tables are provided in a separate document.

### `lua_upstream`

**Syntax:** `lua_upstream name { ... }`

**Default:** `-`

**Context:** `http`, `server`

Defines a named upstream configuration block that can be retrieved at request time via the Lua API. The `name` may only contain lowercase letters, digits, and underscores. Duplicate names within the same context are not allowed. When defined at both `http` and `server` levels, the `server`-level definition completely overrides the `http`-level one (no merging).

Inside the block, two types of entries are supported:

**Server entries:**

```
server host[:port] [level=N] [weight=N] [down];
```

*   `host`: An IP address (IPv4 or IPv6 in `[addr]` notation) or domain name. Unix domain sockets (`unix:`) are not supported. Variables are not allowed.
*   `port`: Optional port number. Defaults to `0` if omitted.
*   `level`: Server level, defaults to `0`.
*   `weight`: Server weight, defaults to `1`.
*   `down`: Marks the server as unavailable.

**Config items:**

```
key;                      # boolean (true)
key value;                # key-value pair
key value if=condition;   # conditional value
key value if!=condition;  # negative conditional value
```

*   `key`: Only lowercase letters, digits, and underscores allowed.
*   `value`: An arbitrary string, supports variables.
*   `if=`/`if!=`: Conditional evaluation. If the condition evaluates to `"0"` or an empty string, the entry is skipped and the next definition for the same key is evaluated. If no definition matches, the key is omitted from the result.

**Example:**

```nginx
http {
    lua_upstream backend {
        server 127.0.0.1:8080 level=0 weight=5;
        server [::1]:8081 level=1;
        server backup.example.com:9090 level=2 down;
        keepalive_timeout 60s;
    }

    server {
        # This completely overrides the http-level "backend"
        lua_upstream backend {
            server 10.0.0.1:9090;
        }
    }
}
```

# Variables

### `$lua_config_name`

Accesses the value of a specific `lua_config` item by its `name`.

**Example:**

```nginx
lua_config data_source my_data_source;
add_header My-Config-Value $lua_config_data_source;
```

# Lua API

In Lua, `lua_config` items defined in the Nginx configuration can be accessed via the `ngx.lua_config` table.

### `ngx.lua_config.get(key)`

**Syntax:** `value = ngx.lua_config.get(key)`

**Context:** `set_by_lua*`, `rewrite_by_lua*`, `access_by_lua*`, `content_by_lua*`, `header_filter_by_lua*`, `body_filter_by_lua*`, `log_by_lua*`, `balancer_by_lua*`

Retrieves the value of a specific `lua_config` item by its `key`.
* `key`: A string representing the key name of the configuration item to query.
* The value of the corresponding configuration item (string type) if found.
* `nil` if the configuration item is not found.

**Example:**

```lua
local my_data_source = ngx.lua_config.get("data_source")
if my_data_source then
    ngx.log(ngx.INFO, "Data source: ", my_data_source)
else
    ngx.log(ngx.WARN, "Data source not found!")
end
```

### `ngx.lua_config.get_upstream(name)`

**Syntax:** `result = ngx.lua_config.get_upstream(name)`

**Context:** `set_by_lua*`, `rewrite_by_lua*`, `access_by_lua*`, `content_by_lua*`, `header_filter_by_lua*`, `body_filter_by_lua*`, `log_by_lua*`, `balancer_by_lua*`

Retrieves the upstream configuration defined by `lua_upstream` for the given `name`.

*   `name`: A string representing the upstream name to look up.
*   Returns `nil` if the upstream is not found.
*   Returns a table with the following fields:
    *   `name` (string): The upstream name.
    *   `servers` (array): Each entry is a table with:
        *   `host` (string): The server host.
        *   `port` (number): The server port (`0` if not specified).
        *   `level` (number): The server level.
        *   `weight` (number): The server weight.
        *   `down` (boolean): Whether the server is marked down.
    *   Config keys: Each key defined in the block appears as a field. Boolean keys have value `true`; other keys have their resolved string value (with variables evaluated and conditions applied).
    *   `crc32` (string): A CRC32 checksum (decimal string) computed from the upstream name, all server entries, and all config key-value pairs (keys sorted alphabetically). The checksum changes when any resolved value changes, making it useful for detecting configuration drift.

**Example:**

```lua
local lua_config = require "ngx.lua_config"
local up = lua_config.get_upstream("backend")
if not up then
    ngx.say("upstream not found")
    return
end

ngx.say("name: ", up.name)
ngx.say("crc32: ", up.crc32)

for i, srv in ipairs(up.servers) do
    ngx.say("server ", i, ": ", srv.host, ":", srv.port,
            " level=", srv.level, " weight=", srv.weight,
            " down=", tostring(srv.down))
end

if up.keepalive_timeout then
    ngx.say("keepalive_timeout: ", up.keepalive_timeout) -- "60s" (string value)
end
```

# Author
Hanada im@hanada.info

# License
This Nginx module is licensed under BSD 2-Clause License.