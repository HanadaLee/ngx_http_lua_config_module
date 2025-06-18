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
- [Variables](#variables)
    - [`$lua_config_name`](#lua_config_name)
- [Lua API](#lua-api)
    - [`ngx.lua_config.get(key)`](#ngxlua_configgetkey)
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

**Syntax:** `lua_config key value;`

**Default:** `-`

**Context:** `http`, `server`, `location`

Defines a key-value configuration item.
*   `key`: The key name, only allowed to contain lowercase letters, numbers, and underscores. The same key cannot be defined repeatedly within the same `context`.
*   `value`: The key's value, an arbitrary string. Nginx variables are not parsed, but you can parse them in lua code.

Duplicate keys defined in child blocks will override the definitions from parent blocks.

**Example:**

```nginx
lua_config data_source primary;
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

# Author
Hanada im@hanada.info

# License
This Nginx module is licensed under BSD 2-Clause License.