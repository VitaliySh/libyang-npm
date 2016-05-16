/**
 * @file resolve.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief libyang resolve functions
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

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "libyang.h"
#include "resolve.h"
#include "common.h"
#include "xpath.h"
#include "parser.h"
#include "parser_yang.h"
#include "xml_internal.h"
#include "dict_private.h"
#include "tree_internal.h"

/**
 * @brief Parse an identifier.
 *
 * ;; An identifier MUST NOT start with (('X'|'x') ('M'|'m') ('L'|'l'))
 * identifier          = (ALPHA / "_")
 *                       *(ALPHA / DIGIT / "_" / "-" / ".")
 *
 * @param[in] id Identifier to use.
 *
 * @return Number of characters successfully parsed.
 */
int
parse_identifier(const char *id)
{
    int parsed = 0;

    assert(id);

    if (((id[0] == 'x') || (id[0] == 'X'))
            && (id[0] && ((id[1] == 'm') || (id[0] == 'M')))
            && (id[1] && ((id[2] == 'l') || (id[2] == 'L')))) {
        return -parsed;
    }

    if (!isalpha(id[0]) && (id[0] != '_')) {
        return -parsed;
    }

    ++parsed;
    ++id;

    while (isalnum(id[0]) || (id[0] == '_') || (id[0] == '-') || (id[0] == '.')) {
        ++parsed;
        ++id;
    }

    return parsed;
}

/**
 * @brief Parse a node-identifier.
 *
 * node-identifier     = [module-name ":"] identifier
 *
 * @param[in] id Identifier to use.
 * @param[out] mod_name Points to the module name, NULL if there is not any.
 * @param[out] mod_name_len Length of the module name, 0 if there is not any.
 * @param[out] name Points to the node name.
 * @param[out] nam_len Length of the node name.
 *
 * @return Number of characters successfully parsed,
 *         positive on success, negative on failure.
 */
static int
parse_node_identifier(const char *id, const char **mod_name, int *mod_name_len, const char **name, int *nam_len)
{
    int parsed = 0, ret;

    assert(id);
    if (mod_name) {
        *mod_name = NULL;
    }
    if (mod_name_len) {
        *mod_name_len = 0;
    }
    if (name) {
        *name = NULL;
    }
    if (nam_len) {
        *nam_len = 0;
    }

    if ((ret = parse_identifier(id)) < 1) {
        return ret;
    }

    if (mod_name) {
        *mod_name = id;
    }
    if (mod_name_len) {
        *mod_name_len = ret;
    }

    parsed += ret;
    id += ret;

    /* there is prefix */
    if (id[0] == ':') {
        ++parsed;
        ++id;

    /* there isn't */
    } else {
        if (name && mod_name) {
            *name = *mod_name;
        }
        if (mod_name) {
            *mod_name = NULL;
        }

        if (nam_len && mod_name_len) {
            *nam_len = *mod_name_len;
        }
        if (mod_name_len) {
            *mod_name_len = 0;
        }

        return parsed;
    }

    /* identifier (node name) */
    if ((ret = parse_identifier(id)) < 1) {
        return -parsed+ret;
    }

    if (name) {
        *name = id;
    }
    if (nam_len) {
        *nam_len = ret;
    }

    return parsed+ret;
}

/**
 * @brief Parse a path-predicate (leafref).
 *
 * path-predicate      = "[" *WSP path-equality-expr *WSP "]"
 * path-equality-expr  = node-identifier *WSP "=" *WSP path-key-expr
 *
 * @param[in] id Identifier to use.
 * @param[out] prefix Points to the prefix, NULL if there is not any.
 * @param[out] pref_len Length of the prefix, 0 if there is not any.
 * @param[out] name Points to the node name.
 * @param[out] nam_len Length of the node name.
 * @param[out] path_key_expr Points to the path-key-expr.
 * @param[out] pke_len Length of the path-key-expr.
 * @param[out] has_predicate Flag to mark whether there is another predicate following.
 *
 * @return Number of characters successfully parsed,
 *         positive on success, negative on failure.
 */
static int
parse_path_predicate(const char *id, const char **prefix, int *pref_len, const char **name, int *nam_len,
                     const char **path_key_expr, int *pke_len, int *has_predicate)
{
    const char *ptr;
    int parsed = 0, ret;

    assert(id);
    if (prefix) {
        *prefix = NULL;
    }
    if (pref_len) {
        *pref_len = 0;
    }
    if (name) {
        *name = NULL;
    }
    if (nam_len) {
        *nam_len = 0;
    }
    if (path_key_expr) {
        *path_key_expr = NULL;
    }
    if (pke_len) {
        *pke_len = 0;
    }
    if (has_predicate) {
        *has_predicate = 0;
    }

    if (id[0] != '[') {
        return -parsed;
    }

    ++parsed;
    ++id;

    while (isspace(id[0])) {
        ++parsed;
        ++id;
    }

    if ((ret = parse_node_identifier(id, prefix, pref_len, name, nam_len)) < 1) {
        return -parsed+ret;
    }

    parsed += ret;
    id += ret;

    while (isspace(id[0])) {
        ++parsed;
        ++id;
    }

    if (id[0] != '=') {
        return -parsed;
    }

    ++parsed;
    ++id;

    while (isspace(id[0])) {
        ++parsed;
        ++id;
    }

    if ((ptr = strchr(id, ']')) == NULL) {
        return -parsed;
    }

    --ptr;
    while (isspace(ptr[0])) {
        --ptr;
    }
    ++ptr;

    ret = ptr-id;
    if (path_key_expr) {
        *path_key_expr = id;
    }
    if (pke_len) {
        *pke_len = ret;
    }

    parsed += ret;
    id += ret;

    while (isspace(id[0])) {
        ++parsed;
        ++id;
    }

    assert(id[0] == ']');

    if (id[1] == '[') {
        *has_predicate = 1;
    }

    return parsed+1;
}

/**
 * @brief Parse a path-key-expr (leafref). First call parses "current()", all
 *        the ".." and the first node-identifier, other calls parse a single
 *        node-identifier each.
 *
 * path-key-expr       = current-function-invocation *WSP "/" *WSP
 *                       rel-path-keyexpr
 * rel-path-keyexpr    = 1*(".." *WSP "/" *WSP)
 *                       *(node-identifier *WSP "/" *WSP)
 *                       node-identifier
 *
 * @param[in] id Identifier to use.
 * @param[out] prefix Points to the prefix, NULL if there is not any.
 * @param[out] pref_len Length of the prefix, 0 if there is not any.
 * @param[out] name Points to the node name.
 * @param[out] nam_len Length of the node name.
 * @param[out] parent_times Number of ".." in the path. Must be 0 on the first call,
 *                          must not be changed between consecutive calls.
 * @return Number of characters successfully parsed,
 *         positive on success, negative on failure.
 */
static int
parse_path_key_expr(const char *id, const char **prefix, int *pref_len, const char **name, int *nam_len,
                    int *parent_times)
{
    int parsed = 0, ret, par_times = 0;

    assert(id);
    assert(parent_times);
    if (prefix) {
        *prefix = NULL;
    }
    if (pref_len) {
        *pref_len = 0;
    }
    if (name) {
        *name = NULL;
    }
    if (nam_len) {
        *nam_len = 0;
    }

    if (!*parent_times) {
        /* current-function-invocation *WSP "/" *WSP rel-path-keyexpr */
        if (strncmp(id, "current()", 9)) {
            return -parsed;
        }

        parsed += 9;
        id += 9;

        while (isspace(id[0])) {
            ++parsed;
            ++id;
        }

        if (id[0] != '/') {
            return -parsed;
        }

        ++parsed;
        ++id;

        while (isspace(id[0])) {
            ++parsed;
            ++id;
        }

        /* rel-path-keyexpr */
        if (strncmp(id, "..", 2)) {
            return -parsed;
        }
        ++par_times;

        parsed += 2;
        id += 2;

        while (isspace(id[0])) {
            ++parsed;
            ++id;
        }
    }

    /* 1*(".." *WSP "/" *WSP) *(node-identifier *WSP "/" *WSP) node-identifier
     *
     * first parent reference with whitespaces already parsed
     */
    if (id[0] != '/') {
        return -parsed;
    }

    ++parsed;
    ++id;

    while (isspace(id[0])) {
        ++parsed;
        ++id;
    }

    while (!strncmp(id, "..", 2) && !*parent_times) {
        ++par_times;

        parsed += 2;
        id += 2;

        while (isspace(id[0])) {
            ++parsed;
            ++id;
        }

        if (id[0] != '/') {
            return -parsed;
        }

        ++parsed;
        ++id;

        while (isspace(id[0])) {
            ++parsed;
            ++id;
        }
    }

    if (!*parent_times) {
        *parent_times = par_times;
    }

    /* all parent references must be parsed at this point */
    if ((ret = parse_node_identifier(id, prefix, pref_len, name, nam_len)) < 1) {
        return -parsed+ret;
    }

    parsed += ret;
    id += ret;

    return parsed;
}

/**
 * @brief Parse path-arg (leafref).
 *
 * path-arg            = absolute-path / relative-path
 * absolute-path       = 1*("/" (node-identifier *path-predicate))
 * relative-path       = 1*(".." "/") descendant-path
 *
 * @param[in] id Identifier to use.
 * @param[out] prefix Points to the prefix, NULL if there is not any.
 * @param[out] pref_len Length of the prefix, 0 if there is not any.
 * @param[out] name Points to the node name.
 * @param[out] nam_len Length of the node name.
 * @param[out] parent_times Number of ".." in the path. Must be 0 on the first call,
 *                          must not be changed between consecutive calls. -1 if the
 *                          path is relative.
 * @param[out] has_predicate Flag to mark whether there is a predicate specified.
 *
 * @return Number of characters successfully parsed,
 *         positive on success, negative on failure.
 */
static int
parse_path_arg(const char *id, const char **prefix, int *pref_len, const char **name, int *nam_len, int *parent_times,
               int *has_predicate)
{
    int parsed = 0, ret, par_times = 0;

    assert(id);
    assert(parent_times);
    if (prefix) {
        *prefix = NULL;
    }
    if (pref_len) {
        *pref_len = 0;
    }
    if (name) {
        *name = NULL;
    }
    if (nam_len) {
        *nam_len = 0;
    }
    if (has_predicate) {
        *has_predicate = 0;
    }

    if (!*parent_times && !strncmp(id, "..", 2)) {
        ++par_times;

        parsed += 2;
        id += 2;

        while (!strncmp(id, "/..", 3)) {
            ++par_times;

            parsed += 3;
            id += 3;
        }
    }

    if (!*parent_times) {
        if (par_times) {
            *parent_times = par_times;
        } else {
            *parent_times = -1;
        }
    }

    if (id[0] != '/') {
        return -parsed;
    }

    /* skip '/' */
    ++parsed;
    ++id;

    /* node-identifier ([prefix:]identifier) */
    if ((ret = parse_node_identifier(id, prefix, pref_len, name, nam_len)) < 1) {
        return -parsed-ret;
    }

    parsed += ret;
    id += ret;

    /* there is no predicate */
    if ((id[0] == '/') || !id[0]) {
        return parsed;
    } else if (id[0] != '[') {
        return -parsed;
    }

    if (has_predicate) {
        *has_predicate = 1;
    }

    return parsed;
}

/**
 * @brief Parse instance-identifier in JSON data format. That means that prefixes
 *        (which are mandatory) are actually model names.
 *
 * instance-identifier = 1*("/" (node-identifier *predicate))
 *
 * @param[in] id Identifier to use.
 * @param[out] model Points to the model name.
 * @param[out] mod_len Length of the model name.
 * @param[out] name Points to the node name.
 * @param[out] nam_len Length of the node name.
 * @param[out] has_predicate Flag to mark whether there is a predicate specified.
 *
 * @return Number of characters successfully parsed,
 *         positive on success, negative on failure.
 */
static int
parse_instance_identifier(const char *id, const char **model, int *mod_len, const char **name, int *nam_len,
                          int *has_predicate)
{
    int parsed = 0, ret;

    assert(id);
    if (model) {
        *model = NULL;
    }
    if (mod_len) {
        *mod_len = 0;
    }
    if (name) {
        *name = NULL;
    }
    if (nam_len) {
        *nam_len = 0;
    }
    if (has_predicate) {
        *has_predicate = 0;
    }

    if (id[0] != '/') {
        return -parsed;
    }

    ++parsed;
    ++id;

    if ((ret = parse_node_identifier(id, model, mod_len, name, nam_len)) < 1) {
        return -parsed+ret;
    } else if (model && !*model) {
        return -parsed;
    }

    parsed += ret;
    id += ret;

    if ((id[0] == '[') && has_predicate) {
        *has_predicate = 1;
    }

    return parsed;
}

/**
 * @brief Parse predicate (instance-identifier) in JSON data format. That means that prefixes
 *        (which are mandatory) are actually model names.
 *
 * predicate           = "[" *WSP (predicate-expr / pos) *WSP "]"
 * predicate-expr      = (node-identifier / ".") *WSP "=" *WSP
 *                       ((DQUOTE string DQUOTE) /
 *                        (SQUOTE string SQUOTE))
 * pos                 = non-negative-integer-value
 *
 * @param[in] id Identifier to use.
 * @param[out] model Points to the model name.
 * @param[out] mod_len Length of the model name.
 * @param[out] name Points to the node name. Can be identifier (from node-identifier), "." or pos.
 * @param[out] nam_len Length of the node name.
 * @param[out] value Value the node-identifier must have (string from the grammar),
 *                   NULL if there is not any.
 * @param[out] val_len Length of the value, 0 if there is not any.
 * @param[out] has_predicate Flag to mark whether there is a predicate specified.
 *
 * @return Number of characters successfully parsed,
 *         positive on success, negative on failure.
 */
static int
parse_predicate(const char *id, const char **model, int *mod_len, const char **name, int *nam_len,
                const char **value, int *val_len, int *has_predicate)
{
    const char *ptr;
    int parsed = 0, ret;
    char quote;

    assert(id);
    if (model) {
        *model = NULL;
    }
    if (mod_len) {
        *mod_len = 0;
    }
    if (name) {
        *name = NULL;
    }
    if (nam_len) {
        *nam_len = 0;
    }
    if (value) {
        *value = NULL;
    }
    if (val_len) {
        *val_len = 0;
    }
    if (has_predicate) {
        *has_predicate = 0;
    }

    if (id[0] != '[') {
        return -parsed;
    }

    ++parsed;
    ++id;

    while (isspace(id[0])) {
        ++parsed;
        ++id;
    }

    /* pos */
    if (isdigit(id[0])) {
        if (name) {
            *name = id;
        }

        if (id[0] == '0') {
            ++parsed;
            ++id;

            if (isdigit(id[0])) {
                return -parsed;
            }
        }

        while (isdigit(id[0])) {
            ++parsed;
            ++id;
        }

        if (nam_len) {
            *nam_len = id-(*name);
        }

    /* "." */
    } else if (id[0] == '.') {
        if (name) {
            *name = id;
        }
        if (nam_len) {
            *nam_len = 1;
        }

        ++parsed;
        ++id;

    /* node-identifier */
    } else {
        if ((ret = parse_node_identifier(id, model, mod_len, name, nam_len)) < 1) {
            return -parsed+ret;
        } else if (model && !*model) {
            return -parsed;
        }

        parsed += ret;
        id += ret;
    }

    while (isspace(id[0])) {
        ++parsed;
        ++id;
    }

    if (id[0] != '=') {
        return -parsed;
    }

    ++parsed;
    ++id;

    while (isspace(id[0])) {
        ++parsed;
        ++id;
    }

    /* ((DQUOTE string DQUOTE) / (SQUOTE string SQUOTE)) */
    if ((id[0] == '\"') || (id[0] == '\'')) {
        quote = id[0];

        ++parsed;
        ++id;

        if ((ptr = strchr(id, quote)) == NULL) {
            return -parsed;
        }
        ret = ptr-id;

        if (value) {
            *value = id;
        }
        if (val_len) {
            *val_len = ret;
        }

        parsed += ret+1;
        id += ret+1;
    } else {
        return -parsed;
    }

    while (isspace(id[0])) {
        ++parsed;
        ++id;
    }

    if (id[0] != ']') {
        return -parsed;
    }

    ++parsed;
    ++id;

    if ((id[0] == '[') && has_predicate) {
        *has_predicate = 1;
    }

    return parsed;
}

/**
 * @brief Parse schema-nodeid.
 *
 * schema-nodeid       = absolute-schema-nodeid /
 *                       descendant-schema-nodeid
 * absolute-schema-nodeid = 1*("/" node-identifier)
 * descendant-schema-nodeid = ["." "/"]
 *                       node-identifier
 *                       absolute-schema-nodeid
 *
 * @param[in] id Identifier to use.
 * @param[out] mod_name Points to the module name, NULL if there is not any.
 * @param[out] mod_name_len Length of the module name, 0 if there is not any.
 * @param[out] name Points to the node name.
 * @param[out] nam_len Length of the node name.
 * @param[out] is_relative Flag to mark whether the nodeid is absolute or descendant. Must be -1
 *                         on the first call, must not be changed between consecutive calls.
 * @param[out] has_predicate Flag to mark whether there is a predicate specified. It cannot be
 *                           based on the grammar, in those cases use NULL.
 *
 * @return Number of characters successfully parsed,
 *         positive on success, negative on failure.
 */
int
parse_schema_nodeid(const char *id, const char **mod_name, int *mod_name_len, const char **name, int *nam_len,
                    int *is_relative, int *has_predicate)
{
    int parsed = 0, ret;

    assert(id);
    assert(is_relative);
    if (mod_name) {
        *mod_name = NULL;
    }
    if (mod_name_len) {
        *mod_name_len = 0;
    }
    if (name) {
        *name = NULL;
    }
    if (nam_len) {
        *nam_len = 0;
    }
    if (has_predicate) {
        *has_predicate = 0;
    }

    if (id[0] != '/') {
        if (*is_relative != -1) {
            return -parsed;
        } else {
            *is_relative = 1;
        }
        if (!strncmp(id, "./", 2)) {
            parsed += 2;
            id += 2;
        }
    } else {
        if (*is_relative == -1) {
            *is_relative = 0;
        }
        ++parsed;
        ++id;
    }

    if ((ret = parse_node_identifier(id, mod_name, mod_name_len, name, nam_len)) < 1) {
        return -parsed+ret;
    }

    parsed += ret;
    id += ret;

    if ((id[0] == '[') && has_predicate) {
        *has_predicate = 1;
    }

    return parsed;
}

/**
 * @brief Parse schema predicate (special format internally used).
 *
 * predicate           = "[" *WSP predicate-expr *WSP "]"
 * predicate-expr      = "." / identifier / key-with-value
 * key-with-value      = identifier *WSP "=" *WSP
 *                       ((DQUOTE string DQUOTE) /
 *                        (SQUOTE string SQUOTE))
 *
 * @param[in] id Identifier to use.
 * @param[out] name Points to the list key name.
 * @param[out] nam_len Length of \p name.
 * @param[out] value Points to the key value. If specified, key-with-value is expected.
 * @param[out] val_len Length of \p value.
 * @param[out] has_predicate Flag to mark whether there is another predicate specified.
 */
