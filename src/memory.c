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

#include "memory.h"

#include "exception.h"
#include "mutex.h"

#define OCI_MUTEXED_CALL(exp)               \
                                            \
    if (OCILib.mem_mutex)                   \
    {                                       \
       MutexAcquire(OCILib.mem_mutex);      \
    }                                       \
                                            \
    (exp);                                  \
                                            \
    if (OCILib.mem_mutex)                   \
    {                                       \
       MutexRelease(OCILib.mem_mutex);      \
    }                                       \


/* --------------------------------------------------------------------------------------------- *
 * MemAlloc
 * --------------------------------------------------------------------------------------------- */

void * MemAlloc
(
    int     ptr_type,
    size_t  block_size,
    size_t  block_count,
    boolean zero_fill
)
{
    OCI_MemoryBlock * mem_block = NULL;
    const size_t size = sizeof(OCI_MemoryBlock) + (block_size * block_count);

    mem_block = (OCI_MemoryBlock *)malloc(size);

    if (mem_block)
    {
        if (zero_fill)
        {
            memset(mem_block, 0, size);
        }

        mem_block->type = ptr_type;
        mem_block->size = (unsigned int) size;

        MemUpdateBytes(mem_block->type, mem_block->size);
    }
    else
    {
        ExceptionMemory(ptr_type, size, NULL, NULL);
    }

    return ((unsigned char *) mem_block) + sizeof(*mem_block);
}

/* --------------------------------------------------------------------------------------------- *
 * MemRealloc
 * --------------------------------------------------------------------------------------------- */

void * MemRealloc
(
    void * ptr_mem,
    int    ptr_type,
    size_t block_size,
    size_t block_count,
    boolean zero_fill
)
{
    OCI_MemoryBlock * mem_block = NULL;
    size_t size = 0;

    if (ptr_mem)
    {
        mem_block = (OCI_MemoryBlock *) (((unsigned char*)ptr_mem) - sizeof(*mem_block));
    }

    size = sizeof(OCI_MemoryBlock) + (block_size * block_count);

    if (!mem_block || mem_block->size < size)
    {
        void *ptr_new = realloc(mem_block, size);

        if (ptr_new)
        {
            big_int size_diff = 0;
            mem_block = (OCI_MemoryBlock *) ptr_new;

            size_diff = (big_int) size - mem_block->size;

            if (zero_fill)
            {
                memset(((unsigned char *)mem_block) + mem_block->size, 0, size - mem_block->size);
            }

            mem_block->type = ptr_type;
            mem_block->size = (unsigned int) size;

            MemUpdateBytes(mem_block->type, size_diff);
        }
        else if (ptr_mem)
        { 
            MemFree(ptr_mem);

            ExceptionMemory(ptr_type, size, NULL, NULL);

            mem_block = NULL;
        }
    }

    return mem_block ? ((unsigned char *)mem_block) + sizeof(*mem_block) : NULL;
}

/* --------------------------------------------------------------------------------------------- *
 * MemFree
 * --------------------------------------------------------------------------------------------- */

void MemFree
(
    void * ptr_mem
)
{
    if (ptr_mem)
    {
        OCI_MemoryBlock *mem_block = (OCI_MemoryBlock *)(((unsigned char*)ptr_mem) - sizeof(*mem_block));
       
        if (mem_block)
        {
            MemUpdateBytes(mem_block->type, (big_int) 0 - mem_block->size);

            free(mem_block);
        }
    }
}

/* --------------------------------------------------------------------------------------------- *
* MemUpdateBytes
* --------------------------------------------------------------------------------------------- */

void MemUpdateBytes
(
    int type,
    big_int size
)
{
    if (OCI_IPC_ORACLE == type)
    {
        OCI_MUTEXED_CALL(OCILib.mem_bytes_oci += size)
    }
    else
    {
        OCI_MUTEXED_CALL(OCILib.mem_bytes_lib += size)
    }
}


/* --------------------------------------------------------------------------------------------- *
 * MemHandleAlloc
 * --------------------------------------------------------------------------------------------- */

boolean MemHandleAlloc
(
    CONST dvoid *parenth,
    dvoid      **hndlpp,
    ub4          type
 )
{
    const sword ret = OCIHandleAlloc(parenth, hndlpp, type, 0, NULL);

    if (OCI_SUCCESSFUL(ret))
    {
        OCI_MUTEXED_CALL(OCILib.nb_hndlp++)
    }

    return OCI_SUCCESSFUL(ret);
}

/* --------------------------------------------------------------------------------------------- *
 * MemHandleFree
 * --------------------------------------------------------------------------------------------- */

boolean MemHandleFree
(
    dvoid *hndlp,
    ub4    type
)
{
    sword ret = OCI_SUCCESS;

    if (hndlp)
    {
        OCI_MUTEXED_CALL(OCILib.nb_hndlp--)

        ret = OCIHandleFree(hndlp, type);
    }

    return OCI_SUCCESSFUL(ret);
}

/* --------------------------------------------------------------------------------------------- *
 * MemDescriptorAlloc
 * --------------------------------------------------------------------------------------------- */

