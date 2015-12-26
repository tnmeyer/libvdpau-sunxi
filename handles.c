/*
 * Copyright (c) 2013-2014 Jens Kuske <jenskuske@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "vdpau_private.h"
#include <stdio.h>

#define INITIAL_SIZE 16

struct dataVault
{
   void*    data;
   uint32_t refCnt;
   enum HandleType type;
};

static struct
{
        struct dataVault *data;
	size_t size;
	pthread_rwlock_t lock;
} ht = { .lock = PTHREAD_RWLOCK_INITIALIZER,
         .size = 0,
         .data = NULL };

void *handle_create(size_t size, VdpHandle *handle, enum HandleType type)
{
   unsigned int index;
   void *data = NULL;
   *handle = VDP_INVALID_HANDLE;

	if (pthread_rwlock_wrlock(&ht.lock))
		return NULL;

	for (index = 0; index < ht.size; index++)
		if (ht.data[index].refCnt == 0)
			break;

	if (index >= ht.size)
	{
		int new_size = ht.size ? ht.size * 2 : INITIAL_SIZE;
                struct dataVault *new_data = realloc(ht.data, new_size * sizeof(struct dataVault));
		if (!new_data)
			goto out;

		memset(new_data + ht.size, 0, (new_size - ht.size) * sizeof(struct dataVault));
		ht.data = new_data;
		ht.size = new_size;
	}

	data = calloc(1, size);
	if (!data)
		goto out;

	ht.data[index].data = data;
        ht.data[index].refCnt = 1;
        ht.data[index].type = type;
	*handle = index + 1;

out:
	pthread_rwlock_unlock(&ht.lock);
	return data;
}

void *handle_get(VdpHandle handle)
{
   unsigned int index = handle - 1;
   void *data = NULL;
      if (handle == VDP_INVALID_HANDLE)
		return NULL;

	if (pthread_rwlock_rdlock(&ht.lock))
		return NULL;

        if (index >= 0 && index < ht.size && ht.data[index].refCnt > 0)
        {
		data = ht.data[index].data;
                if(data)
                    ht.data[index].refCnt++;
        }

	pthread_rwlock_unlock(&ht.lock);
	return data;
}

void handle_destroy(VdpHandle handle)
{
   int index = handle - 1;
   if (pthread_rwlock_wrlock(&ht.lock))
		return;


	if (index >= 0 && index < ht.size)
	{
#if 1	
           if(ht.data[index].refCnt > 0)
           {
              ht.data[index].refCnt--;
              if(ht.data[index].refCnt == 0 && ht.data[index].data)
              {
                 free(ht.data[index].data);
                 ht.data[index].data = NULL;
              }
           }
#endif
	}
        else
        {
           printf("wrong handle %X\n", handle);
        }
	pthread_rwlock_unlock(&ht.lock);
}
void handle_release (VdpHandle handle)
{
   handle_destroy(handle);
}

void handles_print()
{
	int i;
	for(i=0; i < ht.size; ++i)
	{
		printf("handle %d=%p\n", i+1, ht.data[i].data);
	}

}

