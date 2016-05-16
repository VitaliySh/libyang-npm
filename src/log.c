/**
 * @file log.c
 * @author Radek Krejci <rkrejci@cesnet.cz>
 * @brief libyang logger implementation
 *
 * Copyright (c) 2015 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#define _GNU_SOURCE
#define _BSD_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "common.h"
#include "tree_internal.h"

extern LY_ERR ly_errno_int;
volatile uint8_t ly_log_level = LY_LLERR;
static void (*ly_log_clb)(LY_LOG_LEVEL level, const char *msg, const char *path);
static volatile int path_flag = 1;

API void
ly_verb(LY_LOG_LEVEL level)
{
    ly_log_level = level;
}

API void
ly_set_log_clb(void (*clb)(LY_LOG_LEVEL level, const char *msg, const char *path), int path)
{
    ly_log_clb = clb;
    path_flag = path;
}

API void
(*ly_get_log_clb(void))(LY_LOG_LEVEL, const char *, const char *)
{
    return ly_log_clb;
}

static void
log_vprintf(LY_LOG_LEVEL level, uint8_t hide, const char *format, const char *path, va_list args)
{
    char *msg, *bufdup = NULL;
    int free_flag = 0;

    if (&ly_errno == &ly_errno_int) {
        msg = "Internal logger error";
    } else if (!format) {
        /* postponed print of path related to the previous error, do not rewrite stored original message */
        msg = NULL;
        if (asprintf(&msg, "Path related to the last error: \"%s\".", path) == -1) {
            msg = "Internal logger error (asprint() failed).";
        } else {
            free_flag = 1;
        }
    } else {
        if (level == LY_LLERR) {
            /* store error message into msg buffer ... */
            msg = ((struct ly_err *)&ly_errno)->msg;
        } else if (!hide) {
            /* other messages are stored in working string buffer and not available for later access */
            msg = ((struct ly_err *)&ly_errno)->buf;
            if (ly_buf_used && msg[0]) {
                bufdup = strndup(msg, LY_BUF_SIZE - 1);
            }
        } else { /* hide */
            return;
        }
        vsnprintf(msg, LY_BUF_SIZE - 1, format, args);
        msg[LY_BUF_SIZE - 1] = '\0';
    }

    if (level == LY_LLERR) {
        if (!path) {
            /* erase previous path */
            ((struct ly_err *)&ly_errno)->path_index = LY_BUF_SIZE - 1;
            ((struct ly_err *)&ly_errno)->path_obj = NULL;
        }

        /* if the error-app-tag should be set, do it after calling LOGVAL */
        ((struct ly_err *)&ly_errno)->apptag[0] = '\0';
    }

    if (hide) {
        return;
    }

    if (ly_log_clb) {
        ly_log_clb(level, msg, path);
    } else {
        fprintf(stderr, "libyang[%d]: %s%s", level, msg, path ? " " : "\n");
        if (path) {
            fprintf(stderr, "(path: %s)\n", path);
        }
    }

    if (free_flag) {
        free(msg);
    } else if (bufdup) {
        /* return previous internal buffer content */
        strncpy(msg, bufdup, LY_BUF_SIZE - 1);
        free(bufdup);
    }
}

void
ly_log(LY_LOG_LEVEL level, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    log_vprintf(level, 0, format, NULL, ap);
    va_end(ap);
}

