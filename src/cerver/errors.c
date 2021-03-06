#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/errors.h"
#include "cerver/packets.h"

#include "cerver/threads/thread.h"

u8 cerver_error_event_unregister (Cerver *cerver, const CerverErrorType error_type);

#pragma region types

// get the description for the current error type
const char *cerver_error_type_description (CerverErrorType type) {

	switch (type) {
		#define XX(num, name, description) case CERVER_ERROR_##name: return #description;
		CERVER_ERROR_MAP(XX)
		#undef XX
	}

	return cerver_error_type_description (CERVER_ERROR_UNKNOWN);

}

#pragma endregion

#pragma region data

static CerverErrorEventData *cerver_error_event_data_new (void) {

	CerverErrorEventData *error_event_data = (CerverErrorEventData *) malloc (sizeof (CerverErrorEventData));
	if (error_event_data) {
		error_event_data->cerver = NULL;

		error_event_data->client = NULL;
		error_event_data->connection = NULL;

		error_event_data->action_args = NULL;

		error_event_data->error_message = NULL;
	}

	return error_event_data;

}

void cerver_error_event_data_delete (CerverErrorEventData *error_event_data) {

	if (error_event_data) {
		str_delete (error_event_data->error_message);

		free (error_event_data);
	}

}

static CerverErrorEventData *cerver_error_event_data_create (
	const Cerver *cerver,
	const Client *client, const Connection *connection,
	void *action_args,
	const char *error_message
) {

	CerverErrorEventData *error_event_data = cerver_error_event_data_new ();
	if (error_event_data) {
		error_event_data->cerver = cerver;

		error_event_data->client = client;
		error_event_data->connection = connection;

		error_event_data->action_args = action_args;

		error_event_data->error_message = error_message ? str_new (error_message) : NULL;
	}

	return error_event_data;

}

#pragma endregion

#pragma region event

static CerverErrorEvent *cerver_error_event_new (void) {

	CerverErrorEvent *cerver_error_event = (CerverErrorEvent *) malloc (sizeof (CerverErrorEvent));
	if (cerver_error_event) {
		cerver_error_event->type = CERVER_ERROR_NONE;

		cerver_error_event->create_thread = false;
		cerver_error_event->drop_after_trigger = false;

		cerver_error_event->action = NULL;
		cerver_error_event->action_args = NULL;
		cerver_error_event->delete_action_args = NULL;
	}

	return cerver_error_event;

}

void cerver_error_event_delete (void *event_ptr) {

	if (event_ptr) {
		CerverErrorEvent *event = (CerverErrorEvent *) event_ptr;

		if (event->action_args) {
			if (event->delete_action_args)
				event->delete_action_args (event->action_args);
		}

		free (event);
	}

}

// registers an action to be triggered when the specified error event occurs
// if there is an existing action registered to an error event, it will be overrided
// a newly allocated CerverErrorEventData structure will be passed to your method
// that should be free using the cerver_error_event_data_delete () method
// returns 0 on success, 1 on error
u8 cerver_error_event_register (
	Cerver *cerver,
	const CerverErrorType error_type,
	Action action, void *action_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
) {

	u8 retval = 1;

	if (cerver) {
		CerverErrorEvent *error = cerver_error_event_new ();
		if (error) {
			error->type = error_type;

			error->create_thread = create_thread;
			error->drop_after_trigger = drop_after_trigger;

			error->action = action;
			error->action_args = action_args;
			error->delete_action_args = delete_action_args;

			// search if there is an action already registred for that error and remove it
			(void) cerver_error_event_unregister (cerver, error_type);

			cerver->errors[error_type] = error;

			retval = 0;
		}
	}

	return retval;

}

// unregister the action associated with an error event
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error or if error is NOT registered
u8 cerver_error_event_unregister (Cerver *cerver, const CerverErrorType error_type) {

	u8 retval = 1;

	if (cerver) {
		if (cerver->errors[error_type]) {
			cerver_error_event_delete (cerver->errors[error_type]);
			cerver->errors[error_type] = NULL;

			retval = 0;
		}
	}

	return retval;

}

