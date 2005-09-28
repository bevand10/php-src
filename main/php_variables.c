/*
   +----------------------------------------------------------------------+
   | PHP Version 5                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997-2005 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.0 of the PHP license,       |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_0.txt.                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Rasmus Lerdorf <rasmus@lerdorf.on.ca>                       |
   |          Zeev Suraski <zeev@zend.com>                                |
   +----------------------------------------------------------------------+
 */

/* $Id$ */

#include <stdio.h>
#include "php.h"
#include "ext/standard/php_standard.h"
#include "ext/standard/credits.h"
#include "php_variables.h"
#include "php_globals.h"
#include "php_content_types.h"
#include "SAPI.h"
#include "php_logos.h"
#include "zend_globals.h"

/* for systems that need to override reading of environment variables */
void _php_import_environment_variables(zval *array_ptr TSRMLS_DC);
PHPAPI void (*php_import_environment_variables)(zval *array_ptr TSRMLS_DC) = _php_import_environment_variables;

PHPAPI void php_register_variable(char *var, char *strval, zval *track_vars_array TSRMLS_DC)
{
	php_register_variable_safe(var, strval, strlen(strval), track_vars_array TSRMLS_CC);
}

/* binary-safe version */
PHPAPI void php_register_variable_safe(char *var, char *strval, int str_len, zval *track_vars_array TSRMLS_DC)
{
	zval new_entry;
	assert(strval != NULL);
	
	/* Prepare value */
	Z_STRLEN(new_entry) = str_len;
	if (PG(magic_quotes_gpc)) {
		Z_STRVAL(new_entry) = php_addslashes(strval, Z_STRLEN(new_entry), &Z_STRLEN(new_entry), 0 TSRMLS_CC);
	} else {
		Z_STRVAL(new_entry) = estrndup(strval, Z_STRLEN(new_entry));
	}
	Z_TYPE(new_entry) = IS_STRING;

	php_register_variable_ex(var, &new_entry, track_vars_array TSRMLS_CC);
}

PHPAPI void php_u_register_variable_safe(UChar *var, UChar *strval, int32_t str_len, zval *track_vars_array TSRMLS_DC)
{
	zval new_entry;
	assert(strval != NULL);
	
	/* Prepare value */
	Z_USTRLEN(new_entry) = str_len;
	if (PG(magic_quotes_gpc)) {
		/* UTODO implement php_u_addslashes() */
		//Z_USTRVAL(new_entry) = php_addslashes(strval, Z_USTRLEN(new_entry), &Z_USTRLEN(new_entry), 0 TSRMLS_CC);
		Z_USTRVAL(new_entry) = eustrndup(strval, Z_USTRLEN(new_entry));
	} else {
		Z_USTRVAL(new_entry) = eustrndup(strval, Z_USTRLEN(new_entry));
	}
	Z_TYPE(new_entry) = IS_UNICODE;

	php_u_register_variable_ex(var, &new_entry, track_vars_array TSRMLS_CC);
}

