/*
Copyright (C) 2002  Erik Fears

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software

      Foundation, Inc.
      59 Temple Place - Suite 330
      Boston, MA  02111-1307, USA.

*/

#include "setup.h"

#ifdef STDC_HEADERS
#include <string.h>
#include <stdlib.h>
#endif

#include <stdio.h>
#include <time.h>

#include "config.h"
#include "log.h"
#include "misc.h"
#include "extern.h"
#include "match.h"

/* Global Configuration Variables */

char *CONF_SERVER          = 0;
char *CONF_PASSWORD        = 0;
char *CONF_USER            = 0;
char *CONF_NICK            = 0;
char *CONF_OPER            = 0;
char *CONF_OPER_MODES      = 0;
char *CONF_SCANIP          = 0;
char *CONF_BINDIRC         = 0;
char *CONF_BINDSCAN        = 0;
char *CONF_CHANNELS        = 0;
char *CONF_NICKSERV_IDENT  = 0;
char *CONF_CHANSERV_INVITE = 0;
char *CONF_KLINE_COMMAND   = 0;
char *CONF_DNSBL_ZONE      = 0;
char *CONF_DNSBL_FROM      = 0;
char *CONF_DNSBL_TO        = 0;
char *CONF_SENDMAIL        = 0;
char *CONF_HELP_EMAIL      = 0;
char *CONF_AWAY            = 0;
char *CONF_TARGET_STRING   = 0;
string_list *CONF_EXCLUDE  = 0;

int  CONF_SCANPORT         = 0;
int  CONF_PORT             = 0;
int  CONF_FDLIMIT          = 0;

/* Configuration Hash , Hashes Config Params to their Function Handlers*/
/*      NAME                  , TYPE   , REQ, REQMET, PTR TO VAR        */
config_hash hash[] = {
       {"SERVER",              TYPE_STRING, 1,0,    &CONF_SERVER             },
       {"PORT",                TYPE_INT   , 1,0,    &CONF_PORT               },
       {"PASSWORD",            TYPE_STRING, 0,0,    &CONF_PASSWORD           },
       {"USER",                TYPE_STRING, 1,0,    &CONF_USER               },
       {"NICK",                TYPE_STRING, 1,0,    &CONF_NICK               },
       {"OPER",                TYPE_STRING, 1,0,    &CONF_OPER               },
       {"OPER_MODES",          TYPE_STRING, 1,0,    &CONF_OPER_MODES         },
       {"SCANIP",              TYPE_STRING, 1,0,    &CONF_SCANIP             },
       {"SCANPORT",            TYPE_INT   , 1,0,    &CONF_SCANPORT           },
       {"BINDIRC",             TYPE_STRING, 0,0,    &CONF_BINDIRC            },
       {"BINDSCAN",            TYPE_STRING, 0,0,    &CONF_BINDSCAN           },
       {"FDLIMIT",             TYPE_INT   , 1,0,    &CONF_FDLIMIT            },
       {"CHANNELS",            TYPE_STRING, 1,0,    &CONF_CHANNELS           },
       {"NICKSERV_IDENT",      TYPE_STRING, 0,0,    &CONF_NICKSERV_IDENT     },
       {"CHANSERV_INVITE",     TYPE_STRING, 0,0,    &CONF_CHANSERV_INVITE    },
       {"KLINE_COMMAND",       TYPE_STRING, 1,0,    &CONF_KLINE_COMMAND      },
       {"DNSBL_ZONE",          TYPE_STRING, 0,0,    &CONF_DNSBL_ZONE         },
       {"DNSBL_FROM",          TYPE_STRING, 0,0,    &CONF_DNSBL_FROM         },
       {"DNSBL_TO",            TYPE_STRING, 0,0,    &CONF_DNSBL_TO           },
       {"SENDMAIL",            TYPE_STRING, 0,0,    &CONF_SENDMAIL           },
       {"HELP_EMAIL",          TYPE_STRING, 1,0,    &CONF_HELP_EMAIL         },
       {"AWAY",                TYPE_STRING, 1,0,    &CONF_AWAY               },
       {"TARGET_STRING",       TYPE_STRING, 1,0,    &CONF_TARGET_STRING      },
       {"EXCLUDE",             TYPE_LIST,   0,0,    &CONF_EXCLUDE            },
       {0,                     0,           0,0,    0                        },
};



