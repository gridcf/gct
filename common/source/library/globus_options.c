/*
 * Copyright 1999-2006 University of Chicago
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "globus_common.h"

#define HELP_LINE_LENGTH 78
#define HELP_LINE_COLUMN 29

typedef struct globus_l_options_table_s
{
    void *                              user_arg;
    globus_options_entry_t *            table;
    int                                 table_size;
} globus_l_options_table_t;

typedef struct globus_l_options_handle_s
{
    globus_options_unknown_callback_t   unknown_func;
    globus_list_t *                     table_list;
    void *                              unknown_arg;
} globus_l_options_handle_t;

globus_result_t
globus_options_init(
    globus_options_handle_t *           out_handle,
    globus_options_unknown_callback_t   unknown_func,
    void *                              unknown_arg)
{
    globus_l_options_handle_t *         handle;

    handle = (globus_l_options_handle_t *)
        globus_calloc(1, sizeof(globus_l_options_handle_t));
    if(handle == NULL)
    {
    }
    handle->unknown_func = unknown_func;
    handle->unknown_arg = unknown_arg;
    handle->table_list = NULL;

    *out_handle = handle;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_options_add_table(
    globus_options_handle_t             handle,
    globus_options_entry_t *            in_table,
    void *                              user_arg)
{
    int                                 i;
    globus_l_options_table_t *          table;

    table = (globus_l_options_table_t *)
        globus_calloc(1, sizeof(globus_l_options_table_t));

    table->user_arg = user_arg;
    table->table = in_table;

    for(i = 0; table->table[i].func != NULL; i++)
    {
    }
    table->table_size = i;

    globus_list_insert(&handle->table_list, table);

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_options_destroy(
    globus_options_handle_t              handle)
{
    globus_l_options_table_t *          table;

    table = (globus_l_options_table_t *)globus_list_first(handle->table_list);
    globus_free(table);
    globus_list_free(handle->table_list);
    globus_free(handle);

    return GLOBUS_SUCCESS;
}

static
int
globus_l_alphabetize_list(
    void *                              low_datum,
    void *                              high_datum,
    void *                              arg)
{
    globus_options_entry_t *            e1 = low_datum;
    globus_options_entry_t *            e2 = high_datum;

    if (!e2->short_opt)
    {
        return GLOBUS_TRUE;
    }
    else if (!e1->short_opt)
    {
        return GLOBUS_FALSE;
    }
    else if (globus_libc_strncasecmp(e1->short_opt, e2->short_opt, strlen(e1->short_opt)) <= 0)
    {
        return GLOBUS_TRUE;
    }
    else
    {
        return GLOBUS_FALSE;
    }
}

void
globus_options_help(
    globus_options_handle_t             handle)
{
    globus_l_options_table_t *          table;
    char *                              ptr;
    int                                 i;
    int                                 length;
    int                                 ndx;
    globus_list_t *                     list;
    int                                 skip;
    char                                buf[HELP_LINE_LENGTH+8];
    globus_list_t *                     alphabetized_options = NULL;
    globus_options_entry_t *            entry;

    for(list = handle->table_list;
        !globus_list_empty(list);
        list = globus_list_rest(list))
    {
        table = (globus_l_options_table_t *) globus_list_first(list);

        for(i = 0; i < table->table_size; i++)
        {
            globus_list_insert(&alphabetized_options, &table->table[i]);
        }
    }

    alphabetized_options = globus_list_sort_destructive(
            alphabetized_options, globus_l_alphabetize_list, NULL);

    for(list = alphabetized_options;
        !globus_list_empty(list);
        list = globus_list_rest(list))
    {
        entry = globus_list_first(list);

        /* Print out -short | --opt-name PARMS_DESC
         * with any of -short, --opt-name, parms-desc being optional, and if
         * not present, avoid adding inter-item punctuation.
         */
        fprintf(stdout, " %s%s%s%s%s%s%s%n",
            entry->short_opt ? "-" : "",
            entry->short_opt ? entry->short_opt : "",
            entry->short_opt && entry->opt_name ? " | " : "",
            entry->opt_name ? "--" : "",
            entry->opt_name ? entry->opt_name : "",
            entry->parms_desc ? " " : "",
            entry->parms_desc ? entry->parms_desc : "",
            &skip);
        if (skip >= HELP_LINE_COLUMN - 1)
        {
            fprintf(stdout, "\n");
            skip = 0;
        }
        if(entry->description != NULL)
        {
            ndx = 0;
            do 
            {
                while (entry->description[ndx] == ' ')
                {
                    ndx++;
                }
                length = strlen(&entry->description[ndx]);
                if(length > (HELP_LINE_LENGTH-HELP_LINE_COLUMN))
                {
                    ptr = &entry->description[ndx] 
                        + HELP_LINE_LENGTH - HELP_LINE_COLUMN;
                    while(ptr > &entry->description[ndx] &&
                        *ptr != ' ')
                    {
                        ptr--;
                    }
                    if(ptr != &entry->description[ndx])
                    {
                        length = ptr - &entry->description[ndx];
                    }
                    else
                    {
                        length = HELP_LINE_LENGTH - HELP_LINE_COLUMN;
                    }
                }
                if (length > 0)
                {
                    memset(buf, ' ', HELP_LINE_COLUMN - skip);
                    snprintf(buf + HELP_LINE_COLUMN - skip,
                        length+1,"%s",&entry->description[ndx]);
                    buf[HELP_LINE_COLUMN-skip+length+1] = '\0';
                    fprintf(stdout, "%s\n", buf);
                    ndx += length;
                    skip = 0;
                }
            } while(length > 0);
        }
    }
    globus_list_free(alphabetized_options);
}

