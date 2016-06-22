// UCLA CS 111 Lab 1 command reading

// Copyright 2012-2014 Paul Eggert.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "command.h"
#include "command-internals.h"

#include "alloc.h"
#include <error.h>

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG 0

/* Other */

enum
{
    MAX_STR_LEN = 10,
    MAX_WORD_LEN = 5
};

enum token_type
{
    IF_TOKEN,
    THEN_TOKEN,
    ELSE_TOKEN,
    FI_TOKEN,
    
    WHILE_TOKEN,
    UNTIL_TOKEN,
    DO_TOKEN,
    DONE_TOKEN,
    
    WORD_TOKEN,
    
    NEWLINE_TOKEN,
    
    SEQUENCE_TOKEN,
    PIPE_TOKEN,
    
    L_SUBSHELL_TOKEN,
    R_SUBSHELL_TOKEN,
    
    L_REDIR_TOKEN,
    R_REDIR_TOKEN,
    
    MAX_TOKEN_NO
};


/* Struct Declarations */

typedef struct command_node *command_node_t;

typedef struct command_node
{
    command_t command;
    command_node_t next;
    command_node_t prev;
} command_node;

typedef struct command_stream
{
    command_node_t head;
} command_stream;

typedef struct token
{
    enum token_type token_type;
    int is_operator;
    int is_reserved_word;
    char *word;
    int line;
    struct token *next;
    struct token *prev;
} token;

typedef struct compound_token
{
    enum token_type token_type;
    struct compound_token *next;
    struct compound_token *prev;
} compound_token;

typedef struct token_stream
{
    struct token *token;
    struct token_stream *next;
} token_stream;


// Function declrations
// ====================

void print_custom_error(int line_number, char message);
void print_custom_error_str(int line_number, char* message_str);
int is_word_char(int c);
int is_operator(int c);
int is_valid(int c);

token* init_token (void);
compound_token *init_compound_token (void);
token_stream *init_token_stream (void);
command_t init_command_t (void);
command_node * init_command_node (void);
command_stream_t init_command_stream_t (void);

int precedence (enum token_type type);
command_t process_command_and_operator_stacks(command_node **last_command_node_p, token **last_operator_p);
command_t process_token (token **token_pp);


// Main functions
// ==============

token *
get_next_token(token *current_token)
{
    token *temp_t = current_token;
    current_token = current_token->next;
    free(temp_t);
    
    return current_token;
}

void
remove_extra_semicolon_if_exists(token **last_token_pp)
{
    token *last_token = *last_token_pp;
    
    // remove the optional ';' if it precedes ')'
    if (last_token && last_token->token_type == SEQUENCE_TOKEN)
    {
        token *temp = last_token;
        
        last_token = last_token->prev;
        
        if (last_token)
            last_token->next = NULL;
        
        free(temp);
        
        *last_token_pp = last_token;
    }
}

