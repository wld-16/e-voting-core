//
// Created by wnabo on 31.10.2021.
//

#include <iostream>
#include <zmq.hpp>
#include <regex>
#include <nlohmann/json.hpp>
#include "peer.h"
#include <fstream>
#include "electionBuilder.h"
#include "sodium/utils.h"
#include <sodium/randombytes.h>
#include <sodium/crypto_aead_chacha20poly1305.h>

#define ADDITIONAL_DATA (const unsigned char *) "123456"
#define ADDITIONAL_DATA_LEN 6

unsigned char nonce[crypto_aead_chacha20poly1305_IETF_NPUBBYTES];

void peer::printConnections() {
    // Iterate through "receiver" nodes
    size_t index = 0;

    _logger.log("print connections");
    std::cout << std::endl;

    std::cout << "Host Ip Addresses:" << std::endl;

    std::for_each(known_peer_addresses.begin(), known_peer_addresses.end(), [&index](const std::string ip_address) {
        std::cout << "[" << index << "] " << ip_address << std::endl;
        index++;
    });

    // Iterate through connections
    index = 0;
    std::cout << std::endl << "Connections:" << std::endl;

    std::for_each(connection_table.begin(), connection_table.end(),
                  [&index](const std::pair<std::string, std::string> connection) {
                      std::cout << "[" << index << "] " << connection.first << "->" << connection.second << std::endl;
                      index++;
                  });

    nlohmann::json send_json = nlohmann::json();
    send_json["nodes"] = nlohmann::ordered_json(known_peer_addresses);
    send_json["connections"] = nlohmann::ordered_json(connection_table);

    std::cout << std::endl << "As Json:" << std::endl << nlohmann::to_string(send_json) << std::endl;
}

void printMetaData(zmq::message_t &msg) {
    std::cout << std::endl;
    std::cout << "Socket-Type: ";
    try {
        std::cout << msg.gets("Socket-Type") << std::endl;
    } catch (zmq::error_t er) {
        std::cout << "[]" << std::endl;
    }

    std::cout << "Peer-address: ";
    try {
        std::cout << msg.gets("Peer-Address") << std::endl;
    } catch (zmq::error_t er) {
        std::cout << "[]" << std::endl;
    }

    std::cout << "User-Id: ";
    try {
        std::cout << msg.gets("User-Id") << std::endl;
    } catch (zmq::error_t er) {
        std::cout << "[]" << std::endl;
    }

    std::cout << "Routing-Id: ";
    try {
        std::cout << msg.gets("Routing-Id") << std::endl;
    } catch (zmq::error_t er) {
        std::cout << "[]" << std::endl;
    }
    std::cout << std::endl;
}

void peer::connect(std::string &input, void *abstractContext) {
    _logger.log("is connecting to " + input);
    const std::string delimiter = " ";
    size_t position_of_whitespace = input.find(delimiter);
    auto address = input.substr(position_of_whitespace + delimiter.size(), input.size() - position_of_whitespace);

    zmq::context_t *zmq_context = (zmq::context_t *) abstractContext;
    zmq::socket_t sock(*zmq_context, zmq::socket_type::req);

    sock.connect("tcp://" + std::string(address) + ":5555");

    sock.send(zmq::message_t(address), zmq::send_flags::none);

    zmq::message_t reply;
    sock.recv(reply, zmq::recv_flags::none);
    if (!reply.empty()) {
        _logger.log("has replied" + reply.to_string(), reply.gets("Peer-Address"));
        if (std::regex_match(reply.to_string(),
                             std::regex("[0-9]{1,3}[.][0-9]{1,3}[.][0-9]{1,3}[.][0-9]{1,3}"))) {
            _logger.log("print message metadata\n\n");
            printMetaData(reply);

            _logger.log("added ip");

            peer_address = std::string(address);
            known_peer_addresses.insert(std::string(address));
            connection_table[reply.to_string()] = std::string(address);
        } else {
            _logger.warn("requesting peer already has a bound a peer to the net");
        }
    }
}

