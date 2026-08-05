#ifndef RESOURCE_H
#define RESOURCE_H

/* FUN_060048d8 — confirmed: if a resource handle is open (non-null pointer
 * + nonzero type flag) AND its type is 2 or 3 (async CD-streaming reads),
 * retry-call a drain/finish function until it stops returning negative
 * (i.e. block until any in-flight async CD read completes), then call the
 * real close callback and zero the handle + type fields. Intentionally
 * NOT ported as a stateful close: this pc-port's file loading
 * (Load_DirectorPPB / Res_LoadFileByName) is synchronous fopen/fread/fclose
 * within a single call, so there is no persistent open handle or in-flight
 * async read to drain -- the entire mechanism this function exists for
 * doesn't apply here. Kept as a no-op with this reasoning documented
 * rather than faking handle state that nothing else uses. */
void Res_CloseIfOpen(void);

/* FUN_06004eac — retry-loop file read: (1,1,0,0xffffffff,buffer,name).
 * On PC this becomes a normal fopen/fread from the extracted disc files. */
int Load_DirectorPPB(const char *disc_root, void *out_buffer, unsigned int buffer_size);

/* Res_LoadFileByName — cached file loader, matches the resource-cache
 * behavior traced around Res_GetFileSize/Res_CopyFilename12 in A.BIN's
 * file-loader cluster (docs/jump_table_functions.md): repeated loads of
 * the same filename are served from an in-memory cache instead of hitting
 * disk again. Returns bytes copied into out_buffer, or -1 on failure. */
int Res_LoadFileByName(const char *disc_root, const char *filename, void *out_buffer, unsigned int buffer_size);

/* Drops all cached file data (e.g. on a stage transition). */
void Res_ClearCache(void);

#endif