command_stream_t
make_command_stream (int (*get_next_byte) (void *),
		     void *get_next_byte_argument)
{
    int line_count = 1;
    int inside_comment = 0;
    int get_next = 1;
    
    // token stream
    token_stream *root_token_stream = NULL;
    token_stream *current_token_stream = root_token_stream;
    token_stream *last_token_stream = current_token_stream;
    
    // token
    token *last_token = NULL;
    
    // compound token
    compound_token *last_compound_token = NULL;
    
    // char from input
    int current_char = get_next_byte(get_next_byte_argument);
    
    // misc
    int last_char_is_whitespace = 0;
    
    while (current_char != EOF)
    {
        if (!is_valid(current_char))
        {
            if (!inside_comment)
                print_custom_error(line_count, current_char);  // error: invalid char
        }
        else if (!current_token_stream)
        {
            if (DEBUG)
                printf("\n*** Creating a new token stream ***\n\n");
            
            // create token stream for a new complete command
            current_token_stream = init_token_stream();
            
            if (!root_token_stream)
                root_token_stream = current_token_stream;
            
            if (last_token_stream)
                last_token_stream->next = current_token_stream;
            
            last_token_stream = current_token_stream;
            
            // Reset pointers to tokens
            last_token = NULL;
            last_compound_token = NULL;
        }
        
        
        // create new token
        token *current_token = init_token();
        current_token->line = line_count;
        
        if (current_char == '\n')
        {
            line_count++;
            inside_comment = 0;
            
            if (DEBUG)
            {
                if (!last_compound_token)
                {
                    printf("\nCompound token stack is empty!\n\n");
                }
            }
            
            // check if we need to create a new token stream (for a new complete command)
            if (last_token
                && last_token->token_type != NEWLINE_TOKEN  // this can potentially cause an error
                && !last_compound_token
                && (!last_token->is_operator
                    || last_token->token_type == SEQUENCE_TOKEN
                    || last_token->token_type == R_SUBSHELL_TOKEN
                    || last_token->token_type == FI_TOKEN       // unnecessary
                    || last_token->token_type == DONE_TOKEN))   // unnecessary
            {
                current_token_stream = NULL;
                
                // remove extra semicolon
                remove_extra_semicolon_if_exists(&last_token);
                
                if (DEBUG)
                    printf("\n***Ending current token stream***\n\n");
            }
            // *** IMPORTANT ***
            // This block has to precede the below blocks
            // because it checks for existing NEWLINE before it
            // gets converted to a SEMICOLON
            else if (last_token
                     && (last_token->token_type == L_REDIR_TOKEN
                         || last_token->token_type == R_REDIR_TOKEN))
            {
                // error: No newlines allowed after '<' or '>'
                print_custom_error(line_count, current_char);
            }
            else if (last_token && !last_compound_token && last_token->token_type != NEWLINE_TOKEN)
            {
                if (DEBUG)
                    printf("\nCreating a NEWLINE token\n\n");
                
                // Create a new token
                current_token->token_type = NEWLINE_TOKEN;
                current_token->is_operator = 0;
                current_token->is_reserved_word = 0;
                
                if (last_token)
                {
                    last_token->next = current_token;
                    current_token->prev = last_token;
                }
                last_token = current_token;
            }
            else if (last_token
                     && last_compound_token
                     && last_token->token_type != SEQUENCE_TOKEN
                     && (last_token->token_type == DONE_TOKEN
                         || last_token->token_type == FI_TOKEN
                         || last_token->token_type == R_SUBSHELL_TOKEN
                         || last_token->token_type == WORD_TOKEN))
            {
                if (DEBUG)
                    printf("Replacing current newline with a semicolon\n");
                
                current_char = ';';
                goto process_operator;
            }
        }
        else if (current_char == ' ' || current_char == '\t' || inside_comment)
        {
            // do nothing
            if (!inside_comment)
                last_char_is_whitespace = 1;
        }
        else if (is_word_char(current_char))
        {
            size_t max_word_size = sizeof(char) * MAX_STR_LEN;
            char *current_word = (char *)checked_malloc(max_word_size);
            int current_word_len = 0;
            
            // get the next char
            do {
                current_word[current_word_len++] = current_char;
                
                if (current_word_len == MAX_STR_LEN)
                    current_word = (char *)checked_grow_alloc(current_word, &max_word_size);
                
                current_char = get_next_byte(get_next_byte_argument);
                
            } while (is_word_char(current_char));
            
            // terminate the string
            current_word[current_word_len] = 0;
            
            if (DEBUG)
                printf("Word is %s\n", current_word);
            
            if (last_token
                && (last_token->token_type == R_SUBSHELL_TOKEN
                    || last_token->token_type == DONE_TOKEN
                    || last_token->token_type == FI_TOKEN))
            {
                // error: word cannot follow ')'
                print_custom_error_str(line_count, current_word);
            }
            
            // no need to get the next byte because we already did
            get_next = 0;
            
            enum token_type token_type = WORD_TOKEN;
            int is_reserved_word = 0;
            
            /**
             * Check if this word is a reserved word
             *
             * reserved word is only recognized if it is the first word
             * of a simple command
             *
             */
            
            // do special checking for if, until, and while
            // if can be preceded by ';', '\n', '|', '(', if, until, while
            
            // This word can potentially be a reserved word
            
            is_reserved_word = 1;
            
            if (!strcmp(current_word, "if"))
            {
                token_type = IF_TOKEN;
            }
            else if (!strcmp(current_word, "then"))
            {
                token_type = THEN_TOKEN;
            }
            else if (!strcmp(current_word, "else"))
            {
                token_type = ELSE_TOKEN;
            }
            else if (!strcmp(current_word, "fi"))
            {
                token_type = FI_TOKEN;
            }
            else if (!strcmp(current_word, "while"))
            {
                token_type = WHILE_TOKEN;
            }
            else if (!strcmp(current_word, "until"))
            {
                token_type = UNTIL_TOKEN;
            }
            else if (!strcmp(current_word, "do"))
            {
                token_type = DO_TOKEN;
            }
            else if (!strcmp(current_word, "done"))
            {
                token_type = DONE_TOKEN;
            }
            else
            {
                is_reserved_word = 0;
            }
            
            if (is_reserved_word)
            {
                if (DEBUG)
                    printf("This word is potentially a reserved word...\n");
                
                if (!last_token
                    || last_token->token_type == PIPE_TOKEN
                    || last_token->is_reserved_word)
                {
                    switch (token_type)
                    {
                        case IF_TOKEN:
                        case UNTIL_TOKEN:
                        case WHILE_TOKEN:
                            break;
                            
                        default:
                            // Can only be if, until, while
                            print_custom_error_str(line_count, current_word);
                            break;
                    }
                }
                else if (last_token->token_type == WORD_TOKEN)
                {
                    // This word is not a reserved word
                    token_type = WORD_TOKEN;
                    is_reserved_word = 0;
                }
                else if (last_token->token_type == L_REDIR_TOKEN
                         || last_token->token_type == R_REDIR_TOKEN)
                {
                    // '<' and '>' only follows non-reserved WORD
                    print_custom_error_str(line_count, current_word);
                }
            }
            
            // Create a new token
            current_token->token_type = token_type;
            current_token->is_operator = 0;
            current_token->is_reserved_word = is_reserved_word;
            current_token->word = current_word;
            
            if (current_token->is_reserved_word)
            {
                if (DEBUG)
                    printf("Current word is a reserved word...\n");
                
                compound_token *new_compound_token = init_compound_token();
                
                if (!last_compound_token)
                {
                    if (DEBUG)
                        printf("****Creating a compound token stack****\n");
                    
                    switch (current_token->token_type)
                    {
                        case IF_TOKEN:
                        case WHILE_TOKEN:
                        case UNTIL_TOKEN:
                            break;
                            
                        default:
                            // error: a compound command can only begin
                            // with 'if', 'while', 'until', or '('
                            print_custom_error_str(line_count, current_word);
                            break;
                    }
                    
                    if (DEBUG)
                        printf("ADDING A >>%s<< COMPOUND TOKEN TO STACK\n", current_token->word);
                    
                    new_compound_token->token_type = current_token->token_type;
                    last_compound_token = new_compound_token;
                }
                else
                {
                    switch (current_token->token_type)
                    {
                        case FI_TOKEN:
                            if (last_compound_token->token_type == THEN_TOKEN
                                || last_compound_token->token_type == ELSE_TOKEN)
                            {
                                // no error
                                
                                if (last_compound_token->token_type == ELSE_TOKEN)
                                {
                                    compound_token *temp3 = last_compound_token;
                                    
                                    last_compound_token = last_compound_token->prev;
                                    
                                    if (last_compound_token)
                                        last_compound_token->next = NULL;
                                    
                                    free(temp3);
                                }
                                
                                compound_token *temp = last_compound_token;
                                compound_token *temp2 = last_compound_token->prev;
                                
                                last_compound_token = last_compound_token->prev->prev;
                                
                                if (last_compound_token)
                                    last_compound_token->next = NULL;
                                
                                free(temp);
                                free(temp2);
                                
                                remove_extra_semicolon_if_exists(&last_token);
                            }
                            else
                            {
                                // error: fi can only be followed by then or else
                                print_custom_error_str(line_count, current_word);
                            }
                            
                            break;
                        
                        case ELSE_TOKEN:
                            if (last_compound_token->token_type != THEN_TOKEN)
                            {
                                // error: else can only be followed by then
                                print_custom_error_str(line_count, current_word);
                            }
                            
                            new_compound_token->token_type = current_token->token_type;
                            
                            last_compound_token->next = new_compound_token;
                            new_compound_token->prev = last_compound_token;
                            last_compound_token = new_compound_token;
                            
                            remove_extra_semicolon_if_exists(&last_token);
                            
                            break;
                            
                        case THEN_TOKEN:
                            if (last_compound_token->token_type != IF_TOKEN)
                            {
                                // error: then can only be followed by if
                                print_custom_error_str(line_count, current_word);
                            }
                            
                            new_compound_token->token_type = current_token->token_type;
                            
                            last_compound_token->next = new_compound_token;
                            new_compound_token->prev = last_compound_token;
                            last_compound_token = new_compound_token;
                            
                            if (DEBUG)
                                printf("ADDING A >>%s<< COMPOUND TOKEN TO STACK\n", current_token->word);
                            
                            remove_extra_semicolon_if_exists(&last_token);
                            
                            break;
                            
                        case DONE_TOKEN:
                            if (last_compound_token->token_type != DO_TOKEN)
                            {
                                // error: done can only be followed by do
                                print_custom_error_str(line_count, current_word);
                            }
                            
                            // no error
                            compound_token *temp = last_compound_token;
                            compound_token *temp2 = last_compound_token->prev;
                            
                            last_compound_token = last_compound_token->prev->prev;
                            
                            if (last_compound_token)
                            {
                                if (last_compound_token->token_type >= MAX_TOKEN_NO)
                                    last_compound_token = NULL;
                                else
                                    last_compound_token->next = NULL;
                            }
                            
                            free(temp);
                            free(temp2);
                            
                            remove_extra_semicolon_if_exists(&last_token);
                            
                            break;
                            
                        case DO_TOKEN:
                            if (last_compound_token->token_type != WHILE_TOKEN
                                && last_compound_token->token_type != UNTIL_TOKEN)
                            {
                                // error: do can only be followed by while or until
                                print_custom_error_str(line_count, current_word);
                            }
                            
                            new_compound_token->token_type = current_token->token_type;
                            
                            last_compound_token->next = new_compound_token;
                            new_compound_token->prev = last_compound_token;
                            last_compound_token = new_compound_token;
                            
                            remove_extra_semicolon_if_exists(&last_token);
                            
                            break;
                            
                        default:
                            // IF, UNTIL, or WHILE
                            new_compound_token->token_type = current_token->token_type;
                            
                            last_compound_token->next = new_compound_token;
                            new_compound_token->prev = last_compound_token;
                            last_compound_token = new_compound_token;
                            
                            if (DEBUG)
                                printf("ADDING A >>%s<< COMPOUND TOKEN TO STACK\n", current_token->word);
                            
                            break;
                    }
                    
                    // There is no syntax error
                }
            }   // if current_token->is_reserved_word
            
            if (DEBUG)
                printf("Done creating WORD/RESERVED TOKEN\n");
            
            
            if (last_token)
            {
                // update linked list of tokens
                last_token->next = current_token;
                current_token->prev = last_token;
            }
            else
            {
                // this token is root token in the current token stream
                current_token_stream->token = current_token;
            }
            last_token = current_token;
            last_token->next = NULL;
        }
        else if (is_operator(current_char))
        {
        process_operator:
            {
                enum token_type token_type;
                
                switch (current_char)
                {
                    case ';':
                        token_type = SEQUENCE_TOKEN;
                        
                        // ';' cannot follow a reserved word
                        // unless it is the closing reserved word
                        if (last_token
                            && last_token->is_reserved_word
                            && last_token->token_type != FI_TOKEN
                            && last_token->token_type != DONE_TOKEN)
                            print_custom_error(line_count, current_char);
                        
                        break;
                        
                    case '|':
                        token_type = PIPE_TOKEN;
                        break;
                        
                    case '(':
                        // error: ')', '<', '>', WORD cannot follow '('
                        if (last_token
                            && (last_token->token_type == R_SUBSHELL_TOKEN
                                || last_token->token_type == L_REDIR_TOKEN
                                || last_token->token_type == R_REDIR_TOKEN
                                || last_token->token_type == WORD_TOKEN))
                            print_custom_error(line_count, current_char);
                        
                        token_type = L_SUBSHELL_TOKEN;
                        break;
                        
                    case ')':
                        token_type = R_SUBSHELL_TOKEN;
                        
                        remove_extra_semicolon_if_exists(&last_token);
                        
                        break;
                        
                    case '<':
                        token_type = L_REDIR_TOKEN;
                        break;
                        
                    case '>':
                        token_type = R_REDIR_TOKEN;
                        break;
                }
                
                if (last_token
                    && last_token->token_type == NEWLINE_TOKEN
                    && token_type != L_SUBSHELL_TOKEN
                    && token_type != R_SUBSHELL_TOKEN)
                {
                    // error: newline can only appear before '(' or ')'
                    print_custom_error(line_count, current_char);
                }
                
                // Create token
                current_token->token_type = token_type;
                current_token->is_operator = 1;
                current_token->is_reserved_word = 0;
                
                char *word = (char *)checked_malloc(sizeof(char));
                word[0] = current_char;
                current_token->word = word;
                
                
                // Push/pop '(' and ')' to/from Compound Token Stack
                if (current_token->token_type == L_SUBSHELL_TOKEN)
                {
                    if (DEBUG)
                        printf("Current token type is LEFT_SUBSHELL_TOKEN\n");
                    
                    compound_token *new_compound_token = init_compound_token();
                    new_compound_token->token_type = current_token->token_type;
                    
                    if (last_compound_token)
                    {
                        last_compound_token->next = new_compound_token;
                        new_compound_token->prev = last_compound_token;
                    }
                    
                    last_compound_token = new_compound_token;
                }
                else if (current_token->token_type == R_SUBSHELL_TOKEN)
                {
                    // Check if there is matching '(' for ')'
                    // ')' can not directly follow '('
                    // aka. there has to be a command inside a subshell
                    if (!last_compound_token
                        || last_token->token_type == L_SUBSHELL_TOKEN
                        || last_compound_token->token_type != L_SUBSHELL_TOKEN)
                    {
                        // error
                        print_custom_error(line_count, current_char);
                    }
                    
                    if (DEBUG)
                        printf("No syntax error for ')'\n");
                    
                    if (last_compound_token)
                    {
                        compound_token *temp = last_compound_token;
                        
                        last_compound_token = last_compound_token->prev;
                        
                        if (last_compound_token)
                            last_compound_token->next = NULL;
                        
                        free(temp);
                    }
                }
                
                if (last_token)
                {
                    last_token->next = current_token;
                    current_token->prev = last_token;
                    
                    if (last_token->is_operator)
                    {
                        if (last_token->token_type == L_SUBSHELL_TOKEN
                            && current_token->is_operator)
                        {
                            // error: '(' cannot be followed by an operator
                            print_custom_error(line_count, current_char);
                        }
                        
                        if (last_token->token_type != R_SUBSHELL_TOKEN
                            && last_token->token_type != L_SUBSHELL_TOKEN
                            && current_token->token_type != R_SUBSHELL_TOKEN
                            && current_token->token_type != L_SUBSHELL_TOKEN)
                        {
                            // error: consecutive operators except '(', ')'
                            print_custom_error(line_count, current_char);
                        }
                    }
                }
                else
                {
                    // This token is the root token of the current token stream
                    if (current_token->token_type != L_SUBSHELL_TOKEN)
                    {
                        // error: '(' is the only operator
                        // that is allowed to start a token stream
                        print_custom_error(line_count, current_char);
                    }
                    
                    // this token is root token in the current token stream
                    current_token_stream->token = current_token;
                }
                last_token = current_token;
                
            }
        }
        else if (current_char == '#')
        {
            // indicate that we are inside a comment
            // so all the characters up to a newline will
            // be ignored
            //
            // TODO:
            // It cannot be immediately preceded by an ordinary token
            // What is an ordinary token? WORD
            
            if (last_token
                && last_token->token_type == WORD_TOKEN
                && !last_char_is_whitespace)
                print_custom_error(line_count, current_char);
            
            inside_comment = 1;
        }
        
        if (current_char != ' ' && current_char != '\t')
            last_char_is_whitespace = 0;
        
        // keep the value of the last char so we can use it when checking reserved word
        if (get_next)
            current_char = get_next_byte(get_next_byte_argument);
    
        get_next = 1;
        
    } // end of while loop
    
    // check compound tokens
    if (last_compound_token)
    {
        // error: syntax error: unexpected end of file
        error(1, 0, "%i: syntax error: unexpected end of file", line_count);
    }
    
    // check the last char in the input
    remove_extra_semicolon_if_exists(&last_token);
    
    // There is no syntax error
    // we can construct tokens into commands
    // a token stream corresponds to a command node
    // we store the command node in the command stream
    
    command_stream_t a_command_stream = init_command_stream_t();
    
    command_node *last_command_node = NULL;
    
    while (root_token_stream)
    {
        command_node *new_command_node = init_command_node();
        
        if (!root_token_stream->token)
        {
            root_token_stream = root_token_stream->next;
            continue;
        }
        
        token *root_token = root_token_stream->token;
        new_command_node->command = process_token(&root_token);
        
        if (!a_command_stream->head)
            a_command_stream->head = new_command_node;
        
        if (last_command_node)
            last_command_node->next = new_command_node;
        
        last_command_node = new_command_node;
        
        // go to the next token stream and free the current one
        token_stream *temp_ts = root_token_stream;
        root_token_stream = root_token_stream->next;
        free(temp_ts);
    }
   
  return a_command_stream;
}