void peer::receive(void *abstractContext) {
    _logger.log("Wait for connection");

    zmq::context_t *zmq_context = (zmq::context_t *) abstractContext;
    zmq::socket_t sock(*zmq_context, zmq::socket_type::rep);

    zmq::version();
    sock.bind("tcp://*:5555");
    zmq::message_t request;

    sock.recv(request, zmq::recv_flags::none);

    if (!request.empty()) {
        if (!known_peer_addresses.contains(request.to_string())) {
            if (std::regex_match(request.to_string(),
                                 std::regex("[0-9]{1,3}[.][0-9]{1,3}[.][0-9]{1,3}[.][0-9]{1,3}"))) {

                // Add to connection to topology
                known_peer_addresses.insert(request.to_string());
                known_peer_addresses.insert(std::string(request.gets("Peer-Address")));
                connection_table.insert(
                        std::make_pair(std::string(request.gets("Peer-Address")), std::string(request.to_string())));

                printMetaData(request);
                _logger.log("added: " + request.to_string() + " to network");

                sock.send(zmq::message_t(std::string(request.gets("Peer-Address"))), zmq::send_flags::none);
            } else {
                sock.send(zmq::message_t(std::string("rejected")), zmq::send_flags::none);
                _logger.log("the requested peer address is rejected, an ip4 is required");
            }
        } else {
            sock.send(zmq::message_t(std::string("rejected")), zmq::send_flags::none);
            _logger.log("Peer rejected, already ip address already belongs to a known peer");
        }
    }
}

peer::~peer() {
    connection_table.clear();
    known_peer_addresses.clear();
}

peer::peer() {
    connection_table = std::map<std::string, std::string>();
    known_peer_addresses = std::set<std::string>();
    peer_address = "";
}

void peer::initSyncThread(void *context, straightLineSyncThread &thread, std::string initial_receiver_address) {
    thread.setParams(context, connection_table, known_peer_addresses, initial_receiver_address);
    thread.StartInternalThread();
}

const std::set<std::string> &peer::getKnownPeerAddresses() const {
    return known_peer_addresses;
}

void peer::setKnownPeerAddresses(const std::set<std::string> &known_peer_addresses) {
    peer::known_peer_addresses = known_peer_addresses;
}

const std::map<std::string, std::string> &peer::getConnectionTable() const {
    return connection_table;
}

void peer::setConnectionTable(const std::map<std::string, std::string> &connection_table) {
    peer::connection_table = connection_table;
}

void peer::exportPeerConnections(std::string exportPath) {
    std::ofstream exportStream;
    exportStream.open(exportPath + "connections.json");
    nlohmann::json connectionsJson = nlohmann::json();
    connectionsJson["connections"] = nlohmann::ordered_json(connection_table);
    exportStream << connectionsJson.dump() << "\n";
    exportStream.close();
}

void peer::exportPeersList(std::string exportPath) {
    std::ofstream exportStream;
    exportStream.open(exportPath + "peers.json");
    nlohmann::json peersJson = nlohmann::json();
    peersJson["peers"] = nlohmann::ordered_json(known_peer_addresses);
    exportStream << peersJson.dump() << "\n";
    exportStream.close();
}

void peer::importPeerConnections(std::string importPath) {
    std::ifstream importStream;
    std::string line;

    _logger.log("is importing connections file from " + importPath + "connections.json");

    // File Open in the Read Mode
    importStream.open(importPath + "connections.json");

    if (importStream.is_open()) {
        if (getline(importStream, line)) {

            nlohmann::json connectionsJson = nlohmann::json::parse(line);
            _logger.log("File contents: ");
            _logger.displayData(connectionsJson.dump());
            this->connection_table = connectionsJson["connections"].get<std::map<std::string, std::string>>();

        };
        // File Close
        importStream.close();
        _logger.log("Successfully imported connections");
    } else {
        _logger.error("Unable to open the file!");
    }
}

