#include "rfproxy.hh"

RFProxy::RFProxy(const char* address, const int port, const int n_workers,
	bool secure) : OFServer(address, port, n_workers, secure,
	OFServerSettings().supported_version(1)) {
		
        this->ipc = new MongoIPCMessageService(MONGO_ADDRESS,
            MONGO_DB_NAME,
            to_string<uint64_t>(ID));
        this->factory = new RFProtocolFactory();
        this->ipc->listen(RFSERVER_RFPROXY_CHANNEL, factory, this, false);
    }


bool RFProxy::send_of_msg(uint64_t dp_id, uint8_t* msg) {
    struct ofp_header* ofph = (struct ofp_header*) msg;
    uint16_t length = ntohs(ofph->length);

    if (this->dp_id_to_conn.find(dp_id) != this->dp_id_to_conn.end()) {
        OFConnection* ofconn = this->dp_id_to_conn[dp_id];
        ofconn->send(msg, length);
        return SUCCESS;
    }
    return FAILURE;
}

bool RFProxy::send_packet_out(fluid_msg::of10::PacketIn& pi,
    uint64_t dp_id, uint32_t out_port) {
        if (this->dp_id_to_conn.find(dp_id) != this->dp_id_to_conn.end()) {
            fluid_msg::of10::PacketOut po(pi.xid(), -1, OFPP_NONE);
            po.data(pi.data(), pi.data_len());
            fluid_msg::of10::OutputAction act(out_port, 1024);
            po.add_action(act);

            uint8_t* buf;
            buf = po.pack();

            OFConnection* ofconn = this->dp_id_to_conn[dp_id];
            ofconn->send(buf, po.length());

            fluid_msg::OFMsg::free_buffer(buf);

            return SUCCESS;
        }
        printf("!! -> Fail sending packet out!\n");
        return FAILURE;
}

bool RFProxy::process(const string &from, const string &to,
    const string &channel, IPCMessage& msg) {

    int type = msg.get_type();
    if (type == ROUTE_MOD) {
        RouteMod* rmmsg = static_cast<RouteMod*>(&msg);
        boost::shared_array<uint8_t> ofmsg = create_flow_mod(rmmsg->get_mod(),
                                    rmmsg->get_matches(),
                                    rmmsg->get_actions(),
                                    rmmsg->get_options());
        if (ofmsg.get() == NULL) {
            printf("!! -> Failed to create OpenFlow FlowMod\n");
        } else {
            send_of_msg(rmmsg->get_id(), ofmsg.get());
            printf("!! -> Installing flows...\n");
        }
    }
    else if (type == DATA_PLANE_MAP) {
        DataPlaneMap* dpmmsg = dynamic_cast<DataPlaneMap*>(&msg);
        table.update_dp_port(dpmmsg->get_dp_id(), dpmmsg->get_dp_port(),
                             dpmmsg->get_vs_id(), dpmmsg->get_vs_port());
    }
    return true;
}