command_t
process_token (token **token_pp)
{
    // command stack
    command_node *last_command_node = NULL;
    
    // operator stack
    token *last_operator = NULL;
    
    // root token
    token *current_token = *token_pp;
    
    // misc
    int should_get_next_token = 1;
    
    while (current_token)
    {
        if (current_token->token_type == WORD_TOKEN)
        {
            if (DEBUG)
                printf("Current token is word\n");
                
            size_t word_size = sizeof(char *) * MAX_WORD_LEN;
            
            // change this to a more appropriate len
            char **word = (char **)checked_malloc(word_size);
            
            int word_count = 0;
            
            do
            {
                word[word_count++] = current_token->word;
                
                if (word_count == MAX_WORD_LEN)
                {
                    word = (char **)checked_grow_alloc(word, &word_size);   // check
                }
                
                // Release current token and get the next one
                current_token = get_next_token(current_token);
            }
            while(current_token && current_token->token_type == WORD_TOKEN);
            
            should_get_next_token = 0; // we already called the next token
            
            // terminate sentence
            word[word_count] = 0;
            
            // We have a simple command consisting of one or more words
            
            command_t c = init_command_t();
            c->type = SIMPLE_COMMAND;
            c->status = -1;  // TODO: check
            
            c->u.word = word;
            
            // Push current command to command stack
            command_node *cn = init_command_node();
            cn->command = c;
            
            if (last_command_node)
            {
                last_command_node->next = cn;
                cn->prev = last_command_node;
            }
            
            last_command_node = cn;
        }
        else if (current_token->is_operator)
        {
            if (DEBUG)
                printf("Current token is an operator\n");
            
            switch (current_token->token_type)
            {
                case PIPE_TOKEN:
                    
                    if (DEBUG)
                        printf("Process PIPE TOKEN\n");
                    
                case SEQUENCE_TOKEN:
                    
                    // Check operator precedence
                    while (last_operator
                           && precedence(current_token->token_type) <= precedence(last_operator->token_type))
                    {
                        command_t command = init_command_t();
                        command->type = last_operator->token_type == PIPE_TOKEN ? PIPE_COMMAND : SEQUENCE_COMMAND;
                        command->status = -1;
                        
                        command->u.command[1] = last_command_node->command;
                        command->u.command[0] = last_command_node->prev->command;
                        
                        
                        // Push new command to command stack
                        // and clean up
                        command_node *cn_temp = last_command_node;
                        
                        last_command_node = last_command_node->prev;
                        
                        if (last_command_node)
                        {
                            last_command_node->next = NULL;
                            last_command_node->command = command;
                        }
                        
                        free(cn_temp);
                        
                        
                        // Pop current operator from the operator stack
                        token *t_temp = last_operator;
                        
                        last_operator = last_operator->prev;
                        
                        if (last_operator)
                            last_operator->next = NULL;
                        
                        free(t_temp);
                    }
                    
                    if (DEBUG)
                        printf("No operator in stack\n");
                    
                    // Push newly created operator
                    token *operator = init_token();
                    operator->token_type = current_token->token_type;
                    
                    if (last_operator)
                    {
                        last_operator->next = operator;
                        operator->prev = last_operator;
                    }
                    
                    last_operator = operator;
                    
                    break;
                    
                case L_SUBSHELL_TOKEN:
                {
                    if (DEBUG)
                        printf("Lets process left subshell\n");
                    
                    command_t c = init_command_t();
                    c->type = SUBSHELL_COMMAND;
                    c->status = -1;
                    
                    current_token = get_next_token(current_token);
                    c->u.command[0] = process_token(&current_token);  // aka. token_pp
                    
                    if (DEBUG)
                    {
                        printf("DOne with command inside subshell\n");
                        printf("Current char: %s\n", current_token->word);
                    }
                    
                    c->u.command[1] = 0;
                    
                    // Push current command to command stack
                    command_node *cn = init_command_node();
                    cn->command = c;
                    
                    if (last_command_node)
                    {
                        last_command_node->next = cn;
                        cn->prev = last_command_node;
                    }
                    
                    last_command_node = cn;
                    
                    if (DEBUG)
                        printf("DOne with left subshell\n");
                    
                    break;
                }
                case R_SUBSHELL_TOKEN:
                    
                    if (DEBUG)
                        printf("RETURN A RIGHT SUBSHELL\n");
                    
                    *token_pp = current_token;
                    
                    return process_command_and_operator_stacks(&last_command_node, last_operator ? &last_operator : NULL);
                    break;
                    
                case L_REDIR_TOKEN:
                {
                    current_token = get_next_token(current_token);
                    
                    last_command_node->command->input = current_token->word;
                    break;
                }
                case R_REDIR_TOKEN:
                {
                    current_token = get_next_token(current_token);
                    
                    last_command_node->command->output = current_token->word;
                    break;
                }
                default:
                    break;
            }
        }
        else if (current_token->is_reserved_word)
        {
            if (DEBUG)
                printf("It is a reserved word TOKEN\n");
            
            switch (current_token->token_type)
            {
                case IF_TOKEN:
                case UNTIL_TOKEN:
                case WHILE_TOKEN:
                {
                    if (DEBUG)
                        printf("Start IF, UNTIL, WHILE\n");
                    
                    command_t c = init_command_t();
                    
                    c->type = (current_token->token_type == IF_TOKEN)
                                ? IF_COMMAND
                                : (current_token->token_type == UNTIL_TOKEN)
                                    ? UNTIL_COMMAND
                                    : WHILE_COMMAND;
                    
                    c->status = -1;
                    
                    current_token = get_next_token(current_token);
                    c->u.command[0] = process_token(&current_token);
                    
                    current_token = get_next_token(current_token);
                    c->u.command[1] = process_token(&current_token);
                    
                    // *** check special case
                    c->u.command[2] = 0;
                    if (current_token->token_type == ELSE_TOKEN)
                    {
                        current_token = get_next_token(current_token);
                        c->u.command[2] = process_token(&current_token);
                    }
                    
                    // Push current command to command stack
                    command_node *cn = init_command_node();
                    cn->command = c;
                    
                    if (last_command_node)
                    {
                        last_command_node->next = cn;
                        cn->prev = last_command_node;
                    }
                    
                    last_command_node = cn;
                    
                    if (DEBUG)
                    {
                        if (last_command_node)
                        {
                            printf("There is a last command node...\n");
                            
                            if (last_command_node->command->type == IF_COMMAND)
                                printf("This command is IF COMMAND\n");
                        }
                        
                        printf("Done with IF, UNTIL, WHILE\n");
                    }
                    
                    
                    break;
                }
                case THEN_TOKEN:
                case ELSE_TOKEN:
                case FI_TOKEN:
                case DO_TOKEN:
                case DONE_TOKEN:
                {
                    if (DEBUG)
                        printf("READY TO RETURN A THEN/ELSE/FI/DO/DONE\n");
                    
                    *token_pp = current_token;
                    
                    return process_command_and_operator_stacks(&last_command_node, last_operator ? &last_operator : NULL);
                    break;
                }
                default:
                    break;
            }   // end of switch for reserved word token
        }
        
        if (should_get_next_token)
        {
            current_token = get_next_token(current_token);
        }
        
        should_get_next_token = 1;
    }
    
    if (DEBUG)
        printf("No more tokens in the stream\n");
    
    return process_command_and_operator_stacks(&last_command_node, last_operator ? &last_operator : NULL);
}

