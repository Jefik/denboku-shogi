#include "upnp.h"

#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

/* SSDP finds the gateway, its description gives the control URL of the WAN connection service,
 * and SOAP calls map the port and read the public address. All asynchronous, in the main loop
 * (the Kindle's GLib predates the thread API the headers of the sysroot describe). */

typedef void (*HttpDone)(char *body, const char *local_ip, gpointer user);

typedef struct {
    char *hostport, *request, *local_ip;
    GString *response;
    GSocketConnection *conn;
    HttpDone done;
    gpointer user;
    char buf[4096];
} Request;

typedef struct {
    int port;
    char *description, *location, *control, *service, *local_ip;
    UpnpDone done;
    void *data;
    GSocket *ssdp;
    GSource *ssdp_source;
    guint ssdp_timeout;
    unsigned generation; /* upnp_close() since it started: the room it was for is gone */
} Job;

static unsigned generation;

static char *mapped_control, *mapped_service;
static int mapped_port;

static char *between(const char *s, const char *open, const char *close)
{
    const char *a = s ? strstr(s, open) : NULL;
    if (!a)
        return NULL;
    a += strlen(open);
    const char *b = strstr(a, close);
    return b ? g_strndup(a, b - a) : NULL;
}

/* A case-insensitive header value from an HTTP/SSDP response. */
static char *header(const char *response, const char *name)
{
    char **lines = g_strsplit(response, "\n", -1);
    char *value = NULL;
    for (int i = 0; lines[i] && !value; i++)
        if (!g_ascii_strncasecmp(lines[i], name, strlen(name)) && lines[i][strlen(name)] == ':')
            value = g_strstrip(g_strdup(lines[i] + strlen(name) + 1));
    g_strfreev(lines);
    return value;
}

/* ---- plain HTTP over GIO ---- */

static void request_finish(Request *r)
{
    char *body = NULL, *start = strstr(r->response->str, "\r\n\r\n");
    const char *status = strstr(r->response->str, " 200 ");
    /* Only a 200 counts; the body follows the blank line. */
    if (g_str_has_prefix(r->response->str, "HTTP/1.") && status && start && status < start)
        body = g_strdup(start + 4);
    r->done(body, r->local_ip, r->user);
    if (r->conn)
        g_object_unref(r->conn);
    g_string_free(r->response, TRUE);
    g_free(r->hostport);
    g_free(r->request);
    g_free(r->local_ip);
    g_free(r);
}

static void read_cb(GObject *src, GAsyncResult *res, gpointer user)
{
    Request *r = user;
    gssize n = g_input_stream_read_finish(G_INPUT_STREAM(src), res, NULL);
    if (n > 0 && r->response->len < 256 * 1024) {
        g_string_append_len(r->response, r->buf, n);
        g_input_stream_read_async(G_INPUT_STREAM(src), r->buf, sizeof r->buf, G_PRIORITY_DEFAULT, NULL, read_cb, r);
    } else {
        request_finish(r);
    }
}

static void connect_cb(GObject *src, GAsyncResult *res, gpointer user)
{
    Request *r = user;
    r->conn = g_socket_client_connect_to_host_finish(G_SOCKET_CLIENT(src), res, NULL);
    g_object_unref(src);
    if (!r->conn) {
        request_finish(r);
        return;
    }
    GSocketAddress *a = g_socket_connection_get_local_address(r->conn, NULL);
    if (a) {
        r->local_ip = g_inet_address_to_string(g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(a)));
        g_object_unref(a);
    }
    /* A request of a few hundred bytes goes out at once on a fresh connection. */
    GOutputStream *out = g_io_stream_get_output_stream(G_IO_STREAM(r->conn));
    if (!g_output_stream_write_all(out, r->request, strlen(r->request), NULL, NULL, NULL)) {
        request_finish(r);
        return;
    }
    g_input_stream_read_async(g_io_stream_get_input_stream(G_IO_STREAM(r->conn)), r->buf, sizeof r->buf,
                              G_PRIORITY_DEFAULT, NULL, read_cb, r);
}