/* Parse File */

void config_load(char *filename)
{
    FILE *in;

    char line[1024];  /* 1k buffer for reading the file */

    char *key;
    char *args;

    string_list *list, *oldlist, *nextlist;

    int i;

    if(!(in = fopen(filename, "r")))
     {
	log("CONFIG -> No config file found, aborting.");
	exit(1);
     }
    
    /* Clear anything we have already */
    for(i = 0; i < (sizeof(hash) / sizeof(config_hash) - 1); i++)
      {
         switch(hash[i].type)
          { 
              case TYPE_STRING:
                  if(( *(char**) hash[i].var))
                       free(*(char**)hash[i].var);
                  *(char**)hash[i].var = 0;
                  break;
              case TYPE_INT:
                  *(int *) hash[i].var = 0;
		  break;
              case TYPE_LIST:
                  for(list = * (string_list **) hash[i].var;list;)
                   {
                       nextlist = list->next;
                       free(list->text);
                       free(list);
                       list = nextlist;
                   }
                   list = 0;
                   *(string_list **) hash[i].var = 0;
 
                   break;
          }
         hash[i].reqmet = 0;
      }

    while(fgets(line,1023, in))  
      {

	    if(line[0] == '#')
		 continue;
		    
            key = strtok(line, " ");
            args = strtok(NULL, "\n");

            if(!args)
               continue;


            args = clean(args); /* Strip leading and tailing spaces */

            for(i = 0; i < (sizeof(hash) / sizeof(config_hash)) - 1; i++)
              if(!strcasecmp(key, hash[i].key))
                {
                      switch(hash[i].type)
                        {
                            case TYPE_STRING: 
                                 *(char**) hash[i].var = strdup(args);
                                 break;
                            case TYPE_INT:
                                 *(int *) hash[i].var = atoi(args);
                                 break;
			    case TYPE_LIST:
				 list = malloc(sizeof(string_list));
		                 if(!list)
				    config_memfail();
				 list->next = NULL;
				 list->text = strdup(args);
				 if(!list->text)
				    config_memfail();
				 collapse(list->text);
				 oldlist = * (string_list **) (hash[i].var);
				 if(oldlist)
				  {
				    while(oldlist)
				     {
				       if(strcasecmp(args, oldlist->text))
					{
					  if(oldlist->next)
					     oldlist = oldlist->next;
					  else
					   {
					     oldlist->next = list;
					     oldlist = NULL;
					   }
					}
				       else
					{
					  free(list);
					  oldlist = NULL;
					}
				     }
				  }
				 else
				  {
				    * (string_list **) (hash[i].var) = list;
				  }
				break;
                        }
                       hash[i].reqmet = 1;
                }
      }

  fclose(in);
  config_checkreq(); /* Check required parameters */
}

void config_checkreq()
{
      int i;
      int errfnd = 0;
      string_list *list;

      for(i = 0; i < (sizeof(hash) / sizeof(config_hash) - 1); i++)
        if(hash[i].req && !hash[i].reqmet)
         {               
            log("CONFIG -> Parameter [%s] required but not defined in config.", hash[i].key);
            errfnd++;
         }
        else if(OPT_DEBUG >= 3 && hash[i].reqmet)
         {
            switch(hash[i].type)
             {
                 case TYPE_STRING:
                      log("CONFIG -> Set [%s]: %s", hash[i].key, *(char**) hash[i].var);
                      break;
                 case TYPE_INT:
                      log("CONFIG -> Set [%s]: %d", hash[i].key, *(int *) hash[i].var);
                      break;
                 case TYPE_LIST:
                      for(list = *(string_list**) (hash[i].var); list; list = list->next)
                         log("CONFIG-> Set [%s]: %s", hash[i].key, list->text);
                      break;
             }
         }


      if(errfnd)
       {
          log("CONFIG -> %d parameters missing from config file, aborting.", errfnd);
          exit(1);
       }
}

/*  Called when memory allocation somewhere returns
 *  an error
 */

void config_memfail()
{
     log("CONFIG -> Error allocating memory.");
     exit(1);
}