PHPAPI void php_register_variable_ex(char *var, zval *val, zval *track_vars_array TSRMLS_DC)
{
	char *p = NULL;
	char *ip;		/* index pointer */
	char *index, *escaped_index = NULL;
	int var_len, index_len;
	zval *gpc_element, **gpc_element_p;
	zend_bool is_array;
	HashTable *symtable1 = NULL;

	assert(var != NULL);
	
	if (track_vars_array) {
		symtable1 = Z_ARRVAL_P(track_vars_array);
	} else if (PG(register_globals)) {
		symtable1 = EG(active_symbol_table);
	}
	if (!symtable1) {
		/* Nothing to do */
		zval_dtor(val);
		return;
	}

	/*
	 * Prepare variable name
	 */
	ip = strchr(var, '[');
	if (ip) {
		is_array = 1;
		*ip = 0;
	} else {
		is_array = 0;
	}
	/* ignore leading spaces in the variable name */
	while (*var && *var==' ') {
		var++;
	}
	var_len = strlen(var);
	if (var_len==0) { /* empty variable name, or variable name with a space in it */
		zval_dtor(val);
		return;
	}

	/* GLOBALS hijack attempt, reject parameter */
	if (symtable1 == EG(active_symbol_table) && !strcmp("GLOBALS", var)) {
		zval_dtor(val);
		return;
	}

	/* ensure that we don't have spaces or dots in the variable name (not binary safe) */
	for (p=var; *p; p++) {
		switch (*p) {
			case ' ':
			case '.':
				*p='_';
				break;
		}
	}

	index = var;
	index_len = var_len;

	while (1) {
		if (is_array) {
			char *index_s;
			int new_idx_len = 0;

			ip++;
			index_s = ip;
			if (isspace(*ip)) {
				ip++;
			}
			if (*ip==']') {
				index_s = NULL;
			} else {
				ip = strchr(ip, ']');
				if (!ip) {
					/* PHP variables cannot contain '[' in their names, so we replace the character with a '_' */
					*(index_s - 1) = '_';

					index_len = var_len = 0;
					if (index) {
						index_len = var_len = strlen(index);
					}
					goto plain_var;
					return;
				}
				*ip = 0;
				new_idx_len = strlen(index_s);	
			}

			if (!index) {
				MAKE_STD_ZVAL(gpc_element);
				array_init(gpc_element);
				zend_hash_next_index_insert(symtable1, &gpc_element, sizeof(zval *), (void **) &gpc_element_p);
			} else {
				if (PG(magic_quotes_gpc) && (index != var)) {
					/* no need to addslashes() the index if it's the main variable name */
					escaped_index = php_addslashes(index, index_len, &index_len, 0 TSRMLS_CC);
				} else {
					escaped_index = index;
				}
				if (zend_symtable_find(symtable1, escaped_index, index_len + 1, (void **) &gpc_element_p) == FAILURE
					|| Z_TYPE_PP(gpc_element_p) != IS_ARRAY) {
					MAKE_STD_ZVAL(gpc_element);
					array_init(gpc_element);
					zend_symtable_update(symtable1, escaped_index, index_len + 1, &gpc_element, sizeof(zval *), (void **) &gpc_element_p);
				}
				if (index != escaped_index) {
					efree(escaped_index);
				}
			}
			symtable1 = Z_ARRVAL_PP(gpc_element_p);
			/* ip pointed to the '[' character, now obtain the key */
			index = index_s;
			index_len = new_idx_len;

			ip++;
			if (*ip == '[') {
				is_array = 1;
				*ip = 0;
			} else {
				is_array = 0;
			}
		} else {
plain_var:
			MAKE_STD_ZVAL(gpc_element);
			gpc_element->value = val->value;
			Z_TYPE_P(gpc_element) = Z_TYPE_P(val);
			if (!index) {
				zend_hash_next_index_insert(symtable1, &gpc_element, sizeof(zval *), (void **) &gpc_element_p);
			} else {
				zval **tmp;

				if (PG(magic_quotes_gpc)) { 
					escaped_index = php_addslashes(index, index_len, &index_len, 0 TSRMLS_CC);
				} else {
					escaped_index = index;
				}
				/* 
				 * According to rfc2965, more specific paths are listed above the less specific ones.
				 * If we encounter a duplicate cookie name, we should skip it, since it is not possible
				 * to have the same (plain text) cookie name for the same path and we should not overwrite
				 * more specific cookies with the less specific ones.
				 */
				if (PG(http_globals)[TRACK_VARS_COOKIE] && symtable1 == Z_ARRVAL_P(PG(http_globals)[TRACK_VARS_COOKIE]) && 
					zend_symtable_find(symtable1, escaped_index, index_len+1, (void **) &tmp) != FAILURE) {
					if (index != escaped_index) {
						efree(escaped_index);
					}
					break;
				}
				zend_symtable_update(symtable1, escaped_index, index_len + 1, &gpc_element, sizeof(zval *), (void **) &gpc_element_p);
				if (index != escaped_index) {
					efree(escaped_index);
				}
			}
			break;
		}
	}
}

PHPAPI void php_u_register_variable_ex(UChar *var, zval *val, pval *track_vars_array TSRMLS_DC)
{
	UChar *p = NULL;
	UChar *ip;		/* index pointer */
	UChar *index;
	int32_t var_len, index_len;
	zval *gpc_element, **gpc_element_p;
	zend_bool is_array;
	HashTable *symtable1=NULL;

	assert(var != NULL);

	if (track_vars_array) {
		symtable1 = Z_ARRVAL_P(track_vars_array);
	} else if (PG(register_globals)) {
		symtable1 = EG(active_symbol_table);
	}
	if (!symtable1) {
		/* Nothing to do */
		zval_dtor(val);
		return;
	}

	/*
	 * Prepare variable name
	 */
	ip = u_strchr(var, 0x5b /*'['*/);
	if (ip) {
		is_array = 1;
		*ip = 0;
	} else {
		is_array = 0;
	}
	/* ignore leading spaces in the variable name */
	while (*var && *var==0x20 /*' '*/) {
		var++;
	}
	var_len = u_strlen(var);
	if (var_len==0) { /* empty variable name, or variable name with a space in it */
		zval_dtor(val);
		return;
	}
	/* ensure that we don't have spaces or dots in the variable name (not binary safe) */
	for (p=var; *p; p++) {
		switch(*p) {
			case 0x20:   /*' '*/
			case 0x2e:   /*'.'*/
				*p=0x5f; /*'_'*/ 
				break;
		}
	}

	index = var;
	index_len = var_len;

	while (1) {
		if (is_array) {
			UChar *escaped_index = NULL, *index_s;
			int32_t new_idx_len = 0;

			ip++;
			index_s = ip;
			if (u_isspace(*ip)) {
				ip++;
			}
			if (*ip==0x5d /*']'*/) {
				index_s = NULL;
			} else {
				ip = u_strchr(ip, 0x5d /*']'*/);
				if (!ip) {
					/* PHP variables cannot contain '[' in their names, so we replace the character with a '_' */
					*(index_s - 1) = 0x5f; /*'_'*/

					index_len = var_len = 0;
					if (index) {
						index_len = var_len = u_strlen(index);
					}
					goto plain_var;
					return;
				}
				*ip = 0;
				new_idx_len = u_strlen(index_s);	
			}

			if (!index) {
				MAKE_STD_ZVAL(gpc_element);
				array_init(gpc_element);
				zend_hash_next_index_insert(symtable1, &gpc_element, sizeof(zval *), (void **) &gpc_element_p);
			} else {
				if (PG(magic_quotes_gpc) && (index!=var)) {
					/* UTODO fix for magic_quotes_gpc case */
					/* no need to addslashes() the index if it's the main variable name */
					//escaped_index = php_addslashes(index, index_len, &index_len, 0 TSRMLS_CC);
					escaped_index = index;
				} else {
					escaped_index = index;
				}
				if (zend_u_symtable_find(symtable1, IS_UNICODE, escaped_index, index_len+1, (void **) &gpc_element_p)==FAILURE
					|| Z_TYPE_PP(gpc_element_p) != IS_ARRAY) {
					MAKE_STD_ZVAL(gpc_element);
					array_init(gpc_element);
					zend_u_symtable_update(symtable1, IS_UNICODE, escaped_index, index_len+1, &gpc_element, sizeof(zval *), (void **) &gpc_element_p);
				}
				if (index!=escaped_index) {
					efree(escaped_index);
				}
			}
			symtable1 = Z_ARRVAL_PP(gpc_element_p);
			/* ip pointed to the '[' character, now obtain the key */
			index = index_s;
			index_len = new_idx_len;

			ip++;
			if (*ip==0x5b /*'['*/) {
				is_array = 1;
				*ip = 0;
			} else {
				is_array = 0;
			}
		} else {
plain_var:
			MAKE_STD_ZVAL(gpc_element);
			gpc_element->value = val->value;
			Z_TYPE_P(gpc_element) = Z_TYPE_P(val);
			if (!index) {
				zend_hash_next_index_insert(symtable1, &gpc_element, sizeof(zval *), (void **) &gpc_element_p);
			} else {
				/* UTODO fix for php_addslashes case */
				//char *escaped_index = php_addslashes(index, index_len, &index_len, 0 TSRMLS_CC);
				UChar *escaped_index = index;
				zend_u_symtable_update(symtable1, IS_UNICODE, escaped_index, index_len+1, &gpc_element, sizeof(zval *), (void **) &gpc_element_p);
				//efree(escaped_index);
			}
			break;
		}
	}
}

