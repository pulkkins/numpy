/*
 * This file implements assignment from an ndarray to another ndarray.
 *
 * Written by Mark Wiebe (mwwiebe@gmail.com)
 * Copyright (c) 2011 by Enthought, Inc.
 *
 * See LICENSE.txt for the license.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#define NPY_NO_DEPRECATED_API NPY_API_VERSION
#define _MULTIARRAYMODULE
#include <numpy/ndarraytypes.h>

#include "npy_config.h"
#include "numpy/npy_3kcompat.h"

#include "convert_datatype.h"
#include "methods.h"
#include "shape.h"
#include "lowlevel_strided_loops.h"
#include "na_object.h"
#include "na_mask.h"

#include "array_assign.h"

/*
 * Assigns the array from 'src' to 'dst'. The strides must already have
 * been broadcast.
 *
 * Returns 0 on success, -1 on failure.
 */
NPY_NO_EXPORT int
raw_array_assign_array(int ndim, npy_intp *shape,
        PyArray_Descr *dst_dtype, char *dst_data, npy_intp *dst_strides,
        PyArray_Descr *src_dtype, char *src_data, npy_intp *src_strides)
{
    int idim;
    npy_intp shape_it[NPY_MAXDIMS];
    npy_intp dst_strides_it[NPY_MAXDIMS];
    npy_intp src_strides_it[NPY_MAXDIMS];
    npy_intp coord[NPY_MAXDIMS];

    PyArray_StridedUnaryOp *stransfer = NULL;
    NpyAuxData *transferdata = NULL;
    int aligned, needs_api = 0;
    npy_intp src_itemsize = src_dtype->elsize;

    NPY_BEGIN_THREADS_DEF;

    /* Check alignment */
    aligned = raw_array_is_aligned(ndim,
                        dst_data, dst_strides, dst_dtype->alignment) &&
              raw_array_is_aligned(ndim,
                        src_data, src_strides, src_dtype->alignment);

    /* Use raw iteration with no heap allocation */
    if (PyArray_PrepareTwoRawArrayIter(
                    ndim, shape,
                    dst_data, dst_strides,
                    src_data, src_strides,
                    &ndim, shape_it,
                    &dst_data, dst_strides_it,
                    &src_data, src_strides_it) < 0) {
        return -1;
    }

    /*
     * Overlap check for the 1D case. Higher dimensional arrays and
     * opposite strides cause a temporary copy before getting here.
     */
    if (ndim == 1 && src_data < dst_data &&
                src_data + shape_it[0] * src_strides_it[0] > dst_data) {
        src_data += (shape_it[0] - 1) * src_strides_it[0];
        dst_data += (shape_it[0] - 1) * dst_strides_it[0];
        src_strides_it[0] = -src_strides_it[0];
        dst_strides_it[0] = -dst_strides_it[0];
    }

    /* Get the function to do the casting */
    if (PyArray_GetDTypeTransferFunction(aligned,
                        src_strides_it[0], dst_strides_it[0],
                        src_dtype, dst_dtype,
                        0,
                        &stransfer, &transferdata,
                        &needs_api) != NPY_SUCCEED) {
        return -1;
    }

    if (!needs_api) {
        NPY_BEGIN_THREADS;
    }

    NPY_RAW_ITER_START(idim, ndim, coord, shape_it) {
        /* Process the innermost dimension */
        stransfer(dst_data, dst_strides_it[0], src_data, src_strides_it[0],
                    shape_it[0], src_itemsize, transferdata);
    } NPY_RAW_ITER_TWO_NEXT(idim, ndim, coord, shape_it,
                            dst_data, dst_strides_it,
                            src_data, src_strides_it);

    if (!needs_api) {
        NPY_END_THREADS;
    }

    NPY_AUXDATA_FREE(transferdata);

    return (needs_api && PyErr_Occurred()) ? -1 : 0;
}