globus_result_t
globus_options_command_line_process(
    globus_options_handle_t             handle,
    int                                 argc,
    char **                             argv)
{
    int                                 func_argc;
    globus_result_t                     res;
    int                                 i;
    int                                 j;
    char **                             arg_parm;
    int                                 used;
    char *                              current_arg;
    globus_bool_t                       found;
    globus_l_options_table_t *          table;
    globus_list_t *                     list;
    GlobusFuncName(globus_options_command_line_process);

    for(i = 1; i < argc; i++)
    {
        current_arg = argv[i];
        found = GLOBUS_FALSE;

        for(list = handle->table_list;
            !globus_list_empty(list);
            list = globus_list_rest(list))
        {
            table = (globus_l_options_table_t *)globus_list_first(list);
            for(j = 0; j < table->table_size && !found; j++)
            {
                found = GLOBUS_FALSE;
                if(table->table[j].short_opt != NULL &&
                    current_arg[0] == '-' &&
                    strcmp(&current_arg[1], table->table[j].short_opt) == 0)
                {
                    found = GLOBUS_TRUE;
                }
                else if(table->table[j].opt_name != NULL &&
                    ((current_arg[0] == '-' && 
                        strcmp(&current_arg[1], 
                            table->table[j].opt_name) == 0) ||
                     (current_arg[0] == '-' && current_arg[1] == '-' &&
                    strcmp(&current_arg[2], table->table[j].opt_name) == 0)))
                {
                    found = GLOBUS_TRUE;
                }

                if(found)
                {
                    func_argc = argc - i;
                    if(table->table[j].arg_count == 0)
                    {
                        arg_parm = NULL;
                    }
                    else
                    {
                        arg_parm = &argv[i+1];
                    }

                    if(func_argc > table->table[j].arg_count)
                    {
                        /* initialized the in/out to the max */
                        used = table->table[j].arg_count;

                        res = table->table[j].func(
                            handle,
                            table->table[j].opt_name,
                            arg_parm,
                            table->user_arg,
                            &used);
                        if(used < 0)
                        {
                            res=globus_error_put(globus_error_construct_error(
                                NULL,
                                NULL,
                                GLOBUS_OPTIONS_NOT_ENOUGH_ARGS,
                                __FILE__,
                                _globus_func_name,
                                __LINE__,
                                "Not enough parameters for: %s",
                                current_arg));
                            return res;
                        }
                        if(res != GLOBUS_SUCCESS)
                        {
                            return res;
                        }
                        i += used;
                    }
                    else
                    {
                        return globus_error_put(globus_error_construct_error(
                            NULL,
                            NULL,
                            GLOBUS_OPTIONS_NOT_ENOUGH_ARGS,
                            __FILE__,
                            _globus_func_name,
                            __LINE__,
                            "Not enough parameters for: %s",
                            current_arg));
                    }
                }
            }
        }
        if(!found)
        {
            /* call unknown handler with an array of all remaining args */
            if(handle->unknown_func != NULL)
            {
                res = handle->unknown_func(handle, handle->unknown_arg, argc-i, &argv[i]);
                return res;
            }
            else
            {
                return globus_error_put(globus_error_construct_error(
                    NULL,
                    NULL,
                    GLOBUS_OPTIONS_INVALID_PARAMETER,
                    __FILE__,
                    _globus_func_name,
                    __LINE__,
                    "Invalid parameter: %s",
                    current_arg));
            }
        }
    }

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_options_env_process(
    globus_options_handle_t             handle)
{
    globus_list_t *                     list;
    globus_l_options_table_t *          table;
    globus_result_t                     res;
    int                                 i;
    char *                              tmp_str;
    int                                 used;

    for(list = handle->table_list;
        !globus_list_empty(list);
        list = globus_list_rest(list))
    {
        table = (globus_l_options_table_t *) globus_list_first(list);

        for(i = 0; i < table->table_size; i++)
        {
            if(table->table[i].env != NULL)
            {
                tmp_str = getenv(table->table[i].env);
                /* TODO: parse into mulitple args */
                if(tmp_str != NULL)
                {
                    if(*tmp_str == '\0')
                    {
                        tmp_str = NULL;
                    }
                    used = 1;
                    res = table->table[i].func(
                        handle,
                        table->table[i].opt_name,
                        &tmp_str,
                        table->user_arg,
                        &used);
                    if(res != GLOBUS_SUCCESS)
                    {
                        return res;
                    }
                }
            }
        }
    }

    return GLOBUS_SUCCESS;
}

static
char *
globus_l_options_trim(
    char *                              ins)
{
    char *                              end;
    while(isspace(*ins))
    {
        ins++;
    }

    end = strchr(ins, '\0') - 1;

    while(isspace(*end))
    {
        end--;
    } 

    end++;
    *end = '\0';

    return ins;
}


static
globus_result_t
globus_l_options_file_process(
    globus_options_handle_t             handle,
    char *                              filename,
    char *                              xinetd_name)
{
    globus_list_t *                     list;
    globus_l_options_table_t *          table;
    char *                              opt_arg;
    int                                 used;
    globus_result_t                     res;
    FILE *                              fptr;
    char *                              trim_line;
    char                                line[1024];
    char                                file_option[1024];
    char                                value[1024];
    int                                 i;
    int                                 rc;
    int                                 line_num;
    int                                 optlen;
    char *                              p;
    globus_bool_t                       done;
    GlobusFuncName(globus_options_file_process);

    fptr = fopen(filename, "r");
    if(fptr == NULL)
    {
        return globus_error_put(globus_error_construct_error(
            NULL,
            NULL,
            GLOBUS_OPTIONS_HELP,
            __FILE__,
            _globus_func_name,
            __LINE__,
            "No such file"));
    }

    line_num = 0;
    if(xinetd_name != NULL)
    {
        done = GLOBUS_FALSE;
        while(!done && fgets(line, sizeof(line), fptr) != NULL)
        {
            trim_line = globus_l_options_trim(line);

            if(strcmp(line, "{") == 0)
            {
                done = GLOBUS_TRUE;
            }
            line_num++;
        }
    }

    while(fgets(line, sizeof(line), fptr) != NULL)
    {
        trim_line = globus_l_options_trim(line);
        if(xinetd_name != NULL)
        {
            char *                      tmp_str;

            tmp_str = strstr(line, "+");
            if(tmp_str != NULL)
            {
                tmp_str[0] = ' ';
            }
            tmp_str = strchr(line, '=');
            if(tmp_str != NULL)
            {
                *tmp_str = ' ';
            }
            tmp_str = globus_l_options_trim(line);
            if(strcmp(tmp_str, "}") == 0)
            {
                break; /* so lazy */
            }
        }

        line_num++;
        p = trim_line;
        optlen = 0;
        if(*p == '\0')
        {
            continue;
        }
        if(*p == '#')
        {
            continue;
        }

        if(*p == '"')
        {
            rc = sscanf(p, "\"%[^\"]\"", file_option);
            optlen = 2;
        }
        else
        {
            rc = sscanf(p, "%s", file_option);
        }
        if(rc != 1)
        {
            goto error_parse;
        }
        optlen += strlen(file_option);
        p = p + optlen;

        optlen = 0;
        while(*p && isspace(*p))
        {
            p++;
        }
        if(*p == '"')
        {
            rc = sscanf(p, "\"%[^\"]\"", value);
            optlen = 2;
        }
        else
        {
            strcpy(value, p);
            rc = 1;
        }
        if(rc != 1)
        {
            opt_arg = NULL;
            optlen = 0;
        }
        else
        {
            opt_arg = value;
            optlen += strlen(value);
        }

        for(list = handle->table_list;
            !globus_list_empty(list);
            list = globus_list_rest(list))
        {
            table = (globus_l_options_table_t *) globus_list_first(list);
                        
            for(i = 0; i < table->table_size; i++)
            {
                if(strcmp(file_option, table->table[i].opt_name) == 0)
                {
                    res = table->table[i].func(
                        handle,
                        table->table[i].opt_name,
                        &opt_arg,
                        table->user_arg,
                        &used);
                    if(res != GLOBUS_SUCCESS)
                    {
                        return res;
                    }
                }
            }
        }
    }

    fclose(fptr);

    return GLOBUS_SUCCESS;

error_parse:
    fclose(fptr);
    fprintf(stderr, "Problem parsing config file %s: line %d\n",
        filename, line_num);
    return -1;
}

globus_result_t
globus_options_file_process(
    globus_options_handle_t             handle,
    char *                              filename)
{
    return globus_l_options_file_process(handle, filename, NULL);
}

globus_result_t
globus_options_xinetd_file_process(
    globus_options_handle_t             handle,
    char *                              filename,
    char *                              service_name)
{
    return globus_l_options_file_process(handle, filename, service_name);
}