int
parse_schema_json_predicate(const char *id, const char **name, int *nam_len, const char **value, int *val_len,
                            int *has_predicate)
{
    const char *ptr;
    int parsed = 0, ret;
    char quote;

    assert(id);
    if (name) {
        *name = NULL;
    }
    if (nam_len) {
        *nam_len = 0;
    }
    if (value) {
        *value = NULL;
    }
    if (val_len) {
        *val_len = 0;
    }
    if (has_predicate) {
        *has_predicate = 0;
    }

    if (id[0] != '[') {
        return -parsed;
    }

    ++parsed;
    ++id;

    while (isspace(id[0])) {
        ++parsed;
        ++id;
    }

    /* identifier */
    if (id[0] == '.') {
        ret = 1;
    } else if ((ret = parse_identifier(id)) < 1) {
        return -parsed + ret;
    }
    if (name) {
        *name = id;
    }
    if (nam_len) {
        *nam_len = ret;
    }

    parsed += ret;
    id += ret;

    while (isspace(id[0])) {
        ++parsed;
        ++id;
    }

    /* there is value as well */
    if (id[0] == '=') {
        ++parsed;
        ++id;

        while (isspace(id[0])) {
            ++parsed;
            ++id;
        }

        /* ((DQUOTE string DQUOTE) / (SQUOTE string SQUOTE)) */
        if ((id[0] == '\"') || (id[0] == '\'')) {
            quote = id[0];

            ++parsed;
            ++id;

            if ((ptr = strchr(id, quote)) == NULL) {
                return -parsed;
            }
            ret = ptr - id;

            if (value) {
                *value = id;
            }
            if (val_len) {
                *val_len = ret;
            }

            parsed += ret + 1;
            id += ret + 1;
        } else {
            return -parsed;
        }

        while (isspace(id[0])) {
            ++parsed;
            ++id;
        }
    } else if (value) {
        /* if value was expected, it's mandatory */
        return -parsed;
    }

    if (id[0] != ']') {
        return -parsed;
    }

    ++parsed;
    ++id;

    if ((id[0] == '[') && has_predicate) {
        *has_predicate = 1;
    }

    return parsed;
}

/**
 * @brief Resolve (find) a data node based on a schema-nodeid.
 *
 * Used for resolving unique statements - so id is expected to be relative and local (without reference to a different
 * module).
 *
 */
struct lyd_node *
resolve_data_descendant_schema_nodeid(const char *nodeid, struct lyd_node *start)
{
    char *str, *token, *p;
    struct lyd_node *result = NULL, *iter;
    const struct lys_node *schema = NULL;
    int shorthand = 0;

    assert(nodeid && start);

    if (nodeid[0] == '/') {
        return NULL;
    }

    str = p = strdup(nodeid);
    if (!str) {
        LOGMEM;
        return NULL;
    }

    while (p) {
        token = p;
        p = strchr(p, '/');
        if (p) {
            *p = '\0';
            p++;
        }

        if (p) {
            /* inner node */
            if (resolve_descendant_schema_nodeid(token, schema ? schema->child : start->schema,
                                                 LYS_CONTAINER | LYS_CHOICE | LYS_CASE | LYS_LEAF, 0, 0, &schema)
                    || !schema) {
                result = NULL;
                break;
            }

            if (schema->nodetype & (LYS_CHOICE | LYS_CASE)) {
                continue;
            } else if (lys_parent(schema)->nodetype == LYS_CHOICE) {
                /* shorthand case */
                if (!shorthand) {
                    shorthand = 1;
                    schema = lys_parent(schema);
                    continue;
                } else {
                    shorthand = 0;
                    if (schema->nodetype == LYS_LEAF) {
                        /* should not be here, since we have leaf, which is not a shorthand nor final node */
                        result = NULL;
                        break;
                    }
                }
            }
        } else {
            /* final node */
            if (resolve_descendant_schema_nodeid(token, schema ? schema->child : start->schema, LYS_LEAF,
                                                 shorthand ? 0 : 1, 0, &schema)
                    || !schema) {
                result = NULL;
                break;
            }
        }
        LY_TREE_FOR(result ? result->child : start, iter) {
            if (iter->schema == schema) {
                /* move in data tree according to returned schema */
                result = iter;
                break;
            }
        }
        if (!iter) {
            /* instance not found */
            result = NULL;
            break;
        }
    }
    free(str);

    return result;
}

/*
 *  0 - ok (done)
 *  1 - continue
 *  2 - break
 * -1 - error
 */
static int
schema_nodeid_siblingcheck(const struct lys_node *sibling, int8_t *shorthand, const char *id,
                           const struct lys_module *module, const char *mod_name, int mod_name_len,
                           const struct lys_node **start)
{
    const struct lys_module *prefix_mod;
    int sh = 0;

    /* module check */
    prefix_mod = lys_get_import_module(module, NULL, 0, mod_name, mod_name_len);
    if (!prefix_mod) {
        return -1;
    }
    if (prefix_mod != lys_node_module(sibling)) {
        return 1;
    }

    /* check for shorthand cases - then 'start' does not change */
    if (lys_parent(sibling) && (lys_parent(sibling)->nodetype == LYS_CHOICE) && (sibling->nodetype != LYS_CASE)) {
        if (*shorthand != -1) {
            *shorthand = *shorthand ? 0 : 1;
        }
        sh = 1;
    }

    /* the result node? */
    if (!id[0]) {
        if (*shorthand == 1) {
            return 1;
        }
        return 0;
    }

    if (!sh) {
        /* move down the tree, if possible */
        if (sibling->nodetype & (LYS_LEAF | LYS_LEAFLIST | LYS_ANYXML)) {
            return -1;
        }
        *start = sibling->child;
    }

    return 2;
}

/* start - relative, module - absolute, -1 error, EXIT_SUCCESS ok (but ret can still be NULL), >0 unexpected char on ret - 1 */
int
resolve_augment_schema_nodeid(const char *nodeid, const struct lys_node *start, const struct lys_module *module,
                              const struct lys_node **ret)
{
    const char *name, *mod_name, *id;
    const struct lys_node *sibling;
    int r, nam_len, mod_name_len, is_relative = -1;
    int8_t shorthand = 0;
    /* resolved import module from the start module, it must match the next node-name-match sibling */
    const struct lys_module *start_mod;

    assert(nodeid && (start || module) && !(start && module) && ret);

    id = nodeid;

    if ((r = parse_schema_nodeid(id, &mod_name, &mod_name_len, &name, &nam_len, &is_relative, NULL)) < 1) {
        return ((id - nodeid) - r) + 1;
    }
    id += r;

    if ((is_relative && !start) || (!is_relative && !module)) {
        return -1;
    }

    /* descendant-schema-nodeid */
    if (is_relative) {
        module = start_mod = start->module;

    /* absolute-schema-nodeid */
    } else {
        start_mod = lys_get_import_module(module, NULL, 0, mod_name, mod_name_len);
        if (!start_mod) {
            return -1;
        }
        start = start_mod->data;
    }

    while (1) {
        sibling = NULL;
        while ((sibling = lys_getnext(sibling, lys_parent(start), start_mod,
                                      LYS_GETNEXT_WITHCHOICE | LYS_GETNEXT_WITHCASE | LYS_GETNEXT_WITHINOUT))) {
            /* name match */
            if (sibling->name && !strncmp(name, sibling->name, nam_len) && !sibling->name[nam_len]) {
                r = schema_nodeid_siblingcheck(sibling, &shorthand, id, module, mod_name, mod_name_len, &start);
                if (r == 0) {
                    *ret = sibling;
                    return EXIT_SUCCESS;
                } else if (r == 1) {
                    continue;
                } else if (r == 2) {
                    break;
                } else {
                    return -1;
                }
            }
        }

        /* no match */
        if (!sibling) {
            *ret = NULL;
            return EXIT_SUCCESS;
        }

        if ((r = parse_schema_nodeid(id, &mod_name, &mod_name_len, &name, &nam_len, &is_relative, NULL)) < 1) {
            return ((id - nodeid) - r) + 1;
        }
        id += r;
    }

    /* cannot get here */
    LOGINT;
    return -1;
}

/* unique, refine,
 * >0  - unexpected char on position (ret - 1),
 *  0  - ok (but ret can still be NULL),
 * -1  - error,
 * -2  - violated no_innerlist  */
int
resolve_descendant_schema_nodeid(const char *nodeid, const struct lys_node *start, int ret_nodetype,
                                 int check_shorthand, int no_innerlist, const struct lys_node **ret)
{
    const char *name, *mod_name, *id;
    const struct lys_node *sibling;
    int r, nam_len, mod_name_len, is_relative = -1;
    int8_t shorthand = check_shorthand ? 0 : -1;
    /* resolved import module from the start module, it must match the next node-name-match sibling */
    const struct lys_module *module;

    assert(nodeid && start && ret);
    assert(!(ret_nodetype & (LYS_USES | LYS_AUGMENT)) && ((ret_nodetype == LYS_GROUPING) || !(ret_nodetype & LYS_GROUPING)));

    id = nodeid;
    module = start->module;

    if ((r = parse_schema_nodeid(id, &mod_name, &mod_name_len, &name, &nam_len, &is_relative, NULL)) < 1) {
        return ((id - nodeid) - r) + 1;
    }
    id += r;

    if (!is_relative) {
        return -1;
    }

    while (1) {
        sibling = NULL;
        while ((sibling = lys_getnext(sibling, lys_parent(start), module,
                                      LYS_GETNEXT_WITHCHOICE | LYS_GETNEXT_WITHCASE))) {
            /* name match */
            if (sibling->name && !strncmp(name, sibling->name, nam_len) && !sibling->name[nam_len]) {
                r = schema_nodeid_siblingcheck(sibling, &shorthand, id, module, mod_name, mod_name_len, &start);
                if (r == 0) {
                    if (!(sibling->nodetype & ret_nodetype)) {
                        /* wrong node type, too bad */
                        continue;
                    }
                    *ret = sibling;
                    return EXIT_SUCCESS;
                } else if (r == 1) {
                    continue;
                } else if (r == 2) {
                    break;
                } else {
                    return -1;
                }
            }
        }

        /* no match */
        if (!sibling) {
            *ret = NULL;
            return EXIT_SUCCESS;
        } else if (no_innerlist && sibling->nodetype == LYS_LIST) {
            *ret = NULL;
            return -2;
        }

        if ((r = parse_schema_nodeid(id, &mod_name, &mod_name_len, &name, &nam_len, &is_relative, NULL)) < 1) {
            return ((id - nodeid) - r) + 1;
        }
        id += r;
    }

    /* cannot get here */
    LOGINT;
    return -1;
}

/* choice default */
int
resolve_choice_default_schema_nodeid(const char *nodeid, const struct lys_node *start, const struct lys_node **ret)
{
    /* cannot actually be a path */
    if (strchr(nodeid, '/')) {
        return -1;
    }

    return resolve_descendant_schema_nodeid(nodeid, start, LYS_NO_RPC_NOTIF_NODE, 1, 0, ret);
}

/* uses, -1 error, EXIT_SUCCESS ok (but ret can still be NULL), >0 unexpected char on ret - 1 */
static int
resolve_uses_schema_nodeid(const char *nodeid, const struct lys_node *start, const struct lys_node_grp **ret)
{
    const struct lys_module *module;
    const char *mod_prefix, *name;
    int i, mod_prefix_len, nam_len;

    /* parse the identifier, it must be parsed on one call */
    if (((i = parse_node_identifier(nodeid, &mod_prefix, &mod_prefix_len, &name, &nam_len)) < 1) || nodeid[i]) {
        return -i + 1;
    }

    module = lys_get_import_module(start->module, mod_prefix, mod_prefix_len, NULL, 0);
    if (!module) {
        return -1;
    }
    if (module != start->module) {
        start = module->data;
    }

    *ret = lys_find_grouping_up(name, (struct lys_node *)start);

    return EXIT_SUCCESS;
}

int
resolve_absolute_schema_nodeid(const char *nodeid, const struct lys_module *module, int ret_nodetype,
                               const struct lys_node **ret)
{
    const char *name, *mod_name, *id;
    const struct lys_node *sibling, *start;
    int r, nam_len, mod_name_len, is_relative = -1;
    int8_t shorthand = 0;
    const struct lys_module *abs_start_mod;

    assert(nodeid && module && ret);
    assert(!(ret_nodetype & (LYS_USES | LYS_AUGMENT)) && ((ret_nodetype == LYS_GROUPING) || !(ret_nodetype & LYS_GROUPING)));

    id = nodeid;
    start = module->data;

    if ((r = parse_schema_nodeid(id, &mod_name, &mod_name_len, &name, &nam_len, &is_relative, NULL)) < 1) {
        return ((id - nodeid) - r) + 1;
    }
    id += r;

    if (is_relative) {
        return -1;
    }

    abs_start_mod = lys_get_import_module(module, NULL, 0, mod_name, mod_name_len);
    if (!abs_start_mod) {
        return -1;
    }

    while (1) {
        sibling = NULL;
        while ((sibling = lys_getnext(sibling, lys_parent(start), abs_start_mod, LYS_GETNEXT_WITHCHOICE
                                      | LYS_GETNEXT_WITHCASE | LYS_GETNEXT_WITHINOUT | LYS_GETNEXT_WITHGROUPING))) {
            /* name match */
            if (sibling->name && !strncmp(name, sibling->name, nam_len) && !sibling->name[nam_len]) {
                r = schema_nodeid_siblingcheck(sibling, &shorthand, id, module, mod_name, mod_name_len, &start);
                if (r == 0) {
                    if (!(sibling->nodetype & ret_nodetype)) {
                        /* wrong node type, too bad */
                        continue;
                    }
                    *ret = sibling;
                    return EXIT_SUCCESS;
                } else if (r == 1) {
                    continue;
                } else if (r == 2) {
                    break;
                } else {
                    return -1;
                }
            }
        }

        /* no match */
        if (!sibling) {
            *ret = NULL;
            return EXIT_SUCCESS;
        }

        if ((r = parse_schema_nodeid(id, &mod_name, &mod_name_len, &name, &nam_len, &is_relative, NULL)) < 1) {
            return ((id - nodeid) - r) + 1;
        }
        id += r;
    }

    /* cannot get here */
    LOGINT;
    return -1;
}

static int
resolve_json_schema_list_predicate(const char *predicate, const struct lys_node_list *list, int *parsed)
{
    const char *name;
    int nam_len, has_predicate, i;

    if (((i = parse_schema_json_predicate(predicate, &name, &nam_len, NULL, NULL, &has_predicate)) < 1)
            || !strncmp(name, ".", nam_len)) {
        LOGVAL(LYE_PATH_INCHAR, LY_VLOG_NONE, NULL, predicate[-i], &predicate[-i]);
        return -1;
    }

    predicate += i;
    *parsed += i;

    for (i = 0; i < list->keys_size; ++i) {
        if (!strncmp(list->keys[i]->name, name, nam_len) && !list->keys[i]->name[nam_len]) {
            break;
        }
    }

    if (i == list->keys_size) {
        LOGVAL(LYE_PATH_INKEY, LY_VLOG_NONE, NULL, name);
        return -1;
    }

    /* more predicates? */
    if (has_predicate) {
        return resolve_json_schema_list_predicate(predicate, list, parsed);
    }

    return 0;
}

/* cannot return LYS_GROUPING, LYS_AUGMENT, LYS_USES, logs directly
 * data_nodeid - 0 schema nodeid, 1 - data nodeid with RPC input, 2 - data nodeid with RPC output */
const struct lys_node *
resolve_json_schema_nodeid(const char *nodeid, struct ly_ctx *ctx, const struct lys_node *start, int data_nodeid)
{
    char *module_name = ly_buf(), *buf_backup = NULL, *str;
    const char *name, *mod_name, *id;
    const struct lys_node *sibling;
    int r, nam_len, mod_name_len, is_relative = -1, has_predicate, shorthand = 0;
    /* resolved import module from the start module, it must match the next node-name-match sibling */
    const struct lys_module *prefix_mod, *module, *prev_mod;

    assert(nodeid && (ctx || start));
    if (!ctx) {
        ctx = start->module->ctx;
    }

    id = nodeid;

    if ((r = parse_schema_nodeid(id, &mod_name, &mod_name_len, &name, &nam_len, &is_relative, &has_predicate)) < 1) {
        LOGVAL(LYE_PATH_INCHAR, LY_VLOG_NONE, NULL, id[-r], &id[-r]);
        return NULL;
    }
    id += r;

    if (is_relative) {
        assert(start);
        start = start->child;
        if (!start) {
            /* no descendants, fail for sure */
            str = strndup(nodeid, (name + nam_len) - nodeid);
            LOGVAL(LYE_PATH_INNODE, LY_VLOG_STR, str);
            free(str);
            return NULL;
        }
        module = start->module;
    } else {
        if (!mod_name) {
            str = strndup(nodeid, (name + nam_len) - nodeid);
            LOGVAL(LYE_PATH_MISSMOD, LY_VLOG_STR, nodeid);
            free(str);
            return NULL;
        } else if (mod_name_len > LY_BUF_SIZE - 1) {
            LOGINT;
            return NULL;
        }

        if (ly_buf_used && module_name[0]) {
            buf_backup = strndup(module_name, LY_BUF_SIZE - 1);
        }
        ly_buf_used++;

        memmove(module_name, mod_name, mod_name_len);
        module_name[mod_name_len] = '\0';
        module = ly_ctx_get_module(ctx, module_name, NULL);

        if (buf_backup) {
            /* return previous internal buffer content */
            strcpy(module_name, buf_backup);
            free(buf_backup);
            buf_backup = NULL;
        }
        ly_buf_used--;

        if (!module) {
            str = strndup(nodeid, (mod_name + mod_name_len) - nodeid);
            LOGVAL(LYE_PATH_INMOD, LY_VLOG_STR, str);
            free(str);
            return NULL;
        }
        start = module->data;

        /* now it's as if there was no module name */
        mod_name = NULL;
        mod_name_len = 0;
    }

    prev_mod = module;

    while (1) {
        sibling = NULL;
        while ((sibling = lys_getnext(sibling, lys_parent(start), module, (data_nodeid ?
                0 : LYS_GETNEXT_WITHCHOICE | LYS_GETNEXT_WITHCASE | LYS_GETNEXT_WITHINOUT)))) {
            /* name match */
            if (sibling->name && !strncmp(name, sibling->name, nam_len) && !sibling->name[nam_len]) {

                /* data RPC input/output check */
                if ((data_nodeid == 1) && lys_parent(sibling) && (lys_parent(sibling)->nodetype == LYS_OUTPUT)) {
                    continue;
                } else if ((data_nodeid == 2) && lys_parent(sibling) && (lys_parent(sibling)->nodetype == LYS_INPUT)) {
                    continue;
                }

                /* module check */
                if (mod_name) {
                    if (mod_name_len > LY_BUF_SIZE - 1) {
                        LOGINT;
                        return NULL;
                    }

                    if (ly_buf_used && module_name[0]) {
                        buf_backup = strndup(module_name, LY_BUF_SIZE - 1);
                    }
                    ly_buf_used++;

                    memmove(module_name, mod_name, mod_name_len);
                    module_name[mod_name_len] = '\0';
                    /* will also find an augment module */
                    prefix_mod = ly_ctx_get_module(ctx, module_name, NULL);

                    if (buf_backup) {
                        /* return previous internal buffer content */
                        strncpy(module_name, buf_backup, LY_BUF_SIZE - 1);
                        free(buf_backup);
                        buf_backup = NULL;
                    }
                    ly_buf_used--;

                    if (!prefix_mod) {
                        str = strndup(nodeid, (mod_name + mod_name_len) - nodeid);
                        LOGVAL(LYE_PATH_INMOD, LY_VLOG_STR, str);
                        free(str);
                        return NULL;
                    }
                } else {
                    prefix_mod = prev_mod;
                }
                if (prefix_mod != lys_node_module(sibling)) {
                    continue;
                }

                /* do we have some predicates on it? */
                if (has_predicate) {
                    r = 0;
                    if (sibling->nodetype & (LYS_LEAF | LYS_LEAFLIST)) {
                        if ((r = parse_schema_json_predicate(id, NULL, NULL, NULL, NULL, &has_predicate)) < 1) {
                            LOGVAL(LYE_PATH_INCHAR, LY_VLOG_NONE, NULL, id[-r], &id[-r]);
                            return NULL;
                        }
                    } else if (sibling->nodetype == LYS_LIST) {
                        if (resolve_json_schema_list_predicate(id, (const struct lys_node_list *)sibling, &r)) {
                            return NULL;
                        }
                    } else {
                        LOGVAL(LYE_PATH_INCHAR, LY_VLOG_NONE, NULL, id[0], id);
                        return NULL;
                    }
                    id += r;
                }

                /* check for shorthand cases - then 'start' does not change */
                if (lys_parent(sibling) && (lys_parent(sibling)->nodetype == LYS_CHOICE) && (sibling->nodetype != LYS_CASE)) {
                    shorthand = ~shorthand;
                }

                /* the result node? */
                if (!id[0]) {
                    if (shorthand) {
                        /* wrong path for shorthand */
                        sibling = NULL;
                        break;
                    }
                    return sibling;
                }

                if (!shorthand) {
                    /* move down the tree, if possible */
                    if (sibling->nodetype & (LYS_LEAF | LYS_LEAFLIST | LYS_ANYXML)) {
                        LOGVAL(LYE_PATH_INCHAR, LY_VLOG_NONE, NULL, id[0], id);
                        return NULL;
                    }
                    start = sibling->child;
                }

                /* update prev mod */
                prev_mod = start->module;
                break;
            }
        }

        /* no match */
        if (!sibling) {
            str = strndup(nodeid, (name + nam_len) - nodeid);
            LOGVAL(LYE_PATH_INNODE, LY_VLOG_STR, str);
            free(str);
            return NULL;
        }

        if ((r = parse_schema_nodeid(id, &mod_name, &mod_name_len, &name, &nam_len, &is_relative, &has_predicate)) < 1) {
            LOGVAL(LYE_PATH_INCHAR, LY_VLOG_NONE, NULL, id[-r], &id[-r]);
            return NULL;
        }
        id += r;
    }

    /* cannot get here */
    LOGINT;
    return NULL;
}

