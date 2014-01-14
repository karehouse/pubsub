/// compact.cpp

/**
*    Copyright (C) 2013 10gen Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,b
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

#include <string>
#include <vector>

#include "mongo/db/auth/action_set.h"
#include "mongo/db/auth/action_type.h"
#include "mongo/db/auth/privilege.h"
#include "mongo/db/background.h"
#include "mongo/db/commands.h"
#include "mongo/db/catalog/database.h"
#include "mongo/db/d_concurrency.h"
#include "mongo/db/curop-inl.h"
#include "mongo/db/index_builder.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/kill_current_op.h"
#include "mongo/db/catalog/collection.h"

namespace mongo {

    // from repl/rs.cpp
    bool isCurrentlyAReplSetPrimary();

    class CompactCmd : public Command {
    public:
        virtual LockType locktype() const { return NONE; }
        virtual bool adminOnly() const { return false; }
        virtual bool slaveOk() const { return true; }
        virtual bool maintenanceMode() const { return true; }
        virtual bool logTheOp() { return false; }
        virtual void addRequiredPrivileges(const std::string& dbname,
                                           const BSONObj& cmdObj,
                                           std::vector<Privilege>* out) {
            ActionSet actions;
            actions.addAction(ActionType::compact);
            out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
        }
        virtual void help( stringstream& help ) const {
            help << "compact collection\n"
                "warning: this operation blocks the server and is slow. you can cancel with cancelOp()\n"
                "{ compact : <collection_name>, [force:<bool>], [validate:<bool>],\n"
                "  [paddingFactor:<num>], [paddingBytes:<num>] }\n"
                "  force - allows to run on a replica set primary\n"
                "  validate - check records are noncorrupt before adding to newly compacting extents. slower but safer (defaults to true in this version)\n";
        }
        CompactCmd() : Command("compact") { }

        virtual std::vector<BSONObj> stopIndexBuilds(const std::string& dbname,
                                                     const BSONObj& cmdObj) {
            std::string systemIndexes = dbname+".system.indexes";
            std::string coll = cmdObj.firstElement().valuestr();
            std::string ns = dbname + "." + coll;
            BSONObj criteria = BSON("ns" << systemIndexes << "op" << "insert" << "insert.ns" << ns);

            return IndexBuilder::killMatchingIndexBuilds(criteria);
        }

        virtual bool run(const string& db, BSONObj& cmdObj, int, string& errmsg, BSONObjBuilder& result, bool fromRepl) {
            string coll = cmdObj.firstElement().valuestr();
            if( coll.empty() || db.empty() ) {
                errmsg = "no collection name specified";
                return false;
            }

            if( isCurrentlyAReplSetPrimary() && !cmdObj["force"].trueValue() ) {
                errmsg = "will not run compact on an active replica set primary as this is a slow blocking operation. use force:true to force";
                return false;
            }

            NamespaceString ns(db,coll);
            if ( !ns.isNormal() ) {
                errmsg = "bad namespace name";
                return false;
            }

            if ( ns.isSystem() ) {
                // items in system.* cannot be moved as there might be pointers to them
                // i.e. system.indexes entries are pointed to from NamespaceDetails
                errmsg = "can't compact a system namespace";
                return false;
            }

            CompactOptions compactOptions;

            if ( cmdObj["preservePadding"].trueValue() ) {
                compactOptions.paddingMode = CompactOptions::PRESERVE;
                if ( cmdObj.hasElement( "paddingFactor" ) ||
                     cmdObj.hasElement( "paddingBytes" ) ) {
                    errmsg = "cannot mix preservePadding and paddingFactor|paddingBytes";
                    return false;
                }
            }
            else if ( cmdObj.hasElement( "paddingFactor" ) || cmdObj.hasElement( "paddingBytes" ) ) {
                compactOptions.paddingMode = CompactOptions::MANUAL;
                if ( cmdObj.hasElement("paddingFactor") ) {
                    compactOptions.paddingFactor = cmdObj["paddingFactor"].Number();
                    if ( compactOptions.paddingFactor < 1 ||
                         compactOptions.paddingFactor > 4 ){
                        errmsg = "invalid padding factor";
                        return false;
                    }
                }
                if ( cmdObj.hasElement("paddingBytes") ) {
                    compactOptions.paddingBytes = cmdObj["paddingBytes"].numberInt();
                    if ( compactOptions.paddingBytes < 0 ||
                         compactOptions.paddingBytes > ( 1024 * 1024 ) ) {
                        errmsg = "invalid padding bytes";
                        return false;
                    }
                }
            }

            if ( cmdObj.hasElement("validate") )
                compactOptions.validateDocuments = cmdObj["validate"].trueValue();


            Lock::DBWrite lk(ns.ns());
            BackgroundOperation::assertNoBgOpInProgForNs(ns.ns());
            Client::Context ctx(ns);

            Collection* collection = ctx.db()->getCollection(ns.ns());
            if( ! collection ) {
                errmsg = "namespace does not exist";
                return false;
            }

            if ( collection->isCapped() ) {
                errmsg = "cannot compact a capped collection";
                return false;
            }

            log() << "compact " << ns << " begin, options: " << compactOptions.toString();

            std::vector<BSONObj> indexesInProg = stopIndexBuilds(db, cmdObj);

            StatusWith<CompactStats> status = collection->compact( &compactOptions );
            if ( !status.isOK() )
                return appendCommandStatus( result, status.getStatus() );

            if ( status.getValue().corruptDocuments > 0 )
                result.append("invalidObjects", status.getValue().corruptDocuments );

            log() << "compact " << ns << " end";

            IndexBuilder::restoreIndexes(indexesInProg);

            return true;
        }
    };
    static CompactCmd compactCmd;

}
