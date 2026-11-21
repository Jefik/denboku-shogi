#ifndef ONLINE_H
#define ONLINE_H

/* Playing on servers (src/core calls these): lishogi through its Board API (lishogi.c), and CSA
 * servers such as Floodgate (csa.c). A fork for another game points them at its own servers. */

#include <gtk/gtk.h>

#include "online_setup.h"

#define ONLINE_URL "https://lishogi.org"
#define ONLINE_TOKEN_URL ONLINE_URL "/account/oauth/token/create?scopes[]=board:play&description=Denboku+Sh%C5%8Dgi"
/* Labels (English, the keys of the translations). */
#define ONLINE_LABEL "Lishogi"
#define ONLINE_OPEN_LABEL "Open from Lishogi…"
#define ONLINE_TOKEN_LABEL "Lishogi token"
#define SERVER_LABEL "CSA"

GtkWidget *token_field(GtkWidget **entry);
char *online_token(void);
void online_stop(void);
void online_post(const char *path, gboolean resync);
/* Waits (a few seconds at most) for the posts still on their way, before the program ends. */
void online_flush(void);
void online_chat(const char *text);
/* Whether a chat line's author is us; resigns (or aborts) the online game in play, if any. */
bool online_is_me(const char *who);
/* The game on the site is against its AI (no chat greetings then). */
bool online_vs_ai(void);
void online_resign(void);
void online_send_move(const Move *m);
gboolean online_events_cb(gpointer data);
/* `user`: the one challenged (ONLINE_USER) or the invitation link (ONLINE_ACCEPT). */
void online_game(const OnlineSetup *s, const char *user);
/* Asks for a game's link and opens its record. */
void online_open_game(void);

void server_stop(void);
void server_send_move(const Move *m);
void server_resign(void);
void server_connect(const char *server, const char *name, const char *password);

#endif