SAPI_API SAPI_POST_HANDLER_FUNC(php_std_post_handler)
{
	char *var, *val;
	char *strtok_buf = NULL;
	zval *array_ptr = (zval *) arg;
	UConverter *input_conv = UG(http_input_encoding_conv);

	if (SG(request_info).post_data == NULL) {
		return;
	}	

	if (!input_conv) {
		input_conv = ZEND_U_CONVERTER(UG(output_encoding_conv));
	}

	var = php_strtok_r(SG(request_info).post_data, "&", &strtok_buf);

	while (var) {
		val = strchr(var, '=');
		if (val) { /* have a value */
			if (UG(unicode)) {
				UChar *u_var, *u_val;
				int32_t u_var_len, u_val_len;
				int32_t var_len;
				int32_t val_len;
				UErrorCode status1 = U_ZERO_ERROR, status2 = U_ZERO_ERROR;

				*val++ = '\0';
				var_len = strlen(var);
				php_url_decode(var, var_len);
				val_len = php_url_decode(val, strlen(val));
				zend_convert_to_unicode(input_conv, &u_var, &u_var_len, var, var_len, &status1);
				zend_convert_to_unicode(input_conv, &u_val, &u_val_len, val, val_len, &status2);
				if (U_SUCCESS(status1) && U_SUCCESS(status2)) {
					/* UTODO add input filtering */
					php_u_register_variable_safe(u_var, u_val, u_val_len, array_ptr TSRMLS_CC);
				} else {
					/* UTODO set a user-accessible flag to indicate that conversion failed? */
				}
				efree(u_var);
				efree(u_val);
			} else {
				unsigned int val_len, new_val_len;

				*val++ = '\0';
				php_url_decode(var, strlen(var));
				val_len = php_url_decode(val, strlen(val));
				val = estrndup(val, val_len);
				if (sapi_module.input_filter(PARSE_POST, var, &val, val_len, &new_val_len TSRMLS_CC)) {
					php_register_variable_safe(var, val, new_val_len, array_ptr TSRMLS_CC);
				}
				efree(val);
			}
		}
		var = php_strtok_r(NULL, "&", &strtok_buf);
	}
}

SAPI_API SAPI_INPUT_FILTER_FUNC(php_default_input_filter)
{
	/* TODO: check .ini setting here and apply user-defined input filter */
	if(new_val_len) *new_val_len = val_len;
	return 1;
}

