%{
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
#include "globus_rsl.h"

struct globus_parse_state_s;
#include "globus_rsl_parser.h"
#include "globus_rsl_scanner.h"

#include "globus_i_rsl_parser.h"
#include <stdio.h>
#include <memory.h>
#include <string.h>
/* stdlib is required on aix */
#include <stdlib.h>
#include <assert.h>


extern globus_mutex_t globus_i_rsl_mutex;
int globus_rslget_column  (yyscan_t yyscanner);


    
#define GLOBUS_RSL_MIN(x,y) ((x) < (y) ? (x) : (y))

void
yyerror(YYLTYPE *loc, yyscan_t scanner, struct globus_parse_state_s *parse_state, char * yymsg);
/* Provide our own function for reading input.  The default YY_INPUT
 * is overridden in globus_rsl_parser.l
 * from page 157 of Nutshell lex & yacc
 */
%}

%pure_parser
%error-verbose
%locations
%locations
%parse-param { void * scanner }
%parse-param { struct globus_parse_state_s * parse_state }
%lex-param { void * scanner }
%debug

%start start

%union
{
  int                  Int;
  char               * String;

  globus_rsl_t       * RSL;
  globus_rsl_value_t * RSLval;
  globus_list_t      * List;
}

%token <String> RSL_STRING
%token RSL_OP_AND RSL_OP_OR RSL_OP_MULTIREQ
%token RSL_OP_EQ RSL_OP_NEQ RSL_OP_LT RSL_OP_LTEQ RSL_OP_GT RSL_OP_GTEQ
%token RSL_OP_CONCATENATE
%token RSL_LPAREN RSL_RPAREN 
%token RSL_VARIABLE_START

%type <RSL>    specification
%type <RSL>    request
%type <List>   spec_list
%type <List>   value_list
%type <RSLval> value
%type <RSLval> simple_value
%type <RSLval> variable
%type <RSLval> sequence
%type <String> literal_string
%type <Int> infix_operator
%type <Int> prefix_operator

%%

start:
  specification 	{ parse_state->rsl_spec = $<RSL>1; }


specification:
  prefix_operator spec_list
    {
      $$ = globus_rsl_make_boolean ($<Int>1, $<List>2);
      assert (($$)!=NULL);
    }
  | request
    { 
      $$ = $<RSL>1;
    }
  ;


prefix_operator:
  RSL_OP_MULTIREQ	{ $$ = GLOBUS_RSL_MULTIREQ; }
  | RSL_OP_AND		{ $$ = GLOBUS_RSL_AND; }
  | RSL_OP_OR		{ $$ = GLOBUS_RSL_OR; }
  ;

  
spec_list:
  RSL_LPAREN specification RSL_RPAREN spec_list
    {
      $$ = globus_list_cons ((void *) $<RSL>2, $<List>4);
      assert (($$)!=NULL);
    }
  | RSL_LPAREN specification RSL_RPAREN
    {
      $$ = globus_list_cons ((void *) $<RSL>2, NULL);
      assert (($$)!=NULL);
    }
  ;


request:
  literal_string infix_operator sequence
    {
      $$ = globus_rsl_make_relation ($<Int>2, $<String>1, $<RSLval>3);
      assert (($$)!=NULL);
    }
  ;


infix_operator:
  RSL_OP_EQ 		{ $$ = GLOBUS_RSL_EQ; }
  | RSL_OP_NEQ 		{ $$ = GLOBUS_RSL_NEQ; }
  | RSL_OP_GT 		{ $$ = GLOBUS_RSL_GT; }
  | RSL_OP_GTEQ 	{ $$ = GLOBUS_RSL_GTEQ; }
  | RSL_OP_LT 		{ $$ = GLOBUS_RSL_LT; }
  | RSL_OP_LTEQ 	{ $$ = GLOBUS_RSL_LTEQ; }
  ;

sequence:
  value_list
    {
      $$ = globus_rsl_value_make_sequence ($<List>1);
      assert (($$)!=NULL);
    }
  ;

