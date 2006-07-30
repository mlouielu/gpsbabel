/*

    Common GPSBabel file I/O API

    Copyright (C) 2006 Olaf Klein 

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111 USA

 */

#include "defs.h"
#include "gbfile.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define MYNAME "gbfile"


/* GPSBabel 'file' standard calls */

gbfile *
gbfopen(const char *filename, const char *mode, const char *module)
{
	gbfile *file;
	const char *m;
	int len;
		
	file = xcalloc(1, sizeof(*file));
	
	file->name = xstrdup(filename);
	file->module = xstrdup(module);
	file->line = xstrdup("");
	file->mode = 'r'; // default
	file->binary = (strchr(mode, 'b') != NULL);
	
	for (m = mode; *m; m++) {
		switch(tolower(*m)) {
			case 'r':
				file->mode = 'r';
				file->gzapi = 1;
				break;
			case 'w':
				file->mode = 'w';
				break;
		}
	}
	
	/* Do we have a '.gz' extension in the filename ? */
	len = strlen(file->name);
	if ((len > 3) && (case_ignore_strcmp(&file->name[len-3], ".gz") == 0)) {
		/* force gzipped files on output */
		file->gzapi = 1;
	}
	
	if (file->gzapi) {
		file->handle.gz = gzopen(filename, mode);
		if (file->handle.gz == NULL) {
			fatal("%s: Cannot %s file '%s'!\n", 
				module, 
				(file->mode == 'r') ? "open" : "create",
				filename);
		}
		file->gzapi = 1;
	}
	else {
		file->handle.std = xfopen(filename, mode, module);
	}
#ifdef DEBUG_MEM
	file->buffsz = 1;
#else
	file->buffsz = 256;
#endif
	file->buff = xmalloc(file->buffsz);

	return file;
}

/* 
 * gbfopen_be: as gbfopen, but set the BIG-ENDIAN flag 
 */

gbfile *
gbfopen_be(const char *filename, const char *mode, const char *module)
{
	gbfile *result;
	
	result = gbfopen(filename, mode, module);
	result->big_endian = 1;
	
	return result;
}

void
gbfclose(gbfile *file)
{
	if (!file) return;

	if (file->gzapi) {
		gzclose(file->handle.gz);
	}
	else {
		fclose(file->handle.std);
	}
	xfree(file->name);
	xfree(file->module);
	xfree(file->line);
	xfree(file->buff);
	xfree(file);
}

int 
gbfgetc(gbfile *file)
{
	unsigned char c;

	/* errors are caught in gbfread */
	if (gbfread(&c, 1, 1, file) == 0) {
		return EOF;
	}
	else {
		return (unsigned int)c;
	}
}

char * 
gbfgets(char *buf, int len, gbfile *file)
{
	char *result = buf;
	
	while (--len > 0) {
		int c = gbfgetc(file);

		if (c == EOF) break;
		
		*(unsigned char *)buf = (unsigned char)c;
		buf++;
		
		if (c == '\r') {
			c = gbfgetc(file);
			if ((c != '\n') && (c != EOF)) gbfungetc(c, file);
			break;
		}
		else if (c == '\n')
			break;
	}
	*buf = '\0';
	return (*result != '\0') ? result : NULL;
}


gbsize_t
gbfread(void *buf, const gbsize_t size, const gbsize_t members, gbfile *file)
{
	if ((size == 0) || (members == 0)) return 0;
	
	if (file->gzapi) {
		int result = gzread(file->handle.gz, buf, size * members) / size;
		if ((result < 0) || ((gbsize_t)result < members)) {
			int errnum;
			const char *errtxt;
			
			errtxt = gzerror(file->handle.gz, &errnum);
			if ((errnum != Z_STREAM_END) && (errnum != 0))
				fatal("%s: zlib returned error %d ('%s')!\n",
					file->module, errnum, errtxt);
		}
		return (gbsize_t) result;
	}
	else {
		int errno;
		gbsize_t result = fread(buf, size, members, file->handle.std);
		
		if ((result < members) && (errno = ferror(file->handle.std))) {
			fatal("%s: Error %d occured during read of file '%s'!\n",
				file->module, errno, file->name);
		}
		return result;
	}
}