void peer::importPeersList(std::string importPath) {
    std::ifstream importStream;
    std::string line;

    _logger.log("is importing peers file from " + importPath + "peers.json");

    // File Open in the Read Mode
    importStream.open(importPath + "peers.json");

    if (importStream.is_open()) {
        if (getline(importStream, line)) {

            nlohmann::json peersJson = nlohmann::json::parse(line);
            _logger.log("File contents as json: ");
            _logger.displayData(peersJson.dump());
            this->known_peer_addresses = peersJson["peers"].get<std::set<std::string>>();

        };
        // File Close
        importStream.close();
        _logger.log("Successfully imported connections");
    } else {
        _logger.error("Unable to open the file!");
    }
}

void peer::vote() {
    if (election_box.size() > 0) {
        size_t chosen_id = selectElection();
        election &chosen_election = election_box.at(chosen_id);

        const std::map<size_t, std::string> &map = chosen_election.getOptions();

        std::for_each(map.begin(), map.end(), [](std::pair<size_t, std::string> idToOption) {
            std::cout << idToOption.first << ": " << idToOption.second << std::endl;
        });

        std::string input;

        std::getline(std::cin, input);
        std::cout << input << std::endl;
        std::size_t chosen_option = std::stoi(input);

        std::cout << "identity: " << peer_identity << std::endl;
        std::cout << "option:" << chosen_option << std::endl;
        std::cout << "option:" << map.at(chosen_option) << std::endl;


        const size_t options_pot = ((int) chosen_election.getOptions().size()) / 10 + 1;
        unsigned char encryptedVote[64];

        encryptVote(chosen_election, input, encryptedVote);

        chosen_election.placeVote(peer_identity, reinterpret_cast<const char *>(encryptedVote));
    }
};

election peer::createElection(size_t election_id) {
    _logger.promptUserInput("Enter how many possible Options can be elected");

    std::string input;
    std::getline(std::cin, input);

    std::size_t number_of_options = std::atoi(input.c_str());
    std::vector<std::string> options(number_of_options);
    std::cout << options.size() << std::endl;

    size_t index = 0;
    std::transform(options.begin(), options.end(), options.begin(),
                   [&index, this](const std::string &option) -> std::string {
                       this->_logger.promptUserInput("Enter Option #" + std::to_string(index));
                       std::string input;
                       std::getline(std::cin, input);
                       index++;
                       return input;
                   });

    nlohmann::json electionJson = nlohmann::json(options);
    std::cout << electionJson.dump() << std::endl;

    std::map<size_t, std::string> map_options = std::map<size_t, std::string>();

    index = 0;
    if (electionJson.is_array()) {
        std::for_each(electionJson.begin(), electionJson.end(), [&map_options, &index](const nlohmann::json &option) {
            map_options[index] = option.dump();
            index++;
            std::cout << option.dump() << std::endl;
        });
    }

    election initialElectionState = election::create(election_id)
            .withSequenceNumber(0)
            .withVoteOptions(map_options);

    _logger.log("show election");
    initialElectionState.print();

    return initialElectionState;
}

void peer::importPeerIdentity(std::string importPath) {
    std::ifstream importStream;
    std::string line;

    _logger.log("is importing peers file from " + importPath + "id.dat");

    // File Open in the Read Mode
    importStream.open(importPath + "id.dat");

    if (importStream.is_open()) {
        if (getline(importStream, line)) {
            _logger.log("Display File Content:");
            _logger.displayData(line);

            peer_identity = line;
            peer_address = line;

        };
        // File Close
        importStream.close();
        _logger.error("Could not return line");
    } else {
        _logger.error("Unable to open the file!");
    }
}