value_list:
  value value_list
    {
      $$ = globus_list_cons ((void *) $<RSL>1, $<List>2);
      assert (($$)!=NULL);
    }
  | value 
    {
      $$ = globus_list_cons ((void *) $<RSL>1, NULL);
      assert (($$)!=NULL);
    }
  ;


value:
  RSL_LPAREN sequence RSL_RPAREN
    { 
      $$ = $<RSLval>2;
    }
  | simple_value 	{ $$ = $<RSLval>1; }
  ;


simple_value:
  simple_value RSL_OP_CONCATENATE simple_value
    {
      $$ = globus_rsl_value_make_concatenation ($<RSLval>1, $<RSLval>3);
      assert (($$)!=NULL);
    }
  | literal_string
    { 
      $$ = globus_rsl_value_make_literal ($<String>1);
      assert (($$)!=NULL);
    }
  | variable 		{ $$ = $<RSLval>1; }
  ;


variable:
  RSL_VARIABLE_START RSL_LPAREN sequence RSL_RPAREN
    {
      $$ = globus_rsl_value_make_variable ($<RSLval>3);
      assert (($$)!=NULL);
    }
  ;

literal_string: RSL_STRING
    { $$ = $<String>1;
    }
  ;

%%

void
yyerror(YYLTYPE * loc, yyscan_t scanner, struct globus_parse_state_s * parse_state, char * yymsg)
{
    parse_state->error_structure =
       (globus_rsl_parse_error_t *) malloc(sizeof(globus_rsl_parse_error_t));
    parse_state->rsl_spec = (globus_rsl_t *) NULL;
    parse_state->error_structure->code = 1;
    globus_rslget_lineno(scanner),
    globus_rslget_column(scanner),
    sprintf(parse_state->error_structure->message,"%s: %s",
            globus_rslget_text(scanner),
            yymsg);
}

/**
 * @brief Parse an RSL string
 * @ingroup globus_rsl
 * @details
 * The globus_rsl_parse() function parses the string pointed to by
 * the @a buf parameter into an RSL syntax tree. The caller is responsible
 * for freeing that tree by calling globus_rsl_free_recursive().
 *
 * @param buf
 *     A NULL-terminated string that contains an RSL relation or boolean
 *     composition.
 *
 * @return
 *     Upon success, the globus_rsl_parse() function returns the parse
 *     tree generated by processing its input. If an error occurs, 
 *     globus_rsl_parse() returns NULL.
 */
globus_rsl_t *globus_rsl_parse(char *buf)
{
    globus_rsl_t *                      rsl = NULL;
    yyscan_t                            scanner;
    struct globus_parse_state_s                parse_state = {0};


    if (!buf)
    {
        goto null_buf;
    }

    if (parse_state.error_structure)
    {
        parse_state.error_structure->code = 0;
    }
    parse_state.myinput = buf;
    parse_state.myinputptr = buf;
    parse_state.myinputlim = buf + strlen(buf);

    globus_rsllex_init(&scanner);
    globus_rslset_extra(&parse_state, scanner);

    yyparse(scanner, &parse_state);

    if (parse_state.globus_parse_error_flag)
    {
        goto parse_error;
    }
    else
    {
        rsl = parse_state.rsl_spec;
    }
    globus_rsllex_destroy(scanner);
parse_error:
null_buf:
    return rsl;
} /* globus_rsl_parse() */

extern
int
globus_i_rsl_yyinput(struct globus_parse_state_s *parse_state, char *buf, yy_size_t *num_read, int max_size)
{

    int n = GLOBUS_RSL_MIN(max_size,
                          (parse_state->myinputlim - parse_state->myinputptr));

    if (n > 0)
    {
        memcpy(buf, parse_state->myinputptr, n);
        parse_state->myinputptr += n;
    }
    *num_read = n;

    return n;
}
/* globus_i_rsl_yyinput() */