SAPI_API SAPI_TREAT_DATA_FUNC(php_default_treat_data)
{
	char *res = NULL, *var, *val, *separator = NULL;
	const char *c_var;
	zval *array_ptr;
	int free_buffer = 0;
	char *strtok_buf = NULL;
	UConverter *input_conv = UG(http_input_encoding_conv);
	
	switch (arg) {
		case PARSE_POST:
		case PARSE_GET:
		case PARSE_COOKIE:
			ALLOC_ZVAL(array_ptr);
			array_init(array_ptr);
			INIT_PZVAL(array_ptr);
			switch (arg) {
				case PARSE_POST:
					if (PG(http_globals)[TRACK_VARS_POST]) {
						zval_ptr_dtor(&PG(http_globals)[TRACK_VARS_POST]);
					}
					PG(http_globals)[TRACK_VARS_POST] = array_ptr;
					break;
				case PARSE_GET:
					if (PG(http_globals)[TRACK_VARS_GET]) {
						zval_ptr_dtor(&PG(http_globals)[TRACK_VARS_GET]);
					}
					PG(http_globals)[TRACK_VARS_GET] = array_ptr;
					break;
				case PARSE_COOKIE:
					if (PG(http_globals)[TRACK_VARS_COOKIE]) {
						zval_ptr_dtor(&PG(http_globals)[TRACK_VARS_COOKIE]);
					}
					PG(http_globals)[TRACK_VARS_COOKIE] = array_ptr;
					break;
			}
			break;
		default:
			array_ptr = destArray;
			break;
	}

	if (arg == PARSE_POST) {
		sapi_handle_post(array_ptr TSRMLS_CC);
		return;
	}

	if (arg == PARSE_GET) {		/* GET data */
		c_var = SG(request_info).query_string;
		if (c_var && *c_var) {
			res = (char *) estrdup(c_var);
			free_buffer = 1;
		} else {
			free_buffer = 0;
		}
	} else if (arg == PARSE_COOKIE) {		/* Cookie data */
		c_var = SG(request_info).cookie_data;
		if (c_var && *c_var) {
			res = (char *) estrdup(c_var);
			free_buffer = 1;
		} else {
			free_buffer = 0;
		}
	} else if (arg == PARSE_STRING) {		/* String data */
		res = str;
		free_buffer = 1;
	}

	if (!res) {
		return;
	}

	switch (arg) {
		case PARSE_GET:
		case PARSE_STRING:
			separator = (char *) estrdup(PG(arg_separator).input);
			break;
		case PARSE_COOKIE:
			separator = ";\0";
			break;
	}
	
	if (!input_conv) {
		input_conv = ZEND_U_CONVERTER(UG(output_encoding_conv));
	}

	var = php_strtok_r(res, separator, &strtok_buf);
	
	while (var) {
		int32_t var_len;

		val = strchr(var, '=');
		if (val) {
			*val++ = '\0';
		}
		var_len = strlen(var);
		php_url_decode(var, var_len);

		if (UG(unicode)) {
			UChar *u_var, *u_val;
			int32_t u_var_len, u_val_len;
			UErrorCode status = U_ZERO_ERROR;

			zend_convert_to_unicode(input_conv, &u_var, &u_var_len, var, var_len, &status);
			if (U_FAILURE(status)) {
				/* UTODO set a user-accessible flag to indicate that conversion failed? */
				efree(u_var);
				goto next_var;
			}

			if (val) { /* have a value */
				int val_len;
				unsigned int new_val_len;

				val_len = php_url_decode(val, strlen(val));
				zend_convert_to_unicode(input_conv, &u_val, &u_val_len, val, val_len, &status);
				if (U_FAILURE(status)) {
					/* UTODO set a user-accessible flag to indicate that conversion failed? */
					efree(u_var);
					efree(u_val);
					goto next_var;
				}
				php_u_register_variable_safe(u_var, u_val, u_val_len, array_ptr TSRMLS_CC);
				/* UTODO need to make input_filter Unicode aware */
				/*
				if (sapi_module.input_filter(arg, var, &val, val_len, &new_val_len TSRMLS_CC)) {
					php_register_variable_safe(var, val, new_val_len, array_ptr TSRMLS_CC);
				}
				*/
				efree(u_var);
				efree(u_val);
			} else {
				u_val_len = 0;
				u_val = USTR_MAKE("");
				php_u_register_variable_safe(u_var, u_val, u_val_len, array_ptr TSRMLS_CC);
				/*
				if (sapi_module.input_filter(arg, var, &val, val_len, &new_val_len TSRMLS_CC)) {
					php_register_variable_safe(var, val, new_val_len, array_ptr TSRMLS_CC);
				}
				*/
				efree(u_val);
			}
		} else {
			if (val) { /* have a value */
				int val_len;
				unsigned int new_val_len;

				val_len = php_url_decode(val, strlen(val));
				val = estrndup(val, val_len);
				if (sapi_module.input_filter(arg, var, &val, val_len, &new_val_len TSRMLS_CC)) {
					php_register_variable_safe(var, val, new_val_len, array_ptr TSRMLS_CC);
				}
				efree(val);
			} else {
				int val_len;
				unsigned int new_val_len;

				val_len = 0;
				val = estrndup("", val_len);
				if (sapi_module.input_filter(arg, var, &val, val_len, &new_val_len TSRMLS_CC)) {
					php_register_variable_safe(var, val, new_val_len, array_ptr TSRMLS_CC);
				}
				efree(val);
			}
		}
next_var:
		var = php_strtok_r(NULL, separator, &strtok_buf);
	}

	if (arg != PARSE_COOKIE) {
		efree(separator);
	}

	if (free_buffer) {
		efree(res);
	}
}

