/* $Id$ */
#if !defined(__CONNECTION_H__)
#define __CONNECTION_H__


#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _sc_aggregator_connection *sc_aggregator_connection_ref;

sc_aggregator_connection_ref
sc_aggregator_connection_new(const char* host, int port);

int sc_aggregator_connection_open(sc_aggregator_connection_ref conn);

int sc_aggregator_connection_is_opened(sc_aggregator_connection_ref conn);

int sc_aggregator_connection_close(sc_aggregator_connection_ref conn);

int sc_aggregator_connection_send_message(sc_aggregator_connection_ref conn, sc_log_message* msg);
int sc_aggregator_connection_receive_message(sc_aggregator_connection_ref conn, sc_log_message** pmsg);

void sc_aggregator_connection_destroy(sc_aggregator_connection_ref conn);

#if defined(__cplusplus)
}
#endif


#endif