int 
gbfprintf(gbfile *file, const char *format, ...)
{
	int len;
	
	for (;;) {
		va_list args;
		
		va_start(args, format);
		len = vsnprintf(file->buff, file->buffsz, format, args);
		va_end(args);

		if (len < 0)
			fatal(MYNAME ": Unexpected vsnprintf error %d (%s/%s)!\n", 
				len, file->module, file->name);
		else if (len == 0)
			return 0;
		else if (len < file->buffsz) 
			break;

		while (file->buffsz <= len) 
			file->buffsz *= 2;
			
		file->buff = xrealloc(file->buff, file->buffsz);
	}
	return gbfwrite(file->buff, 1, len, file);
}

int 
gbfputc(int c, gbfile *file)
{
	unsigned char temp = (unsigned int) c;
	
	gbfwrite(&temp, 1, 1, file);
	
	return c;
}

int 
gbfputs(const char *s, gbfile *file)
{
	return gbfwrite(s, 1, strlen(s), file);
}

int 
gbfwrite(const void *buf, const gbsize_t size, const gbsize_t members, gbfile *file)
{
	int result;
	
	if ((size == 0) || (members == 0)) return 0;

	if (file->gzapi) {
		result = gzwrite(file->handle.gz, buf, size * members) / size;
	}
	else {
		result = fwrite(buf, size, members, file->handle.std);
	}

	if (result != members) {
		fatal("%s: Could not write %u bytes to %s!\n", 
			file->module,
			(members - result) * size,
			file->name);
	}
		
	return result;
}

int
gbfflush(gbfile *file)
{
	if (file->gzapi) {
		return gzflush(file->handle.gz, Z_SYNC_FLUSH);
	}
	else {
		return fflush(file->handle.std);
	}
}

void
gbfclearerr(gbfile *file)
{
	if (file->gzapi) {
		gzclearerr(file->handle.gz);
	}
	else {
		clearerr(file->handle.std);
	}
}

int
gbferror(gbfile *file)
{
	int errnum;
	
	if (file->gzapi) {
		(void)gzerror(file->handle.gz, &errnum);
	}
	else {
		errnum = ferror(file->handle.std);
	}
	return errnum;
}

void
gbfrewind(gbfile *file)
{
	(void)gbfseek(file, 0, SEEK_SET);
	gbfclearerr(file);
}

int
gbfseek(gbfile *file, gbint32 offset, int whence)
{

	if (file->gzapi) {
		int result;
		
		assert(whence != SEEK_END);
		result = gzseek(file->handle.gz, offset, whence);
		is_fatal(result < 0,
			"%s: online compression not yet supported for this format!", file->module);
		return 0;
		
	}
	else {
		return fseek(file->handle.std, offset, whence);
	}
}

gbsize_t 
gbftell(gbfile *file)
{
	if (file->gzapi) {
		return gztell(file->handle.gz);
	}
	else {
		return ftell(file->handle.std);
	}
}

int 
gbfeof(gbfile *file)
{
	if (file->gzapi) {
		return gzeof(file->handle.gz);
	}
	else {
		return feof(file->handle.std);
	}
}

int
gbfungetc(const int c, gbfile *file)
{
	if (file->gzapi) {
		return gzungetc(c, file->handle.gz);
	}
	else {
		return ungetc(c, file->handle.std);
	}
}

/* GPSBabel 'file' enhancements */

gbint32
gbfgetint32(gbfile *file)
{
	char buf[4];
	
	gbfread(buf, 1, sizeof(buf), file);
	
	if (file->big_endian)
		return be_read32(buf);
	else
		return le_read32(buf);
}