static int
resolve_partial_json_data_list_predicate(const char *predicate, const char *node_name, struct lyd_node *node, int *parsed)
{
    const char *name, *value;
    int nam_len, val_len, has_predicate = 1, r;
    uint16_t i;
    struct lyd_node_leaf_list *key;

    assert(node);
    assert(node->schema->nodetype == LYS_LIST);

    key = (struct lyd_node_leaf_list *)node->child;
    for (i = 0; i < ((struct lys_node_list *)node->schema)->keys_size; ++i) {
        if (!key) {
            /* invalid data */
            LOGINT;
            return -1;
        }

        if (!has_predicate) {
            LOGVAL(LYE_PATH_MISSKEY, LY_VLOG_NONE, NULL, node_name);
            return -1;
        }

        if (((r = parse_schema_json_predicate(predicate, &name, &nam_len, &value, &val_len, &has_predicate)) < 1)
                || !strncmp(name, ".", nam_len)) {
            LOGVAL(LYE_PATH_INCHAR, LY_VLOG_NONE, NULL, predicate[-r], &predicate[-r]);
            return -1;
        }

        predicate += r;
        *parsed += r;

        if (strncmp(key->schema->name, name, nam_len) || key->schema->name[nam_len]) {
            LOGVAL(LYE_PATH_INKEY, LY_VLOG_NONE, NULL, name);
            return -1;
        }

        /* value does not match */
        if (strncmp(key->value_str, value, val_len) || key->value_str[val_len]) {
            return 1;
        }

        key = (struct lyd_node_leaf_list *)key->next;
    }

    if (has_predicate) {
        LOGVAL(LYE_PATH_INKEY, LY_VLOG_NONE, NULL, name);
        return -1;
    }

    return 0;
}

struct lyd_node *
resolve_partial_json_data_nodeid(const char *nodeid, const char *llist_value, struct lyd_node *start, int options,
                                 int *parsed)
{
    char *module_name = ly_buf(), *buf_backup = NULL, *str;
    const char *id, *mod_name, *name;
    int r, ret, mod_name_len, nam_len, is_relative = -1, has_predicate, last_parsed;
    struct lyd_node *sibling, *last_match = NULL;
    struct lyd_node_leaf_list *llist;
    const struct lys_module *prefix_mod, *prev_mod;
    struct ly_ctx *ctx;

    assert(nodeid && start && parsed);

    ctx = start->schema->module->ctx;
    id = nodeid;

    if ((r = parse_schema_nodeid(id, &mod_name, &mod_name_len, &name, &nam_len, &is_relative, &has_predicate)) < 1) {
        LOGVAL(LYE_PATH_INCHAR, LY_VLOG_NONE, NULL, id[-r], &id[-r]);
        *parsed = -1;
        return NULL;
    }
    id += r;
    /* add it to parsed only after the data node was actually found */
    last_parsed = r;

    if (is_relative) {
        prev_mod = start->schema->module;
        start = start->child;
    } else {
        for (; start->parent; start = start->parent);
        prev_mod = start->schema->module;
    }

    while (1) {
        LY_TREE_FOR(start, sibling) {
            /* RPC data check, return simply invalid argument, because the data tree is invalid */
            if (lys_parent(sibling->schema)) {
                if (options & LYD_PATH_OPT_OUTPUT) {
                    if (lys_parent(sibling->schema)->nodetype == LYS_INPUT) {
                        LOGERR(LY_EINVAL, "Provided data tree includes some RPC input nodes (%s).", sibling->schema->name);
                        *parsed = -1;
                        return NULL;
                    }
                } else {
                    if (lys_parent(sibling->schema)->nodetype == LYS_OUTPUT) {
                        LOGERR(LY_EINVAL, "Provided data tree includes some RPC output nodes (%s).", sibling->schema->name);
                        *parsed = -1;
                        return NULL;
                    }
                }
            }

            /* name match */
            if (!strncmp(name, sibling->schema->name, nam_len) && !sibling->schema->name[nam_len]) {

                /* module check */
                if (mod_name) {
                    if (mod_name_len > LY_BUF_SIZE - 1) {
                        LOGINT;
                        *parsed = -1;
                        return NULL;
                    }

                    if (ly_buf_used && module_name[0]) {
                        buf_backup = strndup(module_name, LY_BUF_SIZE - 1);
                    }
                    ly_buf_used++;

                    memmove(module_name, mod_name, mod_name_len);
                    module_name[mod_name_len] = '\0';
                    /* will also find an augment module */
                    prefix_mod = ly_ctx_get_module(ctx, module_name, NULL);

                    if (buf_backup) {
                        /* return previous internal buffer content */
                        strncpy(module_name, buf_backup, LY_BUF_SIZE - 1);
                        free(buf_backup);
                        buf_backup = NULL;
                    }
                    ly_buf_used--;

                    if (!prefix_mod) {
                        str = strndup(nodeid, (mod_name + mod_name_len) - nodeid);
                        LOGVAL(LYE_PATH_INMOD, LY_VLOG_STR, str);
                        free(str);
                        *parsed = -1;
                        return NULL;
                    }
                } else {
                    prefix_mod = prev_mod;
                }
                if (prefix_mod != lys_node_module(sibling->schema)) {
                    continue;
                }

                /* leaf-list, did we find it with the correct value or not? */
                if (sibling->schema->nodetype == LYS_LEAFLIST) {
                    llist = (struct lyd_node_leaf_list *)sibling;
                    if ((!llist_value && llist->value_str && llist->value_str[0])
                            || (llist_value && strcmp(llist_value, llist->value_str))) {
                        continue;
                    }
                }

                /* list, we need predicates'n'stuff then */
                if (sibling->schema->nodetype == LYS_LIST) {
                    r = 0;
                    if (!has_predicate) {
                        LOGVAL(LYE_PATH_MISSKEY, LY_VLOG_NONE, NULL, name);
                        *parsed = -1;
                        return NULL;
                    }
                    ret = resolve_partial_json_data_list_predicate(id, name, sibling, &r);
                    if (ret == -1) {
                        *parsed = -1;
                        return NULL;
                    } else if (ret == 1) {
                        /* this list instance does not match */
                        continue;
                    }
                    id += r;
                    last_parsed += r;
                }

                *parsed += last_parsed;

                /* the result node? */
                if (!id[0]) {
                    return sibling;
                }

                /* move down the tree, if possible */
                if (sibling->schema->nodetype & (LYS_LEAF | LYS_LEAFLIST | LYS_ANYXML)) {
                    LOGVAL(LYE_PATH_INCHAR, LY_VLOG_NONE, NULL, id[0], id);
                    *parsed = -1;
                    return NULL;
                }
                last_match = sibling;
                start = sibling->child;
                if (start) {
                    prev_mod = start->schema->module;
                }
                break;
            }
        }

        /* no match, return last match */
        if (!sibling) {
            return last_match;
        }

        if ((r = parse_schema_nodeid(id, &mod_name, &mod_name_len, &name, &nam_len, &is_relative, &has_predicate)) < 1) {
            LOGVAL(LYE_PATH_INCHAR, LY_VLOG_NONE, NULL, id[-r], &id[-r]);
            *parsed = -1;
            return NULL;
        }
        id += r;
        last_parsed = r;
    }

    /* cannot get here */
    LOGINT;
    *parsed = -1;
    return NULL;
}

/**
 * @brief Resolves length or range intervals. Does not log.
 * Syntax is assumed to be correct, *ret MUST be NULL.
 *
 * @param[in] str_restr Restriction as a string.
 * @param[in] type Type of the restriction.
 * @param[out] ret Final interval structure that starts with
 * the interval of the initial type, continues with intervals
 * of any superior types derived from the initial one, and
 * finishes with intervals from our \p type.
 *
 * @return EXIT_SUCCESS on succes, -1 on error.
 */
int
resolve_len_ran_interval(const char *str_restr, struct lys_type *type, struct len_ran_intv **ret)
{
    /* 0 - unsigned, 1 - signed, 2 - floating point */
    int kind;
    int64_t local_smin, local_smax;
    uint64_t local_umin, local_umax;
    long double local_fmin, local_fmax;
    const char *seg_ptr, *ptr;
    struct len_ran_intv *local_intv = NULL, *tmp_local_intv = NULL, *tmp_intv, *intv = NULL;

    switch (type->base) {
    case LY_TYPE_BINARY:
        kind = 0;
        local_umin = 0;
        local_umax = 18446744073709551615UL;

        if (!str_restr && type->info.binary.length) {
            str_restr = type->info.binary.length->expr;
        }
        break;
    case LY_TYPE_DEC64:
        kind = 2;
        local_fmin = -9223372036854775808.0;
        local_fmin /= 1 << type->info.dec64.dig;
        local_fmax = 9223372036854775807.0;
        local_fmax /= 1 << type->info.dec64.dig;

        if (!str_restr && type->info.dec64.range) {
            str_restr = type->info.dec64.range->expr;
        }
        break;
    case LY_TYPE_INT8:
        kind = 1;
        local_smin = __INT64_C(-128);
        local_smax = __INT64_C(127);

        if (!str_restr && type->info.num.range) {
            str_restr = type->info.num.range->expr;
        }
        break;
    case LY_TYPE_INT16:
        kind = 1;
        local_smin = __INT64_C(-32768);
        local_smax = __INT64_C(32767);

        if (!str_restr && type->info.num.range) {
            str_restr = type->info.num.range->expr;
        }
        break;
    case LY_TYPE_INT32:
        kind = 1;
        local_smin = __INT64_C(-2147483648);
        local_smax = __INT64_C(2147483647);

        if (!str_restr && type->info.num.range) {
            str_restr = type->info.num.range->expr;
        }
        break;
    case LY_TYPE_INT64:
        kind = 1;
        local_smin = __INT64_C(-9223372036854775807) - __INT64_C(1);
        local_smax = __INT64_C(9223372036854775807);

        if (!str_restr && type->info.num.range) {
            str_restr = type->info.num.range->expr;
        }
        break;
    case LY_TYPE_UINT8:
        kind = 0;
        local_umin = __UINT64_C(0);
        local_umax = __UINT64_C(255);

        if (!str_restr && type->info.num.range) {
            str_restr = type->info.num.range->expr;
        }
        break;
    case LY_TYPE_UINT16:
        kind = 0;
        local_umin = __UINT64_C(0);
        local_umax = __UINT64_C(65535);

        if (!str_restr && type->info.num.range) {
            str_restr = type->info.num.range->expr;
        }
        break;
    case LY_TYPE_UINT32:
        kind = 0;
        local_umin = __UINT64_C(0);
        local_umax = __UINT64_C(4294967295);

        if (!str_restr && type->info.num.range) {
            str_restr = type->info.num.range->expr;
        }
        break;
    case LY_TYPE_UINT64:
        kind = 0;
        local_umin = __UINT64_C(0);
        local_umax = __UINT64_C(18446744073709551615);

        if (!str_restr && type->info.num.range) {
            str_restr = type->info.num.range->expr;
        }
        break;
    case LY_TYPE_STRING:
        kind = 0;
        local_umin = __UINT64_C(0);
        local_umax = __UINT64_C(18446744073709551615);

        if (!str_restr && type->info.str.length) {
            str_restr = type->info.str.length->expr;
        }
        break;
    default:
        LOGINT;
        return -1;
    }

    /* process superior types */
    if (type->der) {
        if (resolve_len_ran_interval(NULL, &type->der->type, &intv)) {
            LOGINT;
            return -1;
        }
        assert(!intv || (intv->kind == kind));
    }

    if (!str_restr) {
        /* we do not have any restriction, return superior ones */
        *ret = intv;
        return EXIT_SUCCESS;
    }

    /* adjust local min and max */
    if (intv) {
        tmp_intv = intv;

        if (kind == 0) {
            local_umin = tmp_intv->value.uval.min;
        } else if (kind == 1) {
            local_smin = tmp_intv->value.sval.min;
        } else if (kind == 2) {
            local_fmin = tmp_intv->value.fval.min;
        }

        while (tmp_intv->next) {
            tmp_intv = tmp_intv->next;
        }

        if (kind == 0) {
            local_umax = tmp_intv->value.uval.max;
        } else if (kind == 1) {
            local_smax = tmp_intv->value.sval.max;
        } else if (kind == 2) {
            local_fmax = tmp_intv->value.fval.max;
        }
    }

    /* finally parse our restriction */
    seg_ptr = str_restr;
    while (1) {
        if (!tmp_local_intv) {
            assert(!local_intv);
            local_intv = malloc(sizeof *local_intv);
            tmp_local_intv = local_intv;
        } else {
            tmp_local_intv->next = malloc(sizeof *tmp_local_intv);
            tmp_local_intv = tmp_local_intv->next;
        }
        if (!tmp_local_intv) {
            LOGMEM;
            goto error;
        }

        tmp_local_intv->kind = kind;
        tmp_local_intv->type = type;
        tmp_local_intv->next = NULL;

        /* min */
        ptr = seg_ptr;
        while (isspace(ptr[0])) {
            ++ptr;
        }
        if (isdigit(ptr[0]) || (ptr[0] == '+') || (ptr[0] == '-')) {
            if (kind == 0) {
                tmp_local_intv->value.uval.min = atoll(ptr);
            } else if (kind == 1) {
                tmp_local_intv->value.sval.min = atoll(ptr);
            } else if (kind == 2) {
                tmp_local_intv->value.fval.min = atoll(ptr);
            }

            if ((ptr[0] == '+') || (ptr[0] == '-')) {
                ++ptr;
            }
            while (isdigit(ptr[0])) {
                ++ptr;
            }
        } else if (!strncmp(ptr, "min", 3)) {
            if (kind == 0) {
                tmp_local_intv->value.uval.min = local_umin;
            } else if (kind == 1) {
                tmp_local_intv->value.sval.min = local_smin;
            } else if (kind == 2) {
                tmp_local_intv->value.fval.min = local_fmin;
            }

            ptr += 3;
        } else if (!strncmp(ptr, "max", 3)) {
            if (kind == 0) {
                tmp_local_intv->value.uval.min = local_umax;
            } else if (kind == 1) {
                tmp_local_intv->value.sval.min = local_smax;
            } else if (kind == 2) {
                tmp_local_intv->value.fval.min = local_fmax;
            }

            ptr += 3;
        } else {
            LOGINT;
            goto error;
        }

        while (isspace(ptr[0])) {
            ptr++;
        }

        /* no interval or interval */
        if ((ptr[0] == '|') || !ptr[0]) {
            if (kind == 0) {
                tmp_local_intv->value.uval.max = tmp_local_intv->value.uval.min;
            } else if (kind == 1) {
                tmp_local_intv->value.sval.max = tmp_local_intv->value.sval.min;
            } else if (kind == 2) {
                tmp_local_intv->value.fval.max = tmp_local_intv->value.fval.min;
            }
        } else if (!strncmp(ptr, "..", 2)) {
            /* skip ".." */
            ptr += 2;
            while (isspace(ptr[0])) {
                ++ptr;
            }

            /* max */
            if (isdigit(ptr[0]) || (ptr[0] == '+') || (ptr[0] == '-')) {
                if (kind == 0) {
                    tmp_local_intv->value.uval.max = atoll(ptr);
                } else if (kind == 1) {
                    tmp_local_intv->value.sval.max = atoll(ptr);
                } else if (kind == 2) {
                    tmp_local_intv->value.fval.max = atoll(ptr);
                }
            } else if (!strncmp(ptr, "max", 3)) {
                if (kind == 0) {
                    tmp_local_intv->value.uval.max = local_umax;
                } else if (kind == 1) {
                    tmp_local_intv->value.sval.max = local_smax;
                } else if (kind == 2) {
                    tmp_local_intv->value.fval.max = local_fmax;
                }
            } else {
                LOGINT;
                goto error;
            }
        } else {
            LOGINT;
            goto error;
        }

        /* next segment (next OR) */
        seg_ptr = strchr(seg_ptr, '|');
        if (!seg_ptr) {
            break;
        }
        seg_ptr++;
    }

    /* check local restrictions against superior ones */
    if (intv) {
        tmp_intv = intv;
        tmp_local_intv = local_intv;

        while (tmp_local_intv && tmp_intv) {
            /* reuse local variables */
            if (kind == 0) {
                local_umin = tmp_local_intv->value.uval.min;
                local_umax = tmp_local_intv->value.uval.max;

                /* it must be in this interval */
                if ((local_umin >= tmp_intv->value.uval.min) && (local_umin <= tmp_intv->value.uval.max)) {
                    /* this interval is covered, next one */
                    if (local_umax <= tmp_intv->value.uval.max) {
                        tmp_local_intv = tmp_local_intv->next;
                        continue;
                    /* ascending order of restrictions -> fail */
                    } else {
                        goto error;
                    }
                }
            } else if (kind == 1) {
                local_smin = tmp_local_intv->value.sval.min;
                local_smax = tmp_local_intv->value.sval.max;

                if ((local_smin >= tmp_intv->value.sval.min) && (local_smin <= tmp_intv->value.sval.max)) {
                    if (local_smax <= tmp_intv->value.sval.max) {
                        tmp_local_intv = tmp_local_intv->next;
                        continue;
                    } else {
                        goto error;
                    }
                }
            } else if (kind == 2) {
                local_fmin = tmp_local_intv->value.fval.min;
                local_fmax = tmp_local_intv->value.fval.max;

                 if ((local_fmin >= tmp_intv->value.fval.min) && (local_fmin <= tmp_intv->value.fval.max)) {
                    if (local_fmax <= tmp_intv->value.fval.max) {
                        tmp_local_intv = tmp_local_intv->next;
                        continue;
                    } else {
                        goto error;
                    }
                }
            }

            tmp_intv = tmp_intv->next;
        }

        /* some interval left uncovered -> fail */
        if (tmp_local_intv) {
            goto error;
        }
    }

    /* append the local intervals to all the intervals of the superior types, return it all */
    if (intv) {
        for (tmp_intv = intv; tmp_intv->next; tmp_intv = tmp_intv->next);
        tmp_intv->next = local_intv;
    } else {
        intv = local_intv;
    }
    *ret = intv;

    return EXIT_SUCCESS;

error:
    while (intv) {
        tmp_intv = intv->next;
        free(intv);
        intv = tmp_intv;
    }
    while (local_intv) {
        tmp_local_intv = local_intv->next;
        free(local_intv);
        local_intv = tmp_local_intv;
    }

    return -1;
}

