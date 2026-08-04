#ifndef RESOURCE_H
#define RESOURCE_H

/* FUN_060048d8 — poll-close-then-free a resource handle. */
void Res_CloseIfOpen(void);

/* FUN_06004eac — retry-loop file read: (1,1,0,0xffffffff,buffer,name).
 * On PC this becomes a normal fopen/fread from the extracted disc files. */
int Load_DirectorPPB(const char *disc_root, void *out_buffer, unsigned int buffer_size);

#endif