gbint16
gbfgetint16(gbfile *file)
{
	char buf[2];
	
	gbfread(buf, 1, sizeof(buf), file);
	
	if (file->big_endian)
		return be_read16(buf);
	else
		return le_read16(buf);
}

double 
gbfgetdbl(gbfile *file)
{
	char buf[8];
	double result;
	
	gbfread(buf, 1, sizeof(buf), file);
	le_read64(&result, buf);

	return result;
}

float
gbfgetflt(gbfile *file)
{
	union {
		float f;
		gbint32 i;
	} x;
	
	x.i = gbfgetint32(file);
	return x.f;
}

/*
 * gbfgetcstr: Reads a string from file until either a '\0' or eof.
 *             The result is a temporary allocated entity: use it or free it!
 */
char *
gbfgetcstr(gbfile *file)
{
	int len, size;
	char *result;
	
	len = size = 0;
	result = xstrdup("");
	
	while (1) {
		char c = gbfgetc(file);
		
		if ((c == 0) || (c == EOF)) break;
		
		if (len == size) {
			size += 32;
			result = xrealloc(result, size + 1);
		}
		result[len] = c;
		len++;
	}
	
	if ((len + 1) != size)
		result = xrealloc(result, len + 1);
	
	return result;
}

/*
 * gbfgetpstr: Reads a pascal string (first byte is length) from file.
 *             The result is a temporary allocated entity: use it or free it!
 */
char *
gbfgetpstr(gbfile *file)
{
	int len;
	char *result;
	
	len = gbfgetc(file);
	result = xmalloc(len + 1);
	
	if (len > 0)
		gbfread(result, 1, len, file);
	result[len] = '\0';
	
	return result;
}

/*
 * gbfgetstr: Reads a string from file (util any type of line-breaks or eof or error)
 *            except xfree and free you can do all possible things with the result
 */
char *
gbfgetstr(gbfile *file)
{
	int len;
	char *result = file->line;
	
	len = file->linesz = 0;
	
	while (1) {
		char c = gbfgetc(file);
		
		if ((c == EOF) || (c == 0x1A)) {
			if (len == 0) {
				return NULL;
			}
			break;
		}
		else if (c == '\r') {
			c = gbfgetc(file);
			if ((c != '\n') && (c != EOF)) gbfungetc(c, file);
			break;
		}
		else if (c == '\n') {
			break;
		}
		if (len == file->linesz) {
			file->linesz = len + 128;
			result = file->line = xrealloc(file->line, len + 128 + 1);
		}
		result[len] = c;
		len++;
	}
	result[len] = '\0'; // terminate resulting string
	
	return result;
}

int
gbfputint16(const gbint16 i, gbfile *file)
{
	char buf[2];
	
	if (file->big_endian)
		be_write16(buf, i);
	else
		le_write16(buf, i);
	return gbfwrite(buf, 1, sizeof(buf), file);
}

int
gbfputint32(const gbint32 i, gbfile *file)
{
	char buf[4];
	
	if (file->big_endian)
		be_write32(buf, i);
	else
		le_write32(buf, i);
	return gbfwrite(buf, 1, sizeof(buf), file);
}

int
gbfputdbl(const double d, gbfile *file)
{
	char buf[8];
	
	le_read64(buf, (char *)&d);
	return gbfwrite(buf, 1, sizeof(buf), file);
}

int 
gbfputflt(const float f, gbfile *file)
{
	union {
		float f;
		gbint32 i; } x;
		
	x.f = f;
	return gbfputint32(x.i, file);
}

int 
gbfputcstr(const char *s, gbfile *file)
{
	return gbfwrite(s, 1, strlen(s) + 1, file);
}

int
gbfputpstr(const char *s, gbfile *file)
{
	int len;
	
	len = strlen(s);
	if (len > 255)
		len = 255;
	gbfputc(len, file);
	gbfwrite(s, 1, len, file);
	
	return len + 1;
}

/* Thats all, sorry */