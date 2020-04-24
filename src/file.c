/*
 * OCILIB - C Driver for Oracle (C Wrapper for Oracle OCI)
 *
 * Website: http://www.ocilib.net
 *
 * Copyright (c) 2007-2020 Vincent ROGIER <vince.rogier@ocilib.net>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "file.h"

#include "array.h"
#include "macro.h"
#include "memory.h"
#include "strings.h"

static const unsigned int SeekModeValues[] = { OCI_SEEK_SET, OCI_SEEK_END, OCI_SEEK_CUR };
static const unsigned int FileTypeValues[] = { OCI_CFILE, OCI_BFILE };

/* --------------------------------------------------------------------------------------------- *
 * FileInit
 * --------------------------------------------------------------------------------------------- */

OCI_File * FileInit
(
    OCI_Connection *con,
    OCI_File       *file,
    OCILobLocator  *handle,
    ub4             type
)
{
    OCI_CALL_DECLARE_CONTEXT(TRUE)
    OCI_CALL_CONTEXT_SET_FROM_CONN(con)

    OCI_ALLOCATE_DATA(OCI_IPC_FILE, file, 1);

    if (OCI_STATUS)
    {
        file->type   = type;
        file->con    = con;
        file->handle = handle;
        file->offset = 1;

        /* reset file info */

        if (file->dir)
        {
            file->dir[0] = 0;
        }

        if (file->name)
        {
            file->name[0] = 0;
        }

        if (!file->handle)
        {
            /* allocate handle for non fetched file (local file object) */

            file->hstate = OCI_OBJECT_ALLOCATED;

            OCI_STATUS = MemDescriptorAlloc((dvoid *)file->con->env, (dvoid **)(void *)&file->handle, (ub4)OCI_DTYPE_LOB);
        }
        else if (OCI_OBJECT_ALLOCATED_ARRAY != file->hstate)
        {
            file->hstate = OCI_OBJECT_FETCHED_CLEAN;
        }
    }

    /* check for failure */

    if (!OCI_STATUS && file)
    {
        FileFree(file);
        file = NULL;
    }

    return file;
}

/* --------------------------------------------------------------------------------------------- *
 * FileGetInfo
 * --------------------------------------------------------------------------------------------- */

boolean FileGetInfo
(
    OCI_File *file
)
{
    OCI_CALL_DECLARE_CONTEXT(TRUE)

    OCI_CHECK(NULL == file, FALSE)

    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    /* directory name */

    OCI_ALLOCATE_DATA(OCI_IPC_STRING, file->dir, OCI_SIZE_DIRECTORY + 1)

    if (OCI_STATUS)
    {
        file->dir[0] = 0;
    }

    /* file name */

    OCI_ALLOCATE_DATA(OCI_IPC_STRING, file->name, OCI_SIZE_FILENAME + 1)
    
    if (OCI_STATUS)
    {
        file->name[0] = 0;
    }

    /* retrieve name */

    if (OCI_STATUS)
    {
        dbtext *dbstr1 = NULL;
        dbtext *dbstr2 = NULL;
        int    dbsize1 = 0;
        int    dbsize2 = 0;
        ub2    usize1  = 0;
        ub2    usize2  = 0;

        dbsize1 = (int) OCI_SIZE_DIRECTORY  * (int) sizeof(otext);
        dbstr1  = StringGetOracleString(file->dir, &dbsize1);

        dbsize2 = (int) OCI_SIZE_FILENAME  * (int) sizeof(otext);
        dbstr2  = StringGetOracleString(file->name, &dbsize1);

        usize1 = (ub2) dbsize1;
        usize2 = (ub2) dbsize2;

        OCI_EXEC
        (
            OCILobFileGetName(file->con->env, file->con->err, file->handle,
                              (OraText *) dbstr1, (ub2*) &usize1,
                              (OraText *) dbstr2, (ub2*) &usize2)
        )

        dbsize1 = (int) usize1;
        dbsize2 = (int) usize2;

        StringCopyOracleStringToNativeString(dbstr1, file->dir,  dbcharcount(dbsize1));
        StringCopyOracleStringToNativeString(dbstr2, file->name, dbcharcount(dbsize2));

        StringReleaseOracleString(dbstr1);
        StringReleaseOracleString(dbstr2);
    }

    return OCI_STATUS;
}

/* --------------------------------------------------------------------------------------------- *
 * FileCreate
 * --------------------------------------------------------------------------------------------- */