command_t
process_command_and_operator_stacks(command_node **last_command_node_p, token **last_operator_p)
{
    // There are no more tokens to process in the current context
    // process what is left in command stack and operator stack
    
    command_node *last_command_node = *last_command_node_p;
    token *last_operator = last_operator_p ? *last_operator_p : NULL;
    
    while (last_operator)
    {
        command_t command = init_command_t();
        command->type = last_operator->token_type == PIPE_TOKEN ? PIPE_COMMAND : SEQUENCE_COMMAND;
        command->status = -1;
        
        command->u.command[1] = last_command_node->command;
        command->u.command[0] = last_command_node->prev->command;
        
        
        // Push new command to command stack
        // and clean up
        command_node *cn_temp = last_command_node;
        
        last_command_node = last_command_node->prev;
        
        if (last_command_node)
        {
            last_command_node->next = NULL;
            last_command_node->command = command;
        }
        
        free(cn_temp);
        
        
        // Pop operator from operator stack
        token *t_temp = last_operator;
        
        last_operator = last_operator->prev;
        
        if (last_operator)
            last_operator->next = NULL;
        
        free(t_temp);
    }
    
    // When there are no more operators in the operator stack
    // there must be a command in the command stack
    // pop it and return it
    command_t root_command = last_command_node->command;
    free(last_command_node);
    
    return root_command;
}