const char *ly_errs[] = {
/* LYE_SUCCESS */      "",
/* LYE_XML_MISS */     "Missing %s \"%s\".",
/* LYE_XML_INVAL */    "Invalid %s.",
/* LYE_XML_INCHAR */   "Encountered invalid character sequence \"%.10s\".",

/* LYE_EOF */          "Unexpected end of input data.",
/* LYE_INSTMT */       "Invalid keyword \"%s\".",
/* LYE_INCHILDSTMT */  "Invalid keyword \"%s\" as a child to \"%s\".",
/* LYE_INID */         "Invalid identifier \"%s\" (%s).",
/* LYE_INDATE */       "Invalid date format of \"%s\", \"YYYY-MM-DD\" expected.",
/* LYE_INARG */        "Invalid value \"%s\" of \"%s\".",
/* LYE_MISSSTMT */     "Missing keyword \"%s\".",
/* LYE_MISSCHILDSTMT */ "Missing keyword \"%s\" as a child to \"%s\".",
/* LYE_MISSARG */      "Missing argument \"%s\" to keyword \"%s\".",
/* LYE_TOOMANY */      "Too many instances of \"%s\" in \"%s\".",
/* LYE_DUPID */        "Duplicated %s identifier \"%s\".",
/* LYE_DUPLEAFLIST */  "Duplicated instance of \"%s\" leaf-list (\"%s\").",
/* LYE_DUPLIST */      "Duplicated instance of \"%s\" list.",
/* LYE_NOUNIQ */       "Unique data leaf(s) \"%s\" not satisfied in \"%s\" and \"%s\".",
/* LYE_ENUM_DUPVAL */  "The value \"%d\" of \"%s\" enum has already been assigned to another enum value.",
/* LYE_ENUM_DUPNAME */ "The enum name \"%s\" has already been assigned to another enum.",
/* LYE_ENUM_WS */      "The enum name \"%s\" includes invalid leading or trailing whitespaces.",
/* LYE_BITS_DUPVAL */  "The position \"%d\" of \"%s\" bits has already been used to another named bit.",
/* LYE_BITS_DUPNAME */ "The bit name \"%s\" has already been assigned to another bit.",
/* LYE_INMOD */        "Module name \"%s\" refers to an unknown module.",
/* LYE_INMOD_LEN */    "Module name \"%.*s\" refers to an unknown module.",
/* LYE_KEY_NLEAF */    "Key \"%s\" is not a leaf.",
/* LYE_KEY_TYPE */     "Key \"%s\" must not be the built-in type \"empty\".",
/* LYE_KEY_CONFIG */   "The \"config\" value of the \"%s\" key differs from its list config value.",
/* LYE_KEY_MISS */     "Leaf \"%s\" defined as key in a list not found.",
/* LYE_KEY_DUP */      "Key identifier \"%s\" is not unique.",
/* LYE_INREGEX */      "Regular expression \"%s\" is not valid (%s).",
/* LYE_INRESOLV */     "Failed to resolve %s \"%s\".",
/* LYE_INSTATUS */     "A \"%s\" definition %s references \"%s\" definition %s.",

/* LYE_OBSDATA */      "Obsolete data \"%s\" instantiated.",
/* LYE_OBSTYPE */      "Data node \"%s\" with obsolete type \"%s\" instantiated.",
/* LYE_NORESOLV */     "No resolvents found for \"%s\".",
/* LYE_INELEM */       "Unknown element \"%s\".",
/* LYE_INELEM_LEN */   "Unknown element \"%.*s\".",
/* LYE_MISSELEM */     "Missing required element \"%s\" in \"%s\".",
/* LYE_INVAL */        "Invalid value \"%s\" in \"%s\" element.",
/* LYE_INVALATTR */    "Invalid \"%s\" attribute value \"%s\".",
/* LYE_INATTR */       "Invalid attribute \"%s\" in \"%s\" element.",
/* LYE_MISSATTR */     "Missing attribute \"%s\" in \"%s\" element.",
/* LYE_NOCONSTR */     "Value \"%s\" does not satisfy a constraint (range, length, or pattern).",
/* LYE_INCHAR */       "Unexpected character(s) '%c' (%.15s).",
/* LYE_INPRED */       "Predicate resolution failed on \"%s\".",
/* LYE_MCASEDATA */    "Data for more than one case branch of \"%s\" choice present.",
/* LYE_NOMUST */       "Must condition \"%s\" not satisfied.",
/* LYE_NOWHEN */       "When condition \"%s\" not satisfied.",
/* LYE_INORDER */      "Invalid order of elements \"%s\" and \"%s\".",
/* LYE_INWHEN */       "Irresolvable when condition \"%s\".",
/* LYE_NOMIN */        "Too few \"%s\" elements.",
/* LYE_NOMAX */        "Too many \"%s\" elements.",
/* LYE_NOREQINS */     "Required instance of \"%s\" does not exists.",
/* LYE_NOLEAFREF */    "Leafref \"%s\" of value \"%s\" points to a non-existing leaf.",
/* LYE_NOMANDCHOICE */ "Mandatory choice \"%s\" missing a case branch.",

/* LYE_XPATH_INTOK */  "Unexpected XPath token %s (%.15s).",
/* LYE_XPATH_EOF */    "Unexpected XPath expression end.",
/* LYE_XPATH_INOP_1 */ "Cannot apply XPath operation %s on %s.",
/* LYE_XPATH_INOP_2 */ "Cannot apply XPath operation %s on %s and %s.",
/* LYE_XPATH_INCTX */  "Invalid context type %s in %s.",
/* LYE_XPATH_INARGCOUNT */ "Invalid number of arguments (%d) for the XPath function %s.",
/* LYE_XPATH_INARGTYPE */ "Wrong type of argument #%d (%s) for the XPath function %s.",

/* LYE_PATH_INCHAR */  "Unexpected character(s) '%c' (%s).",
/* LYE_PATH_INMOD */   "Module not found.",
/* LYE_PATH_MISSMOD */ "Missing module name.",
/* LYE_PATH_INNODE */  "Schema node not found.",
/* LYE_PATH_INKEY */   "List key not found or on incorrect position (%s).",
/* LYE_PATH_MISSKEY */ "Not all list keys specified (%s).",
/* LYE_PATH_EXISTS */  "Node already exists.",
/* LYE_PATH_MISSPAR */ "Parent does not exist.",
};

