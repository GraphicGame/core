#include <lua.hpp>

#include <ssengine/uri.h>

typedef struct UriReaderCtx {
	input_stream *fp;
	char buf[LUAL_BUFFERSIZE];
} FileReaderCtx;

static const char *reader_uri(lua_State *L, void *ud, size_t *size)
{
	UriReaderCtx *ctx = (UriReaderCtx *)ud;
	*size = ctx->fp->read(ctx->buf, sizeof(ctx->buf));
	return *size > 0 ? ctx->buf : NULL;
}

int luaL_loaduri(lua_State* L, const char* uri){
	return luaL_loadurix(L, uri, NULL);
}

int luaL_loadurix(lua_State* L, const char* uri, const char* mode){
	UriReaderCtx ctx;
	int status;
	const char *chunkname;

	ctx.fp = ss_uri_open_for_read(uri);
	if (ctx.fp == NULL) {
		lua_pushfstring(L, "cannot open %s", uri);
		return LUA_ERRFILE;
	}
	chunkname = lua_pushfstring(L, "@%s", uri);
	
	status = lua_loadx(L, reader_uri, &ctx, chunkname, mode);
	
	lua_remove(L, -2);
	delete ctx.fp;

	return status;
}

static int readable(const char* uri){
	struct input_stream* is = ss_uri_open_for_read(uri);
	if (!is){
		return 0;
	}
	delete is;
	return 1;
}

static void loaderror(lua_State *L, const char *uri)
{
	luaL_error(L, "error loading module " LUA_QS " from file " LUA_QS ":\n\t%s",
		lua_tostring(L, 1), uri, lua_tostring(L, -1));
}

static const char *pushnexttemplate(lua_State *L, const char *path)
{
	const char *l;
	while (*path == *LUA_PATHSEP) path++;  /* skip separators */
	if (*path == '\0') return NULL;  /* no more templates */
	l = strchr(path, *LUA_PATHSEP);  /* find next separator */
	if (l == NULL) l = path + strlen(path);
	lua_pushlstring(L, path, (size_t)(l - path));  /* template */
	return l;
}

static const char *searchpath(lua_State *L, const char *name,
	const char *path, const char *sep,
	const char *dirsep)
{
	luaL_Buffer msg;  /* to build error message */
	luaL_buffinit(L, &msg);
	if (*sep != '\0')  /* non-empty separator? */
		name = luaL_gsub(L, name, sep, dirsep);  /* replace it by 'dirsep' */
	while ((path = pushnexttemplate(L, path)) != NULL) {
		const char *filename = luaL_gsub(L, lua_tostring(L, -1),
			LUA_PATH_MARK, name);
		lua_remove(L, -2);  /* remove path template */
		if (readable(filename))  /* does file exist and is readable? */
			return filename;  /* return that file name */
		lua_pushfstring(L, "\n\tno file " LUA_QS, filename);
		lua_remove(L, -2);  /* remove file name */
		luaL_addvalue(&msg);  /* concatenate error msg. entry */
	}
	luaL_pushresult(&msg);  /* create error message */
	return NULL;  /* not found */
}

static const char *findfile(lua_State *L, const char *name,
	const char *pname)
{
	const char *path;
	lua_getfield(L, lua_upvalueindex(1), pname);
	path = lua_tostring(L, -1);
	if (path == NULL)
		luaL_error(L, LUA_QL("package.%s") " must be a string", pname);
	return searchpath(L, name, path, ".", LUA_DIRSEP);
}

static int package_loader_lua(lua_State *L)
{
	const char *uri;
	const char *name = luaL_checkstring(L, 1);
	uri = findfile(L, name, "path");
	if (uri == NULL) return 1;  /* library not found in this path */
	if (luaL_loaduri(L, uri) != 0)
		loaderror(L, uri);
	return 1;  /* library loaded successfully */
}

static int loaduri(lua_State* L)
{
	const char *fname = lua_tostring(L, 1);
	const char *mode = luaL_optstring(L, 2, NULL);
	int status;
	lua_pushnil(L);
	status = luaL_loadurix(L, fname,
		mode);
	if (status == 0){
		return 1;
	}
	else {
		return 2;
	}
}

static int douri(lua_State* L){
	const char *fname = lua_tostring(L, 1);
	const char *mode = luaL_optstring(L, 2, NULL);
	int status;
	status = luaL_loadurix(L, fname,
		mode);
	if (status != 0){
		lua_error(L);
	}
	int base = lua_gettop(L) - 1;
	lua_call(L, 0, LUA_MULTRET);
	int outtop = lua_gettop(L);
	return lua_gettop(L) - base;
}

extern "C" int ss_module_uri_loader(lua_State *L){
	lua_getglobal(L, "package");
#if LUA_VERSION_NUM > 502
	lua_getfield(L, -1, "searchers");
#else
	lua_getfield(L, -1, "loaders");
#endif
	lua_pushvalue(L, -2);
	lua_pushcclosure(L, package_loader_lua, 1);
	lua_rawseti(L, -2, 2);
	lua_pop(L, 2);

	lua_pushcfunction(L, loaduri);
	lua_setglobal(L, "loaduri");

	lua_pushcfunction(L, douri);
	lua_setglobal(L, "douri");

	return 0;
}