command_t
read_command_stream (command_stream_t s)
{
    if (!s)
        return NULL;
    
    command_node *cn = s->head;
    
    if (cn)
    {
        s->head = cn->next;
        return cn->command;
    }

  return NULL;
}

// Helper functions

int
precedence (enum token_type type)
{
    int p = 0;
    
    switch (type)
    {
        case PIPE_TOKEN:
            p = 10;
            break;
            
        case SEQUENCE_TOKEN:
            p = 9;
            break;
            
        default:
            ;
    }
    
    return p;
}

command_t
init_command_t (void)
{
    command_t command = (command_t)checked_malloc(sizeof(struct command));
    command->input = 0;
    command->output = 0;
    return command;
}

command_node *
init_command_node (void)
{
    command_node *cn = (command_node *)checked_malloc(sizeof(command_node));
    cn->command = NULL;
    cn->next = NULL;
    cn->prev = NULL;
    return cn;
}

command_stream_t
init_command_stream_t (void)
{
    command_stream_t cs = (command_stream_t)checked_malloc(sizeof(struct command_stream));
    cs->head = NULL;
    return cs;
}

token *
init_token (void)
{
    token* t = (token *)checked_malloc(sizeof(token));
    t->prev = NULL;
    t->next = NULL;
    return t;
}

