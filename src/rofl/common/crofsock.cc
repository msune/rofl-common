/*
 * cchannel.cpp
 *
 *  Created on: 31.12.2013
 *      Author: andreas
 */

#include "crofsock.h"

using namespace rofl;

crofsock::crofsock(
		crofsock_env *env) :
				env(env),
				socket(NULL),
				fragment((cmemory*)0),
				msg_bytes_read(0)
{
	for (unsigned int i = 0; i < QUEUE_MAX; i++) {
		outqueues.push_back(rofqueue());
	}
	outqueues[QUEUE_MGMT].set_limit(8);
	outqueues[QUEUE_FLOW].set_limit(4);
	outqueues[QUEUE_PKT ].set_limit(2);
}



crofsock::~crofsock()
{
	if (fragment)
		delete fragment;
	if (socket)
		delete socket;
}


void
crofsock::accept(enum rofl::csocket::socket_type_t socket_type, int sd)
{
	if (socket)
		delete socket;
	socket = csocket::csocket_factory(socket_type, this);
	socket->accept(sd);
}



void
crofsock::connect(
		enum rofl::csocket::socket_type_t socket_type,
		cparams const& socket_params)
{
	if (socket)
		delete socket;
	(socket = csocket::csocket_factory(socket_type, this))->connect(socket_params);
}



void
crofsock::reconnect()
{
	socket->reconnect();
}



void
crofsock::close()
{
	socket->close();
}



void
crofsock::handle_accepted(
		csocket& socket,
		int newsd,
		caddress const& ra)
{
	logging::info << "[rofl][sock] connection accepted:" << std::endl << *this;
	// this should never happen, as passively opened sockets are handled outside of crofsock
}



void
crofsock::handle_connected(
		csocket& socket)
{
	logging::info << "[rofl][sock] connection established:" << std::endl << *this;
	env->handle_connected(this);
}



void
crofsock::handle_connect_refused(
		csocket& socket)
{
	logging::info << "[rofl][sock] connection refused:" << std::endl << *this;
	env->handle_connect_refused(this);
}



void
crofsock::handle_closed(
			csocket& socket)
{
	logging::info << "[rofl][sock] connection closed:" << std::endl << *this;
	if (fragment)
		delete fragment;
	fragment = (cmemory*)0;
	{
		for (std::vector<rofqueue>::iterator it = outqueues.begin(); it != outqueues.end(); ++it) {
			(*it).clear();
		}
	}
	env->handle_closed(this);
}



void
crofsock::handle_read(
		csocket& socket)
{
	try {
		if (0 == fragment) {
			fragment = new cmemory(sizeof(struct openflow::ofp_header));
			msg_bytes_read = 0;
		}

		while (true) {

			uint16_t msg_len = 0;

			// how many bytes do we have to read?
			if (msg_bytes_read < sizeof(struct openflow::ofp_header)) {
				msg_len = sizeof(struct openflow::ofp_header);
			} else {
				struct openflow::ofp_header *header = (struct openflow::ofp_header*)fragment->somem();
				msg_len = be16toh(header->length);
			}

			// sanity check: 8 <= msg_len <= 2^16
			if (msg_len < sizeof(struct openflow::ofp_header)) {
				logging::warn << "[rofl][sock] received message with invalid length field, closing socket." << std::endl;
				socket.close();
				return;
			}

			// resize msg buffer, if necessary
			if (fragment->memlen() < msg_len) {
				fragment->resize(msg_len);
			}

			// read from socket more bytes, at most "msg_len - msg_bytes_read"
			int rc = socket.recv((void*)(fragment->somem() + msg_bytes_read), msg_len - msg_bytes_read);

			msg_bytes_read += rc;

			// minimum message length received, check completeness of message
			if (fragment->memlen() >= sizeof(struct openflow::ofp_header)) {
				struct openflow::ofp_header *header = (struct openflow::ofp_header*)fragment->somem();
				uint16_t msg_len = be16toh(header->length);

				// ok, message was received completely
				if (msg_len == msg_bytes_read) {
					cmemory *mem = fragment;
					fragment = (cmemory*)0; // just in case, we get an exception from parse_message()
					msg_bytes_read = 0;
					parse_message(mem);
					return;
				}
			}
		}

	} catch (eSocketAgain& e) {

		// more bytes are needed, keep pointer to msg in "fragment"

	} catch (eSysCall& e) {

		logging::warn << "[rofl][sock] failed to read from socket: " << e << std::endl;

	} catch (RoflException& e) {

		logging::warn << "[rofl][sock] dropping invalid message: " << e << std::endl;

		if (fragment) {
			delete fragment; fragment = (cmemory*)0;
		}
	}

}



void
crofsock::handle_write(
		csocket& socket)
{

}



rofl::csocket&
crofsock::get_socket()
{
	return *socket;
}



void
crofsock::send_message(
		rofl::openflow::cofmsg *msg)
{
	if (not socket->is_connected()) {
		delete msg; return;
	}

	log_message(std::string("queueing message for sending:"), *msg);

	switch (msg->get_version()) {
	case rofl::openflow10::OFP_VERSION: {
		switch (msg->get_type()) {
		case rofl::openflow10::OFPT_PACKET_IN:
		case rofl::openflow10::OFPT_PACKET_OUT: {
			outqueues[QUEUE_PKT].store(msg);
		} break;
		case rofl::openflow10::OFPT_FLOW_MOD:
		case rofl::openflow10::OFPT_FLOW_REMOVED: {
			outqueues[QUEUE_FLOW].store(msg);
		} break;
		default: {
			outqueues[QUEUE_MGMT].store(msg);
		};
		}
	} break;
	case rofl::openflow12::OFP_VERSION: {
		switch (msg->get_type()) {
		case rofl::openflow12::OFPT_PACKET_IN:
		case rofl::openflow12::OFPT_PACKET_OUT: {
			outqueues[QUEUE_PKT].store(msg);
		} break;
		case rofl::openflow12::OFPT_FLOW_MOD:
		case rofl::openflow12::OFPT_FLOW_REMOVED: {
			outqueues[QUEUE_FLOW].store(msg);
		} break;
		default: {
			outqueues[QUEUE_MGMT].store(msg);
		};
		}
	} break;
	case rofl::openflow13::OFP_VERSION: {
		switch (msg->get_type()) {
		case rofl::openflow13::OFPT_PACKET_IN:
		case rofl::openflow13::OFPT_PACKET_OUT: {
			outqueues[QUEUE_PKT].store(msg);
		} break;
		case rofl::openflow13::OFPT_FLOW_MOD:
		case rofl::openflow13::OFPT_FLOW_REMOVED: {
			outqueues[QUEUE_FLOW].store(msg);
		} break;
		default: {
			outqueues[QUEUE_MGMT].store(msg);
		};
		}
	} break;
	default: {
		logging::alert << "[rofl][sock] dropping message with unsupported OpenFlow version" << std::endl;
		delete msg; return;
	};
	}

	notify(CROFSOCK_EVENT_WAKEUP);
}