/*
 * Assigns the array from 'src' to 'dst, whereever the 'wheremask'
 * value is True. The strides must already have been broadcast.
 *
 * Returns 0 on success, -1 on failure.
 */
NPY_NO_EXPORT int
raw_array_wheremasked_assign_array(int ndim, npy_intp *shape,
        PyArray_Descr *dst_dtype, char *dst_data, npy_intp *dst_strides,
        PyArray_Descr *src_dtype, char *src_data, npy_intp *src_strides,
        PyArray_Descr *wheremask_dtype, char *wheremask_data,
        npy_intp *wheremask_strides)
{
    int idim;
    npy_intp shape_it[NPY_MAXDIMS];
    npy_intp dst_strides_it[NPY_MAXDIMS];
    npy_intp src_strides_it[NPY_MAXDIMS];
    npy_intp wheremask_strides_it[NPY_MAXDIMS];
    npy_intp coord[NPY_MAXDIMS];

    PyArray_MaskedStridedUnaryOp *stransfer = NULL;
    NpyAuxData *transferdata = NULL;
    int aligned, needs_api = 0;
    npy_intp src_itemsize = src_dtype->elsize;

    NPY_BEGIN_THREADS_DEF;

    /* Check alignment */
    aligned = raw_array_is_aligned(ndim,
                        dst_data, dst_strides, dst_dtype->alignment) &&
              raw_array_is_aligned(ndim,
                        src_data, src_strides, src_dtype->alignment);

    /* Use raw iteration with no heap allocation */
    if (PyArray_PrepareThreeRawArrayIter(
                    ndim, shape,
                    dst_data, dst_strides,
                    src_data, src_strides,
                    wheremask_data, wheremask_strides,
                    &ndim, shape_it,
                    &dst_data, dst_strides_it,
                    &src_data, src_strides_it,
                    &wheremask_data, wheremask_strides_it) < 0) {
        return -1;
    }

    /*
     * Overlap check for the 1D case. Higher dimensional arrays cause
     * a temporary copy before getting here.
     */
    if (ndim == 1 && src_data < dst_data &&
                src_data + shape_it[0] * src_strides_it[0] > dst_data) {
        src_data += (shape_it[0] - 1) * src_strides_it[0];
        dst_data += (shape_it[0] - 1) * dst_strides_it[0];
        wheremask_data += (shape_it[0] - 1) * wheremask_strides_it[0];
        src_strides_it[0] = -src_strides_it[0];
        dst_strides_it[0] = -dst_strides_it[0];
        wheremask_strides_it[0] = -wheremask_strides_it[0];
    }

    /* Get the function to do the casting */
    if (PyArray_GetMaskedDTypeTransferFunction(aligned,
                        src_strides_it[0],
                        dst_strides_it[0],
                        wheremask_strides_it[0],
                        src_dtype, dst_dtype, wheremask_dtype,
                        0,
                        &stransfer, &transferdata,
                        &needs_api) != NPY_SUCCEED) {
        return -1;
    }

    if (!needs_api) {
        NPY_BEGIN_THREADS;
    }

    NPY_RAW_ITER_START(idim, ndim, coord, shape_it) {
        /* Process the innermost dimension */
        stransfer(dst_data, dst_strides_it[0], src_data, src_strides_it[0],
                    (npy_mask *)wheremask_data, wheremask_strides_it[0],
                    shape_it[0], src_itemsize, transferdata);
    } NPY_RAW_ITER_THREE_NEXT(idim, ndim, coord, shape_it,
                            dst_data, dst_strides_it,
                            src_data, src_strides_it,
                            wheremask_data, wheremask_strides_it);

    if (!needs_api) {
        NPY_END_THREADS;
    }

    NPY_AUXDATA_FREE(transferdata);

    return (needs_api && PyErr_Occurred()) ? -1 : 0;
}

/*
 * Assigns the elements of 'src' to 'dst' where the 'wheremask'
 * is True, except for those which are masked as NA according
 * to 'maskna'.
 *
 * Returns 0 on success, -1 on failure.
 */