void peer::dumpElectionBox() {
    std::for_each(election_box.begin(), election_box.end(), [](election electionEntry) {
        electionEntry.print();
    });
}

void peer::passiveDistribution(void *context, straightLineDistributeThread &thread) {
    thread.setInitialDistributer(false);
    thread.setContext(context);
    updateDistributionThread(&thread);
    thread.setIsRunning(true);
    thread.StartInternalThread();
}

size_t peer::selectElection() {
    _logger.promptUserInput("Select which election to distribute by id");
    std::cout << std::endl;
    size_t idx = 0;
    std::for_each(election_box.begin(), election_box.end(), [&idx, this](const election &current_election) {
        this->_logger.displayData("[" + std::to_string(idx) + "]: " + current_election.getElectionOptionsJson().dump() +
                                  ", with election_id="
                                  + std::to_string(current_election.getPollId()));
        idx++;
    });
    std::string input_string;
    size_t selected_election_id;

    while (true && input_string != "exit") {
        std::getline(std::cin, input_string);
        selected_election_id = std::stoi(input_string);
        std::cout << std::endl;
        if (isNumber(input_string) && selected_election_id < election_box.size()) {
            return selected_election_id;
        }
    }
    _logger.log("Exit executable during election selection");
    std::exit(10);
}

void peer::distributeElection(void *context, straightLineDistributeThread &thread) {

    size_t selected_election_index = selectElection();
    election &chosen_election = election_box[selected_election_index];
    if (!chosen_election.isPreparedForDistribution()) {
        chosen_election.prepareForDistribtion(known_peer_addresses);
    }

    std::map<std::string, std::string> reversedConnectionTable;
    std::for_each(connection_table.begin(), connection_table.end(),
                  [&reversedConnectionTable](std::pair<std::string, std::string> addressToAddress) {
                      reversedConnectionTable[addressToAddress.second] = addressToAddress.first;
                  });

    std::string address_up;
    if (connection_table.contains(peer_address)) {
        address_up = connection_table[peer_address];
    }

    std::string address_down;
    if (reversedConnectionTable.contains(peer_address)) {
        address_down = reversedConnectionTable[peer_address];
    }

    calculatePositionFromTable();

    thread.setInitialDistributer(true);
    thread.setParams(context, address_up, address_down, position, known_peer_addresses.size(), chosen_election);
    thread.StartInternalThread();
    _logger.log("Distributing Election, wait for thread to exist", "localhost", "main");
    thread.WaitForInternalThreadToExit();
}

void peer::calculatePositionFromTable() {
    std::map<std::string, std::string> reversed_connection_table;
    auto connection_table_temp = connection_table;

    std::for_each(connection_table.begin(), connection_table.end(),
                  [&reversed_connection_table](std::pair<std::string, std::string> pair) {
                      reversed_connection_table[pair.second] = pair.first;
                  });

    std::string root_address;

    std::for_each(reversed_connection_table.begin(), reversed_connection_table.end(),
                  [&connection_table_temp, &root_address](std::pair<std::string, std::string> pair) {
                      if (!connection_table_temp.contains(pair.first)) {
                          root_address = pair.first;
                      }
                  });

    size_t pos = getAndIncrement(peer_address, root_address, reversed_connection_table, 0);

    this->position = pos;
    _logger.log("Calculated position for " + peer_address + " is " + std::to_string(pos));
}

size_t peer::getAndIncrement(std::string self_address, std::string current_address,
                             std::map<std::string, std::string> &connection_table, size_t current_position) {

    if (current_address == self_address || !connection_table.contains(current_address)) {
        return current_position;
    } else {
        current_position++;
        return getAndIncrement(self_address, connection_table[current_address], connection_table, current_position);
    }
}

bool peer::isNumber(const std::string s) {
    for (char const &ch: s) {
        if (std::isdigit(ch) == 0)
            return false;
    }
    return true;
}

void peer::pushBackElection(election election) {
    election_box.push_back(election);
}