void _php_import_environment_variables(zval *array_ptr TSRMLS_DC)
{
	char buf[128];
	char **env, *p, *t = buf;
	size_t alloc_size = sizeof(buf);
	unsigned long nlen; /* ptrdiff_t is not portable */

	/* turn off magic_quotes while importing environment variables */
	int magic_quotes_gpc = PG(magic_quotes_gpc);
	PG(magic_quotes_gpc) = 0;

	for (env = environ; env != NULL && *env != NULL; env++) {
		p = strchr(*env, '=');
		if (!p) {				/* malformed entry? */
			continue;
		}
		nlen = p - *env;
		if (nlen >= alloc_size) {
			alloc_size = nlen + 64;
			t = (t == buf ? emalloc(alloc_size): erealloc(t, alloc_size));
		}
		memcpy(t, *env, nlen);
		t[nlen] = '\0';
		php_register_variable(t, p + 1, array_ptr TSRMLS_CC);
	}
	if (t != buf && t != NULL) {
		efree(t);
	}
	PG(magic_quotes_gpc) = magic_quotes_gpc;
}

zend_bool php_std_auto_global_callback(char *name, uint name_len TSRMLS_DC)
{
	zend_printf("%s\n", name);
	return 0; /* don't rearm */
}

/* {{{ php_build_argv
 */
static void php_build_argv(char *s, zval *track_vars_array TSRMLS_DC)
{
	zval *arr, *argc, *tmp;
	int count = 0;
	char *ss, *space;
	
	if (! (PG(register_globals) || SG(request_info).argc ||
		   PG(http_globals)[TRACK_VARS_SERVER]) ) {
		return;
	}
	
	ALLOC_ZVAL(arr);
	array_init(arr);
	arr->is_ref = 0;
	arr->refcount = 0;

	/* Prepare argv */
	if (SG(request_info).argc) { /* are we in cli sapi? */
		int i;
		for (i = 0; i < SG(request_info).argc; i++) {
			ALLOC_ZVAL(tmp);
			Z_TYPE_P(tmp) = IS_STRING;
			Z_STRLEN_P(tmp) = strlen(SG(request_info).argv[i]);
			Z_STRVAL_P(tmp) = estrndup(SG(request_info).argv[i], Z_STRLEN_P(tmp));
			INIT_PZVAL(tmp);
			if (zend_hash_next_index_insert(Z_ARRVAL_P(arr), &tmp, sizeof(zval *), NULL) == FAILURE) {
				if (Z_TYPE_P(tmp) == IS_STRING) {
					efree(Z_STRVAL_P(tmp));
				}
			}
		}
	} else 	if (s && *s) {
		ss = s;
		while (ss) {
			space = strchr(ss, '+');
			if (space) {
				*space = '\0';
			}
			/* auto-type */
			ALLOC_ZVAL(tmp);
			Z_TYPE_P(tmp) = IS_STRING;
			Z_STRLEN_P(tmp) = strlen(ss);
			Z_STRVAL_P(tmp) = estrndup(ss, Z_STRLEN_P(tmp));
			INIT_PZVAL(tmp);
			count++;
			if (zend_hash_next_index_insert(Z_ARRVAL_P(arr), &tmp, sizeof(zval *), NULL) == FAILURE) {
				if (Z_TYPE_P(tmp) == IS_STRING) {
					efree(Z_STRVAL_P(tmp));
				}
			}
			if (space) {
				*space = '+';
				ss = space + 1;
			} else {
				ss = space;
			}
		}
	}

	/* prepare argc */
	ALLOC_ZVAL(argc);
	if (SG(request_info).argc) {
		Z_LVAL_P(argc) = SG(request_info).argc;
	} else {
		Z_LVAL_P(argc) = count;
	}
	Z_TYPE_P(argc) = IS_LONG;
	argc->is_ref = 0;
	argc->refcount = 0;

	if (PG(register_globals) || SG(request_info).argc) {
		arr->refcount++;
		argc->refcount++;
		zend_hash_update(&EG(symbol_table), "argv", sizeof("argv"), &arr, sizeof(zval *), NULL);
		zend_hash_add(&EG(symbol_table), "argc", sizeof("argc"), &argc, sizeof(zval *), NULL);
	} 
	if (track_vars_array) {
		arr->refcount++;
		argc->refcount++;
		zend_hash_update(Z_ARRVAL_P(track_vars_array), "argv", sizeof("argv"), &arr, sizeof(zval *), NULL);
		zend_hash_update(Z_ARRVAL_P(track_vars_array), "argc", sizeof("argc"), &argc, sizeof(zval *), NULL);
	}
}
/* }}} */

/* {{{ php_handle_special_queries
 */
PHPAPI int php_handle_special_queries(TSRMLS_D)
{
	if (PG(expose_php) && SG(request_info).query_string && SG(request_info).query_string[0] == '=') {
		if (php_info_logos(SG(request_info).query_string + 1 TSRMLS_CC)) {
			return 1;
		} else if (!strcmp(SG(request_info).query_string + 1, PHP_CREDITS_GUID)) {
			php_print_credits(PHP_CREDITS_ALL TSRMLS_CC);
			return 1;
		}
	}
	return 0;
}
/* }}} */