compound_token *
init_compound_token (void)
{
    compound_token *ct = (compound_token *)checked_malloc(sizeof(compound_token));
    ct->next = NULL;
    ct->prev = NULL;
    return ct;
}

token_stream *
init_token_stream (void)
{
    token_stream *ts = (token_stream *)checked_malloc(sizeof(token_stream));
    ts->token = NULL;
    ts->next = NULL;
    return ts;
}

void
print_custom_error(int line_number, char message)
{
    // ex:
    // error: line 3: syntax error near unexpected token `then'
    
    char *message_str;
    
    if (message == '\n')
    {
        message_str = (char *)checked_malloc(sizeof(char) * 2);
        message_str = "\\n";
    } else {
        message_str = (char *)checked_malloc(sizeof(char));
        message_str[0] = message;
    }
    
    error (1, 0, "%i: syntax error near unexpected token `%s'", line_number, message_str);
}

void
print_custom_error_str(int line_number, char* message_str)
{
    // ex:
    // error: line 3: syntax error near unexpected token `then'
    
    error (1, 0, "%i: syntax error near unexpected token `%s'", line_number, message_str);
}

int is_word_char(int c)
{
    return isalnum(c) || (c == '!') || (c == '%') || (c == '+') || (c == ',')
    || (c == '-') || (c == '.') || (c == '/') || (c == ':')
    || (c == '@') || (c == '^') || (c == '_');
}

int is_operator(int c)
{
    return (c == ';') || (c == '|') || (c == '(') || (c == ')') || (c == '<')
    || (c == '>');
}

int is_valid(int c)
{
    return is_word_char(c) || is_operator(c) || (c == '\n') || (c == '\t') || (c == ' ')
    || (c == '#');
}