void
crofsock::send_from_queue()
{
	bool reschedule = false;

	for (unsigned int queue_id = 0; queue_id < QUEUE_MAX; ++queue_id) {

		for (unsigned int num = 0; num < outqueues[queue_id].get_limit(); ++num) {
			rofl::openflow::cofmsg *msg = outqueues[queue_id].retrieve();
			if (NULL == msg)
				break;
			cmemory *mem = new cmemory(msg->length());

			msg->pack(mem->somem(), mem->memlen());
			delete msg;
			socket->send(mem);
		}

		if (not outqueues[queue_id].empty()) {
			reschedule = true;
		}
	}

	if (reschedule) {
		notify(CROFSOCK_EVENT_WAKEUP);
	}
}



void
crofsock::handle_event(
		cevent const &ev)
{
	switch (ev.cmd) {
	case CROFSOCK_EVENT_WAKEUP: {
		send_from_queue();
	} break;
	default:
		logging::error << "[rofl][sock] unknown event type:" << (int)ev.cmd << std::endl;
	}
}



void
crofsock::parse_message(
		cmemory *mem)
{
	rofl::openflow::cofmsg *msg = (rofl::openflow::cofmsg*)0;
	try {
		assert(NULL != mem);

		struct openflow::ofp_header* header = (struct openflow::ofp_header*)mem->somem();

		switch (header->version) {
		case rofl::openflow10::OFP_VERSION: parse_of10_message(mem, &msg); break;
		case rofl::openflow12::OFP_VERSION: parse_of12_message(mem, &msg); break;
		case rofl::openflow13::OFP_VERSION: parse_of13_message(mem, &msg); break;
		default: msg = new rofl::openflow::cofmsg(mem); break;
		}

		log_message(std::string("received message:"), *msg);

		env->recv_message(this, msg);

	} catch (eBadRequestBadType& e) {

		if (msg) {
			logging::error << "[rofl][sock] eBadRequestBadType: " << std::endl << *msg;
			size_t len = (msg->framelen() > 64) ? 64 : msg->framelen();
			rofl::openflow::cofmsg_error_bad_request_bad_type *error =
					new rofl::openflow::cofmsg_error_bad_request_bad_type(
							msg->get_version(),
							msg->get_xid(),
							msg->soframe(),
							len);
			send_message(error);
			delete msg;
		} else {
			logging::error << "[rofl][sock] eBadRequestBadType " << std::endl;
		}

	} catch (RoflException& e) {

		if (msg) {
			logging::error << "[rofl][sock] RoflException: " << std::endl << *msg;
			delete msg;
		} else {
			logging::error << "[rofl][sock] RoflException " << std::endl;
		}

	}
}