static const LY_VECODE ecode2vecode[] = {
    LYVE_SUCCESS,      /* LYE_SUCCESS */

    LYVE_XML_MISS,     /* LYE_XML_MISS */
    LYVE_XML_INVAL,    /* LYE_XML_INVAL */
    LYVE_XML_INCHAR,   /* LYE_XML_INCHAR */

    LYVE_EOF,          /* LYE_EOF */
    LYVE_INSTMT,       /* LYE_INSTMT */
    LYVE_INSTMT,       /* LYE_INCHILDSTMT */
    LYVE_INID,         /* LYE_INID */
    LYVE_INDATE,       /* LYE_INDATE */
    LYVE_INARG,        /* LYE_INARG */
    LYVE_MISSSTMT,     /* LYE_MISSCHILDSTMT */
    LYVE_MISSSTMT,     /* LYE_MISSSTMT */
    LYVE_MISSARG,      /* LYE_MISSARG */
    LYVE_TOOMANY,      /* LYE_TOOMANY */
    LYVE_DUPID,        /* LYE_DUPID */
    LYVE_DUPLEAFLIST,  /* LYE_DUPLEAFLIST */
    LYVE_DUPLIST,      /* LYE_DUPLIST */
    LYVE_NOUNIQ,       /* LYE_NOUNIQ */
    LYVE_ENUM_DUPVAL,  /* LYE_ENUM_DUPVAL */
    LYVE_ENUM_DUPNAME, /* LYE_ENUM_DUPNAME */
    LYVE_ENUM_WS,      /* LYE_ENUM_WS */
    LYVE_BITS_DUPVAL,  /* LYE_BITS_DUPVAL */
    LYVE_BITS_DUPNAME, /* LYE_BITS_DUPNAME */
    LYVE_INMOD,        /* LYE_INMOD */
    LYVE_INMOD,        /* LYE_INMOD_LEN */
    LYVE_KEY_NLEAF,    /* LYE_KEY_NLEAF */
    LYVE_KEY_TYPE,     /* LYE_KEY_TYPE */
    LYVE_KEY_CONFIG,   /* LYE_KEY_CONFIG */
    LYVE_KEY_MISS,     /* LYE_KEY_MISS */
    LYVE_KEY_DUP,      /* LYE_KEY_DUP */
    LYVE_INREGEX,      /* LYE_INREGEX */
    LYVE_INRESOLV,     /* LYE_INRESOLV */
    LYVE_INSTATUS,     /* LYE_INSTATUS */

    LYVE_OBSDATA,      /* LYE_OBSDATA */
    LYVE_OBSDATA,      /* LYE_OBSTYPE */
    LYVE_NORESOLV,     /* LYE_NORESOLV */
    LYVE_INELEM,       /* LYE_INELEM */
    LYVE_INELEM,       /* LYE_INELEM_LEN */
    LYVE_MISSELEM,     /* LYE_MISSELEM */
    LYVE_INVAL,        /* LYE_INVAL */
    LYVE_INVALATTR,    /* LYE_INVALATTR */
    LYVE_INATTR,       /* LYE_INATTR */
    LYVE_MISSATTR,     /* LYE_MISSATTR */
    LYVE_NOCONSTR,     /* LYE_NOCONSTR */
    LYVE_INCHAR,       /* LYE_INCHAR */
    LYVE_INPRED,       /* LYE_INPRED */
    LYVE_MCASEDATA,    /* LYE_MCASEDATA */
    LYVE_NOMUST,       /* LYE_NOMUST */
    LYVE_NOWHEN,       /* LYE_NOWHEN */
    LYVE_INORDER,      /* LYE_INORDER */
    LYVE_INWHEN,       /* LYE_INWHEN */
    LYVE_NOMIN,        /* LYE_NOMIN */
    LYVE_NOMAX,        /* LYE_NOMAX */
    LYVE_NOREQINS,     /* LYE_NOREQINS */
    LYVE_NOLEAFREF,    /* LYE_NOLEAFREF */
    LYVE_NOMANDCHOICE, /* LYE_NOMANDCHOICE */

    LYVE_XPATH_INTOK,  /* LYE_XPATH_INTOK */
    LYVE_XPATH_EOF,    /* LYE_XPATH_EOF */
    LYVE_XPATH_INOP,   /* LYE_XPATH_INOP_1 */
    LYVE_XPATH_INOP,   /* LYE_XPATH_INOP_2 */
    LYVE_XPATH_INCTX,  /* LYE_XPATH_INCTX */
    LYVE_XPATH_INARGCOUNT, /* LYE_XPATH_INARGCOUNT */
    LYVE_XPATH_INARGTYPE, /* LYE_XPATH_INARGTYPE */

    LYVE_PATH_INCHAR,  /* LYE_PATH_INCHAR */
    LYVE_PATH_INMOD,   /* LYE_PATH_INMOD */
    LYVE_PATH_MISSMOD, /* LYE_PATH_MISSMOD */
    LYVE_PATH_INNODE,  /* LYE_PATH_INNODE */
    LYVE_PATH_INKEY,   /* LYE_PATH_INKEY */
    LYVE_PATH_MISSKEY, /* LYE_PATH_MISSKEY */
    LYVE_PATH_EXISTS,  /* LYE_PATH_EXISTS */
    LYVE_PATH_MISSPAR, /* LYE_PATH_MISSPAR */
};


