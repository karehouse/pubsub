/**
 *    Copyright (C) 2014 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <zmq.hpp>

#include "mongo/util/net/hostandport.h"

namespace mongo {

    // Server Parameters signaling whether pubsub and db events are enabled
    // pubsub defaults to true, dbevents to false
    extern bool pubsubEnabled;
    extern bool publishDataEvents;

    class PubSubSendSocket {
    public:
        // for locking around publish, because it uses a non-thread-safe zmq socket
        static SimpleMutex sendMutex;
        static zmq::context_t zmqContext;
        static zmq::socket_t* dbEventSocket;

        static bool publish(const std::string& channel, const BSONObj& message);
        static void initSharding(const std::string configServers);

        // methods that update which members of a replica set are still connected.
        // updateReplSetMember() adds members to the set if they are not yet connected
        // or marks them as still in use. pruneReplSetMembers() then disconnects from
        // any members who are no longer in the replica set. both called from repl/rs.cpp
        static void updateReplSetMember(HostAndPort hp);
        static void pruneReplSetMembers();

        // zmq PUB socket connected to replica set members' SUB sockets
        static zmq::socket_t* extSendSocket;

        // list of other replica set members we are connected to for pubsub
        // bool is set to indicate live or not live during each call to initFromConfig
        // after which pruneReplSetMembers (above) removes the not live members
        static std::map<HostAndPort, bool> rsMembers;
    };

}

