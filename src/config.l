/*
 * Copyright (C) 2002  Erik Fears
 *
 *    QSTRING , ccomment and hashcomment taken from Hybrid7:
 *    Copyright (C) 2002 by the past and present ircd coders, and others.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to
 *
 *       The Free Software Foundation, Inc.
 *       59 Temple Place - Suite 330
 *       Boston, MA  02111-1307, USA.
 *
 *
 */

%option case-insensitive
%option noyywrap
%option nounput

%{
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "config.tab.h"


void ccomment(void);
void hashcomment(void);

int linenum = 1;
char linebuf[512];

%}

string                 \"[^\"\n]*[\"\n]
comment                #.*
whitespace             [ \t]*

%%

"/*"                    { ccomment(); }

{comment}               { hashcomment(); }

{string}                {
                           /* QSTRING from Hybrid7. Why re-invent the wheel? */

                           if(yytext[yyleng-2] == '\\')
                           {
                              yyless(yyleng-1); /* return last quote */
                              yymore();         /* append next string */
                           }
                           else
                           {
                              yylval.string = yytext+1;
                              if(yylval.string[yyleng-2] != '"') ; /* log error */
                              else
                              {
                                 int i,j;

                                 yylval.string[yyleng-2] = '\0'; /* remove close
                                                                  *  quote
                                                                  */

                                 for (j=i=0 ;yylval.string[i] != '\0'; i++,j++)
                                 {
                                    if (yylval.string[i] != '\\')
                                    {
                                       yylval.string[j] = yylval.string[i];
                                    }
                                    else
                                    {
                                        i++;
                                        yylval.string[j] = yylval.string[i];
                                    }
                                 }
                                 yylval.string[j] = '\0';
                                 return STRING;
                              }
                           }
             
                        }

AWAY                    { return AWAY;     }
CHANNELS                { return CHANNELS; }
IRC                     { return IRC;      }
KEYS                    { return KEYS;     }
MASK                    { return MASK;     }
MODE                    { return MODE;     }
NEGCACHE                { return NEGCACHE; }
NICK                    { return NICK;     }
OPER                    { return OPER;     }
OPTIONS                 { return OPTIONS;  }
PASSWORD                { return PASSWORD; }
PIDFILE                 { return PIDFILE;  }
PORT                    { return PORT;     }
REALNAME                { return REALNAME; }
SCANNER                 { return SCANNER;  }
SERVER                  { return SERVER;   }
USER                    { return USER;     }
USERNAME                { return USERNAME; }
VHOST                   { return VHOST;    }

[0-9]+                  {
                           yylval.number=atoi(yytext); 
                           return NUMBER;
                        }

\n.*                    { 
                           strcpy(linebuf, yytext+1); 
                           linenum++; 
                           yyless(1); 
                        }

{whitespace}            /* ignore whitespace */;

.                       return yytext[0]; 

%%


void hashcomment(void)
{
}


/* C-comment ignoring routine -kre*/
void ccomment(void)
{
  int c;

  /* log(L_NOTICE, "got comment"); */
  while (1)
  {
     while ((c = input()) != '*' && c != EOF)
        if (c == '\n') ++linenum;
     if (c == '*')
     {
        while ((c = input()) == '*');
        if (c == '/') break;
     }
    if (c == EOF)
    {
       YY_FATAL_ERROR("EOF in comment");
       /* XXX hack alert this disables
        * the stupid unused function warning
        * gcc generates
        */
       if(1 == 0)
          yy_fatal_error("EOF in comment");
       break;
    }
  }
}
