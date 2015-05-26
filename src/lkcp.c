/**
 *
 * Copyright (C) 2015 by David Lin
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALING IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include "ikcp.h"

#define RECV_BUFFER_LEN 2000

#define check_kcp(L, idx)\
	*(ikcpcb**)luaL_checkudata(L, idx, "kcp_meta")

#define check_buf(L, idx)\
	(char*)luaL_checkudata(L, idx, "recv_buffer")

union value {
	char* str;
	int32_t i;
};

typedef struct _UserValue {
    uint8_t size;
    union value* v;
    struct _UserValue* next;
} UserValue;

typedef struct _UserInfo {
    lua_State* lp;
    UserValue* lst_head;
    UserValue* lst_tail;
} UserInfo;

static UserInfo* userinfo_create(lua_State* L) {
    UserInfo* u = malloc(sizeof(UserInfo));
    memset(u, 0, sizeof(UserInfo));
    u -> lp = L;
    u -> lst_head = u -> lst_tail = NULL;
    return u;
}

static int userinfo_release(UserInfo* u) {
    if (u == NULL)
        return 0;
    u -> lp = NULL;
    if (u -> lst_head == NULL)
        return 0;
    UserValue* p = u -> lst_head;
    while (p) {
        if (p -> size > 0){
            if (p -> v -> str){
                free(p -> v -> str);
                p -> v -> str = NULL;
            }
        }
        free(p -> v);
        p -> v = NULL;

        UserValue* p2 = p -> next;
        free(p);
        p = p2;
    }
    u -> lst_head = NULL;
    u -> lst_tail = NULL;
    return 0;
}

static int userinfo_add_i(UserInfo* u, int32_t i){
    UserValue* uv = malloc(sizeof(UserValue));
    memset(uv, 0, sizeof(UserValue));
    uv -> size = 0;

    uv -> v = malloc(sizeof(union value));
    memset(uv -> v, 0, sizeof(union value));
    uv -> v -> i = i;
    uv -> next = NULL;
    if (u -> lst_head == NULL || u -> lst_tail == NULL) {
        u -> lst_head = u -> lst_tail = uv;
        return 0;
    }
    u -> lst_tail -> next = uv;
    u -> lst_tail = uv;
    return 0;
}

static int userinfo_add_str(UserInfo* u, const char* s, uint8_t size){
    UserValue* uv = malloc(sizeof(UserValue));
    memset(uv, 0, sizeof(UserValue));
    uv -> size = size;

    uv -> v = malloc(sizeof(union value));
    memset(uv -> v, 0, sizeof(union value));
    uv -> v -> str = malloc(sizeof(char) * size);
    memcpy(uv -> v -> str, s, size);
    uv -> next = NULL;
    if (u -> lst_head == NULL || u -> lst_tail == NULL) {
        u -> lst_head = u -> lst_tail = uv;
        return 0;
    }
    u -> lst_tail -> next = uv;
    u -> lst_tail = uv;
    return 0;
}

static int kcp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
    if (user == NULL){
        return 0;
    }
    UserInfo* uuser = (UserInfo*)user;
    lua_State* L = uuser->lp;

	lua_getfield(L, LUA_REGISTRYINDEX, "kcp_lua_output_callback");
    //return lua table
    lua_newtable(L);
    int32_t top = lua_gettop(L);
    int32_t key = 1;

    UserValue* p = uuser -> lst_head;
    while (p) {
        lua_pushinteger(L, key);
        if (p -> size > 0){
            lua_pushlstring(L, p -> v -> str, p -> size);
        } else {
            lua_pushinteger(L, p -> v -> i);
        }
        lua_settable(L, top);
        p = p -> next;
        key += 1;
    }
	lua_pushlstring(L, buf, len);
    lua_call(L, 2, 0);

	return 0;
}

static int kcp_gc(lua_State* L) {
	ikcpcb* kcp = check_kcp(L, 1);
	if (kcp == NULL) {
        return 0;
	}
    if (kcp->user != NULL) {
        UserInfo* u = kcp -> user;
        userinfo_release(u);
        kcp->user = NULL;
    }
    ikcp_release(kcp);
    kcp = NULL;
    return 0;
}

static int recv_buffer_gc(lua_State* L) {
	char* buf = check_buf(L, 1);
	if (buf == NULL) {
        return 0;
	}
    buf = NULL;
    return 0;
}

static int lkcp_init(lua_State* L) {
	lua_setfield(L, LUA_REGISTRYINDEX, "kcp_lua_output_callback");
    return 0;
}

static int lkcp_create(lua_State* L){
    int32_t conv = luaL_checkinteger(L, 1);

    UserInfo* user = userinfo_create(L);
    if (user == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "error: fail to create userinfo");
        return 2;
    }
    //loop the table
    lua_pushnil(L);
    while (lua_next(L, 2)) {
        int32_t valtype = lua_type(L, -1);
        if (valtype == LUA_TNUMBER) {
            int32_t v = luaL_checkinteger(L, -1);
            userinfo_add_i(user, v);
        } else {
            size_t size;
            const char *s = luaL_checklstring(L, -1, &size);
            userinfo_add_str(user, s, size);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    ikcpcb* kcp = ikcp_create(conv, (void*)user);
    if (kcp == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "error: fail to create kcp");
        return 2;
    }

    kcp->output = kcp_output;

    *(ikcpcb**)lua_newuserdata(L, sizeof(void*)) = kcp;
    luaL_getmetatable(L, "kcp_meta");
    lua_setmetatable(L, -2);
    return 1;
}

static int lkcp_recv(lua_State* L){
	ikcpcb* kcp = check_kcp(L, 1);
	if (kcp == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "error: kcp not args");
        return 2;
	}
    lua_getfield(L, LUA_REGISTRYINDEX, "kcp_lua_recv_buffer");
    char* buf = check_buf(L, -1);
    lua_pop(L, 1);

    int32_t hr = ikcp_recv(kcp, buf, RECV_BUFFER_LEN);
    if (hr <= 0) {
        lua_pushinteger(L, hr);
        return 1;
    }

    lua_pushinteger(L, hr);
	lua_pushlstring(L, (const char *)buf, hr);

    return 2;
}

static int lkcp_send(lua_State* L){
	ikcpcb* kcp = check_kcp(L, 1);
	if (kcp == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "error: kcp not args");
        return 2;
	}
	size_t size;
	const char *data = luaL_checklstring(L, 2, &size);
    int32_t hr = ikcp_send(kcp, data, size);
    
    lua_pushinteger(L, hr);
    return 1;
}

static int lkcp_update(lua_State* L){
	ikcpcb* kcp = check_kcp(L, 1);
	if (kcp == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "error: kcp not args");
        return 2;
	}
    int32_t current = luaL_checkinteger(L, 2);
    ikcp_update(kcp, current);
    return 0;
}

static int lkcp_check(lua_State* L){
	ikcpcb* kcp = check_kcp(L, 1);
	if (kcp == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "error: kcp not args");
        return 2;
	}
    int32_t current = luaL_checkinteger(L, 2);
    int32_t hr = ikcp_check(kcp, current);
    lua_pushinteger(L, hr);
    return 1;
}

static int lkcp_input(lua_State* L){
	ikcpcb* kcp = check_kcp(L, 1);
	if (kcp == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "error: kcp not args");
        return 2;
	}
	size_t size;
	const char *data = luaL_checklstring(L, 2, &size);
    int32_t hr = ikcp_input(kcp, data, size);
    
    lua_pushinteger(L, hr);
    return 1;
}

static int lkcp_flush(lua_State* L){
	ikcpcb* kcp = check_kcp(L, 1);
	if (kcp == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "error: kcp not args");
        return 2;
	}
    ikcp_flush(kcp);
    return 0;
}

static int lkcp_wndsize(lua_State* L){
	ikcpcb* kcp = check_kcp(L, 1);
	if (kcp == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "error: kcp not args");
        return 2;
	}
    int32_t sndwnd = luaL_checkinteger(L, 2);
    int32_t rcvwnd = luaL_checkinteger(L, 3);
    ikcp_wndsize(kcp, sndwnd, rcvwnd);
    return 0;
}

static int lkcp_nodelay(lua_State* L){
	ikcpcb* kcp = check_kcp(L, 1);
	if (kcp == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "error: kcp not args");
        return 2;
	}
    int32_t nodelay = luaL_checkinteger(L, 2);
    int32_t interval = luaL_checkinteger(L, 3);
    int32_t resend = luaL_checkinteger(L, 4);
    int32_t nc = luaL_checkinteger(L, 5);
    int32_t hr = ikcp_nodelay(kcp, nodelay, interval, resend, nc);
    lua_pushinteger(L, hr);
    return 1;
}


static const struct luaL_Reg lkcp_methods [] = {
    { "lkcp_recv" , lkcp_recv },
    { "lkcp_send" , lkcp_send },
    { "lkcp_update" , lkcp_update },
    { "lkcp_check" , lkcp_check },
    { "lkcp_input" , lkcp_input },
    { "lkcp_flush" , lkcp_flush },
    { "lkcp_wndsize" , lkcp_wndsize },
    { "lkcp_nodelay" , lkcp_nodelay },
	{NULL, NULL},
};

static const struct luaL_Reg l_methods[] = {
    { "lkcp_create" , lkcp_create },
    { "lkcp_init" , lkcp_init },
    {NULL, NULL},
};

int luaopen_lkcp(lua_State* L) {
    luaL_checkversion(L);

    luaL_newmetatable(L, "kcp_meta");

    lua_newtable(L);
    luaL_setfuncs(L, lkcp_methods, 0);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, kcp_gc);
    lua_setfield(L, -2, "__gc");

    luaL_newmetatable(L, "recv_buffer");

    lua_pushcfunction(L, recv_buffer_gc);
    lua_setfield(L, -2, "__gc");

    char* global_recv_buffer = lua_newuserdata(L, sizeof(char)*RECV_BUFFER_LEN);
    memset(global_recv_buffer, 0, sizeof(char)*RECV_BUFFER_LEN);
    luaL_getmetatable(L, "recv_buffer");
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, "kcp_lua_recv_buffer");

    luaL_newlib(L, l_methods);

    return 1;
}