OCI_File * FileCreate
(
    OCI_Connection *con,
    unsigned int    type
)
{
    OCI_CALL_ENTER(OCI_File *, NULL)
    OCI_CALL_CHECK_PTR(OCI_IPC_CONNECTION, con)
    OCI_CALL_CHECK_ENUM_VALUE(con, NULL, type, FileTypeValues, OTEXT("File Type"))
    OCI_CALL_CONTEXT_SET_FROM_CONN(con)

    OCI_RETVAL = FileInit(con, NULL, NULL, type);
    OCI_STATUS = (NULL != OCI_RETVAL);

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileFree
 * --------------------------------------------------------------------------------------------- */

boolean FileFree
(
    OCI_File *file
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CHECK_OBJECT_FETCHED(file)
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    OCI_FREE(file->dir)
    OCI_FREE(file->name)

    if (OCI_OBJECT_ALLOCATED == file->hstate)
    {
        MemDescriptorFree((dvoid *) file->handle, (ub4) OCI_DTYPE_LOB);
    }

    if (OCI_OBJECT_ALLOCATED_ARRAY != file->hstate)
    {
        OCI_FREE(file)
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileArrayCreate
 * --------------------------------------------------------------------------------------------- */

OCI_File ** FileArrayCreate
(
    OCI_Connection *con,
    unsigned int    type,
    unsigned int    nbelem
)
{
    OCI_Array *arr = NULL;

    OCI_CALL_ENTER(OCI_File **, NULL)
    OCI_CALL_CHECK_PTR(OCI_IPC_CONNECTION, con)
    OCI_CALL_CHECK_ENUM_VALUE(con, NULL, type, FileTypeValues, OTEXT("File Type"))
    OCI_CALL_CONTEXT_SET_FROM_CONN(con)

    arr = ArrayCreate(con, nbelem, OCI_CDT_FILE, type, sizeof(OCILobLocator *), sizeof(OCI_File), OCI_DTYPE_LOB, NULL);
    OCI_STATUS = (NULL != arr);

    if (arr)
    {
        OCI_RETVAL = (OCI_File **)arr->tab_obj;
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileArrayFree
 * --------------------------------------------------------------------------------------------- */

boolean FileArrayFree
(
    OCI_File **files
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_ARRAY, files)

    OCI_RETVAL = OCI_STATUS = ArrayFreeFromHandles((void **)files);

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileSeek
 * --------------------------------------------------------------------------------------------- */

boolean FileSeek
(
    OCI_File    *file,
    big_uint     offset,
    unsigned int mode
)
{
    big_uint size = 0;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CHECK_ENUM_VALUE(file->con, NULL, mode, SeekModeValues, OTEXT("Seek Mode"))
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    size = FileGetSize(file);

    switch (mode)
    {
        case OCI_SEEK_CUR:
        {
            if ((offset + file->offset - 1) <= size) 
            {
                file->offset += offset;
                OCI_RETVAL   = TRUE;
            }
            break;
        }
        case OCI_SEEK_SET:
        {
            if (offset <= size) 
            {
                file->offset = offset + 1;
                OCI_RETVAL  = TRUE;
            }
            break;
        }
        case OCI_SEEK_END:
        {
            if (offset <= size) 
            {
                file->offset = size - offset + 1;
                OCI_RETVAL  = TRUE;
            }
            break;
        }
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileGetOffset
 * --------------------------------------------------------------------------------------------- */

big_uint FileGetOffset
(
    OCI_File *file
)
{
    OCI_CALL_ENTER(big_uint, 0)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    OCI_RETVAL = file->offset - 1;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileRead
 * --------------------------------------------------------------------------------------------- */

unsigned int FileRead
(
    OCI_File    *file,
    void        *buffer,
    unsigned int len
)
{
    ub4 size_in  = 0;
    ub4 size_out = 0;

    OCI_CALL_ENTER(unsigned int, 0)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CHECK_MIN(file->con, NULL, len, 1)
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    size_out = size_in = len;

    OCI_STATUS = TRUE;

#ifdef OCI_LOB2_API_ENABLED

    if (OCILib.use_lob_ub8)
    {
        ub8 size_char = (ub8) len;
        ub8 size_byte = (ub8) size_in;

        OCI_EXEC
        (
            OCILobRead2(file->con->cxt, file->con->err,
                        file->handle, &size_byte,
                        &size_char, (ub8) file->offset,
                        buffer, (ub8) size_in,
                        (ub1) OCI_ONE_PIECE, (dvoid *) NULL,
                        NULL, (ub2) 0, (ub1) SQLCS_IMPLICIT)
        )

        size_out = (ub4) size_byte;
    }

    else

 #endif

    {
        const ub4 offset = (ub4) file->offset;

        OCI_EXEC
        (
            OCILobRead(file->con->cxt, file->con->err,
                       file->handle,  &size_out, offset,
                       buffer, size_in, (dvoid *) NULL,
                       NULL, (ub2) 0, (ub1) SQLCS_IMPLICIT)
        )
    }

    if (OCI_STATUS)
    {
        file->offset += (big_uint) size_out;
        
        OCI_RETVAL = size_out;
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileGetType
 * --------------------------------------------------------------------------------------------- */

unsigned int FileGetType
(
    OCI_File *file
)
{
    OCI_GET_PROP(unsigned int, OCI_UNKNOWN, OCI_IPC_FILE, file, type, file->con, NULL, file->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * FileGetSize
 * --------------------------------------------------------------------------------------------- */

big_uint FileGetSize
(
    OCI_File *file
)
{
    big_uint size = 0;

    OCI_CALL_ENTER(big_uint, 0)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

#ifdef OCI_LOB2_API_ENABLED

    if (OCILib.use_lob_ub8)
    {
        OCI_EXEC(OCILobGetLength2(file->con->cxt, file->con->err, file->handle, (ub8 *) &size))
    }
    else

#endif

    {
        ub4 size32 = (ub4) size;

        OCI_EXEC(OCILobGetLength(file->con->cxt, file->con->err, file->handle, &size32))

        size = (big_uint) size32;
    }

    OCI_RETVAL = size;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_LobFileExists
 * --------------------------------------------------------------------------------------------- */

boolean FileExists
(
    OCI_File *file
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    OCI_EXEC(OCILobFileExists(file->con->cxt, file->con->err, file->handle, &OCI_RETVAL))

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileSetName
 * --------------------------------------------------------------------------------------------- */

boolean FileSetName
(
    OCI_File    *file,
    const otext *dir,
    const otext *name
)
{
    dbtext *dbstr1  = NULL;
    dbtext *dbstr2  = NULL;
    int     dbsize1 = -1;
    int     dbsize2 = -1;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    dbstr1 = StringGetOracleString(dir,  &dbsize1);
    dbstr2 = StringGetOracleString(name, &dbsize2);

    OCI_EXEC
    (
        OCILobFileSetName(file->con->env, file->con->err,
                          &file->handle,
                          (OraText *) dbstr1, (ub2) dbsize1,
                          (OraText *) dbstr2, (ub2) dbsize2)
    )

    StringReleaseOracleString(dbstr1);
    StringReleaseOracleString(dbstr2);

    if (OCI_STATUS)
    {
        OCI_STATUS = FileGetInfo(file);
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileGetDirectory
 * --------------------------------------------------------------------------------------------- */

const otext * FileGetDirectory
(
    OCI_File *file
)
{
    OCI_CALL_ENTER(const otext *, NULL)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    if (!OCI_STRING_VALID(file->dir))
    {
        OCI_STATUS = FileGetInfo(file);
    }

    OCI_RETVAL = file->dir;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileGetName
 * --------------------------------------------------------------------------------------------- */

const otext * FileGetName
(
    OCI_File *file
)
{
    OCI_CALL_ENTER(const otext *, NULL)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    if (!OCI_STRING_VALID(file->name))
    {
        OCI_STATUS = FileGetInfo(file);
    }

    OCI_RETVAL = file->name;
   
    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileOpen
 * --------------------------------------------------------------------------------------------- */

boolean FileOpen
(
    OCI_File *file
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    OCI_EXEC(OCILobFileOpen(file->con->cxt, file->con->err, file->handle, (ub1) OCI_LOB_READONLY))

    if (OCI_STATUS)
    {
        file->con->nb_files++;
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_LobFileIsOpen
 * --------------------------------------------------------------------------------------------- */

boolean FileIsOpen
(
    OCI_File *file
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    OCI_EXEC(OCILobFileIsOpen(file->con->cxt, file->con->err, file->handle, &OCI_RETVAL))
    
    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileClose
 * --------------------------------------------------------------------------------------------- */

boolean FileClose
(
    OCI_File *file
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    OCI_EXEC(OCILobFileClose(file->con->cxt, file->con->err, file->handle))

    if (OCI_STATUS)
    {
        file->con->nb_files--;
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileIsEqual
 * --------------------------------------------------------------------------------------------- */

boolean FileIsEqual
(
    OCI_File *file,
    OCI_File *file2
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file2)
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    OCI_EXEC(OCILobIsEqual(file->con->env, file->handle, file2->handle, &OCI_RETVAL))

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * FileAssign
 * --------------------------------------------------------------------------------------------- */

boolean FileAssign
(
    OCI_File *file,
    OCI_File *file_src
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file)
    OCI_CALL_CHECK_PTR(OCI_IPC_FILE, file_src)
    OCI_CALL_CONTEXT_SET_FROM_CONN(file->con)

    if ((OCI_OBJECT_ALLOCATED == file->hstate) || (OCI_OBJECT_ALLOCATED_ARRAY == file->hstate))
    {
        OCI_EXEC(OCILobLocatorAssign(file->con->cxt, file->con->err, file_src->handle, &file->handle))
    }
    else
    {
        OCI_EXEC(OCILobAssign(file->con->env, file->con->err, file_src->handle, &file->handle))
    }

    OCI_RETVAL = OCI_STATUS = OCI_STATUS && FileGetInfo(file);

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
* FileGetConnection
* --------------------------------------------------------------------------------------------- */

OCI_Connection * FileGetConnection
(
    OCI_File *file
)
{
    OCI_GET_PROP(OCI_Connection *, NULL, OCI_IPC_FILE, file, con, file->con, NULL, file->con->err)
}