// triggers all the actions that are registred to an error
void cerver_error_event_trigger (
	const CerverErrorType error_type,
	const Cerver *cerver,
	const Client *client, const Connection *connection,
	const char *error_message
) {

	if (cerver) {
		CerverErrorEvent *error = cerver->errors[error_type];
		if (error) {
			// trigger the action
			if (error->action) {
				if (error->create_thread) {
					pthread_t thread_id = 0;
					thread_create_detachable (
						&thread_id,
						(void *(*)(void *)) error->action,
						cerver_error_event_data_create (
							cerver,
							client, connection,
							error->action_args,
							error_message
						)
					);
				}

				else {
					error->action (cerver_error_event_data_create (
						cerver,
						client, connection,
						error->action_args,
						error_message
					));
				}

				if (error->drop_after_trigger) {
					(void) cerver_error_event_unregister ((Cerver *) cerver, error_type);
				}
			}
		}
	}

}

#pragma endregion

#pragma region handler

// TODO: differentiate local errors from the ones coming from the clients
// handles error packets
void cerver_error_packet_handler (Packet *packet) {

	if (packet->data_size >= sizeof (SError)) {
		char *end = (char *) packet->data;
		SError *s_error = (SError *) end;

		switch (s_error->error_type) {
			case CERVER_ERROR_NONE: break;

			case CERVER_ERROR_PACKET_ERROR:
				cerver_error_event_trigger (
					CERVER_ERROR_PACKET_ERROR,
					packet->cerver, packet->client, packet->connection,
					s_error->msg
				);
				break;

			case CERVER_ERROR_GET_FILE:
				cerver_error_event_trigger (
					CERVER_ERROR_GET_FILE,
					packet->cerver, packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CERVER_ERROR_SEND_FILE:
				cerver_error_event_trigger (
					CERVER_ERROR_SEND_FILE,
					packet->cerver, packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CERVER_ERROR_FILE_NOT_FOUND:
				cerver_error_event_trigger (
					CERVER_ERROR_FILE_NOT_FOUND,
					packet->cerver, packet->client, packet->connection,
					s_error->msg
				);
				break;

			default: {
				cerver_error_event_trigger (
					CERVER_ERROR_UNKNOWN,
					packet->cerver, packet->client, packet->connection,
					s_error->msg
				);
			} break;
		}
	}

}

#pragma endregion

#pragma region packets

// creates an error packet ready to be sent
Packet *error_packet_generate (const CerverErrorType type, const char *msg) {

	Packet *packet = packet_new ();
	if (packet) {
		size_t packet_len = sizeof (PacketHeader) + sizeof (SError);

		packet->packet = malloc (packet_len);
		packet->packet_size = packet_len;

		char *end = (char *) packet->packet;
		PacketHeader *header = (PacketHeader *) end;
		header->packet_type = PACKET_TYPE_ERROR;
		header->packet_size = packet_len;

		header->request_type = REQUEST_PACKET_TYPE_NONE;

		end += sizeof (PacketHeader);

		SError *s_error = (SError *) end;
		s_error->error_type = type;
		s_error->timestamp = time (NULL);
		memset (s_error->msg, 0, ERROR_MESSAGE_LENGTH);
		if (msg) strncpy (s_error->msg, msg, ERROR_MESSAGE_LENGTH);
	}

	return packet;

}

// creates and send a new error packet
// returns 0 on success, 1 on error
u8 error_packet_generate_and_send (
	const CerverErrorType type, const char *msg,
	Cerver *cerver, Client *client, Connection *connection
) {

	u8 retval = 1;

	Packet *error_packet = error_packet_generate (type, msg);
	if (error_packet) {
		packet_set_network_values (error_packet, cerver, client, connection, NULL);
		retval = packet_send (error_packet, 0, NULL, false);
		packet_delete (error_packet);
	}

	return retval;

}

#pragma endregion