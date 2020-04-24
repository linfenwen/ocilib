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

#include "object.h"

#include "column.h"
#include "helpers.h"
#include "array.h"
#include "list.h"
#include "macro.h"
#include "memory.h"
#include "strings.h"

#include "collection.h"
#include "date.h"
#include "file.h"
#include "interval.h"
#include "lob.h"
#include "number.h"
#include "ref.h"
#include "timestamp.h"
#include "typeinfo.h"

#define OCI_OBJECT_SET_VALUE(datatype, type, func)                          \
                                                                            \
    OCI_CALL_ENTER(boolean, FALSE)                                          \
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)                                 \
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)                                \
                                                                            \
    OCI_STATUS = FALSE;                                                     \
                                                                            \
    if (!value)                                                             \
    {                                                                       \
        OCI_STATUS = ObjectSetNull(obj, attr);                              \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        int index = ObjectGetAttrIndex(obj, attr, datatype, TRUE);          \
                                                                            \
        if (index >= 0)                                                     \
        {                                                                   \
            OCIInd  *ind  = NULL;                                           \
            type    *data = (type *)  ObjectGetAttr(obj, index, &ind);      \
                                                                            \
            OCI_STATUS = TRUE;                                              \
                                                                            \
            OCI_EXEC(func)                                                  \
                                                                            \
            if (OCI_STATUS)                                                 \
            {                                                               \
                *ind = OCI_IND_NOTNULL;                                     \
            }                                                               \
        }                                                                   \
    }                                                                       \
                                                                            \
    OCI_RETVAL = OCI_STATUS;                                                \
    OCI_CALL_EXIT()

#define OCI_OBJECT_GET_VALUE(datatype, object_type, type, func)             \
                                                                            \
    int index = 0;                                                          \
                                                                            \
    OCI_CALL_ENTER(object_type, NULL)                                       \
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)                                 \
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)                                \
                                                                            \
    OCI_STATUS = FALSE;                                                     \
                                                                            \
    index = ObjectGetAttrIndex(obj, attr, datatype, TRUE);                  \
    if (index >= 0)                                                         \
    {                                                                       \
        OCIInd *ind   = NULL;                                               \
        type   *value = NULL;                                               \
                                                                            \
        OCI_STATUS = TRUE;                                                  \
                                                                            \
        value = (type *) ObjectGetAttr(obj, index, &ind);                   \
                                                                            \
        if (value && ind && (OCI_IND_NULL != *ind))                         \
        {                                                                   \
            OCI_RETVAL = obj->objs[index] = func;                           \
            OCI_STATUS = (NULL != OCI_RETVAL);                              \
        }                                                                   \
    }                                                                       \
                                                                            \
    OCI_CALL_EXIT()


 /* --------------------------------------------------------------------------------------------- *
 * ObjectGetRealTypeInfo
 * --------------------------------------------------------------------------------------------- */