NPY_NO_EXPORT int
raw_array_wheremasked_assign_array_preservena(int ndim, npy_intp *shape,
        PyArray_Descr *dst_dtype, char *dst_data, npy_intp *dst_strides,
        PyArray_Descr *src_dtype, char *src_data, npy_intp *src_strides,
        PyArray_Descr *maskna_dtype, char *maskna_data,
        npy_intp *maskna_strides,
        PyArray_Descr *wheremask_dtype, char *wheremask_data,
        npy_intp *wheremask_strides)
{
    int idim;
    npy_intp shape_it[NPY_MAXDIMS];
    npy_intp dst_strides_it[NPY_MAXDIMS];
    npy_intp src_strides_it[NPY_MAXDIMS];
    npy_intp maskna_strides_it[NPY_MAXDIMS];
    npy_intp wheremask_strides_it[NPY_MAXDIMS];
    npy_intp coord[NPY_MAXDIMS];

    PyArray_MaskedStridedUnaryOp *stransfer = NULL;
    NpyAuxData *transferdata = NULL;
    int aligned, needs_api = 0;
    npy_intp src_itemsize = src_dtype->elsize;

    PyArray_StridedBinaryOp *maskand_stransfer = NULL;
    NpyAuxData *maskand_transferdata = NULL;

    char *maskna_buffer;
    npy_intp maskna_itemsize;

    NPY_BEGIN_THREADS_DEF;

    /* Check alignment */
    aligned = raw_array_is_aligned(ndim,
                        dst_data, dst_strides, dst_dtype->alignment) &&
              raw_array_is_aligned(ndim,
                        src_data, src_strides, src_dtype->alignment);

    /* Use raw iteration with no heap allocation */
    if (PyArray_PrepareFourRawArrayIter(
                    ndim, shape,
                    dst_data, dst_strides,
                    src_data, src_strides,
                    maskna_data, maskna_strides,
                    wheremask_data, wheremask_strides,
                    &ndim, shape_it,
                    &dst_data, dst_strides_it,
                    &src_data, src_strides_it,
                    &maskna_data, maskna_strides_it,
                    &wheremask_data, wheremask_strides_it) < 0) {
        return -1;
    }

    /* Allocate a buffer for inverting/anding the mask */
    maskna_itemsize = maskna_dtype->elsize;
    maskna_buffer = PyArray_malloc(NPY_ARRAY_ASSIGN_BUFFERSIZE *
                                    maskna_itemsize);
    if (maskna_buffer == NULL) {
        PyErr_NoMemory();
        return -1;
    }

    /*
     * Overlap check for the 1D case. Higher dimensional arrays cause
     * a temporary copy before getting here.
     */
    if (ndim == 1 && src_data < dst_data &&
                src_data + shape_it[0] * src_strides_it[0] > dst_data) {
        src_data += (shape_it[0] - 1) * src_strides_it[0];
        dst_data += (shape_it[0] - 1) * dst_strides_it[0];
        maskna_data += (shape_it[0] - 1) * maskna_strides_it[0];
        wheremask_data += (shape_it[0] - 1) * wheremask_strides_it[0];
        src_strides_it[0] = -src_strides_it[0];
        dst_strides_it[0] = -dst_strides_it[0];
        maskna_strides_it[0] = -maskna_strides_it[0];
        wheremask_strides_it[0] = -wheremask_strides_it[0];
    }

    /* Get the function to do the casting */
    if (PyArray_GetMaskedDTypeTransferFunction(aligned,
                        src_strides[0], dst_strides_it[0], maskna_itemsize,
                        src_dtype, dst_dtype, maskna_dtype,
                        0,
                        &stransfer, &transferdata,
                        &needs_api) != NPY_SUCCEED) {
        PyArray_free(maskna_buffer);
        return -1;
    }

    /*
     * Get the function to invert the mask. The output
     * of the binary operation is the dtype 'maskna_dtype'
     */
    if (PyArray_GetMaskAndFunction(
                        maskna_strides_it[0], maskna_dtype, 0,
                        wheremask_strides_it[0], wheremask_dtype, 0,
                        &maskand_stransfer, &maskand_transferdata) < 0) {
        PyArray_free(maskna_buffer);
        NPY_AUXDATA_FREE(transferdata);
        return -1;
    }

    if (!needs_api) {
        NPY_BEGIN_THREADS;
    }

    NPY_RAW_ITER_START(idim, ndim, coord, shape_it) {
        npy_intp buffered_count, count;
        char *dst_d, *src_d, *maskna_d, *wheremask_d;
        /* Process the innermost dimension a buffer size at a time */
        count = shape_it[0];
        dst_d = dst_data;
        src_d = src_data;
        maskna_d = maskna_data;
        wheremask_d = wheremask_data;
        do {
            buffered_count = count < NPY_ARRAY_ASSIGN_BUFFERSIZE
                                        ? count
                                        : NPY_ARRAY_ASSIGN_BUFFERSIZE;

            /* Prepare the mask into the buffer */
            maskand_stransfer(maskna_buffer, maskna_itemsize,
                        maskna_d, maskna_strides_it[0],
                        wheremask_d, wheremask_strides_it[0],
                        buffered_count, maskand_transferdata);

            /* Transfer the data based on the buffered mask */
            stransfer(dst_d, dst_strides_it[0], src_d, src_strides_it[0],
                        (npy_mask *)maskna_buffer, maskna_itemsize,
                        buffered_count, src_itemsize, transferdata);

            dst_d += buffered_count * dst_strides_it[0];
            src_d += buffered_count * src_strides_it[0];
            maskna_d += buffered_count * maskna_strides_it[0];
            wheremask_d += buffered_count * wheremask_strides_it[0];
            count -= buffered_count;
        } while (count > 0);
    } NPY_RAW_ITER_FOUR_NEXT(idim, ndim, coord, shape_it,
                            dst_data, dst_strides_it,
                            src_data, src_strides_it,
                            maskna_data, maskna_strides_it,
                            wheremask_data, wheremask_strides_it);

    if (!needs_api) {
        NPY_END_THREADS;
    }

    PyArray_free(maskna_buffer);
    NPY_AUXDATA_FREE(transferdata);
    NPY_AUXDATA_FREE(maskand_transferdata);

    return (needs_api && PyErr_Occurred()) ? -1 : 0;
}