void peer::startInprocElectionSyncThread(void *context, inprocElectionboxThread &thread) {
    _logger.log("starting election inproc");
    thread.StartInternalThread();
}

std::vector<election> &peer::getElectionBox() {
    return election_box;
}

void peer::updateDistributionThread(straightLineDistributeThread *p_thread) {
    calculatePositionFromTable();
    std::map<std::string, std::string> reversedConnectionTable;
    std::for_each(connection_table.begin(), connection_table.end(),
                  [&reversedConnectionTable](std::pair<std::string, std::string> addressToAddress) {
                      reversedConnectionTable[addressToAddress.second] = addressToAddress.first;
                  });

    std::string address_up;
    if (connection_table.contains(peer_address)) {
        address_up = connection_table[peer_address];
    }

    std::string address_down;
    if (reversedConnectionTable.contains(peer_address)) {
        address_down = reversedConnectionTable[peer_address];
    }

    p_thread->setAddressDown(address_down);
    p_thread->setAddressUp(address_up);
    p_thread->setPosition(position);
    p_thread->setNetworkSize(known_peer_addresses.size());
}

void peer::decryptVote(election election, unsigned char *ciphertext, unsigned char *decry) {
    unsigned long long decryptLength = election.getOptions().size() / 10 + 1;

    unsigned char * decrypted = decry;
    
    const unsigned char *ciphertext_cstr = ciphertext;
    const unsigned char *key = reinterpret_cast<const unsigned char *>(electionKeys[election.getPollId()].c_str());

    if (crypto_aead_chacha20poly1305_ietf_decrypt(decrypted, &decryptLength, NULL, ciphertext_cstr, decryptLength + crypto_aead_chacha20poly1305_IETF_ABYTES,
                                                  ADDITIONAL_DATA, ADDITIONAL_DATA_LEN, nonce, key) != 0) {
        /* message forged! */
    }
}

void peer::encryptVote(election &selected_election, std::string vote, unsigned char *encry) {

    const size_t options_pot = ((int) selected_election.getOptions().size()) / 10 + 1;
    unsigned char key[crypto_aead_chacha20poly1305_IETF_KEYBYTES];
    unsigned char * ciphertext[options_pot + crypto_aead_chacha20poly1305_IETF_ABYTES];
    unsigned char * encoded = encry;
    unsigned long long ciphertext_len = vote.length() + crypto_aead_chacha20poly1305_IETF_ABYTES;

    crypto_aead_chacha20poly1305_ietf_keygen(key);
    randombytes_buf(nonce, sizeof nonce);

    std::cout << "Vote: " << vote << std::endl;

    const unsigned char *vote_str = reinterpret_cast<const unsigned char *>(vote.c_str());

    std::cout << "VoteStr: " << vote_str << std::endl;
    std::cout << "Nonce: "<< nonce << std::endl;
    std::cout << "Key: "<< key << std::endl;

    crypto_aead_chacha20poly1305_ietf_encrypt(reinterpret_cast<unsigned char *>(ciphertext), &ciphertext_len, vote_str, vote.length(), ADDITIONAL_DATA,
                                              ADDITIONAL_DATA_LEN, NULL, nonce, key);

    electionKeys[selected_election.getPollId()] = reinterpret_cast<const char *>(key);


    sodium_bin2base64(reinterpret_cast<char *const>(encoded), 64,
                      reinterpret_cast<const unsigned char *const>(ciphertext),
                      ciphertext_len, sodium_base64_VARIANT_ORIGINAL);

    std::cout << "Ciphertext: "<< ciphertext << std::endl;
    std::cout << "Base64: "<< encoded << std::endl;
}

void peer::eval_votes() {
    size_t chosen_id = selectElection();
    election &chosen_election = election_box.at(chosen_id);

    if(chosen_election.hasFreeEvaluationGroups()){

    }
}