void RFProxy::message_callback(OFConnection* ofconn,
	uint8_t type,
	void* data,
	size_t len) {
    // A datapath is up (we'll notify RFServer)
    if (type == fluid_msg::of10::OFPT_FEATURES_REPLY) {

        fluid_msg::of10::FeaturesReply *fr = new fluid_msg::of10::FeaturesReply();
        fr->unpack((uint8_t*) data);

        uint64_t datapath_id = fr->datapath_id();
        dp_id_to_conn[fr->datapath_id()] = ofconn;
        uint64_t* datapath_data = new uint64_t;
        *datapath_data = datapath_id;
        ofconn->set_application_data(datapath_data);

        vector<fluid_msg::of10::Port> ports = fr->ports();
        for (vector<fluid_msg::of10::Port>::iterator it = ports.begin();
            it != ports.end(); ++it) {

            fluid_msg::of10::Port port = *it;
            uint16_t port_no = port.port_no();

            if (port_no <= fluid_msg::of10::OFPP_MAX) {
                DatapathPortRegister msg(ID, fr->datapath_id(), port_no);
                ipc->send(RFSERVER_RFPROXY_CHANNEL, RFSERVER_ID, msg);

                printf("!! -> Registering datapath port (dp_id=%0#"PRIx64", dp_port=%d)\n",
                    datapath_id,
                    port_no);
            }
        }
    }

    // A packet-in event from the switch (we will redirect the
    // packet as needed)
    else if (type == fluid_msg::of10::OFPT_PACKET_IN) {
        fluid_msg::of10::PacketIn pi;
        pi.unpack((uint8_t*) data);

        // Datapath not yet registered. We'll just drop the packet.
        if (ofconn->get_application_data() == NULL) {
            return;
        }

        uint64_t dp_id = *((uint64_t*) ofconn->get_application_data());
        uint32_t in_port = pi.in_port();
        // Just replicate these for prettier code below
        uint64_t vs_id = dp_id;
        uint32_t vs_port = in_port;

        // TODO: We're assuming there is no VLAN tag. Fix this.
        uint8_t* packet = (uint8_t*) pi.data();
        uint16_t dl_type = ntohs(*((uint16_t*) (packet + 12)));

        // Drop all LLDP traffic.
        if (dl_type == LLDP) {
            return;
        }

        // If we have a mapping packet, inform RFServer through a
        // VirtualPlaneMap message
        if (dl_type == RF_ETH_PROTO) {
            const struct eth_data* data = (struct eth_data*) pi.data();

            printf("!! -> Received mapping packet (vm_id=%0#"PRIx64", vm_port=%d, vs_id=%0#"PRIx64", vs_port=%d)\n", data->vm_id, data->vm_port, vs_id, vs_port);

            // Create a map message and send it
            VirtualPlaneMap mapmsg;
            mapmsg.set_vm_id(data->vm_id);
            mapmsg.set_vm_port(data->vm_port);
            mapmsg.set_vs_id(vs_id);
            mapmsg.set_vs_port(vs_port);
            ipc->send(RFSERVER_RFPROXY_CHANNEL, RFSERVER_ID, mapmsg);

	        return;
        }

        // If the packet came from RFVS, redirect it to the right switch port
        if (IS_RFVS(dp_id)) {
            PORT dp_port = table.vs_port_to_dp_port(dp_id, in_port);
            if (dp_port != NONE) {
                send_packet_out(pi, dp_port.first, dp_port.second);
            }
            else {
                printf("!! -> Unmapped RFVS port (vs_id=%0#"PRIx64", vs_port=%d)\n", dp_id, in_port);
            }
        }
        // If the packet came from a switch, redirect it to the right RFVS port
        else {
            PORT vs_port = table.dp_port_to_vs_port(dp_id, in_port);
            if (vs_port != NONE) {
                send_packet_out(pi, vs_port.first, vs_port.second);
            }
            else {
                printf("!! -> Unmapped datapath port (vs_id=%0#"PRIx64", vs_port=%d)\n", dp_id, in_port);
            }
        }
    }

    // Some error happened! (We'll just log it).
    else if (type == fluid_msg::of10::OFPT_ERROR) {
        printf("!! -> OpenFlow error message received!\n");
    }
}

void RFProxy::connection_callback(OFConnection* ofconn,
	OFConnection::Event type) {

    if (type == OFConnection::EVENT_STARTED) {
        printf("!! -> Connection id=%d started\n", ofconn->get_id());
    }

    else if (type == OFConnection::EVENT_ESTABLISHED) {
        printf("!! -> Connection id=%d established\n", ofconn->get_id());
    }

    else if (type == OFConnection::EVENT_FAILED_NEGOTIATION) {
        printf("!! -> Connection id=%d failed version negotiation\n", ofconn->get_id());
    }

    else if (type == OFConnection::EVENT_CLOSED || type == OFConnection::EVENT_DEAD) {
        printf("!! -> Connection id=%d closed.\n", ofconn->get_id());

		// If we still don't have a dp_id for any reason, just ignore this
		// event.
		if (ofconn->get_application_data() == NULL) {
			return;
		}

		uint64_t dp_id = *((uint64_t*) ofconn->get_application_data());

		printf("!! -> Datapath is down (dp_id=%0#"PRIx64")", dp_id);

		// Delete internal entry
		table.delete_dp(dp_id);

		// Notify RFServer
		DatapathDown dd(ID, dp_id);
		ipc->send(RFSERVER_RFPROXY_CHANNEL, RFSERVER_ID, dd);
    }
}

int main() {
    RFProxy rfproxy("0.0.0.0", 6633, 4);
    rfproxy.start(true);
    return 0;
}