/* GET `url`, or POST a SOAP `body` with `action`. */
static void http(const char *url, const char *action, const char *body, HttpDone done, gpointer user)
{
    const char *rest = g_str_has_prefix(url, "http://") ? url + 7 : NULL;
    if (!rest) {
        done(NULL, NULL, user);
        return;
    }
    const char *slash = strchr(rest, '/');
    Request *r = g_new0(Request, 1);
    r->hostport = slash ? g_strndup(rest, slash - rest) : g_strdup(rest);
    const char *path = slash ? slash : "/";
    r->response = g_string_new(NULL);
    r->done = done;
    r->user = user;
    r->request = body ? g_strdup_printf("POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
                                        "SOAPAction: \"%s\"\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s",
                                        path, r->hostport, action, (unsigned) strlen(body), body)
                      : g_strdup_printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, r->hostport);
    GSocketClient *client = g_socket_client_new();
    g_socket_client_set_timeout(client, 4);
    g_socket_client_connect_to_host_async(client, r->hostport, 80, NULL, connect_cb, r);
}

static void soap(const char *control, const char *service, const char *action, const char *args, HttpDone done,
                 gpointer user)
{
    char *body = g_strdup_printf("<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/"
                                 "envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
                                 "<u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n",
                                 action, service, args, action);
    char *soap_action = g_strdup_printf("%s#%s", service, action);
    http(control, soap_action, body, done, user);
    g_free(soap_action);
    g_free(body);
}

/* ---- the steps ---- */

static void finish(Job *j, const char *ip)
{
    if (j->generation == generation)
        j->done(ip, j->data);
    g_free(j->description);
    g_free(j->location);
    g_free(j->control);
    g_free(j->service);
    g_free(j->local_ip);
    g_free(j);
}

static void got_ip(char *body, const char *local, gpointer user)
{
    (void) local;
    Job *j = user;
    char *ip = between(body, "<NewExternalIPAddress>", "</NewExternalIPAddress>");
    g_free(body);
    finish(j, ip);
    g_free(ip);
}

static void delete_mapping(const char *control, const char *service, int port);

static void mapped(char *body, const char *local, gpointer user)
{
    (void) local;
    Job *j = user;
    if (!body) {
        finish(j, NULL);
        return;
    }
    g_free(body);
    if (j->generation != generation) { /* the room closed meanwhile: the port is not left open */
        delete_mapping(j->control, j->service, j->port);
        finish(j, NULL);
        return;
    }
    g_free(mapped_control);
    g_free(mapped_service);
    mapped_control = g_strdup(j->control), mapped_service = g_strdup(j->service), mapped_port = j->port;
    soap(j->control, j->service, "GetExternalIPAddress", "", got_ip, j);
}

/* The control URL and type of the WANIPConnection (or WANPPPConnection) service. */
static gboolean find_service(Job *j, const char *desc)
{
    static const char *types[] = {"urn:schemas-upnp-org:service:WANIPConnection:2",
                                  "urn:schemas-upnp-org:service:WANIPConnection:1",
                                  "urn:schemas-upnp-org:service:WANPPPConnection:1"};
    for (size_t i = 0; i < G_N_ELEMENTS(types); i++) {
        char *tag = g_strdup_printf("<serviceType>%s</serviceType>", types[i]);
        const char *at = strstr(desc, tag);
        g_free(tag);
        char *url = at ? between(at, "<controlURL>", "</controlURL>") : NULL;
        if (!url)
            continue;
        if (g_str_has_prefix(url, "http://")) {
            j->control = url;
        } else {
            /* Relative to URLBase, or to the description's host. */
            char *base = between(desc, "<URLBase>", "</URLBase>");
            if (!base) {
                const char *p = strchr(j->location + 7, '/');
                base = p ? g_strndup(j->location, p - j->location) : g_strdup(j->location);
            }
            g_strstrip(base);
            if (g_str_has_suffix(base, "/"))
                base[strlen(base) - 1] = 0;
            j->control = g_strconcat(base, url[0] == '/' ? "" : "/", url, NULL);
            g_free(base);
            g_free(url);
        }
        j->service = g_strdup(types[i]);
        return TRUE;
    }
    return FALSE;
}