OCI_TypeInfo * ObjectGetRealTypeInfo(OCI_TypeInfo *typinf, void *object)
{
    OCI_CALL_DECLARE_CONTEXT(TRUE)

    OCI_TypeInfo *result = typinf;

    if (!result)
    {
        return result;
    }

    OCI_CALL_CONTEXT_SET_FROM_CONN(result->con)

    /* if the type is related to UTDs and is virtual (e.g. non final), we must find the real type of the instance */

    if (object && result->type == OCI_TIF_TYPE && !result->is_final)
    {
        OCIRef  *ref = NULL;
        OCIType *tdo = NULL;

        /* create a local REF to store a REF to the object real type */

        OCI_EXEC(MemObjectNew(result->con->env, result->con->err, result->con->cxt, SQLT_REF, (OCIType *)0, NULL, OCI_DURATION_SESSION, 0, (void**)&ref))
        OCI_EXEC(OCIObjectGetTypeRef(result->con->env, result->con->err, (dvoid*)object, ref))
        OCI_EXEC(OCITypeByRef(result->con->env, result->con->err, ref, OCI_DURATION_SESSION, OCI_TYPEGET_HEADER, &tdo))

        /* the object instance type pointer is different only if the instance is from an inherited type */

        if (tdo && tdo != result->tdo)
        {
            /* first try to find it in list */
            const boolean found = ListExists(typinf->con->tinfs, tdo);

            if (!found)
            {
                OCIDescribe *descr = NULL;
                OCIParam    *param = NULL;
                otext *schema_name = NULL;
                otext *object_name = NULL;

                unsigned int size_schema = 0;
                unsigned int size_object = 0;

                otext fullname[(OCI_SIZE_OBJ_NAME * 2) + 2] = OTEXT("");

                OCI_STATUS = MemHandleAlloc(result->con->env, (void**) &descr, OCI_HTYPE_DESCRIBE);

                OCI_EXEC(OCIDescribeAny(result->con->cxt, result->con->err, (dvoid *)tdo, 0, OCI_OTYPE_PTR, OCI_DEFAULT, OCI_PTYPE_UNK, descr))
                OCI_GET_ATTRIB(OCI_HTYPE_DESCRIBE, OCI_ATTR_PARAM, descr, &param, NULL)

                OCI_STATUS = OCI_STATUS && StringGetAttribute(result->con, param, OCI_DTYPE_PARAM, OCI_ATTR_SCHEMA_NAME, &schema_name, &size_schema);
                OCI_STATUS = OCI_STATUS && StringGetAttribute(result->con, param, OCI_DTYPE_PARAM, OCI_ATTR_NAME, &object_name, &size_object);

                if (OCI_STATUS)
                {
                    /* compute link full name */

                    StringGetFullTypeName(schema_name, NULL, object_name, NULL, fullname, (sizeof(fullname) / sizeof(otext)) - 1);

                    /* retrieve the type info of the real object */

                    result = TypeInfoGet(result->con, fullname, OCI_TIF_TYPE);
                }

                MemHandleFree(descr, OCI_HTYPE_DESCRIBE);
            }
        }

        /* free local REF */

        MemObjectFree(result->con->env, result->con->err, ref, OCI_DEFAULT);
    }

    return result;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetIndicatorOffset
 * --------------------------------------------------------------------------------------------- */

ub2 ObjectGetIndOffset
(
    OCI_TypeInfo *typinf,
    int           index
)
{
    ub2 i = 0, j = 1;

    OCI_CHECK(typinf == NULL, 0);

    for (i = 0; i < (int) index; i++)
    {
        if (OCI_CDT_OBJECT == typinf->cols[i].datatype)
        {
            j += ObjectGetIndOffset(typinf->cols[i].typinf, typinf->cols[i].typinf->nb_cols);
        }
        else
        {
            j++;
        }

    }

    return j;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetStructSize
 * --------------------------------------------------------------------------------------------- */

void ObjectGetStructSize
(
    OCI_TypeInfo *typinf,
    size_t       *p_size,
    size_t       *p_align
)
{
    if (!typinf || !p_size || !p_align)
    {
        return;
    }

    if (typinf->struct_size == 0)
    {
        size_t size  = 0;
        size_t size1 = 0;
        size_t size2 = 0;
        size_t align = 0;

        ub2 i = 0;

        /* if the type is a sub type, then it is a subset containing all of his parent members
           In that case the first member is a buffer holding a parent object structure */

        if (typinf->parent_type)
        {
            /* if super type information has not been already cached, then let's compute it now */
            
            ObjectGetStructSize(typinf->parent_type, &size, &align);

            /* copy super type members offsets to the current sub type of members offsets */

            for (; i < typinf->parent_type->nb_cols; i++)
            {
                typinf->offsets[i] = typinf->parent_type->offsets[i];
            }

            /* adjust current member index to start to compute with the first of the derived type */
            
            i = typinf->parent_type->nb_cols;

            /* compute the first derived member in order to not touch to the next for loop code that is working :) */
            
            if (i < typinf->nb_cols)
            {
                size_t next_align = 0;

                /* set current alignment to the parent one as it is the first member of the current structure */

                typinf->align = align;

                /* get current type self first member information (after parent type) */

                ObjectGetAttrInfo(typinf, i, &size2, &next_align);

                /* make sure that parent field is aligned */

                size = ROUNDUP(size, next_align);
            }
        }

        for (; i < typinf->nb_cols; i++)
        {
            if (i > 0)
            {
                size1 = size2;

                typinf->offsets[i] = (int) size;
            }
            else
            {
                ObjectGetAttrInfo(typinf, i, &size1, &align);

                typinf->offsets[i] = 0;
            }

            ObjectGetAttrInfo(typinf, i + 1, &size2, &align);

            size += size1;

            size = ROUNDUP(size, align);
        }

        typinf->struct_size = ROUNDUP(size + size2, typinf->align);
    }

    *p_size  = typinf->struct_size;
    *p_align = typinf->align;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetUserStructSize
 * --------------------------------------------------------------------------------------------- */

void ObjectGetUserStructSize
(
    OCI_TypeInfo* typinf,
    size_t* p_size,
    size_t* p_align
)
{
    size_t size1  = 0;
    size_t size2  = 0;
    size_t align1 = 0;
    size_t align2 = 0;
    size_t align  = 0;

    size_t size = 0;

    if (!typinf || !p_size || !p_align)
    {
        return;
    }

    for (ub2 i = 0; i < typinf->nb_cols; i++)
    {
        ColumnGetAttrInfo(&typinf->cols[i],   typinf->nb_cols, i, &size1, &align1);
        ColumnGetAttrInfo(&typinf->cols[i+1], typinf->nb_cols, i+1, &size2, &align2);

        if (align < align1)
        {
            align = align1;
        }

        if (align < align2)
        {
            align = align2;
        }

        size += size1;

        size = ROUNDUP(size, align2);
    }

    *p_size  = size;
    *p_align = align;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetAttrInfo
 * --------------------------------------------------------------------------------------------- */

boolean ObjectGetAttrInfo
(
    OCI_TypeInfo *typinf,
    int           index,
    size_t       *p_size,
    size_t       *p_align
)
{
    OCI_CHECK(typinf  == NULL, 0);
    OCI_CHECK(p_size  == NULL, 0);
    OCI_CHECK(p_align == NULL, 0);


    if (index >= typinf->nb_cols)
    {
        *p_size = 0;

        return FALSE;
    }

    switch (typinf->cols[index].datatype)
    {
        case OCI_CDT_NUMERIC:
        {
            const ub4 subtype = typinf->cols[index].subtype;

            if (subtype & OCI_NUM_SHORT)
            {
                *p_size  = sizeof(short);
                *p_align = *p_size;
            }
            else if (subtype & OCI_NUM_INT)
            {
                *p_size  = sizeof(int);
                *p_align = *p_size;
            }
            else if (subtype & OCI_NUM_FLOAT)
            {
                *p_size  = sizeof(double);
                *p_align = *p_size;
            }
            else if (subtype & OCI_NUM_DOUBLE)
            {
                *p_size  = sizeof(double);
                *p_align = *p_size;
            }
            else
            {
                *p_size  = sizeof(OCINumber);
                *p_align = sizeof(ub1);
            }
            break;
        }
        case OCI_CDT_DATETIME:
        {
            *p_size  = sizeof(OCIDate);
            *p_align = sizeof(sb2);
            break;
        }
        case OCI_CDT_BOOLEAN:
        {
            *p_size  = sizeof(boolean);
            *p_align = *p_size;
            break;
        }
        case OCI_CDT_OBJECT:
        {
            ObjectGetStructSize(typinf->cols[index].typinf, p_size, p_align);
            break;
        }
        default:
        {
            *p_size  = sizeof(void *);
            *p_align = *p_size;
            break;
        }
    }

    if (*p_align > typinf->align)
    {
        typinf->align = *p_align;
    }


    return TRUE;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectReset
 * --------------------------------------------------------------------------------------------- */

void ObjectReset
(
    OCI_Object *obj
)
{
    if (!obj)
    {
        return;
    }
   
    for (ub2 i = 0; i < obj->typinf->nb_cols; i++)
    {
        if (obj->objs[i])
        {
            OCI_Datatype * data = (OCI_Datatype *) obj->objs[i];

            if (OCI_OBJECT_FETCHED_CLEAN == data->hstate)
            {
                data->hstate =  OCI_OBJECT_FETCHED_DIRTY;
            }

            FreeObjectFromType(obj->objs[i], obj->typinf->cols[i].datatype);
            
            obj->objs[i] = NULL;
        }

        OCI_FREE(obj->tmpbufs[i])

        obj->tmpsizes[i] = 0;
    }
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectInit
 * --------------------------------------------------------------------------------------------- */

OCI_Object * ObjectInit
(
    OCI_Connection *con,
    OCI_Object     *obj,
    void           *handle,
    OCI_TypeInfo   *typinf,
    OCI_Object     *parent,
    int             index,
    boolean         reset
)
{
    OCI_TypeInfo *real_typinf = NULL;
    
    OCI_CALL_DECLARE_CONTEXT(TRUE)

    OCI_CALL_CONTEXT_SET_FROM_CONN(con)

    real_typinf = ObjectGetRealTypeInfo(typinf, handle);
    OCI_STATUS = (NULL != real_typinf);

    OCI_ALLOCATE_DATA(OCI_IPC_OBJECT, obj, 1);

    if (OCI_STATUS)
    {      
        obj->con    = con;
        obj->handle = handle;
        obj->typinf = real_typinf;

        if (real_typinf != typinf)
        {
            OCI_FREE(obj->objs)
            OCI_FREE(obj->tmpbufs)
            OCI_FREE(obj->tmpsizes)
        }

        OCI_ALLOCATE_DATA(OCI_IPC_BUFF_ARRAY, obj->tmpbufs, obj->typinf->nb_cols)
        OCI_ALLOCATE_DATA(OCI_IPC_BUFF_ARRAY, obj->tmpsizes, obj->typinf->nb_cols)
        OCI_ALLOCATE_DATA(OCI_IPC_BUFF_ARRAY, obj->objs, obj->typinf->nb_cols)

        ObjectReset(obj);

        if (OCI_STATUS && (!obj->handle || (OCI_OBJECT_ALLOCATED_ARRAY == obj->hstate)))
        {
            /* allocates handle for non fetched object */

            if (OCI_OBJECT_ALLOCATED_ARRAY != obj->hstate)
            {
                obj->hstate = OCI_OBJECT_ALLOCATED;
            }

            OCI_EXEC
            (
                MemObjectNew(obj->con->env,  obj->con->err, obj->con->cxt,
                            (OCITypeCode) obj->typinf->typecode, obj->typinf->tdo, (dvoid *) NULL,
                            (OCIDuration) OCI_DURATION_SESSION, (boolean) TRUE,
                            (dvoid **) &obj->handle)
            )
        }
        else
        {
            obj->hstate = OCI_OBJECT_FETCHED_CLEAN;
        }

        if (OCI_STATUS && (OCI_UNKNOWN == obj->type))
        {
            ub4 size = sizeof(obj->type);

            /* calling OCIObjectGetProperty() on objects that are attributes of
               parent objects leads to a segmentation fault on MS Windows !
               We need to report that to Oracle! Because sub objects always are
               values, if the parent indicator array is not null, let's assign
               the object type properties ourselves */

            if (!parent)
            {
                OCIObjectGetProperty(obj->con->env, obj->con->err, obj->handle,
                                     (OCIObjectPropId) OCI_OBJECTPROP_LIFETIME,
                                     (void *) &obj->type, &size);
            }
            else
            {
                obj->type = OCI_OBJECT_VALUE;
            }
        }

        if (OCI_STATUS && (reset || !obj->tab_ind))
        {
            if (!parent)
            {
                OCI_EXEC
                (
                    OCIObjectGetInd(obj->con->env, obj->con->err,
                                    (dvoid *) obj->handle,
                                    (dvoid **) &obj->tab_ind)
                )
            }
            else
            {
                obj->tab_ind = parent->tab_ind;
                obj->idx_ind = parent->idx_ind + ObjectGetIndOffset(parent->typinf, index);
            }
        }
    }

    /* check for failure */

    if (!OCI_STATUS && obj)
    {
        ObjectFree(obj);
        obj = NULL;
    }

    return obj;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetAttrIndex
 * --------------------------------------------------------------------------------------------- */

int ObjectGetAttrIndex
(
    OCI_Object  *obj,
    const otext *attr,
    int          type,
    boolean      check
)
{
    int res = -1;

    OCI_CHECK(obj  == NULL, res)
    OCI_CHECK(attr == NULL, res);

    for (ub2 i = 0; i < obj->typinf->nb_cols; i++)
    {
        OCI_Column *col = &obj->typinf->cols[i];

        if (((type == -1) || (col->datatype == type))  && (ostrcasecmp(col->name, attr) == 0))
        {
            res = (int) i;
            break;
        }
    }

    if (check && res == -1)
    {
        ExceptionAttributeNotFound(obj->con, attr);
    }

    return res;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetAttr
 * --------------------------------------------------------------------------------------------- */

void * ObjectGetAttr
(
    OCI_Object  *obj,
    unsigned int index,
    OCIInd     **pind
)
{
    size_t offset = 0;
    size_t size   = 0;
    size_t align  = 0;

    OCI_CHECK(obj  == NULL, NULL)
    OCI_CHECK(pind == NULL, NULL)

    if (obj->typinf->struct_size == 0)
    {
        ObjectGetStructSize(obj->typinf, &size, &align);
    }

    offset = (size_t) obj->typinf->offsets[index];

    if (pind)
    {
        const int ind_index = obj->idx_ind + ObjectGetIndOffset(obj->typinf, index);

        *pind = &obj->tab_ind[ind_index];
    }

    return ((char *) obj->handle + offset);
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetNumberInternal
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetNumberInternal
(
    OCI_Object  *obj,
    const otext *attr,
    void        *value,
    uword        flag
)
{
    int index   = 0;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_PTR(OCI_IPC_STRING, attr)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    OCI_STATUS = FALSE;

    index = ObjectGetAttrIndex(obj, attr, OCI_CDT_NUMERIC, TRUE);

    if (index >= 0)
    {
        OCIInd     *ind = NULL;
        void       *num = ObjectGetAttr(obj, index, &ind);
        OCI_Column *col = &obj->typinf->cols[index];

        OCI_STATUS = TranslateNumericValue(obj->con, value, flag, num, col->subtype);

        if (OCI_STATUS)
        {
            *ind = OCI_IND_NOTNULL;
        }
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetNumberInternal
 * --------------------------------------------------------------------------------------------- */

boolean ObjectGetNumberInternal
(
    OCI_Object  *obj,
    const otext *attr,
    void        *value,
    uword        flag
)
{
    int index   = 0;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_PTR(OCI_IPC_STRING, attr)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    OCI_STATUS = FALSE;

    index = ObjectGetAttrIndex(obj, attr, OCI_CDT_NUMERIC, FALSE);

    if (index >= 0)
    {
        OCIInd *ind = NULL;
        void   *ptr = NULL;

        ptr = ObjectGetAttr(obj, index, &ind);

        if (ptr && (OCI_IND_NULL != *ind))
        {
            OCI_Column *col = &obj->typinf->cols[index];

            OCI_STATUS = TranslateNumericValue(obj->con, ptr, col->subtype, value, flag);
        }
    }
    else
    {
        index = ObjectGetAttrIndex(obj, attr, OCI_CDT_TEXT, FALSE);

        if (index >= 0)
        {
            OCI_STATUS = NumberFromString(obj->con, value, flag, ObjectGetString(obj, attr), NULL);
        }
    }

    if (index == -1)
    {
        ExceptionAttributeNotFound(obj->con, attr);
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectCreate
 * --------------------------------------------------------------------------------------------- */

OCI_Object * ObjectCreate
(
    OCI_Connection *con,
    OCI_TypeInfo   *typinf
)
{
    OCI_CALL_ENTER(OCI_Object *, NULL)
    OCI_CALL_CHECK_PTR(OCI_IPC_CONNECTION, con)
    OCI_CALL_CHECK_PTR(OCI_IPC_TYPE_INFO, typinf)
    OCI_CALL_CONTEXT_SET_FROM_CONN(con)

    OCI_RETVAL = ObjectInit(con, NULL, NULL, typinf, NULL, -1, TRUE);
    OCI_STATUS = (NULL != OCI_RETVAL);

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectFree
 * --------------------------------------------------------------------------------------------- */

boolean ObjectFree
(
    OCI_Object *obj
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_OBJECT_FETCHED(obj)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    /* if the object has sub-objects that have been fetched, we need to free
       these objects */

    ObjectReset(obj);

    OCI_FREE(obj->objs)
    OCI_FREE(obj->tmpbufs)
    OCI_FREE(obj->tmpsizes)

    if ((OCI_OBJECT_ALLOCATED == obj->hstate) || (OCI_OBJECT_ALLOCATED_ARRAY == obj->hstate))
    {
        MemObjectFree(obj->con->env, obj->con->err, obj->handle, OCI_DEFAULT);
    }

    if (OCI_OBJECT_ALLOCATED_ARRAY != obj->hstate)
    {
        OCI_FREE(obj)
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectArrayCreate
 * --------------------------------------------------------------------------------------------- */

OCI_Object ** ObjectArrayCreate
(
    OCI_Connection *con,
    OCI_TypeInfo   *typinf,
    unsigned int    nbelem
)
{
    OCI_Array *arr = NULL;

    OCI_CALL_ENTER(OCI_Object **, NULL)
    OCI_CALL_CHECK_PTR(OCI_IPC_CONNECTION, con)
    OCI_CALL_CHECK_PTR(OCI_IPC_TYPE_INFO, typinf)
    OCI_CALL_CONTEXT_SET_FROM_CONN(con)

    arr = ArrayCreate(con, nbelem, OCI_CDT_OBJECT, 0, sizeof(void *), sizeof(OCI_Object), 0, typinf);
    OCI_STATUS = (NULL != arr);

    if (OCI_STATUS)
    {
        OCI_RETVAL = (OCI_Object **) arr->tab_obj;
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectArrayFree
 * --------------------------------------------------------------------------------------------- */

boolean ObjectArrayFree
(
    OCI_Object **objs
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_ARRAY, objs)

    OCI_RETVAL = OCI_STATUS = ArrayFreeFromHandles((void **)objs);

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectAssign
 * --------------------------------------------------------------------------------------------- */

boolean ObjectAssign
(
    OCI_Object *obj,
    OCI_Object *obj_src
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj_src);
    OCI_CALL_CHECK_COMPAT(obj->con, obj->typinf->tdo == obj_src->typinf->tdo)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    OCI_EXEC
    (
        OCIObjectCopy(obj->con->env, obj->con->err, obj->con->cxt,
                      obj_src->handle, (obj_src->tab_ind + obj_src->idx_ind),
                      obj->handle, (obj->tab_ind + obj->idx_ind),
                      obj->typinf->tdo, OCI_DURATION_SESSION, OCI_DEFAULT)
    )

    if (OCI_STATUS)
    {
        obj->typinf = obj_src->typinf;

        ObjectReset(obj);
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetShort
 * --------------------------------------------------------------------------------------------- */

boolean ObjectGetBoolean
(
    OCI_Object  *obj,
    const otext *attr
)
{
    int index = -1;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_PTR(OCI_IPC_STRING, attr)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    OCI_STATUS = FALSE;

    index = ObjectGetAttrIndex(obj, attr, OCI_CDT_BOOLEAN, TRUE);

    if (index >= 0)
    {
        OCIInd *ind = NULL;
        boolean *value = NULL;

        OCI_STATUS = TRUE;

        value = (boolean *)ObjectGetAttr(obj, index, &ind);

        if (value && ind && (OCI_IND_NULL != *ind))
        {
            OCI_RETVAL = *value;
        }
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetNumber
 * --------------------------------------------------------------------------------------------- */

OCI_Number * ObjectGetNumber
(
    OCI_Object  *obj,
    const otext *attr
)
{
    OCI_OBJECT_GET_VALUE
    (
        OCI_CDT_NUMERIC,
        OCI_Number*,
        OCINumber*,
        NumberInit(obj->con, (OCI_Number *) obj->objs[index], (OCINumber *) value)
    )
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetShort
 * --------------------------------------------------------------------------------------------- */

short ObjectGetShort
(
    OCI_Object  *obj,
    const otext *attr
)
{
    short value = 0;

    ObjectGetNumberInternal(obj, attr, &value, OCI_NUM_SHORT);

    return value;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetUnsignedShort
 * --------------------------------------------------------------------------------------------- */

unsigned short ObjectGetUnsignedShort
(
    OCI_Object  *obj,
    const otext *attr
)
{
    unsigned short value = 0;

    ObjectGetNumberInternal(obj, attr, &value, OCI_NUM_USHORT);

    return value;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetInt
 * --------------------------------------------------------------------------------------------- */

int ObjectGetInt
(
    OCI_Object  *obj,
    const otext *attr
)
{
    int value = 0;

    ObjectGetNumberInternal(obj, attr, &value, OCI_NUM_INT);

    return value;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetUnsignedInt
 * --------------------------------------------------------------------------------------------- */

unsigned int ObjectGetUnsignedInt
(
    OCI_Object  *obj,
    const otext *attr
)
{
    unsigned int value = 0;

    ObjectGetNumberInternal(obj, attr, &value, OCI_NUM_UINT);

    return value;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetBigInt
 * --------------------------------------------------------------------------------------------- */

big_int ObjectGetBigInt
(
    OCI_Object  *obj,
    const otext *attr
)
{
    big_int value = 0;

    ObjectGetNumberInternal(obj, attr, &value, OCI_NUM_BIGINT);

    return value;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetUnsignedBigInt
 * --------------------------------------------------------------------------------------------- */

big_uint ObjectGetUnsignedBigInt
(
    OCI_Object  *obj,
    const otext *attr
)
{
    big_uint value = 0;

    ObjectGetNumberInternal(obj, attr, &value, OCI_NUM_BIGUINT);

    return value;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetDouble
 * --------------------------------------------------------------------------------------------- */

double ObjectGetDouble
(
    OCI_Object  *obj,
    const otext *attr
)
{
    double value = 0.0;

    ObjectGetNumberInternal(obj, attr, &value, OCI_NUM_DOUBLE);

    return value;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetFloat
 * --------------------------------------------------------------------------------------------- */

float ObjectGetFloat
(
    OCI_Object  *obj,
    const otext *attr
)
{
    float value = 0.0f;

    ObjectGetNumberInternal(obj, attr, &value, OCI_NUM_FLOAT);

    return value;
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetString
 * --------------------------------------------------------------------------------------------- */

const otext * ObjectGetString
(
    OCI_Object  *obj,
    const otext *attr
)
{
    int index = -1;

    OCI_CALL_ENTER(const otext *, NULL)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_PTR(OCI_IPC_STRING, attr)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    OCI_STATUS = FALSE;

    index = ObjectGetAttrIndex(obj, attr, OCI_CDT_TEXT, FALSE);

    if (index >= 0)
    {
        OCIInd *ind       = NULL;
        OCIString **value = NULL;

        OCI_STATUS = TRUE;

        value = (OCIString **) ObjectGetAttr(obj, index, &ind);

        if (value && ind && (OCI_IND_NULL != *ind))
        {
            if (OCILib.use_wide_char_conv)
            {
                OCI_RETVAL = StringFromStringPtr(obj->con->env, *value, &obj->tmpbufs[index], &obj->tmpsizes[index]);
            }
            else
            {
                OCI_RETVAL = (otext *)OCIStringPtr(obj->con->env, *value);
            }
        }
    }
    else
    {
        index = ObjectGetAttrIndex(obj, attr, -1, FALSE);

        if (index >= 0)
        {
            OCI_Error   *err   = ErrorGet(TRUE, TRUE);
            OCIInd      *ind   = NULL;
            void        *value = NULL;
            unsigned int size  = 0;
            unsigned int len   = 0;

            OCI_STATUS = TRUE;

            value = ObjectGetAttr(obj, index, &ind);

            /* special case of RAW attribute, we need their length */
            if (OCI_CDT_RAW == obj->typinf->cols[index].datatype)
            {
                if (value && ind && (OCI_IND_NULL != *ind))
                {
                    size  = OCIRawSize(obj->con->env, (*(OCIRaw **) value));
                    value = OCIRawPtr(obj->con->env,  (*(OCIRaw **) value));
                }
            }

            len = StringGetFromType(obj->con, &obj->typinf->cols[index], value, size, NULL, 0, FALSE);
            OCI_STATUS = (NULL == err || OCI_UNKNOWN == err->type);

            if (OCI_STATUS && len > 0)
            {
                OCI_STATUS = StringRequestBuffer(&obj->tmpbufs[index], &obj->tmpsizes[index], len);

                if (OCI_STATUS)
                {
                    const unsigned int real_tmpsize = StringGetFromType(obj->con, &obj->typinf->cols[index], value, size, obj->tmpbufs[index], obj->tmpsizes[index], FALSE);
                
                    OCI_STATUS = (NULL == err || OCI_UNKNOWN == err->type);

                    if (OCI_STATUS && real_tmpsize > 0)
                    {
                        OCI_RETVAL = obj->tmpbufs[index];
                    }
                }
            }
        }
    }

    if (index == -1)
    {
        ExceptionAttributeNotFound(obj->con, attr);
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetRaw
 * --------------------------------------------------------------------------------------------- */

int ObjectGetRaw
(
    OCI_Object  *obj,
    const otext *attr,
    void        *buffer,
    unsigned int len
)
{
    int index = -1;

    OCI_CALL_ENTER(int, 0);
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_PTR(OCI_IPC_STRING, attr)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    OCI_STATUS = FALSE;

    index = ObjectGetAttrIndex(obj, attr, OCI_CDT_RAW, TRUE);

    if (index >= 0)
    {
        OCIInd *ind    = NULL;
        OCIRaw **value = NULL;

        OCI_STATUS = TRUE;

        value = (OCIRaw **) ObjectGetAttr(obj, index, &ind);

        if (value && ind && (OCI_IND_NULL != *ind))
        {
            const ub4 raw_len = OCIRawSize(obj->con->env, *value);

            if (len > raw_len)
            {
                len = raw_len;
            }

            memcpy(buffer, OCIRawPtr(obj->con->env, *value), (size_t) len);

            OCI_RETVAL = (int) len;
        }
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
* ObjectGetRawSize
* --------------------------------------------------------------------------------------------- */

unsigned int ObjectGetRawSize
(
    OCI_Object  *obj,
    const otext *attr
)
{
    ub4 raw_len = 0;
    int index = -1;

    OCI_CALL_ENTER(unsigned int, 0)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_PTR(OCI_IPC_STRING, attr)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    OCI_STATUS = FALSE;

    index = ObjectGetAttrIndex(obj, attr, OCI_CDT_RAW, TRUE);

    if (index >= 0)
    {
        OCIInd *ind = NULL;
        OCIRaw **value = NULL;

        OCI_STATUS = TRUE;

        value = (OCIRaw **)ObjectGetAttr(obj, index, &ind);

        if (value && ind && (OCI_IND_NULL != *ind))
        {
            raw_len = OCIRawSize(obj->con->env, *value);
        }
    }

    OCI_RETVAL = raw_len;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetDate
 * --------------------------------------------------------------------------------------------- */

OCI_Date * ObjectGetDate
(
    OCI_Object  *obj,
    const otext *attr
)
{
    OCI_OBJECT_GET_VALUE
    (
        OCI_CDT_DATETIME,
        OCI_Date*,
        OCIDate,
        DateInit(obj->con, (OCI_Date *) obj->objs[index], value, FALSE, FALSE)
    )
 }

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetTimestamp
 * --------------------------------------------------------------------------------------------- */

OCI_Timestamp * ObjectGetTimestamp
(
    OCI_Object  *obj,
    const otext *attr
)
{
#if OCI_VERSION_COMPILE >= OCI_9_0

    OCI_OBJECT_GET_VALUE
    (
        OCI_CDT_TIMESTAMP,
        OCI_Timestamp*,
        OCIDateTime*,
        TimestampInit(obj->con, (OCI_Timestamp *) obj->objs[index],
                     (OCIDateTime *) *value, obj->typinf->cols[index].subtype)
    )

#else

    OCI_CALL_ENTER( OCI_Timestamp *, NULL)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_EXIT()

#endif
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetInterval
 * --------------------------------------------------------------------------------------------- */

OCI_Interval * ObjectGetInterval
(
    OCI_Object  *obj,
    const otext *attr
)
{
#if OCI_VERSION_COMPILE >= OCI_9_0

    OCI_OBJECT_GET_VALUE
        (
        OCI_CDT_INTERVAL,
        OCI_Interval*,
        OCIInterval *,
        IntervalInit(obj->con, (OCI_Interval *) obj->objs[index],
                     (OCIInterval *) *value, obj->typinf->cols[index].subtype)
    )

#else

    OCI_CALL_ENTER(OCI_Interval *, NULL)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_EXIT()

#endif
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetColl
 * --------------------------------------------------------------------------------------------- */

OCI_Coll * ObjectGetColl
(
    OCI_Object  *obj,
    const otext *attr
)
{
    OCI_OBJECT_GET_VALUE
    (
        OCI_CDT_COLLECTION,
        OCI_Coll*,
        OCIColl*,
        CollInit(obj->con, (OCI_Coll *) obj->objs[index], (OCIColl *) *value, obj->typinf->cols[index].typinf)
    )
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetObject
 * --------------------------------------------------------------------------------------------- */

OCI_Object * ObjectGetObject
(
    OCI_Object  *obj,
    const otext *attr
)
{
    OCI_OBJECT_GET_VALUE
    (
        OCI_CDT_OBJECT,
        OCI_Object*,
        void,
        ObjectInit(obj->con, (OCI_Object *) obj->objs[index], value,
                   obj->typinf->cols[index].typinf,  obj, index, FALSE)
    )
 }

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetLob
 * --------------------------------------------------------------------------------------------- */

OCI_Lob * ObjectGetLob
(
    OCI_Object  *obj,
    const otext *attr
)
{
    OCI_OBJECT_GET_VALUE
    (
        OCI_CDT_LOB,
        OCI_Lob*,
        OCILobLocator*,
        LobInit(obj->con, (OCI_Lob *) obj->objs[index], *value, obj->typinf->cols[index].subtype)
    )
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetFile
 * --------------------------------------------------------------------------------------------- */

OCI_File * ObjectGetFile
(
    OCI_Object  *obj,
    const otext *attr
)
{
    OCI_OBJECT_GET_VALUE
    (
        OCI_CDT_FILE,
        OCI_File *,
        OCILobLocator*,
        FileInit(obj->con, (OCI_File *) obj->objs[index], *value, obj->typinf->cols[index].subtype)
    )
 }

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetRef
 * --------------------------------------------------------------------------------------------- */

OCI_Ref * ObjectGetRef
(
    OCI_Object  *obj,
    const otext *attr
)
{
    OCI_OBJECT_GET_VALUE
    (
        OCI_CDT_REF,
        OCI_Ref*,
        OCIRef*,
        RefInit(obj->con, NULL, (OCI_Ref *) obj->objs[index], *value)
    )
}

/* --------------------------------------------------------------------------------------------- *
* OCI_ObjectSetBoolean
* --------------------------------------------------------------------------------------------- */

boolean ObjectSetBoolean
(
    OCI_Object  *obj,
    const otext *attr,
    boolean      value
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_PTR(OCI_IPC_STRING, attr)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    OCI_STATUS = FALSE;

    const int index = ObjectGetAttrIndex(obj, attr, OCI_CDT_BOOLEAN, TRUE);

    if (index >= 0)
    {
        OCIInd *ind = NULL;
        boolean *data = (boolean *)ObjectGetAttr(obj, index, &ind);

        if (data)
        {
            *data = value;
            *ind = OCI_IND_NOTNULL;

            OCI_STATUS = TRUE;
        }
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetNumber
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetNumber
(
    OCI_Object  *obj,
    const otext *attr,
    OCI_Number  *value
)
{
    OCI_OBJECT_SET_VALUE
    (
        OCI_CDT_NUMERIC,
        OCINumber,
        OCINumberAssign( obj->con->err, value->handle, data)
    )
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetShort
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetShort
(
    OCI_Object  *obj,
    const otext *attr,
    short        value
)
{
    return ObjectSetNumberInternal(obj, attr, &value, (uword) OCI_NUM_SHORT);
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetUnsignedShort
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetUnsignedShort
(
    OCI_Object    *obj,
    const otext   *attr,
    unsigned short value
)
{
    return ObjectSetNumberInternal(obj, attr, &value, (uword) OCI_NUM_USHORT);
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetInt
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetInt
(
    OCI_Object  *obj,
    const otext *attr,
    int          value
)
{
    return ObjectSetNumberInternal(obj, attr, &value, (uword) OCI_NUM_INT);
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetUnsignedInt
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetUnsignedInt
(
    OCI_Object  *obj,
    const otext *attr,
    unsigned int value
)
{
    return ObjectSetNumberInternal(obj, attr, &value, (uword) OCI_NUM_UINT);
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetBigInt
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetBigInt
(
    OCI_Object  *obj,
    const otext *attr,
    big_int      value
)
{
    return ObjectSetNumberInternal(obj, attr, &value, (uword) OCI_NUM_BIGINT);
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetUnsignedBigInt
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetUnsignedBigInt
(
    OCI_Object  *obj,
    const otext *attr,
    big_uint     value
)
{
    return ObjectSetNumberInternal(obj, attr, &value, (uword) OCI_NUM_BIGUINT);
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetDouble
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetDouble
(
    OCI_Object  *obj,
    const otext *attr,
    double       value
)
{
    return ObjectSetNumberInternal(obj, attr, &value, (uword) OCI_NUM_DOUBLE);
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetFloat
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetFloat
(
    OCI_Object  *obj,
    const otext *attr,
    float        value
)
{
    return ObjectSetNumberInternal(obj, attr, &value, (uword) OCI_NUM_FLOAT);
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetString
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetString
(
    OCI_Object  *obj,
    const otext *attr,
    const otext *value
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_PTR(OCI_IPC_STRING, attr)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    OCI_STATUS = FALSE;

    if (!value)
    {
        OCI_STATUS = ObjectSetNull(obj, attr);
    }
    else
    {
        const int index = ObjectGetAttrIndex(obj, attr, OCI_CDT_TEXT, TRUE);

        if (index >= 0)
        {
            OCIInd *ind      = NULL;
            OCIString **data = (OCIString **) ObjectGetAttr(obj, index, &ind);

            OCI_STATUS = StringToStringPtr(obj->con->env, data, obj->con->err, value);

            if (OCI_STATUS)
            {
                *ind = OCI_IND_NOTNULL;
            }
        }
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetRaw
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetRaw
(
    OCI_Object  *obj,
    const otext *attr,
    void       * value,
    unsigned int len
)
{
    OCI_OBJECT_SET_VALUE
    (
        OCI_CDT_RAW,
        OCIRaw*,
        OCIRawAssignBytes(obj->con->env, obj->con->err, (ub1*) value, len, data)
    )
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetDate
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetDate
(
    OCI_Object  *obj,
    const otext *attr,
    OCI_Date    *value
)
{
    OCI_OBJECT_SET_VALUE
    (
        OCI_CDT_DATETIME,
        OCIDate,
        OCIDateAssign(obj->con->err, value->handle, data)
    )
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetTimestamp
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetTimestamp
(
    OCI_Object    *obj,
    const otext   *attr,
    OCI_Timestamp *value
)
{
#if OCI_VERSION_COMPILE >= OCI_9_0

    OCI_OBJECT_SET_VALUE
    (
        OCI_CDT_TIMESTAMP,
        OCIDateTime*,
        OCIDateTimeAssign((dvoid *) obj->con->env, obj->con->err, value->handle, *data)
    )

#else

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_EXIT()

#endif
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetInterval
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetInterval
(
    OCI_Object   *obj,
    const otext  *attr,
    OCI_Interval *value
)
{
#if OCI_VERSION_COMPILE >= OCI_9_0

    OCI_OBJECT_SET_VALUE
    (
        OCI_CDT_INTERVAL,
        OCIInterval*,
        OCIIntervalAssign((dvoid *) obj->con->env, obj->con->err, value->handle, *data)
    )

#else

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_EXIT()

#endif
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetColl
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetColl
(
    OCI_Object  *obj,
    const otext *attr,
    OCI_Coll    *value
)
{
    OCI_OBJECT_SET_VALUE
    (
        OCI_CDT_COLLECTION,
        OCIColl*,
        OCICollAssign(obj->con->env, obj->con->err, value->handle, *data)
    )
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetObject
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetObject
(
    OCI_Object  *obj,
    const otext *attr,
    OCI_Object  *value
)
{
    OCI_OBJECT_SET_VALUE
    (
        OCI_CDT_OBJECT,
        void,
        OCIObjectCopy(obj->con->env, obj->con->err, obj->con->cxt,
                     value->handle, (value->tab_ind + value->idx_ind),
                     data, ind, obj->typinf->cols[index].typinf->tdo,
                     OCI_DURATION_SESSION, OCI_DEFAULT)
    )
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetLob
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetLob
(
    OCI_Object  *obj,
    const otext *attr,
    OCI_Lob     *value
)
{
    OCI_OBJECT_SET_VALUE
    (
        OCI_CDT_LOB,
        OCILobLocator*,
        OCILobLocatorAssign(obj->con->cxt, obj->con->err, value->handle, (OCILobLocator **) data)
    )
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetFile
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetFile
(
    OCI_Object  *obj,
    const otext *attr,
    OCI_File    *value
)
{
    OCI_OBJECT_SET_VALUE
    (
        OCI_CDT_FILE,
        OCILobLocator*,
        OCILobLocatorAssign(obj->con->cxt, obj->con->err, value->handle, (OCILobLocator **) data)
    )
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetRef
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetRef
(
    OCI_Object  *obj,
    const otext *attr,
    OCI_Ref     *value
)
{
    OCI_OBJECT_SET_VALUE
    (
        OCI_CDT_REF,
        OCIRef*,
        OCIRefAssign(obj->con->env, obj->con->err, value->handle, data)
    )
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectSetNull
 * --------------------------------------------------------------------------------------------- */

boolean ObjectSetNull
(
    OCI_Object  *obj,
    const otext *attr
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_PTR(OCI_IPC_STRING, attr)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    const int index = ObjectGetAttrIndex(obj, attr, -1, TRUE);

    if (index >= 0)
    {
        const int ind_index = obj->idx_ind + ObjectGetIndOffset(obj->typinf, index);

        obj->tab_ind[ind_index] = OCI_IND_NULL;

        OCI_STATUS = TRUE;
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectIsNull
 * --------------------------------------------------------------------------------------------- */

boolean ObjectIsNull
(
    OCI_Object  *obj,
    const otext *attr
)
{
    int index   = 0;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_PTR(OCI_IPC_STRING, attr)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    index = ObjectGetAttrIndex(obj, attr, -1, TRUE);

    if (index >= 0)
    {
        const int ind_index = obj->idx_ind + ObjectGetIndOffset(obj->typinf, index);

        OCI_RETVAL = (OCI_IND_NOTNULL != obj->tab_ind[ind_index]);

        OCI_STATUS = TRUE;
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetTypeInfo
 * --------------------------------------------------------------------------------------------- */

OCI_TypeInfo * ObjectGetTypeInfo
(
    OCI_Object *obj
)
{
    OCI_GET_PROP(OCI_TypeInfo*, NULL, OCI_IPC_OBJECT, obj, typinf, obj->con, NULL, obj->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetType
 * --------------------------------------------------------------------------------------------- */

unsigned int ObjectGetType
(
    OCI_Object *obj
)
{
    OCI_GET_PROP(unsigned int, OCI_UNKNOWN, OCI_IPC_OBJECT, obj, type, obj->con, NULL, obj->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetSelfRef
 * --------------------------------------------------------------------------------------------- */

boolean ObjectGetSelfRef
(
    OCI_Object *obj,
    OCI_Ref    *ref
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_PTR(OCI_IPC_REF, ref)
    OCI_CALL_CHECK_COMPAT(obj->con, obj->typinf->tdo == ref->typinf->tdo)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    OCI_EXEC(OCIObjectGetObjectRef(obj->con->env, obj->con->err, obj->handle, ref->handle))

    if (!OCI_STATUS && ref->obj)
    {
        ObjectFree(ref->obj);
        ref->obj = NULL;
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectGetStruct
 * --------------------------------------------------------------------------------------------- */

boolean ObjectGetStruct
(
    OCI_Object *obj,
    void      **pp_struct,
    void      **pp_ind
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    if (pp_struct)
    {
        *pp_struct = (void *) obj->handle;
    }

    if (pp_ind)
    {
        *pp_ind = (void *) obj->tab_ind;
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * ObjectToText
 * --------------------------------------------------------------------------------------------- */

boolean ObjectToText
(
    OCI_Object   *obj,
    unsigned int *size,
    otext        *str
)
{
    OCI_Error   *err   = NULL;
    otext       *attr  = NULL;
    boolean      quote = TRUE;
    unsigned int len   = 0;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_OBJECT, obj)
    OCI_CALL_CHECK_PTR(OCI_IPC_VOID, size)
    OCI_CALL_CONTEXT_SET_FROM_CONN(obj->con)

    err = ErrorGet(TRUE, TRUE);

    if (str)
    {
        *str = 0;
    }

    len += StringAddToBuffer(str, len, obj->typinf->name, (unsigned int) ostrlen(obj->typinf->name), FALSE);
    len += StringAddToBuffer(str, len, OTEXT("("), 1, FALSE);

    for (int i = 0; i < obj->typinf->nb_cols && OCI_STATUS; i++)
    {
        attr  = obj->typinf->cols[i].name;
        quote = TRUE;

        if (ObjectIsNull(obj, attr))
        {
            len += StringAddToBuffer(str, len, OCI_STRING_NULL, OCI_STRING_NULL_SIZE, FALSE);
        }
        else
        {
            void *data = NULL;
            unsigned int data_size = 0;
            const unsigned int data_type = obj->typinf->cols[i].datatype;

            switch (data_type)
            {
                case OCI_CDT_TEXT:
                {
                    OCIInd *ind = NULL;
                    data = ObjectGetAttr(obj, i, &ind);

                    if (data && ind && (OCI_IND_NULL != *ind))
                    {
                        data_size = OCIStringSize(OCILib.env, (*(OCIString **)data));
                        data      = (void *)ObjectGetString(obj, attr);
                    }
                    else
                    {
                        data = NULL;
                    }
                    break;
                }
                case OCI_CDT_BOOLEAN:
                {
                    OCIInd *ind = NULL;
                    data = ObjectGetAttr(obj, i, &ind);
                    quote = FALSE;
                    break;
                }
                case OCI_CDT_NUMERIC:
                {
                    OCIInd *ind = NULL;
                    data  = ObjectGetAttr(obj, i, &ind);
                    quote = FALSE;
                    break;
                }
                case OCI_CDT_RAW:
                {
                    OCIInd *ind = NULL;
                    data = ObjectGetAttr(obj, i, &ind);

                    if (data && ind && (OCI_IND_NULL != *ind))
                    {
                        data_size = OCIRawSize(obj->con->env, (*(OCIRaw **) data));
                        data      = OCIRawPtr(obj->con->env,  (*(OCIRaw **) data));
                    }
                    else
                    {
                        data = NULL;
                    }                
                    break;
                }
                case OCI_CDT_DATETIME:
                {
                    data  = (void *) ObjectGetDate(obj, attr);
                    break;
                }
                case OCI_CDT_TIMESTAMP:
                {
                    data  = (void *) ObjectGetTimestamp(obj, attr);
                    break;
                }
                case OCI_CDT_INTERVAL:
                {
                    data  = (void *) ObjectGetInterval(obj, attr);
                    break;
                }
                case OCI_CDT_LOB:
                {
                    data  = (void *) ObjectGetLob(obj, attr);
                    break;
                }
                case OCI_CDT_FILE:
                {
                    data  = (void *) ObjectGetFile(obj, attr);
                    break;
                }
                case OCI_CDT_REF:
                {
                    data  = (void *) ObjectGetRef(obj, attr);
                    break;
                }
                case OCI_CDT_OBJECT:
                {
                    data  = (void *) ObjectGetObject(obj, attr);
                    quote = FALSE;
                    break;
                }
                case OCI_CDT_COLLECTION:
                {
                    data =  (void *) ObjectGetColl(obj, attr);
                    quote = FALSE;
                }
            }

            OCI_STATUS = (NULL != data || OCI_CDT_TEXT == data_type) && (NULL == err || !err->raise);

            if (OCI_STATUS)
            {
                otext *tmpbuf = str;

                if (tmpbuf)
                {
                    tmpbuf += len;
                }

                if (data)
                {
                    len += StringGetFromType(obj->con, &obj->typinf->cols[i], data, data_size, tmpbuf, tmpbuf && size ? *size - len : 0, quote);
                }
                else
                {
                    len += StringAddToBuffer(str, len, OCI_STRING_NULL, OCI_STRING_NULL_SIZE, FALSE);
                }

                OCI_STATUS = (NULL == err || OCI_UNKNOWN == err->type);
            }
        }

        if (OCI_STATUS && i < (obj->typinf->nb_cols-1))
        {
            len += StringAddToBuffer(str, len, OTEXT(", "), 2, quote);
        }
    }

    if (OCI_STATUS)
    {
        len += StringAddToBuffer(str, len, OTEXT(")"), 1, FALSE);

        *size = len;
    }
    else
    {
        *size = 0;

        if (str)
        {
            *str = 0;
        }
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