uint8_t *ly_vlog_hide_location(void);
void
ly_vlog_hide(int hide)
{
    (*ly_vlog_hide_location()) = hide ? 1 : 0;
}

void
ly_vlog_build_path_reverse(enum LY_VLOG_ELEM elem_type, const void *elem, char *path, uint16_t *index)
{
    int i;
    struct lys_node_list *slist;
    struct lyd_node *dlist, *diter;
    const char *name, *prefix = NULL;
    size_t len;

    while (elem) {
        switch (elem_type) {
        case LY_VLOG_XML:
            name = ((struct lyxml_elem *)elem)->name;
            prefix = ((struct lyxml_elem *)elem)->ns ? ((struct lyxml_elem *)elem)->ns->prefix : NULL;
            elem = ((struct lyxml_elem *)elem)->parent;
            break;
        case LY_VLOG_LYS:
            name = ((struct lys_node *)elem)->name;
            if (!((struct lys_node *)elem)->parent ||
                    lys_node_module((struct lys_node *)elem) != lys_node_module(lys_parent((struct lys_node *)elem))) {
                prefix = lys_node_module((struct lys_node *)elem)->name;
            }
            do {
                elem = lys_parent((struct lys_node *)elem);
            } while (elem && (((struct lys_node *)elem)->nodetype == LYS_USES));
            break;
        case LY_VLOG_LYD:
            name = ((struct lyd_node *)elem)->schema->name;
            if (!((struct lyd_node *)elem)->parent ||
                    lyd_node_module((struct lyd_node *)elem) != lyd_node_module(((struct lyd_node *)elem)->parent)) {
                prefix = lyd_node_module((struct lyd_node *)elem)->name;
            }

            /* handle predicates (keys) in case of lists */
            if (((struct lyd_node *)elem)->schema->nodetype == LYS_LIST) {
                dlist = (struct lyd_node *)elem;
                slist = (struct lys_node_list *)((struct lyd_node *)elem)->schema;
                for (i = slist->keys_size - 1; i > -1; i--) {
                    LY_TREE_FOR(dlist->child, diter) {
                        if (diter->schema == (struct lys_node *)slist->keys[i]) {
                            break;
                        }
                    }
                    if (diter && ((struct lyd_node_leaf_list *)diter)->value_str) {
                        (*index) -= 2;
                        memcpy(&path[(*index)], "']", 2);
                        len = strlen(((struct lyd_node_leaf_list *)diter)->value_str);
                        (*index) -= len;
                        memcpy(&path[(*index)], ((struct lyd_node_leaf_list *)diter)->value_str, len);
                        (*index) -= 2;
                        memcpy(&path[(*index)], "='", 2);
                        len = strlen(diter->schema->name);
                        (*index) -= len;
                        memcpy(&path[(*index)], diter->schema->name, len);
                        if (dlist->schema->module != diter->schema->module) {
                            path[--(*index)] = ':';
                            len = strlen(diter->schema->module->name);
                            (*index) -= len;
                            memcpy(&path[(*index)], diter->schema->module->name, len);
                        }
                        path[--(*index)] = '[';
                    }
                }
            }

            elem = ((struct lyd_node *)elem)->parent;
            break;
        case LY_VLOG_STR:
            len = strlen((const char *)elem) + 1;
            if (len > LY_BUF_SIZE) {
                len = LY_BUF_SIZE - 1;
            }
            (*index) = LY_BUF_SIZE - len;
            memcpy(&path[(*index)], (const char *)elem, len - 1);
            return;
        default:
            /* shouldn't be here */
            LOGINT;
            return;
        }
        len = strlen(name);
        (*index) = (*index) - len;
        memcpy(&path[(*index)], name, len);
        if (prefix) {
            path[--(*index)] = ':';
            len = strlen(prefix);
            (*index) = (*index) - len;
            memcpy(&path[(*index)], prefix, len);
        }
        path[--(*index)] = '/';
    }
}