/*NUMPY_API
 *
 * An array assignment function for copying arrays, broadcasting 'src' into
 * 'dst'. This function makes a temporary copy of 'src' if 'src' and
 * 'dst' overlap, to be able to handle views of the same data with
 * different strides.
 *
 * dst: The destination array.
 * src: The source array.
 * wheremask: If non-NULL, a boolean mask specifying where to copy.
 * casting: An exception is raised if the copy violates this
 *          casting rule.
 * preservena: If 0, overwrites everything in 'dst', if 1, it
 *              preserves elements in 'dst' which are NA.
 * preservewhichna: Must be NULL. When multi-NA support is implemented,
 *                   this will be an array of flags for 'preservena=True',
 *                   indicating which NA payload values to preserve.
 *
 * Returns 0 on success, -1 on failure.
 */
NPY_NO_EXPORT int
PyArray_AssignArray(PyArrayObject *dst, PyArrayObject *src,
                    PyArrayObject *wheremask,
                    NPY_CASTING casting,
                    npy_bool preservena, npy_bool *preservewhichna)
{
    int dst_has_maskna = PyArray_HASMASKNA(dst);
    int src_has_maskna = PyArray_HASMASKNA(src);
    int copied_src = 0;

    npy_intp src_strides[NPY_MAXDIMS], src_maskna_strides[NPY_MAXDIMS];

    /* Use array_assign_scalar if 'src' NDIM is 0 */
    if (PyArray_NDIM(src) == 0) {
        /* If the array is masked, assign to the NA mask */
        if (PyArray_HASMASKNA(src)) {
            NpyNA *na = NpyNA_FromObject((PyObject *)src, 1);

            if (na != NULL) {
                /* TODO: With multi-NA, preservena must also be followed */
                int retcode = PyArray_AssignNA(dst, na, wheremask,
                                            preservena, preservewhichna);
                Py_DECREF(na);
                return retcode;
            }
        }

        return PyArray_AssignRawScalar(
                            dst, PyArray_DESCR(src), PyArray_DATA(src),
                            wheremask, casting, preservena, preservewhichna);
    }

    /*
     * Performance fix for expresions like "a[1000:6000] += x".  In this
     * case, first an in-place add is done, followed by an assignment,
     * equivalently expressed like this:
     *
     *   tmp = a[1000:6000]   # Calls array_subscript_nice in mapping.c
     *   np.add(tmp, x, tmp)
     *   a[1000:6000] = tmp   # Calls array_ass_sub in mapping.c
     *
     * In the assignment the underlying data type, shape, strides, and
     * data pointers are identical, but src != dst because they are separately
     * generated slices.  By detecting this and skipping the redundant
     * copy of values to themselves, we potentially give a big speed boost.
     *
     * Note that we don't call EquivTypes, because usually the exact same
     * dtype object will appear, and we don't want to slow things down
     * with a complicated comparison.  The comparisons are ordered to
     * try and reject this with as little work as possible.
     */
    if (PyArray_DATA(src) == PyArray_DATA(dst) &&
                        PyArray_MASKNA_DATA(src) == PyArray_MASKNA_DATA(dst) &&
                        PyArray_DESCR(src) == PyArray_DESCR(dst) &&
                        PyArray_NDIM(src) == PyArray_NDIM(dst) &&
                        PyArray_CompareLists(PyArray_DIMS(src),
                                             PyArray_DIMS(dst),
                                             PyArray_NDIM(src)) &&
                        PyArray_CompareLists(PyArray_STRIDES(src),
                                             PyArray_STRIDES(dst),
                                             PyArray_NDIM(src))) {
        /*printf("Redundant copy operation detected\n");*/
        return 0;
    }

    /* Check that 'dst' is writeable */
    if (!PyArray_ISWRITEABLE(dst)) {
        PyErr_SetString(PyExc_RuntimeError,
                "cannot assign to a read-only array");
        goto fail;
    }

    /* Check the casting rule */
    if (!PyArray_CanCastTypeTo(PyArray_DESCR(src),
                                PyArray_DESCR(dst), casting)) {
        PyObject *errmsg;
        errmsg = PyUString_FromString("Cannot cast scalar from ");
        PyUString_ConcatAndDel(&errmsg,
                PyObject_Repr((PyObject *)PyArray_DESCR(src)));
        PyUString_ConcatAndDel(&errmsg,
                PyUString_FromString(" to "));
        PyUString_ConcatAndDel(&errmsg,
                PyObject_Repr((PyObject *)PyArray_DESCR(dst)));
        PyUString_ConcatAndDel(&errmsg,
                PyUString_FromFormat(" according to the rule %s",
                        npy_casting_to_string(casting)));
        PyErr_SetObject(PyExc_TypeError, errmsg);
        goto fail;
    }

    if (preservewhichna != NULL) {
        PyErr_SetString(PyExc_RuntimeError,
                "multi-NA support is not yet implemented");
        goto fail;
    }

    if (src_has_maskna && !dst_has_maskna) {
        int containsna = PyArray_ContainsNA(src, wheremask, NULL);
        if (containsna == -1) {
            goto fail;
        }
        else if (containsna) {
            PyErr_SetString(PyExc_ValueError,
                    "Cannot assign NA to an array which "
                    "does not support NAs");
            goto fail;
        }
        else {
            src_has_maskna = 0;
        }
    }

    /*
     * When ndim is 1 and the strides point in the same direction,
     * the lower-level inner loop handles copying
     * of overlapping data. For bigger ndim and opposite-strided 1D
     * data, we make a temporary copy of 'src' if 'src' and 'dst' overlap.'
     */
    if (((PyArray_NDIM(dst) == 1 && PyArray_NDIM(src) >= 1 &&
                    PyArray_STRIDES(dst)[0] *
                            PyArray_STRIDES(src)[PyArray_NDIM(src) - 1] < 0) ||
                    PyArray_NDIM(dst) > 1) && arrays_overlap(src, dst)) {
        PyArrayObject *tmp;

        /*
         * Allocate a temporary copy array.
         */
        tmp = (PyArrayObject *)PyArray_NewLikeArray(dst,
                                        NPY_KEEPORDER, NULL, 0);
        if (tmp == NULL) {
            goto fail;
        }

        /* Make the temporary copy have an NA mask if necessary */
        if (PyArray_HASMASKNA(src)) {
            if (PyArray_AllocateMaskNA(tmp, 1, 0, 1) < 0) {
                Py_DECREF(tmp);
                goto fail;
            }
        }

        if (PyArray_AssignArray(tmp, src,
                                NULL, NPY_UNSAFE_CASTING, 0, NULL) < 0) {
            Py_DECREF(tmp);
            goto fail;
        }

        src = tmp;
        copied_src = 1;
    }

    /* Broadcast 'src' to 'dst' for raw iteration */
    if (PyArray_NDIM(src) > PyArray_NDIM(dst)) {
        int ndim_tmp = PyArray_NDIM(src);
        npy_intp *src_shape_tmp = PyArray_DIMS(src);
        npy_intp *src_strides_tmp = PyArray_STRIDES(src);
        /*
         * As a special case for backwards compatibility, strip
         * away unit dimensions from the left of 'src'
         */
        while (ndim_tmp > PyArray_NDIM(dst) && src_shape_tmp[0] == 1) {
            --ndim_tmp;
            ++src_shape_tmp;
            ++src_strides_tmp;
        }

        if (broadcast_strides(PyArray_NDIM(dst), PyArray_DIMS(dst),
                    ndim_tmp, src_shape_tmp,
                    src_strides_tmp, "input array",
                    src_strides) < 0) {
            goto fail;
        }
    }
    else {
        if (broadcast_strides(PyArray_NDIM(dst), PyArray_DIMS(dst),
                    PyArray_NDIM(src), PyArray_DIMS(src),
                    PyArray_STRIDES(src), "input array",
                    src_strides) < 0) {
            goto fail;
        }
    }

    if (src_has_maskna) {
        if (broadcast_strides(PyArray_NDIM(dst), PyArray_DIMS(dst),
                    PyArray_NDIM(src), PyArray_DIMS(src),
                    PyArray_MASKNA_STRIDES(src), "input array",
                    src_maskna_strides) < 0) {
            goto fail;
        }
    }

    if (wheremask == NULL) {
        /* A straightforward value assignment */
        if (!preservena || !dst_has_maskna) {
            /* If assigning to an array with an NA mask, set to all exposed */
            if (dst_has_maskna) {
                if (src_has_maskna) {
                    /* Assign the NA mask */
                    if (raw_array_assign_array(
                                    PyArray_NDIM(dst), PyArray_DIMS(dst),
                                    PyArray_MASKNA_DTYPE(dst),
                                            PyArray_MASKNA_DATA(dst),
                                            PyArray_MASKNA_STRIDES(dst),
                                    PyArray_MASKNA_DTYPE(src),
                                            PyArray_MASKNA_DATA(src),
                                            src_maskna_strides) < 0) {
                        goto fail;
                    }

                    /* Assign the values based on the 'src' NA mask */
                    if (raw_array_wheremasked_assign_array(
                                    PyArray_NDIM(dst), PyArray_DIMS(dst),
                                    PyArray_DESCR(dst), PyArray_DATA(dst),
                                            PyArray_STRIDES(dst),
                                    PyArray_DESCR(src), PyArray_DATA(src),
                                            src_strides,
                                    PyArray_MASKNA_DTYPE(src),
                                            PyArray_MASKNA_DATA(src),
                                            src_maskna_strides) < 0) {
                        goto fail;
                    }

                    goto finish;
                }
                else {
                    if (PyArray_AssignMaskNA(dst, 1, NULL,
                                        preservena, preservewhichna) < 0) {
                        goto fail;
                    }
                }
            }

            /* Do the assignment with raw array iteration */
            if (raw_array_assign_array(PyArray_NDIM(dst), PyArray_DIMS(dst),
                    PyArray_DESCR(dst), PyArray_DATA(dst), PyArray_STRIDES(dst),
                    PyArray_DESCR(src), PyArray_DATA(src), src_strides) < 0) {
                goto fail;
            }
        }
        /* A value assignment without overwriting NA values */
        else {
            if (src_has_maskna) {
                /* Assign the NA mask, wheremasked with the 'dst' NA mask */
                if (raw_array_wheremasked_assign_array(
                                PyArray_NDIM(dst), PyArray_DIMS(dst),
                                PyArray_MASKNA_DTYPE(dst),
                                        PyArray_MASKNA_DATA(dst),
                                        PyArray_MASKNA_STRIDES(dst),
                                PyArray_MASKNA_DTYPE(src),
                                        PyArray_MASKNA_DATA(src),
                                        src_maskna_strides,
                                PyArray_MASKNA_DTYPE(dst),
                                        PyArray_MASKNA_DATA(dst),
                                        PyArray_MASKNA_STRIDES(dst)) < 0) {
                    goto fail;
                }
            }

            /*
             * The 'dst' NA mask now has exposed precisely the values we
             * want to assign, so use it for this assignment.
             */
            if (raw_array_wheremasked_assign_array(
                    PyArray_NDIM(dst), PyArray_DIMS(dst),
                    PyArray_DESCR(dst), PyArray_DATA(dst),
                            PyArray_STRIDES(dst),
                    PyArray_DESCR(src), PyArray_DATA(src),
                            PyArray_STRIDES(src),
                    PyArray_MASKNA_DTYPE(dst), PyArray_MASKNA_DATA(dst),
                            PyArray_MASKNA_STRIDES(dst)) < 0) {
                goto fail;
            }
        }
    }
    else {
        npy_intp wheremask_strides[NPY_MAXDIMS];
        int containsna = PyArray_ContainsNA(wheremask, NULL, NULL);

        if (containsna == -1) {
            goto fail;
        }
        else if (containsna) {
            if (!dst_has_maskna) {
                PyErr_SetString(PyExc_ValueError,
                        "Cannot assign NA to an array which "
                        "does not support NAs");
                goto fail;
            }
            else {
                /* TODO: add support for this */
                PyErr_SetString(PyExc_ValueError,
                        "A where mask with NA values is not supported "
                        "yet");
                goto fail;
            }
        }

        /* Broadcast the wheremask to 'dst' for raw iteration */
        if (broadcast_strides(PyArray_NDIM(dst), PyArray_DIMS(dst),
                    PyArray_NDIM(wheremask), PyArray_DIMS(wheremask),
                    PyArray_STRIDES(wheremask), "where mask",
                    wheremask_strides) < 0) {
            goto fail;
        }

        /* A straightforward where-masked assignment */
        if (!preservena || !dst_has_maskna) {
            /* If assigning to an array with an NA mask, set to all exposed */
            if (dst_has_maskna) {
                /*
                 * TODO: If the where mask has NA values, this part
                 *       changes too.
                 */
                if (src_has_maskna) {
                    /* Assign the NA mask */
                    if (raw_array_wheremasked_assign_array(
                                    PyArray_NDIM(dst), PyArray_DIMS(dst),
                                    PyArray_MASKNA_DTYPE(dst),
                                            PyArray_MASKNA_DATA(dst),
                                            PyArray_MASKNA_STRIDES(dst),
                                    PyArray_MASKNA_DTYPE(src),
                                            PyArray_MASKNA_DATA(src),
                                            src_maskna_strides,
                                    PyArray_DESCR(wheremask),
                                            PyArray_DATA(wheremask),
                                            wheremask_strides) < 0) {
                        goto fail;
                    }

                    /*
                     * Assign the values based on the wheremask, not
                     * overwriting values also masked by the 'src' NA mask
                     */
                    if (raw_array_wheremasked_assign_array_preservena(
                                    PyArray_NDIM(dst), PyArray_DIMS(dst),
                                    PyArray_DESCR(dst), PyArray_DATA(dst),
                                            PyArray_STRIDES(dst),
                                    PyArray_DESCR(src), PyArray_DATA(src),
                                            src_strides,
                                    PyArray_MASKNA_DTYPE(src),
                                            PyArray_MASKNA_DATA(src),
                                            src_maskna_strides,
                                    PyArray_DESCR(wheremask),
                                            PyArray_DATA(wheremask),
                                            wheremask_strides)) {
                        goto fail;
                    }

                    goto finish;
                }
                else {
                    if (PyArray_AssignMaskNA(dst, 1, wheremask,
                                        preservena, preservewhichna) < 0) {
                        goto fail;
                    }
                }
            }

            /* Do the masked assignment with raw array iteration */
            if (raw_array_wheremasked_assign_array(
                    PyArray_NDIM(dst), PyArray_DIMS(dst),
                    PyArray_DESCR(dst), PyArray_DATA(dst), PyArray_STRIDES(dst),
                    PyArray_DESCR(src), PyArray_DATA(src), src_strides,
                    PyArray_DESCR(wheremask), PyArray_DATA(wheremask),
                            wheremask_strides) < 0) {
                goto fail;
            }
        }
        /* A masked value assignment without overwriting NA values */
        else {
            if (src_has_maskna) {
                /*
                 * Assign the NA mask, wheremasked with the 'dst' NA mask
                 * and the parameter 'wheremask'
                 */
                if (raw_array_wheremasked_assign_array_preservena(
                                PyArray_NDIM(dst), PyArray_DIMS(dst),
                                PyArray_MASKNA_DTYPE(dst),
                                        PyArray_MASKNA_DATA(dst),
                                        PyArray_MASKNA_STRIDES(dst),
                                PyArray_MASKNA_DTYPE(src),
                                        PyArray_MASKNA_DATA(src),
                                        src_maskna_strides,
                                PyArray_MASKNA_DTYPE(dst),
                                        PyArray_MASKNA_DATA(dst),
                                        PyArray_MASKNA_STRIDES(dst),
                                PyArray_DESCR(wheremask),
                                        PyArray_DATA(wheremask),
                                        wheremask_strides) < 0) {
                    goto fail;
                }
            }

            /*
             * The 'dst' NA mask together with the 'wheremask' now have
             * exposed precisely the values we want to assign, so use
             * it's another wheremasked preservena assignment.
             */
            if (raw_array_wheremasked_assign_array_preservena(
                    PyArray_NDIM(dst), PyArray_DIMS(dst),
                    PyArray_DESCR(dst), PyArray_DATA(dst),
                            PyArray_STRIDES(dst),
                    PyArray_DESCR(src), PyArray_DATA(src),
                            PyArray_STRIDES(src),
                    PyArray_MASKNA_DTYPE(dst), PyArray_MASKNA_DATA(dst),
                    PyArray_MASKNA_STRIDES(dst),
                    PyArray_DESCR(wheremask), PyArray_DATA(wheremask),
                    wheremask_strides) < 0) {
                goto fail;
            }
        }
    }

finish:
    if (copied_src) {
        Py_DECREF(src);
    }
    return 0;

fail:
    if (copied_src) {
        Py_DECREF(src);
    }
    return -1;
}
