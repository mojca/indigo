/****************************************************************************
 * Copyright (C) 2009-2011 GGA Software Services LLC
 * 
 * This file is part of Indigo toolkit.
 * 
 * This file may be distributed and/or modified under the terms of the
 * GNU General Public License version 3 as published by the Free Software
 * Foundation and appearing in the file LICENSE.GPL included in the
 * packaging of this file.
 * 
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 ***************************************************************************/

#if !defined(_WIN32)

#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "base_cpp/shmem.h"

using namespace indigo;

SharedMemory::SharedMemory (const char *name, int size, bool no_map_if_first)
{
   snprintf(_filename, sizeof(_filename), "/tmp/indigo_shm_%s", name);

   _pointer = NULL;
   _was_first = false;

   int key = ftok(_filename, 'D'); // 'D' has no actual meaning

   if (key == -1 && errno == ENOENT)
   {
      // create file
      int fd = creat(_filename, 0666);

      if (fd == -1)
         throw Error("can't create %s: %s", _filename, strerror(errno));

      key = ftok(_filename, 'D');
   }

   if (key == -1)
      throw Error("can't get key: %s", strerror(errno));

   _shm_id = -1;

   if (!no_map_if_first)
   {
      // try to create
      _shm_id = shmget(key, size, IPC_CREAT | IPC_EXCL | 0666);

      if (_shm_id == -1)
      {
         if (errno != EEXIST)
            throw Error("can't shmget: %s", strerror(errno));
      }
      else
         _was_first = true;
   }

   if (_shm_id == -1)
   {
      _shm_id = shmget(key, size, 0666);

      if (_shm_id == -1)
      {
         if (errno == ENOENT)
            return;
         throw Error("can't second shmget (%d): %s", key, strerror(errno));
      }
   }

   _pointer = shmat(_shm_id, NULL, 0);

   if (_pointer == (void *)-1)
      throw Error("can't shmat: %s", strerror(errno));

   strncpy(_id, name, sizeof(_id));  
   
   if (_was_first) 
      memset(_pointer, 0, size); 
}

SharedMemory::~SharedMemory ()
{
   struct shmid_ds ds;

   // detach from the memory segment
   if (shmdt((char *)_pointer) != 0)
      return; // throwing exceptions on destructor is bad

   // get information about the segment
   if (shmctl(_shm_id, IPC_STAT, &ds) != 0)
      return;

   if (ds.shm_nattch < 1)
   {
      // nobody is using the segment; remove it
      shmctl(_shm_id, IPC_RMID, NULL);
      // and remove the temporary file
      unlink(_filename);
   }
}

#endif