/* {{{ php_register_server_variables
 */
static inline void php_register_server_variables(TSRMLS_D)
{
	zval *array_ptr = NULL;
	/* turn off magic_quotes while importing server variables */
	int magic_quotes_gpc = PG(magic_quotes_gpc);

	ALLOC_ZVAL(array_ptr);
	array_init(array_ptr);
	INIT_PZVAL(array_ptr);
	if (PG(http_globals)[TRACK_VARS_SERVER]) {
		zval_ptr_dtor(&PG(http_globals)[TRACK_VARS_SERVER]);
	}
	PG(http_globals)[TRACK_VARS_SERVER] = array_ptr;
	PG(magic_quotes_gpc) = 0;

	/* Server variables */
	if (sapi_module.register_server_variables) {
		sapi_module.register_server_variables(array_ptr TSRMLS_CC);
	}

	/* PHP Authentication support */
	if (SG(request_info).auth_user) {
		php_register_variable("PHP_AUTH_USER", SG(request_info).auth_user, array_ptr TSRMLS_CC);
	}
	if (SG(request_info).auth_password) {
		php_register_variable("PHP_AUTH_PW", SG(request_info).auth_password, array_ptr TSRMLS_CC);
	}
	if (SG(request_info).auth_digest) {
		php_register_variable("PHP_AUTH_DIGEST", SG(request_info).auth_digest, array_ptr TSRMLS_CC);
	}
	/* store request init time */
	{
		zval new_entry;
		Z_TYPE(new_entry) = IS_LONG;
		Z_LVAL(new_entry) = sapi_get_request_time(TSRMLS_C);
		php_register_variable_ex("REQUEST_TIME", &new_entry, array_ptr TSRMLS_CC);
	}

	PG(magic_quotes_gpc) = magic_quotes_gpc;
}
/* }}} */

/* {{{ php_autoglobal_merge
 */
static void php_autoglobal_merge(HashTable *dest, HashTable *src TSRMLS_DC)
{
	zval **src_entry, **dest_entry;
	char *string_key;
	uint string_key_len;
	ulong num_key;
	HashPosition pos;
	int key_type;
	int globals_check = (PG(register_globals) && (dest == (&EG(symbol_table))));

	zend_hash_internal_pointer_reset_ex(src, &pos);
	while (zend_hash_get_current_data_ex(src, (void **)&src_entry, &pos) == SUCCESS) {
		key_type = zend_hash_get_current_key_ex(src, &string_key, &string_key_len, &num_key, 0, &pos);
		if (Z_TYPE_PP(src_entry) != IS_ARRAY
			|| (key_type == HASH_KEY_IS_STRING && zend_hash_find(dest, string_key, string_key_len, (void **) &dest_entry) != SUCCESS)
			|| (key_type == HASH_KEY_IS_LONG && zend_hash_index_find(dest, num_key, (void **)&dest_entry) != SUCCESS)
			|| Z_TYPE_PP(dest_entry) != IS_ARRAY
        ) {
			(*src_entry)->refcount++;
			if (key_type == HASH_KEY_IS_STRING) {
				/* if register_globals is on and working with main symbol table, prevent overwriting of GLOBALS */
				if (!globals_check || string_key_len != sizeof("GLOBALS") || memcmp(string_key, "GLOBALS", sizeof("GLOBALS") - 1)) {
					zend_hash_update(dest, string_key, string_key_len, src_entry, sizeof(zval *), NULL);
				} else {
					(*src_entry)->refcount--;
				}
			} else {
				zend_hash_index_update(dest, num_key, src_entry, sizeof(zval *), NULL);
			}
		} else {
			SEPARATE_ZVAL(dest_entry);
			php_autoglobal_merge(Z_ARRVAL_PP(dest_entry), Z_ARRVAL_PP(src_entry) TSRMLS_CC);
		}
		zend_hash_move_forward_ex(src, &pos);
	}
}
/* }}} */

static zend_bool php_auto_globals_create_server(char *name, uint name_len TSRMLS_DC);
static zend_bool php_auto_globals_create_env(char *name, uint name_len TSRMLS_DC);
static zend_bool php_auto_globals_create_request(char *name, uint name_len TSRMLS_DC);

/* {{{ php_hash_environment
 */
