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

#include <lua.h>
#include <lauxlib.h>

#include "ikcp.h"

#define check_kcp(L, idx)\
	*(ikcpcb**)luaL_checkudata(L, idx, "lkcp_methods")

typedef struct _UserInfo {
    lua_State* lp;
    int32_t id;
} UserInfo;

static int kcp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
    if (user == NULL){
        return 0;
    }
    UserInfo* uuser = (UserInfo*)user;
    lua_State* L = uuser->lp;
    int32_t id = uuser->id;

	lua_getfield(L, LUA_REGISTRYINDEX, "kcp_lua_output_callback");
    lua_pushinteger(L, id);
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
        free(kcp->user);
        kcp->user = NULL;
    }
    ikcp_release(kcp);
    kcp = NULL;
    return 0;
}

static int lkcp_init(lua_State* L) {
	lua_setfield(L, LUA_REGISTRYINDEX, "kcp_lua_output_callback");
    return 0;
}

static int lkcp_create(lua_State* L){
    int32_t conv = luaL_checkinteger(L, 1);
    int32_t id = luaL_checkinteger(L, 2);

    UserInfo* user = malloc(sizeof(UserInfo));
    user->lp = L;
    user->id = id;

    ikcpcb* kcp = ikcp_create(conv, (void*)user);
    if (kcp == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "error: fail to create kcp");
        return 2;
    }

    kcp->output = kcp_output;

    *(ikcpcb**)lua_newuserdata(L, sizeof(void*)) = kcp;
    luaL_getmetatable(L, "lkcp_methods");
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
    char buf[2000];
    buf[0] = '\0';
    int32_t hr = ikcp_recv(kcp, buf, 2000);
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

    luaL_newmetatable(L, "lkcp_methods");

    lua_newtable(L);
    luaL_setfuncs(L, lkcp_methods, 0);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, kcp_gc);
    lua_setfield(L, -2, "__gc");

    luaL_newlib(L, l_methods);

    return 1;
}