/**
 * @brief Resolve a typedef, return only resolved typedefs if derived. Does not log.
 *
 * @param[in] name Typedef name.
 * @param[in] mod_name Typedef name module name.
 * @param[in] module Main module.
 * @param[in] parent Parent of the resolved type definition.
 * @param[out] ret Pointer to the resolved typedef. Can be NULL.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 on error.
 */
int
resolve_superior_type(const char *name, const char *mod_name, const struct lys_module *module,
                      const struct lys_node *parent, struct lys_tpdf **ret)
{
    int i, j;
    struct lys_tpdf *tpdf;
    int tpdf_size;

    if (!mod_name) {
        /* no prefix, try built-in types */
        for (i = 1; i < LY_DATA_TYPE_COUNT; i++) {
            if (!strcmp(ly_types[i].def->name, name)) {
                if (ret) {
                    *ret = ly_types[i].def;
                }
                return EXIT_SUCCESS;
            }
        }
    } else {
        if (!strcmp(mod_name, module->name)) {
            /* prefix refers to the current module, ignore it */
            mod_name = NULL;
        }
    }

    if (!mod_name && parent) {
        /* search in local typedefs */
        while (parent) {
            switch (parent->nodetype) {
            case LYS_CONTAINER:
                tpdf_size = ((struct lys_node_container *)parent)->tpdf_size;
                tpdf = ((struct lys_node_container *)parent)->tpdf;
                break;

            case LYS_LIST:
                tpdf_size = ((struct lys_node_list *)parent)->tpdf_size;
                tpdf = ((struct lys_node_list *)parent)->tpdf;
                break;

            case LYS_GROUPING:
                tpdf_size = ((struct lys_node_grp *)parent)->tpdf_size;
                tpdf = ((struct lys_node_grp *)parent)->tpdf;
                break;

            case LYS_RPC:
                tpdf_size = ((struct lys_node_rpc *)parent)->tpdf_size;
                tpdf = ((struct lys_node_rpc *)parent)->tpdf;
                break;

            case LYS_NOTIF:
                tpdf_size = ((struct lys_node_notif *)parent)->tpdf_size;
                tpdf = ((struct lys_node_notif *)parent)->tpdf;
                break;

            case LYS_INPUT:
            case LYS_OUTPUT:
                tpdf_size = ((struct lys_node_rpc_inout *)parent)->tpdf_size;
                tpdf = ((struct lys_node_rpc_inout *)parent)->tpdf;
                break;

            default:
                parent = lys_parent(parent);
                continue;
            }

            for (i = 0; i < tpdf_size; i++) {
                if (!strcmp(tpdf[i].name, name) && tpdf[i].type.base) {
                    if (ret) {
                        *ret = &tpdf[i];
                    }
                    return EXIT_SUCCESS;
                }
            }

            parent = lys_parent(parent);
        }
    } else {
        /* get module where to search */
        module = lys_get_import_module(module, NULL, 0, mod_name, 0);
        if (!module) {
            return -1;
        }
    }

    /* search in top level typedefs */
    for (i = 0; i < module->tpdf_size; i++) {
        if (!strcmp(module->tpdf[i].name, name) && module->tpdf[i].type.base) {
            if (ret) {
                *ret = &module->tpdf[i];
            }
            return EXIT_SUCCESS;
        }
    }

    /* search in submodules */
    for (i = 0; i < module->inc_size && module->inc[i].submodule; i++) {
        for (j = 0; j < module->inc[i].submodule->tpdf_size; j++) {
            if (!strcmp(module->inc[i].submodule->tpdf[j].name, name) && module->inc[i].submodule->tpdf[j].type.base) {
                if (ret) {
                    *ret = &module->inc[i].submodule->tpdf[j];
                }
                return EXIT_SUCCESS;
            }
        }
    }

    return EXIT_FAILURE;
}

/**
 * @brief Check the default \p value of the \p type. Logs directly.
 *
 * @param[in] type Type definition to use.
 * @param[in] value Default value to check.
 * @param[in] module Type module.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 on error.
 */
static int
check_default(struct lys_type *type, const char *value, struct lys_module *module)
{
    struct lyd_node_leaf_list node;
    int ret = EXIT_SUCCESS;

    /* dummy leaf */
    memset(&node, 0, sizeof node);
    node.value_str = value;
    node.value_type = type->base;
    node.schema = calloc(1, sizeof (struct lys_node_leaf));
    if (!node.schema) {
        LOGMEM;
        return -1;
    }
    node.schema->name = strdup("default");
    if (!node.schema->name) {
        LOGMEM;
        return -1;
    }
    node.schema->module = module;
    memcpy(&((struct lys_node_leaf *)node.schema)->type, type, sizeof *type);

    if (type->base == LY_TYPE_LEAFREF) {
        if (!type->info.lref.target) {
            ret = EXIT_FAILURE;
            goto finish;
        }
        ret = check_default(&type->info.lref.target->type, value, module);

    } else if ((type->base == LY_TYPE_INST) || (type->base == LY_TYPE_IDENT)) {
        /* it was converted to JSON format before, nothing else sensible we can do */

    } else {
        ret = lyp_parse_value(&node, NULL, 1);
    }

finish:
    if (node.value_type == LY_TYPE_BITS) {
        free(node.value.bit);
    }
    free((char *)node.schema->name);
    free(node.schema);

    return ret;
}

/**
 * @brief Check a key for mandatory attributes. Logs directly.
 *
 * @param[in] key The key to check.
 * @param[in] flags What flags to check.
 * @param[in] list The list of all the keys.
 * @param[in] index Index of the key in the key list.
 * @param[in] name The name of the keys.
 * @param[in] len The name length.
 *
 * @return EXIT_SUCCESS on success, -1 on error.
 */
