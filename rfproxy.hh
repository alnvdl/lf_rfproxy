#include <fluid/OFServer.hh>
#include <fluid/OFConnection.hh>
#include <fluid/of10msg.hh>

#include <inttypes.h>
#include <iostream>
#include <vector>
#include <map>

#include "OFInterface.hh"

#include "ipc/IPC.h"
#include "ipc/MongoIPC.h"
#include "ipc/RFProtocol.h"
#include "ipc/RFProtocolFactory.h"

#include "converter.h"
#include "defs.h"

using namespace fluid_base;
using namespace std;

#define ID 0

// Return values for some internal functions
#define FAILURE 0
#define SUCCESS 1

#define LLDP 0x88CC

// Map message struct
struct eth_data {
	uint8_t eth_dst[6]; /* Destination MAC address. */
	uint8_t eth_src[6]; /* Source MAC address. */
	uint16_t eth_type; /* Packet type. */
	uint64_t vm_id; /* Number which identifies a Virtual Machine .*/
	uint8_t vm_port; /* Number of the Virtual Machine port */
}__attribute__((packed));

// Association table
typedef pair<uint64_t, uint32_t> PORT;
// We can do this because there can't be a 0xff... datapath ID or port
PORT NONE = PORT(-1, -1);

// This class should be in its own files
class Table {
    public:
        void update_dp_port(uint64_t dp_id, uint32_t dp_port,
                            uint64_t vs_id, uint32_t vs_port) {
            map<PORT, PORT>::iterator it;
            it = dp_to_vs.find(PORT(dp_id, dp_port));
            if (it != dp_to_vs.end()) {
                PORT old_vs_port = dp_to_vs[PORT(dp_id, dp_port)];
                vs_to_dp.erase(old_vs_port);
            }

            dp_to_vs[PORT(dp_id, dp_port)] = PORT(vs_id, vs_port);
            vs_to_dp[PORT(vs_id, vs_port)] = PORT(dp_id, dp_port);
        }

        PORT dp_port_to_vs_port(uint64_t dp_id, uint32_t dp_port) {
            map<PORT, PORT>::iterator it;
            it = dp_to_vs.find(PORT(dp_id, dp_port));
            if (it == dp_to_vs.end())
                return NONE;

            return dp_to_vs[PORT(dp_id, dp_port)];
        }

        PORT vs_port_to_dp_port(uint64_t vs_id, uint32_t vs_port) {
            map<PORT, PORT>::iterator it;
            it = vs_to_dp.find(PORT(vs_id, vs_port));
            if (it == vs_to_dp.end())
                return NONE;

            return vs_to_dp[PORT(vs_id, vs_port)];
        }

        void delete_dp(uint64_t dp_id) {
            map<PORT, PORT>::iterator it = dp_to_vs.begin();
            while (it != dp_to_vs.end()) {
                if ((*it).first.first == dp_id)
                    dp_to_vs.erase(it++);
                else
                    ++it;
            }

            it = vs_to_dp.begin();
            while (it != vs_to_dp.end()) {
                if ((*it).second.first == dp_id)
                    vs_to_dp.erase(it++);
                else
                    ++it;
            }
        }

    private:
        map<PORT, PORT> dp_to_vs;
        map<PORT, PORT> vs_to_dp;
};

class RFProxy : public OFServer, IPCMessageProcessor {
public:
    RFProxy(const char* address = "0.0.0.0",
               const int port = 6633,
               const int n_workers = 4,
               bool secure = false);

private:
    IPCMessageService* ipc;
    IPCMessageProcessor *processor;
    RFProtocolFactory *factory;
    Table table;
    std::map<uint64_t, OFConnection*> dp_id_to_conn;

    bool send_of_msg(uint64_t dp_id, uint8_t* msg);
    bool send_packet_out(fluid_msg::of10::PacketIn& pi,
        uint64_t dp_id, uint32_t out_port);
    bool process(const string &from, const string &to,
        const string &channel, IPCMessage& msg);
    virtual void message_callback(OFConnection* ofconn,
        uint8_t type,
        void* data,
        size_t len);
    virtual void connection_callback(OFConnection* ofconn,
        OFConnection::Event type);
};