boolean MemDescriptorAlloc
(
    CONST dvoid *parenth,
    dvoid      **descpp,
    ub4          type
 )
{
    const sword ret = OCIDescriptorAlloc(parenth, descpp, type, 0, NULL);

    if (OCI_SUCCESSFUL(ret))
    {
        OCI_MUTEXED_CALL(OCILib.nb_descp++)
    }

    return OCI_SUCCESSFUL(ret);
}

/* --------------------------------------------------------------------------------------------- *
 * MemDescriptorArrayAlloc
 * --------------------------------------------------------------------------------------------- */

boolean MemDescriptorArrayAlloc
(
    CONST dvoid *parenth,
    dvoid      **descpp,
    ub4          type,
    ub4          nb_elem
)
{
    sword ret = OCI_SUCCESS;

#if OCI_VERSION_COMPILE >= OCI_11_1

    if (OCILib.version_runtime >= OCI_11_1)
    {
        ret = OCIArrayDescriptorAlloc(parenth, descpp, type, nb_elem, 0, NULL);

    }
    else

#endif

    {
        for (ub4 i = 0; (i < nb_elem) && (OCI_SUCCESS == ret); i++)
        {
            ret = OCIDescriptorAlloc(parenth, &descpp[i], type, 0, NULL);
        }
    }

    if (OCI_SUCCESSFUL(ret))
    {
        OCI_MUTEXED_CALL(OCILib.nb_descp += nb_elem)
    }

    return OCI_SUCCESSFUL(ret);
}

/* --------------------------------------------------------------------------------------------- *
 * MemDescriptorFree
 * --------------------------------------------------------------------------------------------- */

boolean MemDescriptorFree
(
    dvoid *descp,
    ub4   type
)
{
    sword ret = OCI_SUCCESS;

    if (descp)
    {
        OCI_MUTEXED_CALL(OCILib.nb_descp--)

        ret = OCIDescriptorFree(descp, type);
    }

    return OCI_SUCCESSFUL(ret);
}

/* --------------------------------------------------------------------------------------------- *
 * MemDescriptorFree
 * --------------------------------------------------------------------------------------------- */

boolean MemDescriptorArrayFree
(
    dvoid   **descp,
    ub4       type,
    ub4       nb_elem
)
{
    sword ret = OCI_SUCCESS;

    if (descp)
    {

    #if OCI_VERSION_COMPILE >= OCI_11_1

        if (OCILib.version_runtime >= OCI_11_1)
        {
            ret = OCIArrayDescriptorFree(descp, type);

        }
        else

    #endif

        {
            for (ub4 i = 0; (i < nb_elem) && (OCI_SUCCESS == ret); i++)
            {
                ret = OCIDescriptorFree(descp[i], type);
            }
        }

        OCI_MUTEXED_CALL(OCILib.nb_descp -= nb_elem)
    }

    return OCI_SUCCESSFUL(ret);
}

/* --------------------------------------------------------------------------------------------- *
 * MemObjectNew
 * --------------------------------------------------------------------------------------------- */

sword MemObjectNew
(
    OCIEnv          *env,
    OCIError        *err,
    CONST OCISvcCtx *svc,
    OCITypeCode      typecode,
    OCIType         *tdo,
    dvoid           *table,
    OCIDuration      duration,
    boolean          value,
    dvoid          **instance
)
{
    const sword ret = OCIObjectNew(env, err, svc, typecode, tdo, table, duration, value, instance);

    if (OCI_SUCCESSFUL(ret))
    {
        OCI_MUTEXED_CALL(OCILib.nb_objinst++)
    }

    return ret;
}

/* --------------------------------------------------------------------------------------------- *
 * MemObjectFree
 * --------------------------------------------------------------------------------------------- */

sword MemObjectFree
(
    OCIEnv   *env,
    OCIError *err,
    dvoid    *instance,
    ub2       flags
)
{
    sword ret = OCI_SUCCESS;

    if (instance)
    {
        OCI_MUTEXED_CALL(OCILib.nb_objinst--)

        ret = OCIObjectFree(env, err, instance, flags);
    }

    return ret;
}

/* --------------------------------------------------------------------------------------------- *
* MemAllocOracleClient
* --------------------------------------------------------------------------------------------- */

void * MemAllocOracleClient
(
    void *ctxp, 
    size_t size
)
{
    OCI_NOT_USED(ctxp)
        
    return MemAlloc(OCI_IPC_ORACLE, size, 1, FALSE);
}

/* --------------------------------------------------------------------------------------------- *
* MemReallocOracleClient
* --------------------------------------------------------------------------------------------- */

void * MemReallocOracleClient
(
    void *ctxp, 
    void *memptr, 
    size_t newsize
)
{
    OCI_NOT_USED(ctxp)
        
    return MemRealloc(memptr, OCI_IPC_ORACLE, newsize, 1, FALSE);
}

/* --------------------------------------------------------------------------------------------- *
* MemFreeOracleClient
* --------------------------------------------------------------------------------------------- */

void MemFreeOracleClient
(
    void *ctxp,
    void *memptr
)
{
    OCI_NOT_USED(ctxp)

    MemFree(memptr);
}