void
crofsock::parse_of10_message(cmemory *mem, rofl::openflow::cofmsg **pmsg)
{
	struct openflow::ofp_header* header = (struct openflow::ofp_header*)mem->somem();

	switch (header->type) {
	case rofl::openflow10::OFPT_HELLO: {
		(*pmsg = new rofl::openflow::cofmsg_hello(mem))->validate();
	} break;

	case rofl::openflow10::OFPT_ERROR: {
		(*pmsg = new rofl::openflow::cofmsg_error(mem))->validate();
	} break;

	case rofl::openflow10::OFPT_ECHO_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_echo_request(mem))->validate();
	} break;
	case rofl::openflow10::OFPT_ECHO_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_echo_reply(mem))->validate();
	} break;

	case rofl::openflow10::OFPT_VENDOR: {
		(*pmsg = new rofl::openflow::cofmsg_experimenter(mem))->validate();
	} break;

	case rofl::openflow10::OFPT_FEATURES_REQUEST:	{
		(*pmsg = new rofl::openflow::cofmsg_features_request(mem))->validate();
	} break;
	case rofl::openflow10::OFPT_FEATURES_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_features_reply(mem))->validate();
	} break;

	case rofl::openflow10::OFPT_GET_CONFIG_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_get_config_request(mem))->validate();
	} break;
	case rofl::openflow10::OFPT_GET_CONFIG_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_get_config_reply(mem))->validate();
	} break;
	case rofl::openflow10::OFPT_SET_CONFIG: {
		(*pmsg = new rofl::openflow::cofmsg_set_config(mem))->validate();
	} break;

	case rofl::openflow10::OFPT_PACKET_OUT: {
		(*pmsg = new rofl::openflow::cofmsg_packet_out(mem))->validate();
	} break;
	case rofl::openflow10::OFPT_PACKET_IN: {
		(*pmsg = new rofl::openflow::cofmsg_packet_in(mem))->validate();
	} break;

	case rofl::openflow10::OFPT_FLOW_MOD: {
		(*pmsg = new rofl::openflow::cofmsg_flow_mod(mem))->validate();
	} break;
	case rofl::openflow10::OFPT_FLOW_REMOVED: {
		(*pmsg = new rofl::openflow::cofmsg_flow_removed(mem))->validate();
	} break;

	case rofl::openflow10::OFPT_PORT_MOD: {
		(*pmsg = new rofl::openflow::cofmsg_port_mod(mem))->validate();
	} break;
	case rofl::openflow10::OFPT_PORT_STATUS: {
		(*pmsg = new rofl::openflow::cofmsg_port_status(mem))->validate();
	} break;

	case rofl::openflow10::OFPT_STATS_REQUEST: {
		if (mem->memlen() < sizeof(struct rofl::openflow10::ofp_stats_request)) {
			*pmsg = new rofl::openflow::cofmsg(mem);
			throw eBadSyntaxTooShort();
		}
		uint16_t stats_type = be16toh(((struct rofl::openflow10::ofp_stats_request*)mem->somem())->type);

		switch (stats_type) {
		case rofl::openflow10::OFPST_DESC: {
			(*pmsg = new rofl::openflow::cofmsg_desc_stats_request(mem))->validate();
		} break;
		case rofl::openflow10::OFPST_FLOW: {
			(*pmsg = new rofl::openflow::cofmsg_flow_stats_request(mem))->validate();
		} break;
		case rofl::openflow10::OFPST_AGGREGATE: {
			(*pmsg = new rofl::openflow::cofmsg_aggr_stats_request(mem))->validate();
		} break;
		case rofl::openflow10::OFPST_TABLE: {
			(*pmsg = new rofl::openflow::cofmsg_table_stats_request(mem))->validate();
		} break;
		case rofl::openflow10::OFPST_PORT: {
			(*pmsg = new rofl::openflow::cofmsg_port_stats_request(mem))->validate();
		} break;
		case rofl::openflow10::OFPST_QUEUE: {
			(*pmsg = new rofl::openflow::cofmsg_queue_stats_request(mem))->validate();
		} break;
		// TODO: experimenter statistics
		default: {
			(*pmsg = new rofl::openflow::cofmsg_stats_request(mem))->validate();
		} break;
		}

	} break;
	case rofl::openflow10::OFPT_STATS_REPLY: {
		if (mem->memlen() < sizeof(struct rofl::openflow10::ofp_stats_reply)) {
			*pmsg = new rofl::openflow::cofmsg(mem);
			throw eBadSyntaxTooShort();
		}
		uint16_t stats_type = be16toh(((struct rofl::openflow10::ofp_stats_reply*)mem->somem())->type);

		switch (stats_type) {
		case rofl::openflow10::OFPST_DESC: {
			(*pmsg = new rofl::openflow::cofmsg_desc_stats_reply(mem))->validate();
		} break;
		case rofl::openflow10::OFPST_FLOW: {
			(*pmsg = new rofl::openflow::cofmsg_flow_stats_reply(mem))->validate();
		} break;
		case rofl::openflow10::OFPST_AGGREGATE: {
			(*pmsg = new rofl::openflow::cofmsg_aggr_stats_reply(mem))->validate();
		} break;
		case rofl::openflow10::OFPST_TABLE: {
			(*pmsg = new rofl::openflow::cofmsg_table_stats_reply(mem))->validate();
		} break;
		case rofl::openflow10::OFPST_PORT: {
			(*pmsg = new rofl::openflow::cofmsg_port_stats_reply(mem))->validate();
		} break;
		case rofl::openflow10::OFPST_QUEUE: {
			(*pmsg = new rofl::openflow::cofmsg_queue_stats_reply(mem))->validate();
		} break;
		// TODO: experimenter statistics
		default: {
			(*pmsg = new rofl::openflow::cofmsg_stats_reply(mem))->validate();
		} break;
		}

	} break;

	case rofl::openflow10::OFPT_BARRIER_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_barrier_request(mem))->validate();
	} break;
	case rofl::openflow10::OFPT_BARRIER_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_barrier_reply(mem))->validate();
	} break;
	case rofl::openflow10::OFPT_QUEUE_GET_CONFIG_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_queue_get_config_request(mem))->validate();
	} break;
	case rofl::openflow10::OFPT_QUEUE_GET_CONFIG_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_queue_get_config_reply(mem))->validate();
	} break;

	default: {
		(*pmsg = new rofl::openflow::cofmsg(mem))->validate();
		logging::warn << "[rofl][sock] dropping unknown message " << **pmsg << std::endl;
		throw eBadRequestBadType();
	} break;
	}
}



