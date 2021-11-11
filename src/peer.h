//
// Created by wnabo on 31.10.2021.
//

#include <string>
#include <set>
#include <map>
#include "networkPlan.h"

#ifndef VOTE_P2P_PEER_H
#define VOTE_P2P_PEER_H


class peer {
public:
    peer();
    virtual ~peer();
    void receive(void *context);
    void connect(std::string& input, void *context);
    void printConnections();
    void initSyncThread(void* context, networkPlan plan, std::string initial_receiver_address = "");
private:
    std::string peer_identity;
    std::string peer_address;
    std::set<std::string> known_peer_addresses;
    std::map<std::string, std::string> connection_table;
};


#endif //VOTE_P2P_PEER_H