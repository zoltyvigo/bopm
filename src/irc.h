<<<<<<< HEAD
#ifndef IRC_H
#define IRC_H

#include "inet.h"

#define NICKMAX 32 + 1
#define MSGLENMAX 512 + 1


struct bopm_sockaddr
{
   struct sockaddr_in sa4;
};


struct bopm_ircaddr
{
   struct in_addr in4;
};


struct UserInfo
{
   char *irc_nick;
   char *irc_username;
   char *irc_hostname;
};

typedef void (*irc_command) (char **, unsigned int, char *, struct UserInfo *);

struct CommandHash
{
   char       *command;
   irc_command handler;
};


extern void irc_send(char *, ...);
extern void irc_send_channels(char *, ...);
extern void irc_cycle(void);
extern void irc_timer(void);


#define copy_s_addr(a, b)  \
do { \
((uint32_t *)a)[0] = ((uint32_t *)b)[0]; \
((uint32_t *)a)[1] = ((uint32_t *)b)[1]; \
((uint32_t *)a)[2] = ((uint32_t *)b)[2]; \
((uint32_t *)a)[3] = ((uint32_t *)b)[3]; \
} while(0)

=======
/*
 *  Copyright (c) 2002-2003 Erik Fears
 *  Copyright (c) 2014-2017 ircd-hybrid development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 *  USA
 */

#ifndef IRC_H
#define IRC_H

enum { MSGLENMAX = 513 };  /* 510 bytes message length + \r\n\0 */


extern void irc_send(const char *, ...);
extern void irc_send_channels(const char *, ...);
extern void irc_cycle(void);
extern void irc_timer(void);
>>>>>>> remotes/new/master
#endif