void
crofsock::parse_of12_message(cmemory *mem, rofl::openflow::cofmsg **pmsg)
{
	struct openflow::ofp_header* header = (struct openflow::ofp_header*)mem->somem();

	switch (header->type) {
	case rofl::openflow12::OFPT_HELLO: {
		(*pmsg = new rofl::openflow::cofmsg_hello(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_ERROR: {
		(*pmsg = new rofl::openflow::cofmsg_error(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_ECHO_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_echo_request(mem))->validate();
	} break;
	case rofl::openflow12::OFPT_ECHO_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_echo_reply(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_EXPERIMENTER:	{
		(*pmsg = new rofl::openflow::cofmsg_experimenter(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_FEATURES_REQUEST:	{
		(*pmsg = new rofl::openflow::cofmsg_features_request(mem))->validate();
	} break;
	case rofl::openflow12::OFPT_FEATURES_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_features_reply(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_GET_CONFIG_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_get_config_request(mem))->validate();
	} break;
	case rofl::openflow12::OFPT_GET_CONFIG_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_get_config_reply(mem))->validate();
	} break;
	case rofl::openflow12::OFPT_SET_CONFIG: {
		(*pmsg = new rofl::openflow::cofmsg_set_config(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_PACKET_OUT: {
		(*pmsg = new rofl::openflow::cofmsg_packet_out(mem))->validate();
	} break;
	case rofl::openflow12::OFPT_PACKET_IN: {
		(*pmsg = new rofl::openflow::cofmsg_packet_in(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_FLOW_MOD: {
		(*pmsg = new rofl::openflow::cofmsg_flow_mod(mem))->validate();
	} break;
	case rofl::openflow12::OFPT_FLOW_REMOVED: {
		(*pmsg = new rofl::openflow::cofmsg_flow_removed(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_GROUP_MOD: {
		(*pmsg = new rofl::openflow::cofmsg_group_mod(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_PORT_MOD: {
		(*pmsg = new rofl::openflow::cofmsg_port_mod(mem))->validate();
	} break;
	case rofl::openflow12::OFPT_PORT_STATUS: {
		(*pmsg = new rofl::openflow::cofmsg_port_status(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_TABLE_MOD: {
		(*pmsg = new rofl::openflow::cofmsg_table_mod(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_STATS_REQUEST: {

		if (mem->memlen() < sizeof(struct rofl::openflow12::ofp_stats_request)) {
			*pmsg = new rofl::openflow::cofmsg(mem);
			throw eBadSyntaxTooShort();
		}
		uint16_t stats_type = be16toh(((struct rofl::openflow12::ofp_stats_request*)mem->somem())->type);

		switch (stats_type) {
		case rofl::openflow12::OFPST_DESC: {
			(*pmsg = new rofl::openflow::cofmsg_desc_stats_request(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_FLOW: {
			(*pmsg = new rofl::openflow::cofmsg_flow_stats_request(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_AGGREGATE: {
			(*pmsg = new rofl::openflow::cofmsg_aggr_stats_request(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_TABLE: {
			(*pmsg = new rofl::openflow::cofmsg_table_stats_request(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_PORT: {
			(*pmsg = new rofl::openflow::cofmsg_port_stats_request(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_QUEUE: {
			(*pmsg = new rofl::openflow::cofmsg_queue_stats_request(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_GROUP: {
			(*pmsg = new rofl::openflow::cofmsg_group_stats_request(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_GROUP_DESC: {
			(*pmsg = new rofl::openflow::cofmsg_group_desc_stats_request(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_GROUP_FEATURES: {
			(*pmsg = new rofl::openflow::cofmsg_group_features_stats_request(mem))->validate();
		} break;
		// TODO: experimenter statistics
		default: {
			(*pmsg = new rofl::openflow::cofmsg_stats_request(mem))->validate();
		} break;
		}

	} break;
	case rofl::openflow12::OFPT_STATS_REPLY: {
		if (mem->memlen() < sizeof(struct rofl::openflow12::ofp_stats_reply)) {
			*pmsg = new rofl::openflow::cofmsg(mem);
			throw eBadSyntaxTooShort();
		}
		uint16_t stats_type = be16toh(((struct rofl::openflow12::ofp_stats_reply*)mem->somem())->type);

		switch (stats_type) {
		case rofl::openflow12::OFPST_DESC: {
			(*pmsg = new rofl::openflow::cofmsg_desc_stats_reply(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_FLOW: {
			(*pmsg = new rofl::openflow::cofmsg_flow_stats_reply(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_AGGREGATE: {
			(*pmsg = new rofl::openflow::cofmsg_aggr_stats_reply(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_TABLE: {
			(*pmsg = new rofl::openflow::cofmsg_table_stats_reply(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_PORT: {
			(*pmsg = new rofl::openflow::cofmsg_port_stats_reply(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_QUEUE: {
			(*pmsg = new rofl::openflow::cofmsg_queue_stats_reply(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_GROUP: {
			(*pmsg = new rofl::openflow::cofmsg_group_stats_reply(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_GROUP_DESC: {
			(*pmsg = new rofl::openflow::cofmsg_group_desc_stats_reply(mem))->validate();
		} break;
		case rofl::openflow12::OFPST_GROUP_FEATURES: {
			(*pmsg = new rofl::openflow::cofmsg_group_features_stats_reply(mem))->validate();
		} break;
		// TODO: experimenter statistics
		default: {
			(*pmsg = new rofl::openflow::cofmsg_stats_reply(mem))->validate();
		} break;
		}

	} break;

	case rofl::openflow12::OFPT_BARRIER_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_barrier_request(mem))->validate();
	} break;
	case rofl::openflow12::OFPT_BARRIER_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_barrier_reply(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_QUEUE_GET_CONFIG_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_queue_get_config_request(mem))->validate();
	} break;
	case rofl::openflow12::OFPT_QUEUE_GET_CONFIG_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_queue_get_config_reply(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_ROLE_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_role_request(mem))->validate();
	} break;
	case rofl::openflow12::OFPT_ROLE_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_role_reply(mem))->validate();
	} break;

	case rofl::openflow12::OFPT_GET_ASYNC_REQUEST: {
    	(*pmsg = new rofl::openflow::cofmsg_get_async_config_request(mem))->validate();
    } break;
	case rofl::openflow12::OFPT_GET_ASYNC_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_get_async_config_reply(mem))->validate();
	} break;
	case rofl::openflow12::OFPT_SET_ASYNC: {
    	(*pmsg = new rofl::openflow::cofmsg_set_async_config(mem))->validate();
    } break;

	default: {
		(*pmsg = new rofl::openflow::cofmsg(mem))->validate();
		logging::warn << "[rofl][sock] dropping unknown message " << **pmsg << std::endl;
		throw eBadRequestBadType();
	} return;
	}
}



void
crofsock::parse_of13_message(cmemory *mem, rofl::openflow::cofmsg **pmsg)
{
	struct openflow::ofp_header* header = (struct openflow::ofp_header*)mem->somem();

	switch (header->type) {
	case rofl::openflow13::OFPT_HELLO: {
		(*pmsg = new rofl::openflow::cofmsg_hello(mem))->validate();
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_hello&>( **pmsg );
	} break;

	case rofl::openflow13::OFPT_ERROR: {
		(*pmsg = new rofl::openflow::cofmsg_error(mem))->validate();
	} break;

	case rofl::openflow13::OFPT_ECHO_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_echo_request(mem))->validate();
	} break;
	case rofl::openflow13::OFPT_ECHO_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_echo_reply(mem))->validate();
	} break;

	case rofl::openflow13::OFPT_EXPERIMENTER:	{
		(*pmsg = new rofl::openflow::cofmsg_experimenter(mem))->validate();
	} break;

	case rofl::openflow13::OFPT_FEATURES_REQUEST:	{
		(*pmsg = new rofl::openflow::cofmsg_features_request(mem))->validate();
	} break;
	case rofl::openflow13::OFPT_FEATURES_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_features_reply(mem))->validate();
	} break;

	case rofl::openflow13::OFPT_GET_CONFIG_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_get_config_request(mem))->validate();
	} break;
	case rofl::openflow13::OFPT_GET_CONFIG_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_get_config_reply(mem))->validate();
	} break;
	case rofl::openflow13::OFPT_SET_CONFIG: {
		(*pmsg = new rofl::openflow::cofmsg_set_config(mem))->validate();
	} break;

	case rofl::openflow13::OFPT_PACKET_OUT: {
		(*pmsg = new rofl::openflow::cofmsg_packet_out(mem))->validate();
	} break;
	case rofl::openflow13::OFPT_PACKET_IN: {
		(*pmsg = new rofl::openflow::cofmsg_packet_in(mem))->validate();
	} break;

	case rofl::openflow13::OFPT_FLOW_MOD: {
		(*pmsg = new rofl::openflow::cofmsg_flow_mod(mem))->validate();
	} break;
	case rofl::openflow13::OFPT_FLOW_REMOVED: {
		(*pmsg = new rofl::openflow::cofmsg_flow_removed(mem))->validate();
	} break;

	case rofl::openflow13::OFPT_GROUP_MOD: {
		(*pmsg = new rofl::openflow::cofmsg_group_mod(mem))->validate();
	} break;

	case rofl::openflow13::OFPT_PORT_MOD: {
		(*pmsg = new rofl::openflow::cofmsg_port_mod(mem))->validate();
	} break;
	case rofl::openflow13::OFPT_PORT_STATUS: {
		(*pmsg = new rofl::openflow::cofmsg_port_status(mem))->validate();
	} break;

	case rofl::openflow13::OFPT_TABLE_MOD: {
		(*pmsg = new rofl::openflow::cofmsg_table_mod(mem))->validate();
	} break;

	case rofl::openflow13::OFPT_MULTIPART_REQUEST: {

		if (mem->memlen() < sizeof(struct rofl::openflow13::ofp_multipart_request)) {
			*pmsg = new rofl::openflow::cofmsg(mem);
			throw eBadSyntaxTooShort();
		}
		uint16_t stats_type = be16toh(((struct rofl::openflow13::ofp_multipart_request*)mem->somem())->type);

		switch (stats_type) {
		case rofl::openflow13::OFPMP_DESC: {
			(*pmsg = new rofl::openflow::cofmsg_desc_stats_request(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_FLOW: {
			(*pmsg = new rofl::openflow::cofmsg_flow_stats_request(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_AGGREGATE: {
			(*pmsg = new rofl::openflow::cofmsg_aggr_stats_request(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_TABLE: {
			(*pmsg = new rofl::openflow::cofmsg_table_stats_request(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_PORT_STATS: {
			(*pmsg = new rofl::openflow::cofmsg_port_stats_request(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_QUEUE: {
			(*pmsg = new rofl::openflow::cofmsg_queue_stats_request(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_GROUP: {
			(*pmsg = new rofl::openflow::cofmsg_group_stats_request(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_GROUP_DESC: {
			(*pmsg = new rofl::openflow::cofmsg_group_desc_stats_request(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_GROUP_FEATURES: {
			(*pmsg = new rofl::openflow::cofmsg_group_features_stats_request(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_METER: {
			// TODO
		} break;
		case rofl::openflow13::OFPMP_METER_CONFIG: {
			// TODO
		} break;
		case rofl::openflow13::OFPMP_METER_FEATURES: {
			// TODO
		} break;
		case rofl::openflow13::OFPMP_TABLE_FEATURES: {
			(*pmsg = new rofl::openflow::cofmsg_table_features_stats_request(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_PORT_DESC: {
			(*pmsg = new rofl::openflow::cofmsg_port_desc_stats_request(mem))->validate();
		} break;
		// TODO: experimenter statistics
		default: {
			(*pmsg = new rofl::openflow::cofmsg_stats_request(mem))->validate();
		} break;
		}

	} break;
	case rofl::openflow13::OFPT_MULTIPART_REPLY: {
		if (mem->memlen() < sizeof(struct rofl::openflow13::ofp_multipart_reply)) {
			*pmsg = new rofl::openflow::cofmsg(mem);
			throw eBadSyntaxTooShort();
		}
		uint16_t stats_type = be16toh(((struct rofl::openflow13::ofp_multipart_reply*)mem->somem())->type);

		switch (stats_type) {
		case rofl::openflow13::OFPMP_DESC: {
			(*pmsg = new rofl::openflow::cofmsg_desc_stats_reply(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_FLOW: {
			(*pmsg = new rofl::openflow::cofmsg_flow_stats_reply(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_AGGREGATE: {
			(*pmsg = new rofl::openflow::cofmsg_aggr_stats_reply(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_TABLE: {
			(*pmsg = new rofl::openflow::cofmsg_table_stats_reply(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_PORT_STATS: {
			(*pmsg = new rofl::openflow::cofmsg_port_stats_reply(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_QUEUE: {
			(*pmsg = new rofl::openflow::cofmsg_queue_stats_reply(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_GROUP: {
			(*pmsg = new rofl::openflow::cofmsg_group_stats_reply(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_GROUP_DESC: {
			(*pmsg = new rofl::openflow::cofmsg_group_desc_stats_reply(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_GROUP_FEATURES: {
			(*pmsg = new rofl::openflow::cofmsg_group_features_stats_reply(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_METER: {
			// TODO
		} break;
		case rofl::openflow13::OFPMP_METER_CONFIG: {
			// TODO
		} break;
		case rofl::openflow13::OFPMP_METER_FEATURES: {
			// TODO
		} break;
		case rofl::openflow13::OFPMP_TABLE_FEATURES: {
			(*pmsg = new rofl::openflow::cofmsg_table_features_stats_reply(mem))->validate();
		} break;
		case rofl::openflow13::OFPMP_PORT_DESC: {
			(*pmsg = new rofl::openflow::cofmsg_port_desc_stats_reply(mem))->validate();
		} break;
		// TODO: experimenter statistics
		default: {
			(*pmsg = new rofl::openflow::cofmsg_stats_reply(mem))->validate();
		} break;
		}

	} break;

	case rofl::openflow13::OFPT_BARRIER_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_barrier_request(mem))->validate();
	} break;
	case rofl::openflow13::OFPT_BARRIER_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_barrier_reply(mem))->validate();
	} break;

	case rofl::openflow13::OFPT_QUEUE_GET_CONFIG_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_queue_get_config_request(mem))->validate();
	} break;
	case rofl::openflow13::OFPT_QUEUE_GET_CONFIG_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_queue_get_config_reply(mem))->validate();
	} break;

	case rofl::openflow13::OFPT_ROLE_REQUEST: {
		(*pmsg = new rofl::openflow::cofmsg_role_request(mem))->validate();
	} break;
	case rofl::openflow13::OFPT_ROLE_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_role_reply(mem))->validate();
	} break;

	case rofl::openflow13::OFPT_GET_ASYNC_REQUEST: {
    	(*pmsg = new rofl::openflow::cofmsg_get_async_config_request(mem))->validate();
    } break;
	case rofl::openflow13::OFPT_GET_ASYNC_REPLY: {
		(*pmsg = new rofl::openflow::cofmsg_get_async_config_reply(mem))->validate();
	} break;
	case rofl::openflow13::OFPT_SET_ASYNC: {
    	(*pmsg = new rofl::openflow::cofmsg_set_async_config(mem))->validate();
    } break;

	default: {
		(*pmsg = new rofl::openflow::cofmsg(mem))->validate();
		logging::warn << "[rofl][sock] dropping unknown message " << **pmsg << std::endl;
		throw eBadRequestBadType();
	} return;
	}
}








void
crofsock::log_message(
		std::string const& text, rofl::openflow::cofmsg const& msg)
{
	logging::debug << "[rofl][sock] " << text << std::endl;

	try {
	switch (msg.get_version()) {
	case rofl::openflow10::OFP_VERSION: log_of10_message(msg); break;
	case rofl::openflow12::OFP_VERSION: log_of12_message(msg); break;
	case rofl::openflow13::OFP_VERSION: log_of13_message(msg); break;
	default: logging::debug << "[rolf][sock] unknown OFP version found in msg" << std::endl << msg; break;
	}
	} catch (...) {
		logging::debug << "[rofl][sock] log-message" << std::endl;
	}
}






void
crofsock::log_of10_message(
		rofl::openflow::cofmsg const& msg)
{
	switch (msg.get_type()) {
	case rofl::openflow10::OFPT_HELLO: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_hello const&>( msg );
	} break;
	case rofl::openflow10::OFPT_ERROR: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_error const&>( msg );
	} break;
	case rofl::openflow10::OFPT_ECHO_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_echo_request const&>( msg );
	} break;
	case rofl::openflow10::OFPT_ECHO_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_echo_reply const&>( msg );
	} break;
	case rofl::openflow10::OFPT_VENDOR: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_experimenter const&>( msg );
	} break;
	case rofl::openflow10::OFPT_FEATURES_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_features_request const&>( msg );
	} break;
	case rofl::openflow10::OFPT_FEATURES_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_features_reply const&>( msg );
	} break;
	case rofl::openflow10::OFPT_GET_CONFIG_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_get_config_request const&>( msg );
	} break;
	case rofl::openflow10::OFPT_GET_CONFIG_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_get_config_reply const&>( msg );
	} break;
	case rofl::openflow10::OFPT_SET_CONFIG: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_set_config const&>( msg );
	} break;
	case rofl::openflow10::OFPT_PACKET_OUT: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_packet_out const&>( msg );
	} break;
	case rofl::openflow10::OFPT_PACKET_IN: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_packet_in const&>( msg );
	} break;
	case rofl::openflow10::OFPT_FLOW_MOD: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_flow_mod const&>( msg );
	} break;
	case rofl::openflow10::OFPT_FLOW_REMOVED: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_flow_removed const&>( msg );
	} break;
	case rofl::openflow10::OFPT_PORT_MOD: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_mod const&>( msg );
	} break;
	case rofl::openflow10::OFPT_PORT_STATUS: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_status const&>( msg );
	} break;
	case rofl::openflow10::OFPT_STATS_REQUEST: {
		rofl::openflow::cofmsg_stats_request const& stats = dynamic_cast<rofl::openflow::cofmsg_stats_request const&>( msg );
		switch (stats.get_stats_type()) {
		case rofl::openflow10::OFPST_DESC: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_desc_stats_request const&>( msg );
		} break;
		case rofl::openflow10::OFPST_FLOW: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_flow_stats_request const&>( msg );
		} break;
		case rofl::openflow10::OFPST_AGGREGATE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_aggr_stats_request const&>( msg );
		} break;
		case rofl::openflow10::OFPST_TABLE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_table_stats_request const&>( msg );
		} break;
		case rofl::openflow10::OFPST_PORT: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_stats_request const&>( msg );
		} break;
		case rofl::openflow10::OFPST_QUEUE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_queue_stats_request const&>( msg );
		} break;
		// TODO: experimenter statistics
		default:
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_stats_request const&>( msg ); break;
		}

	} break;
	case rofl::openflow10::OFPT_STATS_REPLY: {
		rofl::openflow::cofmsg_stats_reply const& stats = dynamic_cast<rofl::openflow::cofmsg_stats_reply const&>( msg );
		switch (stats.get_stats_type()) {
		case rofl::openflow10::OFPST_DESC: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_desc_stats_reply const&>( msg );
		} break;
		case rofl::openflow10::OFPST_FLOW: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_flow_stats_reply const&>( msg );
		} break;
		case rofl::openflow10::OFPST_AGGREGATE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_aggr_stats_reply const&>( msg );
		} break;
		case rofl::openflow10::OFPST_TABLE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_table_stats_reply const&>( msg );
		} break;
		case rofl::openflow10::OFPST_PORT: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_stats_reply const&>( msg );
		} break;
		case rofl::openflow10::OFPST_QUEUE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_queue_stats_reply const&>( msg );
		} break;
		// TODO: experimenter statistics
		default: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_stats_reply const&>( msg );
		} break;
		}

	} break;
	case rofl::openflow10::OFPT_BARRIER_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_barrier_request const&>( msg );
	} break;
	case rofl::openflow10::OFPT_BARRIER_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_barrier_reply const&>( msg );
	} break;
	case rofl::openflow10::OFPT_QUEUE_GET_CONFIG_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_queue_get_config_request const&>( msg );
	} break;
	case rofl::openflow10::OFPT_QUEUE_GET_CONFIG_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_queue_get_config_reply const&>( msg );
	} break;
	default: {
		logging::debug << "[rofl][sock]  unknown message " << msg << std::endl;
	} break;
	}
}



void
crofsock::log_of12_message(
		rofl::openflow::cofmsg const& msg)
{
	switch (msg.get_type()) {
	case rofl::openflow12::OFPT_HELLO: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_hello const&>( msg );
	} break;
	case rofl::openflow12::OFPT_ERROR: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_error const&>( msg );
	} break;
	case rofl::openflow12::OFPT_ECHO_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_echo_request const&>( msg );
	} break;
	case rofl::openflow12::OFPT_ECHO_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_echo_reply const&>( msg );
	} break;
	case rofl::openflow12::OFPT_EXPERIMENTER:	{
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_experimenter const&>( msg );
	} break;
	case rofl::openflow12::OFPT_FEATURES_REQUEST:	{
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_features_request const&>( msg );
	} break;
	case rofl::openflow12::OFPT_FEATURES_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_features_reply const&>( msg );
	} break;
	case rofl::openflow12::OFPT_GET_CONFIG_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_get_config_request const&>( msg );
	} break;
	case rofl::openflow12::OFPT_GET_CONFIG_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_get_config_reply const&>( msg );
	} break;
	case rofl::openflow12::OFPT_SET_CONFIG: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_set_config const&>( msg );
	} break;
	case rofl::openflow12::OFPT_PACKET_OUT: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_packet_out const&>( msg );
	} break;
	case rofl::openflow12::OFPT_PACKET_IN: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_packet_in const&>( msg );
	} break;
	case rofl::openflow12::OFPT_FLOW_MOD: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_flow_mod const&>( msg );
	} break;
	case rofl::openflow12::OFPT_FLOW_REMOVED: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_flow_removed const&>( msg );
	} break;
	case rofl::openflow12::OFPT_GROUP_MOD: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_mod const&>( msg );
	} break;
	case rofl::openflow12::OFPT_PORT_MOD: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_mod const&>( msg );
	} break;
	case rofl::openflow12::OFPT_PORT_STATUS: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_status const&>( msg );
	} break;
	case rofl::openflow12::OFPT_TABLE_MOD: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_table_mod const&>( msg );
	} break;
	case rofl::openflow12::OFPT_STATS_REQUEST: {
		rofl::openflow::cofmsg_stats_request const& stats = dynamic_cast<rofl::openflow::cofmsg_stats_request const&>( msg );
		switch (stats.get_stats_type()) {
		case rofl::openflow12::OFPST_DESC: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_desc_stats_request const&>( msg );
		} break;
		case rofl::openflow12::OFPST_FLOW: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_flow_stats_request const&>( msg );
		} break;
		case rofl::openflow12::OFPST_AGGREGATE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_aggr_stats_request const&>( msg );
		} break;
		case rofl::openflow12::OFPST_TABLE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_table_stats_request const&>( msg );
		} break;
		case rofl::openflow12::OFPST_PORT: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_stats_request const&>( msg );
		} break;
		case rofl::openflow12::OFPST_QUEUE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_queue_stats_request const&>( msg );
		} break;
		case rofl::openflow12::OFPST_GROUP: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_stats_request const&>( msg );
		} break;
		case rofl::openflow12::OFPST_GROUP_DESC: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_desc_stats_request const&>( msg );
		} break;
		case rofl::openflow12::OFPST_GROUP_FEATURES: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_features_stats_request const&>( msg );
		} break;
		// TODO: experimenter statistics
		default: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_stats_request const&>( msg );
		} break;
		}

	} break;
	case rofl::openflow12::OFPT_STATS_REPLY: {
		rofl::openflow::cofmsg_stats_reply const& stats = dynamic_cast<rofl::openflow::cofmsg_stats_reply const&>( msg );
		switch (stats.get_stats_type()) {
		case rofl::openflow12::OFPST_DESC: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_desc_stats_reply const&>( msg );
		} break;
		case rofl::openflow12::OFPST_FLOW: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_flow_stats_reply const&>( msg );
		} break;
		case rofl::openflow12::OFPST_AGGREGATE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_aggr_stats_reply const&>( msg );
		} break;
		case rofl::openflow12::OFPST_TABLE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_table_stats_reply const&>( msg );
		} break;
		case rofl::openflow12::OFPST_PORT: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_stats_reply const&>( msg );
		} break;
		case rofl::openflow12::OFPST_QUEUE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_queue_stats_reply const&>( msg );
		} break;
		case rofl::openflow12::OFPST_GROUP: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_stats_reply const&>( msg );
		} break;
		case rofl::openflow12::OFPST_GROUP_DESC: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_desc_stats_reply const&>( msg );
		} break;
		case rofl::openflow12::OFPST_GROUP_FEATURES: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_features_stats_reply const&>( msg );
		} break;
		// TODO: experimenter statistics
		default: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_stats_reply const&>( msg );
		} break;
		}

	} break;
	case rofl::openflow12::OFPT_BARRIER_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_barrier_request const&>( msg );
	} break;
	case rofl::openflow12::OFPT_BARRIER_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_barrier_reply const&>( msg );
	} break;
	case rofl::openflow12::OFPT_QUEUE_GET_CONFIG_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_queue_get_config_request const&>( msg );
	} break;
	case rofl::openflow12::OFPT_QUEUE_GET_CONFIG_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_queue_get_config_reply const&>( msg );
	} break;
	case rofl::openflow12::OFPT_ROLE_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_role_request const&>( msg );
	} break;
	case rofl::openflow12::OFPT_ROLE_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_role_reply const&>( msg );
	} break;
	case rofl::openflow12::OFPT_GET_ASYNC_REQUEST: {
    	logging::debug << dynamic_cast<rofl::openflow::cofmsg_get_async_config_request const&>( msg );
    } break;
	case rofl::openflow12::OFPT_GET_ASYNC_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_get_async_config_reply const&>( msg );
	} break;
	case rofl::openflow12::OFPT_SET_ASYNC: {
    	logging::debug << dynamic_cast<rofl::openflow::cofmsg_set_async_config const&>( msg );
    } break;
	default: {
		logging::debug << "[rofl][sock] unknown message " << msg << std::endl;
	} break;
	}
}



void
crofsock::log_of13_message(
		rofl::openflow::cofmsg const& msg)
{
	switch (msg.get_type()) {
	case rofl::openflow13::OFPT_HELLO: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_hello const&>( msg );
	} break;
	case rofl::openflow13::OFPT_ERROR: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_error const&>( msg );
	} break;
	case rofl::openflow13::OFPT_ECHO_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_echo_request const&>( msg );
	} break;
	case rofl::openflow13::OFPT_ECHO_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_echo_reply const&>( msg );
	} break;
	case rofl::openflow13::OFPT_EXPERIMENTER:	{
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_experimenter const&>( msg );
	} break;
	case rofl::openflow13::OFPT_FEATURES_REQUEST:	{
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_features_request const&>( msg );
	} break;
	case rofl::openflow13::OFPT_FEATURES_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_features_reply const&>( msg );
	} break;
	case rofl::openflow13::OFPT_GET_CONFIG_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_get_config_request const&>( msg );
	} break;
	case rofl::openflow13::OFPT_GET_CONFIG_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_get_config_reply const&>( msg );
	} break;
	case rofl::openflow13::OFPT_SET_CONFIG: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_set_config const&>( msg );
	} break;
	case rofl::openflow13::OFPT_PACKET_OUT: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_packet_out const&>( msg );
	} break;
	case rofl::openflow13::OFPT_PACKET_IN: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_packet_in const&>( msg );
	} break;
	case rofl::openflow13::OFPT_FLOW_MOD: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_flow_mod const&>( msg );
	} break;
	case rofl::openflow13::OFPT_FLOW_REMOVED: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_flow_removed const&>( msg );
	} break;
	case rofl::openflow13::OFPT_GROUP_MOD: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_mod const&>( msg );
	} break;
	case rofl::openflow13::OFPT_PORT_MOD: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_mod const&>( msg );
	} break;
	case rofl::openflow13::OFPT_PORT_STATUS: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_status const&>( msg );
	} break;
	case rofl::openflow13::OFPT_TABLE_MOD: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_table_mod const&>( msg );
	} break;
	case rofl::openflow13::OFPT_MULTIPART_REQUEST: {
		rofl::openflow::cofmsg_multipart_request const& stats = dynamic_cast<rofl::openflow::cofmsg_multipart_request const&>( msg );
		switch (stats.get_stats_type()) {
		case rofl::openflow13::OFPMP_DESC: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_desc_stats_request const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_FLOW: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_flow_stats_request const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_AGGREGATE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_aggr_stats_request const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_TABLE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_table_stats_request const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_PORT_STATS: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_stats_request const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_QUEUE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_queue_stats_request const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_GROUP: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_stats_request const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_GROUP_DESC: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_desc_stats_request const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_GROUP_FEATURES: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_features_stats_request const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_METER: {
			// TODO
		} break;
		case rofl::openflow13::OFPMP_METER_CONFIG: {
			// TODO
		} break;
		case rofl::openflow13::OFPMP_METER_FEATURES: {
			// TODO
		} break;
		case rofl::openflow13::OFPMP_TABLE_FEATURES: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_table_features_stats_request const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_PORT_DESC: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_desc_stats_request const&>( msg );
		} break;
		// TODO: experimenter statistics
		default: {

		} break;
		}

	} break;
	case rofl::openflow13::OFPT_MULTIPART_REPLY: {
		rofl::openflow::cofmsg_multipart_reply const& stats = dynamic_cast<rofl::openflow::cofmsg_multipart_reply const&>( msg );
		switch (stats.get_stats_type()) {
		case rofl::openflow13::OFPMP_DESC: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_desc_stats_reply const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_FLOW: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_flow_stats_reply const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_AGGREGATE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_aggr_stats_reply const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_TABLE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_table_stats_reply const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_PORT_STATS: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_stats_reply const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_QUEUE: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_queue_stats_reply const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_GROUP: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_stats_reply const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_GROUP_DESC: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_desc_stats_reply const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_GROUP_FEATURES: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_group_features_stats_reply const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_METER: {
			// TODO
		} break;
		case rofl::openflow13::OFPMP_METER_CONFIG: {
			// TODO
		} break;
		case rofl::openflow13::OFPMP_METER_FEATURES: {
			// TODO
		} break;
		case rofl::openflow13::OFPMP_TABLE_FEATURES: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_table_features_stats_reply const&>( msg );
		} break;
		case rofl::openflow13::OFPMP_PORT_DESC: {
			logging::debug << dynamic_cast<rofl::openflow::cofmsg_port_desc_stats_reply const&>( msg );
		} break;
		// TODO: experimenter statistics
		default: {

		} break;
		}

	} break;

	case rofl::openflow13::OFPT_BARRIER_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_barrier_request const&>( msg );
	} break;
	case rofl::openflow13::OFPT_BARRIER_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_barrier_reply const&>( msg );
	} break;
	case rofl::openflow13::OFPT_QUEUE_GET_CONFIG_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_queue_get_config_request const&>( msg );
	} break;
	case rofl::openflow13::OFPT_QUEUE_GET_CONFIG_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_queue_get_config_reply const&>( msg );
	} break;
	case rofl::openflow13::OFPT_ROLE_REQUEST: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_role_request const&>( msg );
	} break;
	case rofl::openflow13::OFPT_ROLE_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_role_reply const&>( msg );
	} break;
	case rofl::openflow13::OFPT_GET_ASYNC_REQUEST: {
    	logging::debug << dynamic_cast<rofl::openflow::cofmsg_get_async_config_request const&>( msg );
    } break;
	case rofl::openflow13::OFPT_GET_ASYNC_REPLY: {
		logging::debug << dynamic_cast<rofl::openflow::cofmsg_get_async_config_reply const&>( msg );
	} break;
	case rofl::openflow13::OFPT_SET_ASYNC: {
    	logging::debug << dynamic_cast<rofl::openflow::cofmsg_set_async_config const&>( msg );
    } break;
	default: {
		logging::debug << "[rofl][sock] unknown message " << msg << std::endl;
	} break;
	}
}