int php_hash_environment(TSRMLS_D)
{
	char *p;
	unsigned char _gpc_flags[5] = {0, 0, 0, 0, 0};
	zval *dummy_track_vars_array = NULL;
	zend_bool initialized_dummy_track_vars_array=0;
	zend_bool jit_initialization = (PG(auto_globals_jit) && !PG(register_globals) && !PG(register_long_arrays) && !PG(register_argc_argv));
	struct auto_global_record {
		char *name;
		uint name_len;
		char *long_name;
		uint long_name_len;
		zend_bool jit_initialization;
	} auto_global_records[] = {
		{ "_POST", sizeof("_POST"), "HTTP_POST_VARS", sizeof("HTTP_POST_VARS"), 0 },
		{ "_GET", sizeof("_GET"), "HTTP_GET_VARS", sizeof("HTTP_GET_VARS"), 0 },
		{ "_COOKIE", sizeof("_COOKIE"), "HTTP_COOKIE_VARS", sizeof("HTTP_COOKIE_VARS"), 0 },
		{ "_SERVER", sizeof("_SERVER"), "HTTP_SERVER_VARS", sizeof("HTTP_SERVER_VARS"), 1 },
		{ "_ENV", sizeof("_ENV"), "HTTP_ENV_VARS", sizeof("HTTP_ENV_VARS"), 1 },
		{ "_FILES", sizeof("_FILES"), "HTTP_POST_FILES", sizeof("HTTP_POST_FILES"), 0 },
	};
	size_t num_track_vars = sizeof(auto_global_records)/sizeof(struct auto_global_record);
	size_t i;

	/* jit_initialization = 0; */
	for (i=0; i<num_track_vars; i++) {
		PG(http_globals)[i] = NULL;
	}

	for (p=PG(variables_order); p && *p; p++) {
		switch(*p) {
			case 'p':
			case 'P':
				if (!_gpc_flags[0] && !SG(headers_sent) && SG(request_info).request_method && !strcasecmp(SG(request_info).request_method, "POST")) {
					sapi_module.treat_data(PARSE_POST, NULL, NULL TSRMLS_CC);	/* POST Data */
					_gpc_flags[0] = 1;
					if (PG(register_globals)) {
						php_autoglobal_merge(&EG(symbol_table), Z_ARRVAL_P(PG(http_globals)[TRACK_VARS_POST]) TSRMLS_CC);
					}
				}
				break;
			case 'c':
			case 'C':
				if (!_gpc_flags[1]) {
					sapi_module.treat_data(PARSE_COOKIE, NULL, NULL TSRMLS_CC);	/* Cookie Data */
					_gpc_flags[1] = 1;
					if (PG(register_globals)) {
						php_autoglobal_merge(&EG(symbol_table), Z_ARRVAL_P(PG(http_globals)[TRACK_VARS_COOKIE]) TSRMLS_CC);
					}
				}
				break;
			case 'g':
			case 'G':
				if (!_gpc_flags[2]) {
					sapi_module.treat_data(PARSE_GET, NULL, NULL TSRMLS_CC);	/* GET Data */
					_gpc_flags[2] = 1;
					if (PG(register_globals)) {
						php_autoglobal_merge(&EG(symbol_table), Z_ARRVAL_P(PG(http_globals)[TRACK_VARS_GET]) TSRMLS_CC);
					}
				}
				break;
			case 'e':
			case 'E':
				if (!jit_initialization && !_gpc_flags[3]) {
					zend_auto_global_disable_jit("_ENV", sizeof("_ENV")-1 TSRMLS_CC);
					php_auto_globals_create_env("_ENV", sizeof("_ENV")-1 TSRMLS_CC);
					_gpc_flags[3] = 1;
					if (PG(register_globals)) {
						php_autoglobal_merge(&EG(symbol_table), Z_ARRVAL_P(PG(http_globals)[TRACK_VARS_ENV]) TSRMLS_CC);
					}
				}
				break;
			case 's':
			case 'S':
				if (!jit_initialization && !_gpc_flags[4]) {
					zend_auto_global_disable_jit("_SERVER", sizeof("_SERVER")-1 TSRMLS_CC);
					php_register_server_variables(TSRMLS_C);
					_gpc_flags[4] = 1;
					if (PG(register_globals)) {
						php_autoglobal_merge(&EG(symbol_table), Z_ARRVAL_P(PG(http_globals)[TRACK_VARS_SERVER]) TSRMLS_CC);
					}
				}
				break;
		}
	}

	/* argv/argc support */
	if (PG(register_argc_argv)) {
		php_build_argv(SG(request_info).query_string, PG(http_globals)[TRACK_VARS_SERVER] TSRMLS_CC);
	}

	for (i=0; i<num_track_vars; i++) {
		if (jit_initialization && auto_global_records[i].jit_initialization) {
			continue;
		}
		if (!PG(http_globals)[i]) {
			if (!initialized_dummy_track_vars_array) {
				ALLOC_ZVAL(dummy_track_vars_array);
				array_init(dummy_track_vars_array);
				INIT_PZVAL(dummy_track_vars_array);
				initialized_dummy_track_vars_array = 1;
			} else {
				dummy_track_vars_array->refcount++;
			}
			PG(http_globals)[i] = dummy_track_vars_array;
		}

		PG(http_globals)[i]->refcount++;
		zend_hash_update(&EG(symbol_table), auto_global_records[i].name, auto_global_records[i].name_len, &PG(http_globals)[i], sizeof(zval *), NULL);
		if (PG(register_long_arrays)) {
			zend_hash_update(&EG(symbol_table), auto_global_records[i].long_name, auto_global_records[i].long_name_len, &PG(http_globals)[i], sizeof(zval *), NULL);
			PG(http_globals)[i]->refcount++;
		}
	}

	/* Create _REQUEST */
	if (!jit_initialization) {
		zend_auto_global_disable_jit("_REQUEST", sizeof("_REQUEST")-1 TSRMLS_CC);
		php_auto_globals_create_request("_REQUEST", sizeof("_REQUEST")-1 TSRMLS_CC);
	}

	return SUCCESS;
}
/* }}} */

