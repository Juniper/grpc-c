/*
 * Local abstract types
 *
 * Copyright (c) 1997-2007, 2011, Juniper Networks, Inc.
 * All rights reserved.
  */

#ifndef __JNX_AUX_TYPES_H__
#define __JNX_AUX_TYPES_H__

/**
 * @file aux_types.h
 * @brief 
 * Defines local abstract types.
 */

#ifndef UNUSED
#ifdef  __GNUC__
#define UNUSED __attribute__ ((__unused__))
#define PACKED __attribute__ ((__packed__))
#else
#define UNUSED
/* #define PACKED               not defined, we want to cause an error! */
#endif
#endif

#include <sys/types.h>

/**
 * @brief
 * A type for Boolean operations.
 */

typedef u_int8_t boolean;
typedef unsigned int js_boolean_t;

#ifndef	TRUE
#define TRUE  1
#define FALSE 0
#endif

/**
 * @internal
 *
 * Use this macro to add printf like format-args correctness check.
 */
#ifdef JNX_FORMAT_CHECK
#define FORMAT_CHECK(format_position, arg_position)                       \
    __attribute__ ((format (printf, format_position, arg_position)))
#else
#define FORMAT_CHECK(format_pos, arg_pos)
#endif

/**
 * @brief
 * Succes status for various functions.
 */
typedef enum status_e {
/* SUCCESS is defined in rpc_msg.h */
#ifndef _RPC_RPC_MSG_H
    SUCCESS = 0,                /* NO ERROR */
#endif
    EOK = 0,                    /**< Alias for SUCCESS */
    EFAIL                       /**< Generic Failure */
} status_t;


/*
 * Other useful macros
 */

#define	ROUNDUP(a, size) (((a) & ((size)-1)) ? (1 + ((a) | ((size)-1))) : (a))

/*
 * See const_* method below
 */
typedef union {
    void        *cg_vp;
    const void  *cg_cvp;
} const_remove_glue_t;

/*
 * NOTE:
 *     This is EVIL.  The ONLY time you cast from a const is when calling some
 *     legacy function that does not require const:
 *          - you KNOW does not modify the data
 *          - you can't change
 *     That is why this is provided.  In that situation, the legacy API should
 *     have been written to accept const argument.  If you can change the 
 *     function you are calling to accept const, do THAT and DO NOT use this
 *     HACK.
 */
static inline void *
const_drop (const void *ptr)
{
    const_remove_glue_t cg;
    cg.cg_cvp = ptr;
    return cg.cg_vp;
}

/* Macros for min/max. */
#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif /* MIN */
#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif  /* MAX */


/* 
 * PLEASE BE SURE YOU REALLY MEAN TO DO THIS.
 * 
 * The MIPS compiler constantly complains about 
 * potential alignment problems, but 99% of the time 
 * they are spurious and just mean we can't have -Werror on.
 * 
 * This macro hushes the compiler's warning by first casting 
 * any cast to a void.
 */

#define QUIET_CAST(_type, _ptr) 	\
	((_type) (uintptr_t) (void *) (_ptr)) /**< Internal use only.   */

#ifndef __DECONST
#define __DECONST(_type, _ptr) 	\
	((_type) (uintptr_t) (const void *) (_ptr)) /**< Internal use only.   */
#endif

#define QUIET_CONST_CAST(_type, _ptr) __DECONST(_type, _ptr) /**< Internal use only.   */


#endif /* __JNX_AUX_TYPES_H__ */
