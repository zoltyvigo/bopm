#ifndef IRC_H
#define IRC_H
      
      void irc_cycle();
      void irc_init();
      void irc_connect();
      void irc_read();
      void irc_parse();
      void irc_kline();
      void irc_timer();
        
      void do_perform();
#endif