static zend_bool php_auto_globals_create_server(char *name, uint name_len TSRMLS_DC)
{
	if (PG(variables_order) && (strchr(PG(variables_order),'S') || strchr(PG(variables_order),'s'))) {
		php_register_server_variables(TSRMLS_C);
	} else {
		zval *server_vars=NULL;
		ALLOC_ZVAL(server_vars);
		array_init(server_vars);
		INIT_PZVAL(server_vars);
		if (PG(http_globals)[TRACK_VARS_SERVER]) {
			zval_ptr_dtor(&PG(http_globals)[TRACK_VARS_SERVER]);
		}
		PG(http_globals)[TRACK_VARS_SERVER] = server_vars;
	}

	zend_hash_update(&EG(symbol_table), name, name_len + 1, &PG(http_globals)[TRACK_VARS_SERVER], sizeof(zval *), NULL);
	PG(http_globals)[TRACK_VARS_SERVER]->refcount++;

	if (PG(register_long_arrays)) {
		zend_hash_update(&EG(symbol_table), "HTTP_SERVER_VARS", sizeof("HTTP_SERVER_VARS"), &PG(http_globals)[TRACK_VARS_SERVER], sizeof(zval *), NULL);
		PG(http_globals)[TRACK_VARS_SERVER]->refcount++;
	}

	return 0; /* don't rearm */
}

static zend_bool php_auto_globals_create_env(char *name, uint name_len TSRMLS_DC)
{
	zval *env_vars = NULL;
	ALLOC_ZVAL(env_vars);
	array_init(env_vars);
	INIT_PZVAL(env_vars);
	if (PG(http_globals)[TRACK_VARS_ENV]) {
		zval_ptr_dtor(&PG(http_globals)[TRACK_VARS_ENV]);
	}
	PG(http_globals)[TRACK_VARS_ENV] = env_vars;
	
	if (PG(variables_order) && (strchr(PG(variables_order),'E') || strchr(PG(variables_order),'e'))) {
		php_import_environment_variables(PG(http_globals)[TRACK_VARS_ENV] TSRMLS_CC);
	}

	zend_hash_update(&EG(symbol_table), name, name_len + 1, &PG(http_globals)[TRACK_VARS_ENV], sizeof(zval *), NULL);
	PG(http_globals)[TRACK_VARS_ENV]->refcount++;

	if (PG(register_long_arrays)) {
		zend_hash_update(&EG(symbol_table), "HTTP_ENV_VARS", sizeof("HTTP_ENV_VARS"), &PG(http_globals)[TRACK_VARS_ENV], sizeof(zval *), NULL);
		PG(http_globals)[TRACK_VARS_ENV]->refcount++;
	}

	return 0; /* don't rearm */
}

static zend_bool php_auto_globals_create_request(char *name, uint name_len TSRMLS_DC)
{
	zval *form_variables;
	unsigned char _gpc_flags[3] = {0, 0, 0};
	char *p;

	ALLOC_ZVAL(form_variables);
	array_init(form_variables);
	INIT_PZVAL(form_variables);

	for (p = PG(variables_order); p && *p; p++) {
		switch (*p) {
			case 'g':
			case 'G':
				if (!_gpc_flags[0]) {
					php_autoglobal_merge(Z_ARRVAL_P(form_variables), Z_ARRVAL_P(PG(http_globals)[TRACK_VARS_GET]) TSRMLS_CC);
					_gpc_flags[0] = 1;
				}
				break;
			case 'p':
			case 'P':
				if (!_gpc_flags[1]) {
					php_autoglobal_merge(Z_ARRVAL_P(form_variables), Z_ARRVAL_P(PG(http_globals)[TRACK_VARS_POST]) TSRMLS_CC);
					_gpc_flags[1] = 1;
				}
				break;
			case 'c':
			case 'C':
				if (!_gpc_flags[2]) {
					php_autoglobal_merge(Z_ARRVAL_P(form_variables), Z_ARRVAL_P(PG(http_globals)[TRACK_VARS_COOKIE]) TSRMLS_CC);
					_gpc_flags[2] = 1;
				}
				break;
		}
	}

	zend_hash_update(&EG(symbol_table), "_REQUEST", sizeof("_REQUEST"), &form_variables, sizeof(zval *), NULL);
	return 0;
}

void php_startup_auto_globals(TSRMLS_D)
{
	zend_register_auto_global("_GET", sizeof("_GET")-1, NULL TSRMLS_CC);
	zend_register_auto_global("_POST", sizeof("_POST")-1, NULL TSRMLS_CC);
	zend_register_auto_global("_COOKIE", sizeof("_COOKIE")-1, NULL TSRMLS_CC);
	zend_register_auto_global("_SERVER", sizeof("_SERVER")-1, php_auto_globals_create_server TSRMLS_CC);
	zend_register_auto_global("_ENV", sizeof("_ENV")-1, php_auto_globals_create_env TSRMLS_CC);
	zend_register_auto_global("_REQUEST", sizeof("_REQUEST")-1, php_auto_globals_create_request TSRMLS_CC);
	zend_register_auto_global("_FILES", sizeof("_FILES")-1, NULL TSRMLS_CC);
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