static void described(char *body, const char *local, gpointer user)
{
    Job *j = user;
    if (!body || !local || !find_service(j, body)) {
        g_free(body);
        finish(j, NULL);
        return;
    }
    g_free(body);
    j->local_ip = g_strdup(local);
    char *args = g_strdup_printf("<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort>"
                                 "<NewProtocol>TCP</NewProtocol><NewInternalPort>%d</NewInternalPort>"
                                 "<NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
                                 "<NewPortMappingDescription>%s</NewPortMappingDescription>"
                                 "<NewLeaseDuration>7200</NewLeaseDuration>",
                                 j->port, j->port, j->local_ip, j->description);
    soap(j->control, j->service, "AddPortMapping", args, mapped, j);
    g_free(args);
}

static void ssdp_stop(Job *j)
{
    if (j->ssdp_source) {
        g_source_destroy(j->ssdp_source);
        g_source_unref(j->ssdp_source);
    }
    if (j->ssdp_timeout)
        g_source_remove(j->ssdp_timeout);
    if (j->ssdp)
        g_object_unref(j->ssdp);
    j->ssdp_source = NULL, j->ssdp_timeout = 0, j->ssdp = NULL;
}

static gboolean ssdp_answer(GSocket *s, GIOCondition cond, gpointer user)
{
    (void) cond;
    Job *j = user;
    char buf[2048];
    gssize n = g_socket_receive(s, buf, sizeof buf - 1, NULL, NULL);
    if (n <= 0)
        return TRUE;
    buf[n] = 0;
    char *location = header(buf, "LOCATION");
    if (!location)
        return TRUE;
    j->location = location;
    ssdp_stop(j);
    http(j->location, NULL, NULL, described, j);
    return FALSE;
}

static gboolean ssdp_gave_up(gpointer user)
{
    Job *j = user;
    j->ssdp_timeout = 0;
    ssdp_stop(j);
    finish(j, NULL);
    return FALSE;
}

void upnp_open(int port, const char *description, UpnpDone done, void *data)
{
    Job *j = g_new0(Job, 1);
    j->generation = generation;
    j->port = port;
    j->description = g_markup_escape_text(description, -1);
    j->done = done;
    j->data = data;
    j->ssdp = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, NULL);
    if (!j->ssdp) {
        finish(j, NULL);
        return;
    }
    g_socket_set_blocking(j->ssdp, FALSE);
    GInetAddress *group = g_inet_address_new_from_string("239.255.255.250");
    GSocketAddress *to = g_inet_socket_address_new(group, 1900);
    const char *search = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n"
                         "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";
    for (int i = 0; i < 2; i++)
        g_socket_send_to(j->ssdp, to, search, strlen(search), NULL, NULL);
    g_object_unref(to);
    g_object_unref(group);
    j->ssdp_source = g_socket_create_source(j->ssdp, G_IO_IN, NULL);
    g_source_set_callback(j->ssdp_source, (GSourceFunc) (void (*)(void)) ssdp_answer, j, NULL);
    g_source_attach(j->ssdp_source, NULL);
    j->ssdp_timeout = g_timeout_add_seconds(4, ssdp_gave_up, j);
}

static void deleted(char *body, const char *local, gpointer user)
{
    (void) local, (void) user;
    g_free(body);
}

static void delete_mapping(const char *control, const char *service, int port)
{
    char *args = g_strdup_printf("<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort>"
                                 "<NewProtocol>TCP</NewProtocol>",
                                 port);
    soap(control, service, "DeletePortMapping", args, deleted, NULL);
    g_free(args);
}

void upnp_close(void)
{
    generation++;
    if (!mapped_control)
        return;
    delete_mapping(mapped_control, mapped_service, mapped_port);
    g_free(mapped_control);
    g_free(mapped_service);
    mapped_control = mapped_service = NULL;
}