static int
check_key(struct lys_node_list *list, int index, const char *name, int len)
{
    struct lys_node_leaf *key = list->keys[index];
    char *dup = NULL;
    int j;

    /* existence */
    if (!key) {
        if (name[len] != '\0') {
            dup = strdup(name);
            if (!dup) {
                LOGMEM;
                return -1;
            }
            dup[len] = '\0';
            name = dup;
        }
        LOGVAL(LYE_KEY_MISS, LY_VLOG_LYS, list, name);
        free(dup);
        return -1;
    }

    /* uniqueness */
    for (j = index - 1; j >= 0; j--) {
        if (key == list->keys[j]) {
            LOGVAL(LYE_KEY_DUP, LY_VLOG_LYS, list, key->name);
            return -1;
        }
    }

    /* key is a leaf */
    if (key->nodetype != LYS_LEAF) {
        LOGVAL(LYE_KEY_NLEAF, LY_VLOG_LYS, list, key->name);
        return -1;
    }

    /* type of the leaf is not built-in empty */
    if (key->type.base == LY_TYPE_EMPTY) {
        LOGVAL(LYE_KEY_TYPE, LY_VLOG_LYS, list, key->name);
        return -1;
    }

    /* config attribute is the same as of the list */
    if ((list->flags & LYS_CONFIG_MASK) != (key->flags & LYS_CONFIG_MASK)) {
        LOGVAL(LYE_KEY_CONFIG, LY_VLOG_LYS, list, key->name);
        return -1;
    }

    /* key is not placed from augment */
    if (key->parent->nodetype == LYS_AUGMENT) {
        LOGVAL(LYE_KEY_MISS, LY_VLOG_LYS, key, key->name);
        LOGVAL(LYE_SPEC, LY_VLOG_LYS, key, "Key inserted from augment.");
        return -1;
    }

    /* key is not when-conditional */
    if (key->when) {
        LOGVAL(LYE_INCHILDSTMT, LY_VLOG_LYS, key, "when", "leaf");
        LOGVAL(LYE_SPEC, LY_VLOG_LYS, key, "Key definition cannot depend on a \"when\" condition.");
        return -1;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Resolve (test the target exists) unique. Logs directly.
 *
 * @param[in] parent The parent node of the unique structure.
 * @param[in] uniq_str_path One path from the unique string.
 *
 * @return EXIT_SUCCESS on succes, EXIT_FAILURE on forward reference, -1 on error.
 */
int
resolve_unique(struct lys_node *parent, const char *uniq_str_path)
{
    int rc;
    const struct lys_node *leaf = NULL;

    rc = resolve_descendant_schema_nodeid(uniq_str_path, parent->child, LYS_LEAF, 1, 1, &leaf);
    if (rc || !leaf) {
        if (rc) {
            LOGVAL(LYE_INARG, LY_VLOG_LYS, parent, uniq_str_path, "unique");
            if (rc > 0) {
                LOGVAL(LYE_INCHAR, LY_VLOG_LYS, parent, uniq_str_path[rc - 1], &uniq_str_path[rc - 1]);
            } else if (rc == -2) {
                LOGVAL(LYE_SPEC, LY_VLOG_LYS, parent, "Unique argument references list.");
            }
            rc = -1;
        } else {
            LOGVAL(LYE_INARG, LY_VLOG_LYS, parent, uniq_str_path, "unique");
            LOGVAL(LYE_SPEC, LY_VLOG_LYS, parent, "Target leaf not found.");
            rc = EXIT_FAILURE;
        }
        goto error;
    }
    if (leaf->nodetype != LYS_LEAF) {
        LOGVAL(LYE_INARG, LY_VLOG_LYS, parent, uniq_str_path, "unique");
        LOGVAL(LYE_SPEC, LY_VLOG_LYS, parent, "Target is not a leaf.");
        rc = -1;
        goto error;
    }

    /* check status */
    if (lyp_check_status(parent->flags, parent->module, parent->name, leaf->flags, leaf->module, leaf->name, leaf)) {
        return -1;
    }

    /* set leaf's unique flag */
    ((struct lys_node_leaf *)leaf)->flags |= LYS_UNIQUE;

    return EXIT_SUCCESS;

error:

    return rc;
}

/**
 * @brief Resolve (find) a feature definition. Logs directly.
 *
 * @param[in] name Feature name.
 * @param[in] module Module to search in.
 * @param[out] ret Pointer to the resolved feature. Can be NULL.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 on error.
 */
static int
resolve_feature(const char *id, const struct lys_module *module, struct lys_feature **ret)
{
    const char *mod_name, *name;
    int mod_name_len, nam_len, i, j;
    struct lys_node *node;

    assert(id);
    assert(module);

    /* check prefix */
    if ((i = parse_node_identifier(id, &mod_name, &mod_name_len, &name, &nam_len)) < 1) {
        LOGVAL(LYE_INCHAR, LY_VLOG_NONE, NULL, id[-i], &id[-i]);
        return -1;
    }

    module = lys_get_import_module(module, NULL, 0, mod_name, mod_name_len);
    if (!module) {
        /* identity refers unknown data model */
        LOGVAL(LYE_INMOD_LEN, LY_VLOG_NONE, NULL, mod_name_len, mod_name);
        return -1;
    }

    /* search in the identified module ... */
    for (j = 0; j < module->features_size; j++) {
        if (!strcmp(name, module->features[j].name)) {
            if (ret) {
                /* check status */
                node = (struct lys_node *)*ret;
                if (lyp_check_status(node->flags, node->module, node->name, module->features[j].flags,
                                 module->features[j].module, module->features[j].name, node)) {
                    return -1;
                }
                *ret = &module->features[j];
            }
            return EXIT_SUCCESS;
        }
    }
    /* ... and all its submodules */
    for (i = 0; i < module->inc_size; i++) {
        if (!module->inc[i].submodule) {
            /* not yet resolved */
            continue;
        }
        for (j = 0; j < module->inc[i].submodule->features_size; j++) {
            if (!strcmp(name, module->inc[i].submodule->features[j].name)) {
                if (ret) {
                    /* check status */
                    node = (struct lys_node *)*ret;
                    if (lyp_check_status(node->flags, node->module, node->name,
                                     module->inc[i].submodule->features[j].flags,
                                     module->inc[i].submodule->features[j].module,
                                     module->inc[i].submodule->features[j].name, node)) {
                        return -1;
                    }
                    *ret = &(module->inc[i].submodule->features[j]);
                }
                return EXIT_SUCCESS;
            }
        }
    }

    /* not found */
    LOGVAL(LYE_INRESOLV, LY_VLOG_NONE, NULL, "feature", id);
    return EXIT_FAILURE;
}

void
unres_data_del(struct unres_data *unres, uint32_t i)
{
    /* there are items after the one deleted */
    if (i+1 < unres->count) {
        /* we only move the data, memory is left allocated, why bother */
        memmove(&unres->node[i], &unres->node[i+1], (unres->count-(i+1)) * sizeof *unres->node);

    /* deleting the last item */
    } else if (i == 0) {
        free(unres->node);
        unres->node = NULL;
    }

    /* if there are no items after and it is not the last one, just move the counter */
    --unres->count;
}

/**
 * @brief Resolve (find) a data node from a specific module. Does not log.
 *
 * @param[in] mod Module to search in.
 * @param[in] name Name of the data node.
 * @param[in] nam_len Length of the name.
 * @param[in] start Data node to start the search from.
 * @param[in,out] parents Resolved nodes. If there are some parents,
 *                        they are replaced (!!) with the resolvents.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 on error.
 */
static int
resolve_data(const struct lys_module *mod, const char *name, int nam_len, struct lyd_node *start, struct unres_data *parents)
{
    struct lyd_node *node;
    int flag;
    uint32_t i;

    if (!parents->count) {
        parents->count = 1;
        parents->node = malloc(sizeof *parents->node);
        if (!parents->node) {
            LOGMEM;
            return -1;
        }
        parents->node[0] = NULL;
    }
    for (i = 0; i < parents->count;) {
        if (parents->node[i] && (parents->node[i]->schema->nodetype & (LYS_LEAF | LYS_LEAFLIST | LYS_ANYXML))) {
            /* skip */
            ++i;
            continue;
        }
        flag = 0;
        LY_TREE_FOR(parents->node[i] ? parents->node[i]->child : start, node) {
            if (node->schema->module == mod && !strncmp(node->schema->name, name, nam_len)
                    && node->schema->name[nam_len] == '\0') {
                /* matching target */
                if (!flag) {
                    /* put node instead of the current parent */
                    parents->node[i] = node;
                    flag = 1;
                } else {
                    /* multiple matching, so create a new node */
                    ++parents->count;
                    parents->node = ly_realloc(parents->node, parents->count * sizeof *parents->node);
                    if (!parents->node) {
                        return EXIT_FAILURE;
                    }
                    parents->node[parents->count-1] = node;
                    ++i;
                }
            }
        }

        if (!flag) {
            /* remove item from the parents list */
            unres_data_del(parents, i);
        } else {
            ++i;
        }
    }

    return parents->count ? EXIT_SUCCESS : EXIT_FAILURE;
}

/**
 * @brief Resolve (find) a data node. Does not log.
 *
 * @param[in] mod_name Module name of the data node.
 * @param[in] mod_name_len Length of the module name.
 * @param[in] name Name of the data node.
 * @param[in] nam_len Length of the name.
 * @param[in] start Data node to start the search from.
 * @param[in,out] parents Resolved nodes. If there are some parents,
 *                        they are replaced (!!) with the resolvents.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 otherwise.
 */
static int
resolve_data_node(const char *mod_name, int mod_name_len, const char *name, int name_len, struct lyd_node *start,
                    struct unres_data *parents)
{
    const struct lys_module *mod;
    char *str;

    assert(start);

    if (mod_name) {
        /* we have mod_name, find appropriate module */
        str = strndup(mod_name, mod_name_len);
        if (!str) {
            LOGMEM;
            return -1;
        }
        mod = ly_ctx_get_module(start->schema->module->ctx, str, NULL);
        free(str);
        if (!mod) {
            /* invalid prefix */
            return -1;
        }
    } else {
        /* no prefix, module is the same as of current node */
        mod = start->schema->module;
    }

    return resolve_data(mod, name, name_len, start, parents);
}

/**
 * @brief Resolve a path predicate (leafref) in JSON data context. Logs directly
 *        only specific errors, general no-resolvent error is left to the caller.
 *
 * @param[in] pred Predicate to use.
 * @param[in] node Node from which the predicate is being resolved
 * @param[in,out] node_match Nodes satisfying the restriction
 *                           without the predicate. Nodes not
 *                           satisfying the predicate are removed.
 * @param[out] parsed Number of characters parsed, negative on error.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 on error.
 */
static int
resolve_path_predicate_data(const char *pred, struct lyd_node *node, struct unres_data *node_match,
                            int *parsed)
{
    /* ... /node[source = destination] ... */
    struct unres_data source_match, dest_match;
    const char *path_key_expr, *source, *sour_pref, *dest, *dest_pref;
    int pke_len, sour_len, sour_pref_len, dest_len, dest_pref_len, parsed_loc = 0, pke_parsed = 0;
    int has_predicate, dest_parent_times, i, rc;
    uint32_t j;

    source_match.count = 1;
    source_match.node = malloc(sizeof *source_match.node);
    if (!source_match.node) {
        LOGMEM;
        return -1;
    }
    dest_match.count = 1;
    dest_match.node = malloc(sizeof *dest_match.node);
    if (!dest_match.node) {
        LOGMEM;
        return -1;
    }

    do {
        if ((i = parse_path_predicate(pred, &sour_pref, &sour_pref_len, &source, &sour_len, &path_key_expr,
                                      &pke_len, &has_predicate)) < 1) {
            LOGVAL(LYE_INCHAR, LY_VLOG_LYD, node, pred[-i], &pred[-i]);
            rc = -1;
            goto error;
        }
        parsed_loc += i;
        pred += i;

        for (j = 0; j < node_match->count;) {
            /* source */
            source_match.node[0] = node_match->node[j];

            /* must be leaf (key of a list) */
            if ((rc = resolve_data_node(sour_pref, sour_pref_len, source, sour_len, node_match->node[j],
                    &source_match)) || (source_match.count != 1) || (source_match.node[0]->schema->nodetype != LYS_LEAF)) {
                i = 0;
                goto error;
            }

            /* destination */
            dest_match.node[0] = node_match->node[j];
            dest_parent_times = 0;
            if ((i = parse_path_key_expr(path_key_expr, &dest_pref, &dest_pref_len, &dest, &dest_len,
                                            &dest_parent_times)) < 1) {
                LOGVAL(LYE_INCHAR, LY_VLOG_LYD, node, path_key_expr[-i], &path_key_expr[-i]);
                rc = -1;
                goto error;
            }
            pke_parsed = i;
            for (i = 0; i < dest_parent_times; ++i) {
                dest_match.node[0] = dest_match.node[0]->parent;
                if (!dest_match.node[0]) {
                    i = 0;
                    rc = EXIT_FAILURE;
                    goto error;
                }
            }
            while (1) {
                if ((rc = resolve_data_node(dest_pref, dest_pref_len, dest, dest_len, dest_match.node[0],
                        &dest_match)) || (dest_match.count != 1)) {
                    i = 0;
                    goto error;
                }

                if (pke_len == pke_parsed) {
                    break;
                }
                if ((i = parse_path_key_expr(path_key_expr+pke_parsed, &dest_pref, &dest_pref_len, &dest, &dest_len,
                                             &dest_parent_times)) < 1) {
                    LOGVAL(LYE_INCHAR, LY_VLOG_LYD, node, path_key_expr[-i], &path_key_expr[-i]);
                    rc = -1;
                    goto error;
                }
                pke_parsed += i;
            }

            /* check match between source and destination nodes */
            if (((struct lys_node_leaf *)source_match.node[0]->schema)->type.base
                    != ((struct lys_node_leaf *)dest_match.node[0]->schema)->type.base) {
                goto remove_leafref;
            }

            if (!ly_strequal(((struct lyd_node_leaf_list *)source_match.node[0])->value_str,
                             ((struct lyd_node_leaf_list *)dest_match.node[0])->value_str, 1)) {
                goto remove_leafref;
            }

            /* leafref is ok, continue check with next leafref */
            ++j;
            continue;

remove_leafref:
            /* does not fulfill conditions, remove leafref record */
            unres_data_del(node_match, j);
        }
    } while (has_predicate);

    free(source_match.node);
    free(dest_match.node);
    if (parsed) {
        *parsed = parsed_loc;
    }
    return EXIT_SUCCESS;

error:

    if (source_match.count) {
        free(source_match.node);
    }
    if (dest_match.count) {
        free(dest_match.node);
    }
    if (parsed) {
        *parsed = -parsed_loc+i;
    }
    return rc;
}

/**
 * @brief Resolve a path (leafref) in JSON data context. Logs directly.
 *
 * @param[in] node Leafref data node.
 * @param[in] path Path of the leafref.
 * @param[out] ret Matching nodes. Expects an empty, but allocated structure.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 otherwise.
 */
static int
resolve_path_arg_data(struct lyd_node *node, const char *path, struct unres_data *ret)
{
    struct lyd_node *data = NULL;
    const char *prefix, *name;
    int pref_len, nam_len, has_predicate, parent_times, i, parsed, rc;
    uint32_t j;

    assert(node && path && ret && !ret->count);

    parent_times = 0;
    parsed = 0;

    /* searching for nodeset */
    do {
        if ((i = parse_path_arg(path, &prefix, &pref_len, &name, &nam_len, &parent_times, &has_predicate)) < 1) {
            LOGVAL(LYE_INCHAR, LY_VLOG_LYD, node, path[-i], &path[-i]);
            rc = -1;
            goto error;
        }
        path += i;
        parsed += i;

        if (!ret->count) {
            if (parent_times != -1) {
                ret->count = 1;
                ret->node = calloc(1, sizeof *ret->node);
                if (!ret->node) {
                    LOGMEM;
                    rc = -1;
                    goto error;
                }
            }
            for (i = 0; i < parent_times; ++i) {
                /* relative path */
                if (!ret->count) {
                    /* error, too many .. */
                    LOGVAL(LYE_INVAL, LY_VLOG_LYD, node, path, node->schema->name);
                    rc = -1;
                    goto error;
                } else if (!ret->node[0]) {
                    /* first .. */
                    data = ret->node[0] = node->parent;
                } else if (!ret->node[0]->parent) {
                    /* we are in root */
                    ret->count = 0;
                    free(ret->node);
                    ret->node = NULL;
                } else {
                    /* multiple .. */
                    data = ret->node[0] = ret->node[0]->parent;
                }
            }

            /* absolute path */
            if (parent_times == -1) {
                for (data = node; data->parent; data = data->parent);
                /* we're still parsing it and the pointer is not correct yet */
                if (data->prev) {
                    for (; data->prev->next; data = data->prev);
                }
            }
        }

        /* node identifier */
        if ((rc = resolve_data_node(prefix, pref_len, name, nam_len, data, ret))) {
            if (rc == -1) {
                LOGVAL(LYE_INELEM_LEN, LY_VLOG_LYD, node, nam_len, name);
            }
            goto error;
        }

        if (has_predicate) {
            /* we have predicate, so the current results must be lists */
            for (j = 0; j < ret->count;) {
                if (ret->node[j]->schema->nodetype == LYS_LIST &&
                        ((struct lys_node_list *)ret->node[0]->schema)->keys) {
                    /* leafref is ok, continue check with next leafref */
                    ++j;
                    continue;
                }

                /* does not fulfill conditions, remove leafref record */
                unres_data_del(ret, j);
            }
            if ((rc = resolve_path_predicate_data(path, node, ret, &i))) {
                if (rc == -1) {
                    LOGVAL(LYE_NORESOLV, LY_VLOG_LYD, node, path);
                }
                goto error;
            }
            path += i;
            parsed += i;

            if (!ret->count) {
                rc = EXIT_FAILURE;
                goto error;
            }
        }
    } while (path[0] != '\0');

    return EXIT_SUCCESS;

error:

    free(ret->node);
    ret->node = NULL;
    ret->count = 0;

    return rc;
}

/**
 * @brief Resolve a path (leafref) predicate in JSON schema context. Logs directly.
 *
 * @param[in] path Path to use.
 * @param[in] context_node Predicate context node (where the predicate is placed).
 * @param[in] parent Path context node (where the path begins/is placed).
 *
 * @return 0 on forward reference, otherwise the number
 *         of characters successfully parsed,
 *         positive on success, negative on failure.
 */
static int
resolve_path_predicate_schema(const char *path, const struct lys_node *context_node,
                              struct lys_node *parent)
{
    const struct lys_node *src_node, *dst_node;
    const char *path_key_expr, *source, *sour_pref, *dest, *dest_pref;
    int pke_len, sour_len, sour_pref_len, dest_len, dest_pref_len, parsed = 0, pke_parsed = 0;
    int has_predicate, dest_parent_times = 0, i, rc;

    do {
        if ((i = parse_path_predicate(path, &sour_pref, &sour_pref_len, &source, &sour_len, &path_key_expr,
                                      &pke_len, &has_predicate)) < 1) {
            LOGVAL(LYE_INCHAR, parent ? LY_VLOG_LYS : LY_VLOG_NONE, parent, path[-i], path-i);
            return -parsed+i;
        }
        parsed += i;
        path += i;

        /* source (must be leaf) */
        if (!sour_pref) {
            sour_pref = context_node->module->name;
        }
        rc = lys_get_sibling(context_node->child, sour_pref, sour_pref_len, source, sour_len,
                             LYS_LEAF | LYS_AUGMENT, &src_node);
        if (rc) {
            LOGVAL(LYE_NORESOLV, parent ? LY_VLOG_LYS : LY_VLOG_NONE, parent, path-parsed);
            return 0;
        }

        /* destination */
        if ((i = parse_path_key_expr(path_key_expr, &dest_pref, &dest_pref_len, &dest, &dest_len,
                                     &dest_parent_times)) < 1) {
            LOGVAL(LYE_INCHAR, parent ? LY_VLOG_LYS : LY_VLOG_NONE, parent, path_key_expr[-i], path_key_expr-i);
            return -parsed;
        }
        pke_parsed += i;

        /* parent is actually the parent of this leaf, so skip the first ".." */
        for (i = 0, dst_node = parent; i < dest_parent_times; ++i) {
            if (!dst_node) {
                LOGVAL(LYE_NORESOLV, parent ? LY_VLOG_LYS : LY_VLOG_NONE, parent, path_key_expr);
                return 0;
            }
            dst_node = lys_parent(dst_node);
        }
        while (1) {
            if (!dest_pref) {
                dest_pref = dst_node->module->name;
            }
            rc = lys_get_sibling(dst_node->child, dest_pref, dest_pref_len, dest, dest_len,
                                 LYS_CONTAINER | LYS_LIST | LYS_LEAF | LYS_AUGMENT, &dst_node);
            if (rc) {
                LOGVAL(LYE_NORESOLV, parent ? LY_VLOG_LYS : LY_VLOG_NONE, parent, path_key_expr);
                return 0;
            }

            if (pke_len == pke_parsed) {
                break;
            }

            if ((i = parse_path_key_expr(path_key_expr+pke_parsed, &dest_pref, &dest_pref_len, &dest, &dest_len,
                                         &dest_parent_times)) < 1) {
                LOGVAL(LYE_INCHAR, parent ? LY_VLOG_LYS : LY_VLOG_NONE, parent,
                       (path_key_expr+pke_parsed)[-i], (path_key_expr+pke_parsed)-i);
                return -parsed;
            }
            pke_parsed += i;
        }

        /* check source - dest match */
        if (dst_node->nodetype != LYS_LEAF) {
            LOGVAL(LYE_NORESOLV, parent ? LY_VLOG_LYS : LY_VLOG_NONE, parent, path-parsed);
            LOGVAL(LYE_SPEC, parent ? LY_VLOG_LYS : LY_VLOG_NONE, parent,
                   "Destination node is not a leaf, but %s.", strnodetype(dst_node->nodetype));
            return -parsed;
        }
    } while (has_predicate);

    return parsed;
}

/**
 * @brief Resolve a path (leafref) in JSON schema context. Logs directly.
 *
 * @param[in] path Path to use.
 * @param[in] parent_node Parent of the leafref.
 * @param[in] parent_tpdf Flag if the parent node is actually typedef, in that case the path
 *            has to contain absolute path
 * @param[out] ret Pointer to the resolved schema node. Can be NULL.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 on error.
 */
static int
resolve_path_arg_schema(const char *path, struct lys_node *parent, int parent_tpdf,
                        const struct lys_node **ret)
{
    const struct lys_node *node;
    const struct lys_module *mod;
    const char *id, *prefix, *name;
    int pref_len, nam_len, parent_times, has_predicate;
    int i, first_iter, rc;

    first_iter = 1;
    parent_times = 0;
    id = path;

    do {
        if ((i = parse_path_arg(id, &prefix, &pref_len, &name, &nam_len, &parent_times, &has_predicate)) < 1) {
            LOGVAL(LYE_INCHAR, parent_tpdf ? LY_VLOG_NONE : LY_VLOG_LYS, parent_tpdf ? NULL : parent, id[-i], &id[-i]);
            return -1;
        }
        id += i;

        if (first_iter) {
            if (parent_times == -1) {
                /* resolve prefix of the module */
                mod = lys_get_import_module(parent->module, NULL, 0, prefix, pref_len);
                /* get start node */
                node = mod ? mod->data : NULL;
                if (!node) {
                    LOGVAL(LYE_NORESOLV, parent_tpdf ? LY_VLOG_NONE : LY_VLOG_LYS, parent_tpdf ? NULL : parent, path);
                    return EXIT_FAILURE;
                }
            } else if (parent_times > 0) {
                /* node is the parent already, skip one ".." */
                if (parent_tpdf) {
                    /* the path is not allowed to contain relative path since we are in top level typedef */
                    LOGVAL(LYE_NORESOLV, 0, NULL, path);
                    return -1;
                }

                node = parent;
                i = 0;
                while (1) {
                    if (!node) {
                        LOGVAL(LYE_NORESOLV, parent_tpdf ? LY_VLOG_NONE : LY_VLOG_LYS, parent_tpdf ? NULL : parent, path);
                        return EXIT_FAILURE;
                    }

                    /* this node is a wrong node, we actually need the augment target */
                    if (node->nodetype == LYS_AUGMENT) {
                        node = ((struct lys_node_augment *)node)->target;
                        if (!node) {
                            continue;
                        }
                    }

                    ++i;
                    if (i == parent_times) {
                        break;
                    }
                    node = lys_parent(node);
                }

                node = node->child;
            } else {
                LOGINT;
                return -1;
            }

            first_iter = 0;
        } else {
            /* move down the tree, if possible */
            if (node->nodetype & (LYS_LEAF | LYS_LEAFLIST | LYS_ANYXML)) {
                LOGVAL(LYE_INCHAR, parent_tpdf ? LY_VLOG_NONE : LY_VLOG_LYS, parent_tpdf ? NULL : parent, name[0], name);
                return -1;
            }
            node = node->child;
        }

        if (!prefix) {
            prefix = lys_node_module(parent)->name;
        }

        rc = lys_get_sibling(node, prefix, pref_len, name, nam_len, LYS_ANY & ~(LYS_USES | LYS_GROUPING), &node);
        if (rc) {
            LOGVAL(LYE_NORESOLV, parent_tpdf ? LY_VLOG_NONE : LY_VLOG_LYS, parent_tpdf ? NULL : parent, path);
            return EXIT_FAILURE;
        }

        if (has_predicate) {
            /* we have predicate, so the current result must be list */
            if (node->nodetype != LYS_LIST) {
                LOGVAL(LYE_NORESOLV, parent_tpdf ? LY_VLOG_NONE : LY_VLOG_LYS, parent_tpdf ? NULL : parent, path);
                return -1;
            }

            i = resolve_path_predicate_schema(id, node, parent);
            if (!i) {
                return EXIT_FAILURE;
            } else if (i < 0) {
                return -1;
            }
            id += i;
        }
    } while (id[0]);

    /* the target must be leaf or leaf-list */
    if (!(node->nodetype & (LYS_LEAF | LYS_LEAFLIST))) {
        LOGVAL(LYE_NORESOLV, parent_tpdf ? LY_VLOG_NONE : LY_VLOG_LYS, parent_tpdf ? NULL : parent, path);
        return -1;
    }

    /* check status */
    if (lyp_check_status(parent->flags, parent->module, parent->name,
                     node->flags, node->module, node->name, node)) {
        return -1;
    }

    if (ret) {
        *ret = node;
    }
    return EXIT_SUCCESS;
}

/**
 * @brief Resolve instance-identifier predicate in JSON data format.
 *        Does not log.
 *
 * @param[in] pred Predicate to use.
 * @param[in,out] node_match Nodes matching the restriction without
 *                           the predicate. Nodes not satisfying
 *                           the predicate are removed.
 *
 * @return Number of characters successfully parsed,
 *         positive on success, negative on failure.
 */
static int
resolve_predicate(const char *pred, struct unres_data *node_match)
{
    /* ... /node[target = value] ... */
    struct unres_data target_match;
    struct ly_ctx *ctx;
    const struct lys_module *mod;
    const char *model, *name, *value;
    char *str;
    int mod_len, nam_len, val_len, i, has_predicate, cur_idx, idx, parsed;
    uint32_t j;

    assert(pred && node_match->count);

    ctx = node_match->node[0]->schema->module->ctx;
    idx = -1;
    parsed = 0;

    do {
        if ((i = parse_predicate(pred, &model, &mod_len, &name, &nam_len, &value, &val_len, &has_predicate)) < 1) {
            return -parsed+i;
        }
        parsed += i;
        pred += i;

        /* pos */
        if (isdigit(name[0])) {
            idx = atoi(name);
        }

        for (cur_idx = 0, j = 0; j < node_match->count; ++cur_idx) {
            /* target */
            memset(&target_match, 0, sizeof target_match);
            if ((name[0] == '.') || !value) {
                target_match.count = 1;
                target_match.node = malloc(sizeof *target_match.node);
                if (!target_match.node) {
                    LOGMEM;
                    return -1;
                }
                target_match.node[0] = node_match->node[j];
            } else {
                str = strndup(model, mod_len);
                mod = ly_ctx_get_module(ctx, str, NULL);
                free(str);

                if (resolve_data(mod, name, nam_len, node_match->node[j]->child, &target_match)) {
                    goto remove_instid;
                }
            }

            /* check that we have the correct type */
            if (name[0] == '.') {
                if (node_match->node[j]->schema->nodetype != LYS_LEAFLIST) {
                    goto remove_instid;
                }
            } else if (value) {
                if (node_match->node[j]->schema->nodetype != LYS_LIST) {
                    goto remove_instid;
                }
            }

            if ((value && (strncmp(((struct lyd_node_leaf_list *)target_match.node[0])->value_str, value, val_len)
                    || ((struct lyd_node_leaf_list *)target_match.node[0])->value_str[val_len]))
                    || (!value && (idx != cur_idx))) {
                goto remove_instid;
            }

            free(target_match.node);

            /* leafref is ok, continue check with next leafref */
            ++j;
            continue;

remove_instid:
            free(target_match.node);

            /* does not fulfill conditions, remove leafref record */
            unres_data_del(node_match, j);
        }
    } while (has_predicate);

    return parsed;
}

/**
 * @brief Resolve instance-identifier in JSON data format. Logs directly.
 *
 * @param[in] data Data node where the path is used
 * @param[in] path Instance-identifier node value.
 *
 * @return Matching node or NULL if no such a node exists. If error occurs, NULL is returned and ly_errno is set.
 */
static struct lyd_node *
resolve_instid(struct lyd_node *data, const char *path)
{
    int i = 0, j;
    struct lyd_node *result = NULL;
    const struct lys_module *mod = NULL;
    struct ly_ctx *ctx = data->schema->module->ctx;
    const char *model, *name;
    char *str;
    int mod_len, name_len, has_predicate;
    struct unres_data node_match;
    uint32_t k;

    memset(&node_match, 0, sizeof node_match);

    /* we need root to resolve absolute path */
    for (; data->parent; data = data->parent);
    /* we're still parsing it and the pointer is not correct yet */
    if (data->prev) {
        for (; data->prev->next; data = data->prev);
    }

    /* search for the instance node */
    while (path[i]) {
        j = parse_instance_identifier(&path[i], &model, &mod_len, &name, &name_len, &has_predicate);
        if (j <= 0) {
            LOGVAL(LYE_INCHAR, LY_VLOG_LYD, data, path[i-j], &path[i-j]);
            goto error;
        }
        i += j;

        str = strndup(model, mod_len);
        if (!str) {
            LOGMEM;
            goto error;
        }
        mod = ly_ctx_get_module(ctx, str, NULL);
        free(str);

        if (!mod) {
            /* no instance exists */
            return NULL;
        }

        if (resolve_data(mod, name, name_len, data, &node_match)) {
            /* no instance exists */
            return NULL;
        }

        if (has_predicate) {
            /* we have predicate, so the current results must be list or leaf-list */
            for (k = 0; k < node_match.count;) {
                if ((node_match.node[k]->schema->nodetype == LYS_LIST &&
                        ((struct lys_node_list *)node_match.node[k]->schema)->keys)
                        || (node_match.node[k]->schema->nodetype == LYS_LEAFLIST)) {
                    /* instid is ok, continue check with next instid */
                    ++k;
                    continue;
                }

                /* does not fulfill conditions, remove inst record */
                unres_data_del(&node_match, k);
            }

            j = resolve_predicate(&path[i], &node_match);
            if (j < 1) {
                LOGVAL(LYE_INPRED, LY_VLOG_LYD, data, &path[i-j]);
                goto error;
            }
            i += j;

            if (!node_match.count) {
                /* no instance exists */
                return NULL;
            }
        }
    }

    if (!node_match.count) {
        /* no instance exists */
        return NULL;
    } else if (node_match.count > 1) {
        /* instance identifier must resolve to a single node */
        LOGVAL(LYE_TOOMANY, LY_VLOG_LYD, data, path, "data tree");

        goto error;
    } else {
        /* we have required result, remember it and cleanup */
        result = node_match.node[0];
        free(node_match.node);

        return result;
    }

error:

    /* cleanup */
    free(node_match.node);

    return NULL;
}

/**
 * @brief Passes config flag down to children. Does not log.
 *
 * @param[in] node Parent node.
 */
static void
inherit_config_flag(struct lys_node *node)
{
    LY_TREE_FOR(node, node) {
        node->flags |= lys_parent(node)->flags & LYS_CONFIG_MASK;
        inherit_config_flag(node->child);
    }
}

/**
 * @brief Resolve augment target. Logs directly.
 *
 * @param[in] aug Augment to use.
 * @param[in] siblings Nodes where to start the search in. If set, uses augment, if not, standalone augment.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 on error.
 */
static int
resolve_augment(struct lys_node_augment *aug, struct lys_node *siblings)
{
    int rc;
    struct lys_node *sub;

    assert(aug);

    /* resolve target node */
    rc = resolve_augment_schema_nodeid(aug->target_name, siblings, (siblings ? NULL : aug->module), (const struct lys_node **)&aug->target);
    if (rc == -1) {
        return -1;
    }
    if (rc > 0) {
        LOGVAL(LYE_INCHAR, LY_VLOG_LYS, aug, aug->target_name[rc - 1], &aug->target_name[rc - 1]);
        return -1;
    }
    if (!aug->target) {
        LOGVAL(LYE_INRESOLV, LY_VLOG_LYS, aug, "augment", aug->target_name);
        return EXIT_FAILURE;
    }

    if (!aug->child) {
        /* nothing to do */
        LOGWRN("Augment \"%s\" without children.", aug->target_name);
        return EXIT_SUCCESS;
    }

    /* check for mandatory nodes - if the target node is in another module
     * the added nodes cannot be mandatory
     */
    if (!aug->parent && (lys_node_module((struct lys_node *)aug) != lys_node_module(aug->target))
            && lyp_check_mandatory((struct lys_node *)aug)) {
        LOGVAL(LYE_INCHILDSTMT, LY_VLOG_LYS, aug, "mandatory", "augment node");
        LOGVAL(LYE_SPEC, LY_VLOG_LYS, aug, "When augmenting data in another module, mandatory nodes are not allowed.");
        return -1;
    }

    /* check augment target type and then augment nodes type */
    if (aug->target->nodetype & (LYS_CONTAINER | LYS_LIST | LYS_CASE | LYS_INPUT | LYS_OUTPUT | LYS_NOTIF)) {
        LY_TREE_FOR(aug->child, sub) {
            if (!(sub->nodetype & (LYS_ANYXML | LYS_CONTAINER | LYS_LEAF | LYS_LIST | LYS_LEAFLIST | LYS_USES | LYS_CHOICE))) {
                LOGVAL(LYE_INCHILDSTMT, LY_VLOG_LYS, aug, strnodetype(sub->nodetype), "augment");
                LOGVAL(LYE_SPEC, LY_VLOG_LYS, aug, "Cannot augment \"%s\" with a \"%s\".",
                       strnodetype(aug->target->nodetype), strnodetype(sub->nodetype));
                return -1;
            }
        }
    } else if (aug->target->nodetype == LYS_CHOICE) {
        LY_TREE_FOR(aug->child, sub) {
            if (!(sub->nodetype & (LYS_CASE | LYS_ANYXML | LYS_CONTAINER | LYS_LEAF | LYS_LIST | LYS_LEAFLIST))) {
                LOGVAL(LYE_INCHILDSTMT, LY_VLOG_LYS, aug, strnodetype(sub->nodetype), "augment");
                LOGVAL(LYE_SPEC, LY_VLOG_LYS, aug, "Cannot augment \"%s\" with a \"%s\".",
                       strnodetype(aug->target->nodetype), strnodetype(sub->nodetype));
                return -1;
            }
        }
    } else {
        LOGVAL(LYE_INARG, LY_VLOG_LYS, aug, aug->target_name, "target-node");
        LOGVAL(LYE_SPEC, LY_VLOG_LYS, aug, "Invalid augment target node type \"%s\".", strnodetype(aug->target->nodetype));
        return -1;
    }

    /* inherit config information from parent, augment does not have
     * config property, but we need to keep the information for subelements
     */
    aug->flags |= aug->target->flags & LYS_CONFIG_MASK;
    LY_TREE_FOR(aug->child, sub) {
        inherit_config_flag(sub);
    }

    /* check identifier uniqueness as in lys_node_addchild() */
    LY_TREE_FOR(aug->child, sub) {
        if (lys_check_id(sub, aug->target, NULL)) {
            return -1;
        }
    }
    /* reconnect augmenting data into the target - add them to the target child list */
    if (aug->target->child) {
        sub = aug->target->child->prev; /* remember current target's last node */
        sub->next = aug->child;         /* connect augmenting data after target's last node */
        aug->target->child->prev = aug->child->prev; /* new target's last node is last augmenting node */
        aug->child->prev = sub;         /* finish connecting of both child lists */
    } else {
        aug->target->child = aug->child;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Resolve uses, apply augments, refines. Logs directly.
 *
 * @param[in] uses Uses to use.
 * @param[in,out] unres List of unresolved items.
 *
 * @return EXIT_SUCCESS on success, -1 on error.
 */
static int
resolve_uses(struct lys_node_uses *uses, struct unres_schema *unres)
{
    struct ly_ctx *ctx;
    struct lys_node *node = NULL, *next, *iter;
    const struct lys_node *node_aux;
    struct lys_refine *rfn;
    struct lys_restr *must, **old_must;
    int i, j, rc;
    uint8_t size, *old_size;

    assert(uses->grp);
    /* HACK just check that the grouping is resolved */
    assert(!uses->grp->nacm);

    /* copy the data nodes from grouping into the uses context */
    LY_TREE_FOR(uses->grp->child, node_aux) {
        node = lys_node_dup(uses->module, (struct lys_node *)uses, node_aux, uses->flags, uses->nacm, unres, 0);
        if (!node) {
            LOGVAL(LYE_INARG, LY_VLOG_LYS, uses, uses->grp->name, "uses");
            LOGVAL(LYE_SPEC, LY_VLOG_LYS, uses, "Copying data from grouping failed.");
            return -1;
        }
    }
    ctx = uses->module->ctx;

    /* we managed to copy the grouping, the rest must be possible to resolve */

    /* apply refines */
    for (i = 0; i < uses->refine_size; i++) {
        rfn = &uses->refine[i];
        rc = resolve_descendant_schema_nodeid(rfn->target_name, uses->child, LYS_NO_RPC_NOTIF_NODE,
                                              1, 0, (const struct lys_node **)&node);
        if (rc || !node) {
            LOGVAL(LYE_INARG, LY_VLOG_LYS, uses, rfn->target_name, "refine");
            return -1;
        }

        if (rfn->target_type && !(node->nodetype & rfn->target_type)) {
            LOGVAL(LYE_INARG, LY_VLOG_LYS, uses, rfn->target_name, "refine");
            LOGVAL(LYE_SPEC, LY_VLOG_LYS, uses, "Refine substatements not applicable to the target-node.");
            return -1;
        }

        /* description on any nodetype */
        if (rfn->dsc) {
            lydict_remove(ctx, node->dsc);
            node->dsc = lydict_insert(ctx, rfn->dsc, 0);
        }

        /* reference on any nodetype */
        if (rfn->ref) {
            lydict_remove(ctx, node->ref);
            node->ref = lydict_insert(ctx, rfn->ref, 0);
        }

        /* config on any nodetype */
        if (rfn->flags & LYS_CONFIG_MASK) {
            if (lys_parent(node) &&
                    ((lys_parent(node)->flags & LYS_CONFIG_MASK) != (rfn->flags & LYS_CONFIG_MASK)) &&
                    (rfn->flags & LYS_CONFIG_W)) {
                /* setting config true under config false is prohibited */
                LOGVAL(LYE_INARG, LY_VLOG_LYS, uses, "config", "refine");
                LOGVAL(LYE_SPEC, LY_VLOG_LYS, uses,
                       "changing config from 'false' to 'true' is prohibited while "
                       "the target's parent is still config 'false'.");
                return -1;
            }

            node->flags &= ~LYS_CONFIG_MASK;
            node->flags |= (rfn->flags & LYS_CONFIG_MASK);

            /* inherit config change to the target children */
            LY_TREE_DFS_BEGIN(node->child, next, iter) {
                if (rfn->flags & LYS_CONFIG_W) {
                    if (iter->flags & LYS_CONFIG_SET) {
                        /* config is set explicitely, go to next sibling */
                        next = NULL;
                        goto nextsibling;
                    }
                } else { /* LYS_CONFIG_R */
                    if ((iter->flags & LYS_CONFIG_SET) && (iter->flags & LYS_CONFIG_W)) {
                        /* error - we would have config data under status data */
                        LOGVAL(LYE_INARG, LY_VLOG_LYS, uses, "config", "refine");
                        LOGVAL(LYE_SPEC, LY_VLOG_LYS, uses,
                               "changing config from 'true' to 'false' is prohibited while the target "
                               "has still a children with explicit config 'true'.");
                        return -1;
                    }
                }
                /* change config */
                iter->flags &= ~LYS_CONFIG_MASK;
                iter->flags |= (rfn->flags & LYS_CONFIG_MASK);

                /* select next iter - modified LY_TREE_DFS_END */
                if (iter->nodetype & (LYS_LEAF | LYS_LEAFLIST | LYS_ANYXML)) {
                    next = NULL;
                } else {
                    next = iter->child;
                }
nextsibling:
                if (!next) {
                    /* no children */
                    if (iter == node->child) {
                        /* we are done, (START) has no children */
                        break;
                    }
                    /* try siblings */
                    next = iter->next;
                }
                while (!next) {
                    /* parent is already processed, go to its sibling */
                    iter = lys_parent(iter);

                    /* no siblings, go back through parents */
                    if (iter == node) {
                        /* we are done, no next element to process */
                        break;
                    }
                    next = iter->next;
                }
            }
        }

        /* default value ... */
        if (rfn->mod.dflt) {
            if (node->nodetype == LYS_LEAF) {
                /* leaf */
                lydict_remove(ctx, ((struct lys_node_leaf *)node)->dflt);
                ((struct lys_node_leaf *)node)->dflt = lydict_insert(ctx, rfn->mod.dflt, 0);
            } else if (node->nodetype == LYS_CHOICE) {
                /* choice */
                rc = resolve_choice_default_schema_nodeid(rfn->mod.dflt, node->child,
                                                          (const struct lys_node **)&((struct lys_node_choice *)node)->dflt);
                if (rc || !((struct lys_node_choice *)node)->dflt) {
                    LOGVAL(LYE_INARG, LY_VLOG_LYS, uses, rfn->mod.dflt, "default");
                    return -1;
                }
            }
        }

        /* mandatory on leaf, anyxml or choice */
        if (rfn->flags & LYS_MAND_MASK) {
            if (node->nodetype & (LYS_LEAF | LYS_ANYXML | LYS_CHOICE)) {
                /* remove current value */
                node->flags &= ~LYS_MAND_MASK;

                /* set new value */
                node->flags |= (rfn->flags & LYS_MAND_MASK);
            }
        }

        /* presence on container */
        if ((node->nodetype & LYS_CONTAINER) && rfn->mod.presence) {
            lydict_remove(ctx, ((struct lys_node_container *)node)->presence);
            ((struct lys_node_container *)node)->presence = lydict_insert(ctx, rfn->mod.presence, 0);
        }

        /* min/max-elements on list or leaf-list */
        if (node->nodetype == LYS_LIST) {
            if (rfn->flags & LYS_RFN_MINSET) {
                ((struct lys_node_list *)node)->min = rfn->mod.list.min;
            }
            if (rfn->flags & LYS_RFN_MAXSET) {
                ((struct lys_node_list *)node)->max = rfn->mod.list.max;
            }
        } else if (node->nodetype == LYS_LEAFLIST) {
            if (rfn->flags & LYS_RFN_MINSET) {
                ((struct lys_node_leaflist *)node)->min = rfn->mod.list.min;
            }
            if (rfn->flags & LYS_RFN_MAXSET) {
                ((struct lys_node_leaflist *)node)->max = rfn->mod.list.max;
            }
        }

        /* must in leaf, leaf-list, list, container or anyxml */
        if (rfn->must_size) {
            switch (node->nodetype) {
            case LYS_LEAF:
                old_size = &((struct lys_node_leaf *)node)->must_size;
                old_must = &((struct lys_node_leaf *)node)->must;
                break;
            case LYS_LEAFLIST:
                old_size = &((struct lys_node_leaflist *)node)->must_size;
                old_must = &((struct lys_node_leaflist *)node)->must;
                break;
            case LYS_LIST:
                old_size = &((struct lys_node_list *)node)->must_size;
                old_must = &((struct lys_node_list *)node)->must;
                break;
            case LYS_CONTAINER:
                old_size = &((struct lys_node_container *)node)->must_size;
                old_must = &((struct lys_node_container *)node)->must;
                break;
            case LYS_ANYXML:
                old_size = &((struct lys_node_anyxml *)node)->must_size;
                old_must = &((struct lys_node_anyxml *)node)->must;
                break;
            default:
                LOGINT;
                return -1;
            }

            size = *old_size + rfn->must_size;
            must = realloc(*old_must, size * sizeof *rfn->must);
            if (!must) {
                LOGMEM;
                return -1;
            }
            for (i = 0, j = *old_size; i < rfn->must_size; i++, j++) {
                must[j].expr = lydict_insert(ctx, rfn->must[i].expr, 0);
                must[j].dsc = lydict_insert(ctx, rfn->must[i].dsc, 0);
                must[j].ref = lydict_insert(ctx, rfn->must[i].ref, 0);
                must[j].eapptag = lydict_insert(ctx, rfn->must[i].eapptag, 0);
                must[j].emsg = lydict_insert(ctx, rfn->must[i].emsg, 0);
            }

            *old_must = must;
            *old_size = size;
        }
    }

    /* apply augments */
    for (i = 0; i < uses->augment_size; i++) {
        rc = resolve_augment(&uses->augment[i], uses->child);
        if (rc) {
            return -1;
        }
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Resolve base identity recursively. Does not log.
 *
 * @param[in] module Main module.
 * @param[in] ident Identity to use.
 * @param[in] basename Base name of the identity.
 * @param[out] ret Pointer to the resolved identity. Can be NULL.
 *
 * @return EXIT_SUCCESS on success (but ret can still be NULL), EXIT_FAILURE on error.
 */
static int
resolve_base_ident_sub(const struct lys_module *module, struct lys_ident *ident, const char *basename,
                       struct lys_ident **ret)
{
    uint32_t i, j;
    struct lys_ident *base = NULL, *base_iter;

    assert(ret);

    /* search module */
    for (i = 0; i < module->ident_size; i++) {
        if (!strcmp(basename, module->ident[i].name)) {

            if (!ident) {
                /* just search for type, so do not modify anything, just return
                 * the base identity pointer */
                *ret = &module->ident[i];
                return EXIT_SUCCESS;
            }

            base = &module->ident[i];
            goto matchfound;
        }
    }

    /* search submodules */
    for (j = 0; j < module->inc_size && module->inc[j].submodule; j++) {
        for (i = 0; i < module->inc[j].submodule->ident_size; i++) {
            if (!strcmp(basename, module->inc[j].submodule->ident[i].name)) {

                if (!ident) {
                    *ret = &module->inc[j].submodule->ident[i];
                    return EXIT_SUCCESS;
                }

                base = &module->inc[j].submodule->ident[i];
                goto matchfound;
            }
        }
    }

matchfound:
    /* we found it somewhere */
    if (base) {
        /* check for circular reference */
        for (base_iter = base; base_iter; base_iter = base_iter->base) {
            if (ident == base_iter) {
                LOGVAL(LYE_INARG, LY_VLOG_NONE, NULL, base_iter->name, "base");
                LOGVAL(LYE_SPEC, LY_VLOG_NONE, NULL, "Circular reference of \"%s\" identity.", basename);
                return EXIT_FAILURE;
            }
        }
        /* checks done, store the result */
        ident->base = base;

        /* maintain backlinks to the derived identitise */
        while (base) {
            /* 1. get current number of backlinks */
            if (base->der) {
                for (i = 0; base->der[i]; i++);
            } else {
                i = 0;
            }
            base->der = ly_realloc(base->der, (i + 2) * sizeof *(base->der));
            if (!base->der) {
                LOGMEM;
                return EXIT_FAILURE;
            }
            base->der[i] = ident;
            base->der[i + 1] = NULL; /* array termination */

            base = base->base;
        }

        *ret = ident->base;
        return EXIT_SUCCESS;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Resolve base identity. Logs directly.
 *
 * @param[in] module Main module.
 * @param[in] ident Identity to use.
 * @param[in] basename Base name of the identity.
 * @param[in] parent Either "type" or "identity".
 * @param[in,out] type Type structure where we want to resolve identity. Can be NULL.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 on error.
 */
static int
resolve_base_ident(const struct lys_module *module, struct lys_ident *ident, const char *basename, const char* parent,
                   struct lys_type *type)
{
    const char *name;
    int i, mod_name_len = 0;
    struct lys_ident *target, **ret;
    uint16_t flags;
    struct lys_module *mod;

    assert((ident && !type) || (!ident && type));

    if (!type) {
        /* have ident to resolve */
        ret = &target;
        flags = ident->flags;
        mod = ident->module;
    } else {
        /* have type to fill */
        ret = &type->info.ident.ref;
        flags = type->parent->flags;
        mod = type->parent->module;
    }
    *ret = NULL;

    /* search for the base identity */
    name = strchr(basename, ':');
    if (name) {
        /* set name to correct position after colon */
        mod_name_len = name - basename;
        name++;

        if (!strncmp(basename, module->name, mod_name_len) && !module->name[mod_name_len]) {
            /* prefix refers to the current module, ignore it */
            mod_name_len = 0;
        }
    } else {
        name = basename;
    }

    /* get module where to search */
    module = lys_get_import_module(module, NULL, 0, mod_name_len ? basename : NULL, mod_name_len);
    if (!module) {
        /* identity refers unknown data model */
        LOGVAL(LYE_INMOD, LY_VLOG_NONE, NULL, basename);
        return -1;
    }

    /* search in the identified module ... */
    if (resolve_base_ident_sub(module, ident, name, ret)) {
        return EXIT_FAILURE;
    } else if (*ret) {
        goto success;
    }
    /* and all its submodules */
    for (i = 0; i < module->inc_size && module->inc[i].submodule; i++) {
        if (resolve_base_ident_sub((struct lys_module *)module->inc[i].submodule, ident, name, ret)) {
            return EXIT_FAILURE;
        } else if (*ret) {
            goto success;
        }
    }

    LOGVAL(LYE_INRESOLV, LY_VLOG_NONE, NULL, parent, basename);
    return EXIT_FAILURE;

success:
    /* check status */
    if (lyp_check_status(flags, mod, ident ? ident->name : "of type",
                         (*ret)->flags, (*ret)->module, (*ret)->name, NULL)) {
        return -1;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Resolve JSON data format identityref. Logs directly.
 *
 * @param[in] base Base identity.
 * @param[in] ident_name Identityref name.
 * @param[in] node Node where the identityref is being resolved
 *
 * @return Pointer to the identity resolvent, NULL on error.
 */
struct lys_ident *
resolve_identref(struct lys_ident *base, const char *ident_name, struct lyd_node *node)
{
    const char *mod_name, *name;
    int mod_name_len, rc;
    int i;
    struct lys_ident *der;

    if (!base || !ident_name) {
        return NULL;
    }

    rc = parse_node_identifier(ident_name, &mod_name, &mod_name_len, &name, NULL);
    if (rc < 1) {
        LOGVAL(LYE_INCHAR, LY_VLOG_LYD, node, ident_name[-rc], &ident_name[-rc]);
        return NULL;
    } else if (rc < (signed)strlen(ident_name)) {
        LOGVAL(LYE_INCHAR, LY_VLOG_LYD, node, ident_name[rc], &ident_name[rc]);
        return NULL;
    }

    if (!strcmp(base->name, name) && (!mod_name
            || (!strncmp(base->module->name, mod_name, mod_name_len) && !base->module->name[mod_name_len]))) {
        return base;
    }

    if (base->der) {
        for (der = base->der[i = 0]; base->der[i]; der = base->der[++i]) {
            if (!strcmp(der->name, name) &&
                    (!mod_name || (!strncmp(der->module->name, mod_name, mod_name_len) && !der->module->name[mod_name_len]))) {
                /* we have match */
                return der;
            }
        }
    }

    LOGVAL(LYE_INRESOLV, LY_VLOG_LYD, node, "identityref", ident_name);
    return NULL;
}

/**
 * @brief Resolve (find) choice default case. Does not log.
 *
 * @param[in] choic Choice to use.
 * @param[in] dflt Name of the default case.
 *
 * @return Pointer to the default node or NULL.
 */
static struct lys_node *
resolve_choice_dflt(struct lys_node_choice *choic, const char *dflt)
{
    struct lys_node *child, *ret;

    LY_TREE_FOR(choic->child, child) {
        if (child->nodetype == LYS_USES) {
            ret = resolve_choice_dflt((struct lys_node_choice *)child, dflt);
            if (ret) {
                return ret;
            }
        }

        if (ly_strequal(child->name, dflt, 1) && (child->nodetype & (LYS_ANYXML | LYS_CASE
                | LYS_CONTAINER | LYS_LEAF | LYS_LEAFLIST | LYS_LIST))) {
            return child;
        }
    }

    return NULL;
}

/**
 * @brief Resolve unresolved uses. Logs directly.
 *
 * @param[in] uses Uses to use.
 * @param[in] unres Specific unres item.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 on error.
 */
static int
resolve_unres_schema_uses(struct lys_node_uses *uses, struct unres_schema *unres)
{
    int rc;
    struct lys_node *par_grp;

    /* HACK: when a grouping has uses inside, all such uses have to be resolved before the grouping itself
     *       is used in some uses. When we see such a uses, the grouping's nacm member (not used in grouping)
     *       is used to store number of so far unresolved uses. The grouping cannot be used unless the nacm
     *       value is decreased back to 0. To remember that the uses already increased grouping's nacm, the
     *       LYS_USESGRP flag is used. */
    for (par_grp = lys_parent((struct lys_node *)uses); par_grp && (par_grp->nodetype != LYS_GROUPING); par_grp = lys_parent(par_grp));

    if (!uses->grp) {
        rc = resolve_uses_schema_nodeid(uses->name, (const struct lys_node *)uses, (const struct lys_node_grp **)&uses->grp);
        if (rc == -1) {
            LOGVAL(LYE_INRESOLV, LY_VLOG_LYS, uses, "grouping", uses->name);
            return -1;
        } else if (rc > 0) {
            LOGVAL(LYE_INCHAR, LY_VLOG_LYS, uses, uses->name[rc - 1], &uses->name[rc - 1]);
            return -1;
        } else if (!uses->grp) {
            if (par_grp && !(uses->flags & LYS_USESGRP)) {
                /* hack - in contrast to lys_node, lys_node_grp has bigger nacm field
                 * (and smaller flags - it uses only a limited set of flags)
                 */
                ((struct lys_node_grp *)par_grp)->nacm++;
                uses->flags |= LYS_USESGRP;
            }
            return EXIT_FAILURE;
        }
    }

    if (uses->grp->nacm) {
        if (par_grp && !(uses->flags & LYS_USESGRP)) {
            ((struct lys_node_grp *)par_grp)->nacm++;
            uses->flags |= LYS_USESGRP;
        }
        return EXIT_FAILURE;
    }

    rc = resolve_uses(uses, unres);
    if (!rc) {
        /* decrease unres count only if not first try */
        if (par_grp && (uses->flags & LYS_USESGRP)) {
            if (!((struct lys_node_grp *)par_grp)->nacm) {
                LOGINT;
                return -1;
            }
            ((struct lys_node_grp *)par_grp)->nacm--;
            uses->flags &= ~LYS_USESGRP;
        }

        /* check status */
        if (lyp_check_status(uses->flags, uses->module, "of uses",
                         uses->grp->flags, uses->grp->module, uses->grp->name,
                         (struct lys_node *)uses)) {
            return -1;
        }

        return EXIT_SUCCESS;
    } else if ((rc == EXIT_FAILURE) && par_grp && !(uses->flags & LYS_USESGRP)) {
        ((struct lys_node_grp *)par_grp)->nacm++;
        uses->flags |= LYS_USESGRP;
    }

    return rc;
}

/**
 * @brief Resolve list keys. Logs directly.
 *
 * @param[in] list List to use.
 * @param[in] keys_str Keys node value.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 on error.
 */
static int
resolve_list_keys(struct lys_node_list *list, const char *keys_str)
{
    int i, len, rc;
    const char *value;

    for (i = 0; i < list->keys_size; ++i) {
        /* get the key name */
        if ((value = strpbrk(keys_str, " \t\n"))) {
            len = value - keys_str;
            while (isspace(value[0])) {
                value++;
            }
        } else {
            len = strlen(keys_str);
        }

        rc = lys_get_sibling(list->child, lys_main_module(list->module)->name, 0, keys_str, len, LYS_LEAF, (const struct lys_node **)&list->keys[i]);
        if (rc) {
            LOGVAL(LYE_INRESOLV, LY_VLOG_LYS, list, "list keys", keys_str);
            return EXIT_FAILURE;
        }

        if (check_key(list, i, keys_str, len)) {
            /* check_key logs */
            return -1;
        }

        /* check status */
        if (lyp_check_status(list->flags, list->module, list->name,
                             list->keys[i]->flags, list->keys[i]->module, list->keys[i]->name,
                             (struct lys_node *)list->keys[i])) {
            return -1;
        }

        /* prepare for next iteration */
        while (value && isspace(value[0])) {
            value++;
        }
        keys_str = value;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Resolve (check) all must conditions of \p node.
 * Logs directly.
 *
 * @param[in] node Data node with optional must statements.
 *
 * @return EXIT_SUCCESS on pass, EXIT_FAILURE on fail, -1 on error.
 */
static int
resolve_must(struct lyd_node *node)
{
    uint8_t i, must_size;
    struct lys_restr *must;
    struct lyxp_set set;

    assert(node);
    memset(&set, 0, sizeof set);

    switch (node->schema->nodetype) {
    case LYS_CONTAINER:
        must_size = ((struct lys_node_container *)node->schema)->must_size;
        must = ((struct lys_node_container *)node->schema)->must;
        break;
    case LYS_LEAF:
        must_size = ((struct lys_node_leaf *)node->schema)->must_size;
        must = ((struct lys_node_leaf *)node->schema)->must;
        break;
    case LYS_LEAFLIST:
        must_size = ((struct lys_node_leaflist *)node->schema)->must_size;
        must = ((struct lys_node_leaflist *)node->schema)->must;
        break;
    case LYS_LIST:
        must_size = ((struct lys_node_list *)node->schema)->must_size;
        must = ((struct lys_node_list *)node->schema)->must;
        break;
    case LYS_ANYXML:
        must_size = ((struct lys_node_anyxml *)node->schema)->must_size;
        must = ((struct lys_node_anyxml *)node->schema)->must;
        break;
    default:
        must_size = 0;
        break;
    }

    for (i = 0; i < must_size; ++i) {
        if (lyxp_eval(must[i].expr, node, &set, LYXP_MUST)) {
            return -1;
        }

        lyxp_set_cast(&set, LYXP_SET_BOOLEAN, node, LYXP_MUST);

        if (!set.val.bool) {
            LOGVAL(LYE_NOMUST, LY_VLOG_LYD, node, must[i].expr);
            if (must[i].emsg) {
                LOGVAL(LYE_SPEC, LY_VLOG_LYD, node, must[i].emsg);
            }
            if (must[i].eapptag) {
                strncpy(((struct ly_err *)&ly_errno)->apptag, must[i].eapptag, LY_APPTAG_LEN - 1);
            }
            return 1;
        }
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Resolve (find) when condition context node. Does not log.
 *
 * @param[in] node Data node, whose conditional definition is being decided.
 * @param[in] schema Schema node with a when condition.
 *
 * @return Context node.
 */
static struct lyd_node *
resolve_when_ctx_node(struct lyd_node *node, struct lys_node *schema)
{
    struct lyd_node *parent;
    struct lys_node *sparent;
    uint16_t i, data_depth, schema_depth;

    /* find a not schema-only node */
    while (schema->nodetype & (LYS_USES | LYS_CHOICE | LYS_CASE | LYS_AUGMENT | LYS_INPUT | LYS_OUTPUT)) {
        schema = lys_parent(schema);
        if (!schema) {
            return NULL;
        }
    }

    /* get node depths */
    for (parent = node, data_depth = 0; parent; parent = parent->parent, ++data_depth);
    for (sparent = lys_parent(schema), schema_depth = 1; sparent; sparent = lys_parent(sparent)) {
        if (sparent->nodetype & (LYS_CONTAINER | LYS_LEAF | LYS_LEAFLIST | LYS_LIST | LYS_ANYXML | LYS_NOTIF | LYS_RPC)) {
            ++schema_depth;
        }
    }
    if (data_depth < schema_depth) {
        return NULL;
    }

    /* find the corresponding data node */
    for (i = 0; i < data_depth - schema_depth; ++i) {
        node = node->parent;
    }
    if (node->schema != schema) {
        return NULL;
    }

    return node;
}

int
resolve_applies_must(const struct lyd_node *node)
{
    switch (node->schema->nodetype) {
    case LYS_CONTAINER:
        return ((struct lys_node_container *)node->schema)->must_size;
    case LYS_LEAF:
        return ((struct lys_node_leaf *)node->schema)->must_size;
    case LYS_LEAFLIST:
        return ((struct lys_node_leaflist *)node->schema)->must_size;
    case LYS_LIST:
        return ((struct lys_node_list *)node->schema)->must_size;
    case LYS_ANYXML:
        return ((struct lys_node_anyxml *)node->schema)->must_size;
    default:
        return 0;
    }
}

int
resolve_applies_when(const struct lyd_node *node)
{
    struct lys_node *parent;

    assert(node);

    if (!(node->schema->nodetype & (LYS_NOTIF | LYS_RPC)) && (((struct lys_node_container *)node->schema)->when)) {
        return 1;
    }

    parent = node->schema;
    goto check_augment;

    while (parent && (parent->nodetype & (LYS_USES | LYS_CHOICE | LYS_CASE))) {
        if (((struct lys_node_uses *)parent)->when) {
            return 1;
        }
check_augment:

        if ((parent->parent && (parent->parent->nodetype == LYS_AUGMENT) &&
                (((struct lys_node_augment *)parent->parent)->when))) {

        }
        parent = lys_parent(parent);
    }

    return 0;
}

/**
 * @brief Resolve (check) all when conditions relevant for \p node.
 * Logs directly.
 *
 * @param[in] node Data node, whose conditional reference, if such, is being decided.
 *
 * @return
 *  -1 - error, ly_errno is set
 *   0 - true "when" statement
 *   0, ly_vecode = LYVE_NOCOND - false "when" statement
 *   1, ly_vecode = LYVE_INWHEN - nodes needed to resolve are conditional and not yet resolved (under another "when")
 */
static int
resolve_when(struct lyd_node *node)
{
    struct lyd_node *ctx_node = NULL;
    struct lys_node *parent;
    struct lyxp_set set;
    int rc = 0;

    assert(node);
    memset(&set, 0, sizeof set);

    if (!(node->schema->nodetype & (LYS_NOTIF | LYS_RPC)) && (((struct lys_node_container *)node->schema)->when)) {
        rc = lyxp_eval(((struct lys_node_container *)node->schema)->when->cond, node, &set, LYXP_WHEN);
        if (rc) {
            if (rc == 1) {
                LOGVAL(LYE_INWHEN, LY_VLOG_LYD, node, ((struct lys_node_container *)node->schema)->when->cond);
            }
            goto cleanup;
        }

        /* set boolean result of the condition */
        lyxp_set_cast(&set, LYXP_SET_BOOLEAN, node, LYXP_WHEN);
        if (!set.val.bool) {
            ly_vlog_hide(1);
            LOGVAL(LYE_NOWHEN, LY_VLOG_LYD, node, ((struct lys_node_container *)node->schema)->when->cond);
            ly_vlog_hide(0);
            node->when_status |= LYD_WHEN_FALSE;
            goto cleanup;
        }

        /* free xpath set content */
        lyxp_set_cast(&set, LYXP_SET_EMPTY, node, 0);
    }

    parent = node->schema;
    goto check_augment;

    /* check when in every schema node that affects node */
    while (parent && (parent->nodetype & (LYS_USES | LYS_CHOICE | LYS_CASE))) {
        if (((struct lys_node_uses *)parent)->when) {
            if (!ctx_node) {
                ctx_node = resolve_when_ctx_node(node, parent);
                if (!ctx_node) {
                    LOGINT;
                    rc = -1;
                    goto cleanup;
                }
            }
            rc = lyxp_eval(((struct lys_node_uses *)parent)->when->cond, ctx_node, &set, LYXP_WHEN);
            if (rc) {
                if (rc == 1) {
                    LOGVAL(LYE_INWHEN, LY_VLOG_LYD, node, ((struct lys_node_uses *)parent)->when->cond);
                }
                goto cleanup;
            }

            lyxp_set_cast(&set, LYXP_SET_BOOLEAN, ctx_node, LYXP_WHEN);
            if (!set.val.bool) {
                ly_vlog_hide(1);
                LOGVAL(LYE_NOWHEN, LY_VLOG_LYD, node, ((struct lys_node_uses *)parent)->when->cond);
                ly_vlog_hide(0);
                node->when_status |= LYD_WHEN_FALSE;
                goto cleanup;
            }

            /* free xpath set content */
            lyxp_set_cast(&set, LYXP_SET_EMPTY, ctx_node, 0);
        }

check_augment:
        if ((parent->parent && (parent->parent->nodetype == LYS_AUGMENT) && (((struct lys_node_augment *)parent->parent)->when))) {
            if (!ctx_node) {
                ctx_node = resolve_when_ctx_node(node, parent->parent);
                if (!ctx_node) {
                    LOGINT;
                    rc = -1;
                    goto cleanup;
                }
            }
            rc = lyxp_eval(((struct lys_node_augment *)parent->parent)->when->cond, ctx_node, &set, LYXP_WHEN);
            if (rc) {
                if (rc == 1) {
                    LOGVAL(LYE_INWHEN, LY_VLOG_LYD, node, ((struct lys_node_augment *)parent->parent)->when->cond);
                }
                goto cleanup;
            }

            lyxp_set_cast(&set, LYXP_SET_BOOLEAN, ctx_node, LYXP_WHEN);

            if (!set.val.bool) {
                ly_vlog_hide(1);
                LOGVAL(LYE_NOWHEN, LY_VLOG_LYD, node, ((struct lys_node_augment *)parent->parent)->when->cond);
                ly_vlog_hide(0);
                node->when_status |= LYD_WHEN_FALSE;
               goto cleanup;
            }

            /* free xpath set content */
            lyxp_set_cast(&set, LYXP_SET_EMPTY, ctx_node, 0);
        }

        parent = lys_parent(parent);
    }

    node->when_status |= LYD_WHEN_TRUE;

cleanup:

    /* free xpath set content */
    lyxp_set_cast(&set, LYXP_SET_EMPTY, ctx_node ? ctx_node : node, 0);

    return rc;
}

/**
 * @brief Resolve a single unres schema item. Logs indirectly.
 *
 * @param[in] mod Main module.
 * @param[in] item Item to resolve. Type determined by \p type.
 * @param[in] type Type of the unresolved item.
 * @param[in] str_snode String, a schema node, or NULL.
 * @param[in] unres Unres schema structure to use.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 on error.
 */
static int
resolve_unres_schema_item(struct lys_module *mod, void *item, enum UNRES_ITEM type, void *str_snode,
                          struct unres_schema *unres)
{
    int rc = -1, has_str = 0, tpdf_flag = 0;
    struct lys_node *node;
    const char *base_name;

    struct lys_ident *ident;
    struct lys_type *stype;
    struct lys_feature **feat_ptr;
    struct lys_node_choice *choic;
    struct lyxml_elem *yin;
    struct yang_type *yang;

    switch (type) {
    case UNRES_IDENT:
        base_name = str_snode;
        has_str = 1;
        ident = item;

        rc = resolve_base_ident(mod, ident, base_name, "identity", NULL);
        break;
    case UNRES_TYPE_IDENTREF:
        base_name = str_snode;
        has_str = 1;
        stype = item;

        rc = resolve_base_ident(mod, NULL, base_name, "type", stype);
        break;
    case UNRES_TYPE_LEAFREF:
        node = str_snode;
        stype = item;

        /* HACK - when there is no parent, we are in top level typedef and in that
         * case, the path has to contain absolute path, so we let the resolve_path_arg_schema()
         * know it via tpdf_flag */
        if (!node) {
            tpdf_flag = 1;
            node = (struct lys_node *)stype->parent;
        }

        rc = resolve_path_arg_schema(stype->info.lref.path, node, tpdf_flag,
                                     (const struct lys_node **)&stype->info.lref.target);
        if (stype->info.lref.target) {
            /* store the backlink from leafref target */
            if (!stype->info.lref.target->child) {
                stype->info.lref.target->child = (void*)ly_set_new();
                if (!stype->info.lref.target->child) {
                    LOGMEM;
                    return -1;
                }
            }
            ly_set_add((struct ly_set *)stype->info.lref.target->child, stype->parent);
        }

        break;
    case UNRES_TYPE_DER:
        /* parent */
        node = str_snode;
        stype = item;

        /* HACK type->der is temporarily unparsed type statement */
        yin = (struct lyxml_elem *)stype->der;
        stype->der = NULL;

        if (yin->flags & LY_YANG_STRUCTURE_FLAG) {
            yang = (struct yang_type *)yin;
            rc = yang_check_type(mod, node, yang, unres);

            if (rc) {
                if (rc == -1) {
                    yang->type->base = yang->base;
                    lydict_remove(mod->ctx, yang->name);
                    free(yang);
                    stype->der = NULL;
                } else {
                    /* may try again later */
                    stype->der = (struct lys_tpdf *)yang;
                }
            } else {
                /* we need to always be able to free this, it's safe only in this case */
                lydict_remove(mod->ctx, yang->name);
                free(yang);
            }

        } else {
            rc = fill_yin_type(mod, node, yin, stype, unres);
            if (!rc) {
                /* we need to always be able to free this, it's safe only in this case */
                lyxml_free(mod->ctx, yin);
            } else {
                /* may try again later, put all back how it was */
                stype->der = (struct lys_tpdf *)yin;
            }
        }
        break;
    case UNRES_IFFEAT:
        base_name = str_snode;
        has_str = 1;
        feat_ptr = item;

        rc = resolve_feature(base_name, mod, feat_ptr);
        break;
    case UNRES_USES:
        rc = resolve_unres_schema_uses(item, unres);
        break;
    case UNRES_TYPE_DFLT:
        base_name = str_snode;
        has_str = 1;
        stype = item;

        rc = check_default(stype, base_name, mod);
        break;
    case UNRES_CHOICE_DFLT:
        base_name = str_snode;
        has_str = 1;
        choic = item;

        choic->dflt = resolve_choice_dflt(choic, base_name);
        if (choic->dflt) {
            rc = EXIT_SUCCESS;
        } else {
            rc = EXIT_FAILURE;
        }
        break;
    case UNRES_LIST_KEYS:
        has_str = 1;
        rc = resolve_list_keys(item, str_snode);
        break;
    case UNRES_LIST_UNIQ:
        has_str = 1;
        rc = resolve_unique(item, str_snode);
        break;
    case UNRES_AUGMENT:
        rc = resolve_augment(item, NULL);
        break;
    default:
        LOGINT;
        break;
    }

    if (has_str && !rc) {
        lydict_remove(mod->ctx, str_snode);
    }

    return rc;
}

/* logs directly */
static void
print_unres_schema_item_fail(void *item, enum UNRES_ITEM type, void *str_node)
{
    switch (type) {
    case UNRES_IDENT:
        LOGVRB("Resolving %s \"%s\" failed, it will be attempted later.", "identity", (char *)str_node);
        break;
    case UNRES_TYPE_IDENTREF:
        LOGVRB("Resolving %s \"%s\" failed, it will be attempted later.", "identityref", (char *)str_node);
        break;
    case UNRES_TYPE_LEAFREF:
        LOGVRB("Resolving %s \"%s\" failed, it will be attempted later.", "leafref",
               ((struct lys_type *)item)->info.lref.path);
        break;
    case UNRES_TYPE_DER:
        LOGVRB("Resolving %s \"%s\" failed, it will be attempted later.", "derived type",
               ((struct lyxml_elem *)((struct lys_type *)item)->der)->attr->value);
        break;
    case UNRES_IFFEAT:
        LOGVRB("Resolving %s \"%s\" failed, it will be attempted later.", "if-feature", (char *)str_node);
        break;
    case UNRES_USES:
        LOGVRB("Resolving %s \"%s\" failed, it will be attempted later.", "uses", ((struct lys_node_uses *)item)->name);
        break;
    case UNRES_TYPE_DFLT:
        LOGVRB("Resolving %s \"%s\" failed, it will be attempted later.", "type default", (char *)str_node);
        break;
    case UNRES_CHOICE_DFLT:
        LOGVRB("Resolving %s \"%s\" failed, it will be attempted later.", "choice default", (char *)str_node);
        break;
    case UNRES_LIST_KEYS:
        LOGVRB("Resolving %s \"%s\" failed, it will be attempted later.", "list keys", (char *)str_node);
        break;
    case UNRES_LIST_UNIQ:
        LOGVRB("Resolving %s \"%s\" failed, it will be attempted later.", "list unique", (char *)str_node);
        break;
    case UNRES_AUGMENT:
        LOGVRB("Resolving %s \"%s\" failed, it will be attempted later.", "augment target",
               ((struct lys_node_augment *)item)->target_name);
        break;
    default:
        LOGINT;
        break;
    }
}

/**
 * @brief Resolve every unres schema item in the structure. Logs directly.
 *
 * @param[in] mod Main module.
 * @param[in] unres Unres schema structure to use.
 *
 * @return EXIT_SUCCESS on success, -1 on error.
 */
int
resolve_unres_schema(struct lys_module *mod, struct unres_schema *unres)
{
    uint32_t i, resolved = 0, unres_count, res_count;
    int rc;

    assert(unres);

    LOGVRB("Resolving unresolved schema nodes and their constraints...");
    ly_vlog_hide(1);

    /* uses */
    do {
        unres_count = 0;
        res_count = 0;

        for (i = 0; i < unres->count; ++i) {
            /* we do not need to have UNRES_TYPE_IDENTREF or UNRES_TYPE_LEAFREF resolved,
             * we need every type's base only */
            if ((unres->type[i] != UNRES_USES) && (unres->type[i] != UNRES_TYPE_DER)) {
                continue;
            }

            ++unres_count;
            rc = resolve_unres_schema_item(mod, unres->item[i], unres->type[i], unres->str_snode[i], unres);
            if (!rc) {
                unres->type[i] = UNRES_RESOLVED;
                ++resolved;
                ++res_count;
            } else if (rc == -1) {
                ly_vlog_hide(0);
                return -1;
            }
        }
    } while (res_count && (res_count < unres_count));

    if (res_count < unres_count) {
        return -1;
    }

    /* the rest */
    for (i = 0; i < unres->count; ++i) {
        if (unres->type[i] == UNRES_RESOLVED) {
            continue;
        }

        rc = resolve_unres_schema_item(mod, unres->item[i], unres->type[i], unres->str_snode[i], unres);
        if (rc == 0) {
            unres->type[i] = UNRES_RESOLVED;
            ++resolved;
        } else if (rc == -1) {
            ly_vlog_hide(0);
            return rc;
        }
    }

    ly_vlog_hide(0);

    if (resolved < unres->count) {
        /* try to resolve the unresolved nodes again, it will not resolve anything, but it will print
         * all the validation errors
         */
        for (i = 0; i < unres->count; ++i) {
            if (unres->type[i] == UNRES_RESOLVED) {
                continue;
            }
            resolve_unres_schema_item(mod, unres->item[i], unres->type[i], unres->str_snode[i], unres);
        }
        return -1;
    }

    LOGVRB("All schema nodes and constraints resolved.");
    unres->count = 0;
    return EXIT_SUCCESS;
}

/**
 * @brief Try to resolve an unres schema item with a string argument. Logs indirectly.
 *
 * @param[in] mod Main module.
 * @param[in] unres Unres schema structure to use.
 * @param[in] item Item to resolve. Type determined by \p type.
 * @param[in] type Type of the unresolved item.
 * @param[in] str String argument.
 *
 * @return EXIT_SUCCESS on success or storing the item in unres, -1 on error.
 */
int
unres_schema_add_str(struct lys_module *mod, struct unres_schema *unres, void *item, enum UNRES_ITEM type,
                     const char *str)
{
    return unres_schema_add_node(mod, unres, item, type, (struct lys_node *)lydict_insert(mod->ctx, str, 0));
}

/**
 * @brief Try to resolve an unres schema item with a schema node argument. Logs indirectly.
 *
 * @param[in] mod Main module.
 * @param[in] unres Unres schema structure to use.
 * @param[in] item Item to resolve. Type determined by \p type.
 * @param[in] type Type of the unresolved item. UNRES_TYPE_DER is handled specially!
 * @param[in] snode Schema node argument.
 *
 * @return EXIT_SUCCESS on success or storing the item in unres, -1 on error.
 */
int
unres_schema_add_node(struct lys_module *mod, struct unres_schema *unres, void *item, enum UNRES_ITEM type,
                      struct lys_node *snode)
{
    int rc;
    struct lyxml_elem *yin;
    char *path, *msg;

    assert(unres && item && ((type != UNRES_LEAFREF) && (type != UNRES_INSTID) && (type != UNRES_WHEN)
           && (type != UNRES_MUST)));

    ly_vlog_hide(1);
    rc = resolve_unres_schema_item(mod, item, type, snode, unres);
    ly_vlog_hide(0);
    if (rc != EXIT_FAILURE) {
        if (rc == -1 && ly_errno == LY_EVALID) {
            path = strdup(ly_errpath());
            LOGERR(LY_EVALID, "%s%s%s%s", msg = strdup(ly_errmsg()),
                   path[0] ? " (path: " : "", path[0] ? path : "", path[0] ? ")" : "");
            free(path);
            free(msg);
        }
        return rc;
    }

    print_unres_schema_item_fail(item, type, snode);

    /* HACK unlinking is performed here so that we do not do any (NS) copying in vain */
    if (type == UNRES_TYPE_DER) {
        yin = (struct lyxml_elem *)((struct lys_type *)item)->der;
        if (!(yin->flags & LY_YANG_STRUCTURE_FLAG)) {
            lyxml_unlink_elem(mod->ctx, yin, 1);
            ((struct lys_type *)item)->der = (struct lys_tpdf *)yin;
        }
    }

    unres->count++;
    unres->item = ly_realloc(unres->item, unres->count*sizeof *unres->item);
    if (!unres->item) {
        LOGMEM;
        return -1;
    }
    unres->item[unres->count-1] = item;
    unres->type = ly_realloc(unres->type, unres->count*sizeof *unres->type);
    if (!unres->type) {
        LOGMEM;
        return -1;
    }
    unres->type[unres->count-1] = type;
    unres->str_snode = ly_realloc(unres->str_snode, unres->count*sizeof *unres->str_snode);
    if (!unres->str_snode) {
        LOGMEM;
        return -1;
    }
    unres->str_snode[unres->count-1] = snode;
    unres->module = ly_realloc(unres->module, unres->count*sizeof *unres->module);
    if (!unres->module) {
        LOGMEM;
        return -1;
    }
    unres->module[unres->count-1] = mod;

    return EXIT_SUCCESS;
}

/**
 * @brief Duplicate an unres schema item. Logs indirectly.
 *
 * @param[in] mod Main module.
 * @param[in] unres Unres schema structure to use.
 * @param[in] item Old item to be resolved.
 * @param[in] type Type of the old unresolved item.
 * @param[in] new_item New item to use in the duplicate.
 *
 * @return EXIT_SUCCESS on success, -1 on error.
 */
int
unres_schema_dup(struct lys_module *mod, struct unres_schema *unres, void *item, enum UNRES_ITEM type, void *new_item)
{
    int i;

    assert(item && new_item && ((type != UNRES_LEAFREF) && (type != UNRES_INSTID) && (type != UNRES_WHEN)));

    i = unres_schema_find(unres, item, type);

    if (i == -1) {
        return -1;
    }

    if ((type == UNRES_TYPE_LEAFREF) || (type == UNRES_USES) || (type == UNRES_TYPE_DFLT)) {
        if (unres_schema_add_node(mod, unres, new_item, type, unres->str_snode[i]) == -1) {
            LOGINT;
            return -1;
        }
    } else {
        if (unres_schema_add_str(mod, unres, new_item, type, unres->str_snode[i]) == -1) {
            LOGINT;
            return -1;
        }
    }

    return EXIT_SUCCESS;
}

/* does not log */
int
unres_schema_find(struct unres_schema *unres, void *item, enum UNRES_ITEM type)
{
    uint32_t ret = -1, i;

    for (i = 0; i < unres->count; ++i) {
        if ((unres->item[i] == item) && (unres->type[i] == type)) {
            ret = i;
            break;
        }
    }

    return ret;
}

void
unres_schema_free(struct lys_module *module, struct unres_schema **unres)
{
    uint32_t i;
    unsigned int unresolved = 0;
    struct lyxml_elem *yin;
    struct yang_type *yang;

    if (!unres || !(*unres)) {
        return;
    }

    assert(module || (*unres)->count == 0);

    for (i = 0; i < (*unres)->count; ++i) {
        if ((*unres)->module[i] != module) {
            if ((*unres)->type[i] != UNRES_RESOLVED) {
                unresolved++;
            }
            continue;
        }
        if ((*unres)->type[i] == UNRES_TYPE_DER) {
            yin = (struct lyxml_elem *)((struct lys_type *)(*unres)->item[i])->der;
            if (yin->flags & LY_YANG_STRUCTURE_FLAG) {
                yang =(struct yang_type *)yin;
                yang->type->base = yang->base;
                lydict_remove(module->ctx, yang->name);
                free(yang);
            } else {
                lyxml_free(module->ctx, yin);
            }
        }
        (*unres)->type[i] = UNRES_RESOLVED;
    }

    if (!module || (!unresolved && !module->type)) {
        free((*unres)->item);
        free((*unres)->type);
        free((*unres)->str_snode);
        free((*unres)->module);
        free((*unres));
        (*unres) = NULL;
    }
}

/**
 * @brief Resolve a single unres data item. Logs directly.
 *
 * @param[in] node Data node to resolve.
 * @param[in] type Type of the unresolved item.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on forward reference, -1 on error.
 */
int
resolve_unres_data_item(struct lyd_node *node, enum UNRES_ITEM type)
{
    uint32_t i;
    int rc;
    struct lyd_node_leaf_list *leaf;
    struct lys_node_leaf *sleaf;
    struct lyd_node *parent;
    struct unres_data matches;

    memset(&matches, 0, sizeof matches);
    leaf = (struct lyd_node_leaf_list *)node;
    sleaf = (struct lys_node_leaf *)leaf->schema;

    switch (type) {
    case UNRES_LEAFREF:
        assert(sleaf->type.base == LY_TYPE_LEAFREF);
        /* EXIT_FAILURE return keeps leaf->value.lefref NULL, handled later */
        if (resolve_path_arg_data(node, sleaf->type.info.lref.path, &matches) == -1) {
            return -1;
        }

        /* check that value matches */
        for (i = 0; i < matches.count; ++i) {
            if (ly_strequal(leaf->value_str, ((struct lyd_node_leaf_list *)matches.node[i])->value_str, 1)) {
                leaf->value.leafref = matches.node[i];
                break;
            }
        }

        free(matches.node);

        if (!leaf->value.leafref) {
            /* reference not found */
            LOGVAL(LYE_NOLEAFREF, LY_VLOG_LYD, leaf, sleaf->type.info.lref.path, leaf->value_str);
            return EXIT_FAILURE;
        }
        break;

    case UNRES_INSTID:
        assert(sleaf->type.base == LY_TYPE_INST);
        ly_errno = 0;
        leaf->value.instance = resolve_instid(node, leaf->value_str);
        if (!leaf->value.instance) {
            if (ly_errno) {
                return -1;
            } else if (sleaf->type.info.inst.req > -1) {
                LOGVAL(LYE_NOREQINS, LY_VLOG_LYD, leaf, leaf->value_str);
                return EXIT_FAILURE;
            } else {
                LOGVRB("There is no instance of \"%s\", but it is not required.", leaf->value_str);
            }
        }
        break;

    case UNRES_WHEN:
        if ((rc = resolve_when(node))) {
            return rc;
        }
        break;

    case UNRES_MUST:
        if ((rc = resolve_must(node))) {
            return rc;
        }
        break;

    case UNRES_EMPTYCONT:
        do {
            parent = node->parent;
            lyd_free(node);
            node = parent;
        } while (node && (node->schema->nodetype == LYS_CONTAINER) && !node->child
                 && !((struct lys_node_container *)node->schema)->presence);
        break;

    default:
        LOGINT;
        return -1;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief add data unres item
 *
 * @param[in] unres Unres data structure to use.
 * @param[in] node Data node to use.
 *
 * @return 0 on success, -1 on error.
 */
int
unres_data_add(struct unres_data *unres, struct lyd_node *node, enum UNRES_ITEM type)
{
    assert(unres && node);
    assert((type == UNRES_LEAFREF) || (type == UNRES_INSTID) || (type == UNRES_WHEN) || (type == UNRES_MUST)
           || (type == UNRES_EMPTYCONT));

    unres->count++;
    unres->node = ly_realloc(unres->node, unres->count * sizeof *unres->node);
    if (!unres->node) {
        LOGMEM;
        return -1;
    }
    unres->node[unres->count - 1] = node;
    unres->type = ly_realloc(unres->type, unres->count * sizeof *unres->type);
    if (!unres->type) {
        LOGMEM;
        return -1;
    }
    unres->type[unres->count - 1] = type;

    if (type == UNRES_WHEN) {
        /* remove previous result */
        node->when_status = LYD_WHEN;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Resolve every unres data item in the structure. Logs directly.
 *
 * @param[in] unres Unres data structure to use.
 * @param[in,out] root Root node of the data tree. If not NULL, auto-delete is performed on false when condition. If
 * NULL and when condition is false the error is raised.
 * @param[in] options Parer options
 *
 * @return EXIT_SUCCESS on success, -1 on error.
 */
int
resolve_unres_data(struct unres_data *unres, struct lyd_node **root, int options)
{
    uint32_t i, j, first = 1, resolved = 0, del_items = 0, when_stmt = 0;
    int rc, progress;
    char *msg, *path;
    struct lyd_node *parent;

    assert(unres);
    assert((root && (*root)) || (options & LYD_OPT_NOAUTODEL));

    if (!unres->count) {
        return EXIT_SUCCESS;
    }

    LOGVRB("Resolving unresolved data nodes and their constraints...");
    ly_vlog_hide(1);

    /* when-stmt first */
    ly_errno = LY_SUCCESS;
    ly_vecode = LYVE_SUCCESS;
    do {
        progress = 0;
        for(i = 0; i < unres->count; i++) {
            if (unres->type[i] != UNRES_WHEN) {
                continue;
            }
            if (first) {
                /* count when-stmt nodes in unres list */
                when_stmt++;
            }

            /* resolve when condition only when all parent when conditions are already resolved */
            for (parent = unres->node[i]->parent;
                 parent && LYD_WHEN_DONE(parent->when_status);
                 parent = parent->parent) {
                if (!parent->parent && (parent->when_status & LYD_WHEN_FALSE)) {
                    /* the parent node was already unlinked, do not resolve this node,
                     *  it will be removed anyway, so just mark it as resolved
                     */
                    unres->node[i]->when_status |= LYD_WHEN_FALSE;
                    unres->type[i] = UNRES_RESOLVED;
                    resolved++;
                    break;
                }
            }
            if (parent) {
                continue;
            }

            rc = resolve_unres_data_item(unres->node[i], unres->type[i]);
            if (!rc) {
                if (unres->node[i]->when_status & LYD_WHEN_FALSE) {
                    if (!root) {
                        /* false when condition */
                        ly_vlog_hide(0);
                        path = strdup(ly_errpath());
                        LOGERR(LY_EVALID, "%s%s%s%s", msg = strdup(ly_errmsg()), path[0] ? " (path: " : "",
                               path[0] ? path : "", path[0] ? ")" : "");
                        free(path);
                        free(msg);
                        return -1;
                    } /* follows else */

                    /* only unlink now, the subtree can contain another nodes stored in the unres list */
                    /* if it has parent non-presence containers that would be empty, we should actually
                     * remove the container
                     */
                    if (!(options & LYD_OPT_KEEPEMPTYCONT)) {
                        for (parent = unres->node[i];
                                parent->parent && parent->parent->schema->nodetype == LYS_CONTAINER;
                                parent = parent->parent) {
                            if (((struct lys_node_container *)parent->parent->schema)->presence) {
                                /* presence container */
                                break;
                            }
                            if (parent->next || parent->prev != parent) {
                                /* non empty (the child we are in and we are going to remove is not the only child) */
                                break;
                            }
                        }
                        unres->node[i] = parent;
                    }

                    /* auto-delete */
                    LOGVRB("auto-delete node \"%s\" due to when condition (%s)", ly_errpath(),
                                    ((struct lys_node_leaf *)unres->node[i]->schema)->when->cond);
                    if (*root && *root == unres->node[i]) {
                        *root = (*root)->next;
                    }

                    lyd_unlink(unres->node[i]);
                    unres->type[i] = UNRES_DELETE;
                    del_items++;

                    /* update the rest of unres items */
                    for (j = 0; j < unres->count; j++) {
                        if (unres->type[j] == UNRES_RESOLVED || unres->type[j] == UNRES_DELETE) {
                            continue;
                        }

                        /* test if the node is in subtree to be deleted */
                        for (parent = unres->node[j]; parent; parent = parent->parent) {
                            if (parent == unres->node[i]) {
                                /* yes, it is */
                                unres->type[j] = UNRES_RESOLVED;
                                resolved++;
                                break;
                            }
                        }
                    }
                } else {
                    unres->type[i] = UNRES_RESOLVED;
                }
                ly_errno = LY_SUCCESS;
                ly_vecode = LYVE_SUCCESS;
                resolved++;
                progress = 1;
            } else if (rc == -1) {
                ly_vlog_hide(0);
                return -1;
            }
        }
        first = 0;
    } while (progress && resolved < when_stmt);

    /* do we have some unresolved when-stmt? */
    if (when_stmt > resolved) {
        ly_vlog_hide(0);
        path = strdup(ly_errpath());
        LOGERR(LY_EVALID, "%s%s%s%s", msg = strdup(ly_errmsg()), path[0] ? " (path: " : "",
               path[0] ? path : "", path[0] ? ")" : "");
        free(path);
        free(msg);
        return -1;
    }

    for (i = 0; del_items && i < unres->count; i++) {
        /* we had some when-stmt resulted to false, so now we have to sanitize the unres list */
        if (unres->type[i] != UNRES_DELETE) {
            continue;
        }
        if (!unres->node[i]) {
            unres->type[i] = UNRES_RESOLVED;
            del_items--;
            continue;
        }

        /* really remove the complete subtree */
        lyd_free(unres->node[i]);
        unres->type[i] = UNRES_RESOLVED;
        del_items--;
    }

    /* rest */
    for (i = 0; i < unres->count; ++i) {
        if (unres->type[i] == UNRES_RESOLVED) {
            continue;
        }

        rc = resolve_unres_data_item(unres->node[i], unres->type[i]);
        if (rc == 0) {
            unres->type[i] = UNRES_RESOLVED;
            resolved++;
        } else if (rc == -1) {
            ly_vlog_hide(0);
            return -1;
        }
    }

    ly_vlog_hide(0);
    if (resolved < unres->count) {
        /* try to resolve the unresolved data again, it will not resolve anything, but it will print
         * all the validation errors
         */
        for (i = 0; i < unres->count; ++i) {
            if (unres->type[i] == UNRES_RESOLVED) {
                continue;
            }
            resolve_unres_data_item(unres->node[i], unres->type[i]);
        }
        return -1;
    }

    LOGVRB("All data nodes and constraints resolved.");
    unres->count = 0;
    return EXIT_SUCCESS;
}
