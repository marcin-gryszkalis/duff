/*
 * duff - Duplicate file finder
 * Copyright (c) 2004 Camilla Berglund <elmindreda@users.sourceforge.net>
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any
 * damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 *  1. The origin of this software must not be misrepresented; you
 *     must not claim that you wrote the original software. If you use
 *     this software in a product, an acknowledgment in the product
 *     documentation would be appreciated but is not required.
 *
 *  2. Altered source versions must be plainly marked as such, and
 *     must not be misrepresented as being the original software.
 *
 *  3. This notice may not be removed or altered from any source
 *     distribution.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#if HAVE_ERRNO_H
#include <errno.h>
#endif

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_STDIO_H
#include <stdio.h>
#endif

#if HAVE_STRING_H
#include <string.h>
#endif

#include "sha1.h"
#include "duff.h"

/* These flags are defined and documented in duff.c.
 */
extern int quiet_flag;
extern int thorough_flag;

/* Allocates and initialises an entry.
 */
struct Entry* make_entry(const char* path, off_t size)
{
  struct Entry* entry;

  entry = (struct Entry*) malloc(sizeof(struct Entry));
  entry->next = NULL;
  entry->path = strdup(path);
  entry->size = size;
  entry->status = UNTOUCHED;
  memset(entry->checksum, 0, SHA1_HASH_SIZE);
  
  return entry;
}

struct Entry* copy_entry(struct Entry* entry)
{
  struct Entry* copy = (struct Entry*) malloc(sizeof(struct Entry));
  
  copy->next = NULL;
  copy->path = strdup(entry->path);
  copy->size = entry->size;
  copy->status = entry->status;
  memcpy(copy->checksum, entry->checksum, SHA1_HASH_SIZE);
  
  return copy;
}

/* Frees an antry and any dynamically allocated members.
 */
void free_entry(struct Entry* entry)
{
  free(entry->path);
  free(entry);
}

/* Frees a list of entries.
 * On exit, the specified head is set to NULL.
 */
void free_entry_list(struct Entry** entries)
{
  struct Entry* entry;
  
  while (*entries)
  {
    entry = *entries;
    *entries = entry->next;
    free_entry(entry);
  }
}

/* Calculates the checksum of a file, if needed.
 * The status field is used to avoid doing this more than once per file
 * per execution of duff.
 */
int get_entry_checksum(struct Entry* entry)
{
  FILE* file;
  size_t size;
  char buffer[8192];
  SHA1Context context;
  
  if (entry->status == INVALID)
    return -1;
  if (entry->status != UNTOUCHED)
    return 0;
  
  file = fopen(entry->path, "rb");
  if (!file)
  {
    if (!quiet_flag)
      warning("%s: %s", entry->path, strerror(errno));
    
    entry->status = INVALID;
    return -1;
  }

  SHA1Init(&context);

  for (;;)
  {
    size = fread(buffer, 1, sizeof(buffer), file);

    if (ferror(file))
    {
      fclose(file);
  
      if (!quiet_flag)
        warning("%s: %s", entry->path, strerror(errno));

      entry->status = INVALID;
      return -1;
    }

    if (size == 0)
      break;

    SHA1Update(&context, buffer, size);
  }

  fclose(file);
  
  SHA1Final(&context, entry->checksum);
  entry->status = CHECKSUMMED;
  return 0;
}

/* This function defines the high-level comparison algorithm, using
 * lower level primitives.  This is the place to change or add
 * calls to comparison modes.  The general idea is to find proof of
 * un-equality as soon and as quickly as possible.
 */
int compare_entries(struct Entry* first, struct Entry* second)
{
  if (first->size != second->size)
    return -1;
    
  if (compare_entry_checksums(first, second) != 0)
    return -1;

  if (thorough_flag)
  {
    if (compare_entry_contents(first, second))
      return -1;
  }

  return 0;
}

/* Compares the checksum of two files, generating them if neccessary.
 */
int compare_entry_checksums(struct Entry* first, struct Entry* second)
{
  int i;

  if (get_entry_checksum(first) != 0)
    return -1;

  if (get_entry_checksum(second) != 0)
    return -1;

  for (i = 0;  i < SHA1_HASH_SIZE;  i++)
    if (first->checksum[i] != second->checksum[i])
      return -1;

  return 0;
}

/* Performs byte-by-byte comparison of the contents of two files.
 * This is the action we most want to avoid ever having to do.
 * It is also completely un-optmimised.  Enjoy.
 * NOTE: This function assumes that the files are of equal size, as
 * there's little point in calling it otherwise.
 */
int compare_entry_contents(struct Entry* first, struct Entry* second)
{
  int fc, sc, result = 0;
  FILE* first_stream = fopen(first->path, "rb");
  FILE* second_stream = fopen(second->path, "rb");

  if (!first_stream || !second_stream)
  {
    if (first_stream)
      fclose(first_stream);
    if (second_stream)
      fclose(second_stream);
    return -1;
  }

  do
  {
    fc = fgetc(first_stream);
    sc = fgetc(second_stream);
    if (fc != sc)
    {
      result = -1;
      break;
    }
  }
  while (fc != EOF);

  fclose(first_stream);
  fclose(second_stream);
  return result;
}