void
ly_vlog(LY_ECODE code, enum LY_VLOG_ELEM elem_type, const void *elem, ...)
{
    va_list ap;
    const char *fmt;
    char* path = NULL;
    uint16_t *index = NULL;
    const void *iter = elem;

    ly_errno = LY_EVALID;

    if ((code == LYE_PATH) && !path_flag) {
        return;
    }
    if (code > 0) {
        ly_vecode = ecode2vecode[code];
    }

    if (!path_flag) {
        goto log;
    }

    /* resolve path */
    path = ((struct ly_err *)&ly_errno)->path;
    index = &((struct ly_err *)&ly_errno)->path_index;
    if (elem_type) { /* != LY_VLOG_NONE */
        /* check if the path is equal to the last one */
        if (iter == ((struct ly_err *)&ly_errno)->path_obj) {
            /* path is up-to-date (same as the last one) */
            goto log;
        }

        /* update path */
        (*index) = LY_BUF_SIZE - 1;
        path[(*index)] = '\0';
        if (!iter) {
            /* top-level */
            path[--(*index)] = '/';
        } else {
            ly_vlog_build_path_reverse(elem_type, iter, path, index);
            /* store the source of the path */
            ((struct ly_err *)&ly_errno)->path_obj = elem;
        }
    } else {
        /* erase path, the rest will be erased by log_vprintf() since it will get NULL path parameter */
        path[(*index)] = '\0';
    }

log:
    va_start(ap, elem);
    switch (code) {
    case LYE_SPEC:
        fmt = va_arg(ap, char *);
        log_vprintf(LY_LLERR, (*ly_vlog_hide_location()), fmt, index && path[(*index)] ? &path[(*index)] : NULL, ap);
        break;
    case LYE_PATH:
        log_vprintf(LY_LLERR, (*ly_vlog_hide_location()), NULL, &path[(*index)], ap);
        break;
    default:
        log_vprintf(LY_LLERR, (*ly_vlog_hide_location()), ly_errs[code],
                    index && path[(*index)] ? &path[(*index)] : NULL, ap);
        break;
    }
    va_end(ap);
}