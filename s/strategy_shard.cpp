/*
 *    Copyright (C) 2010 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
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
 */

// strategy_sharded.cpp

#include "pch.h"
#include "request.h"
#include "chunk.h"
#include "cursors.h"
#include "stats.h"
#include "client.h"

#include "../client/connpool.h"
#include "../db/commands.h"

// error codes 8010-8040

namespace mongo {

    class ShardStrategy : public Strategy {

        virtual void queryOp( Request& r ) {

            // TODO: These probably should just be handled here.
            if ( r.isCommand() ) {
                SINGLE->queryOp( r );
                return;
            }

            QueryMessage q( r.d() );

            r.checkAuth( Auth::READ );

            LOG(3) << "shard query: " << q.ns << "  " << q.query << endl;

            if ( q.ntoreturn == 1 && strstr(q.ns, ".$cmd") )
                throw UserException( 8010 , "something is wrong, shouldn't see a command here" );

            QuerySpec qSpec( (string)q.ns, q.query, q.fields, q.ntoskip, q.ntoreturn, q.queryOptions );

            ParallelSortClusteredCursor * cursor = new ParallelSortClusteredCursor( qSpec, CommandInfo() );
            assert( cursor );

            // TODO:  Move out to Request itself, not strategy based
            try {
                long long start_millis = 0;
                if ( qSpec.isExplain() ) start_millis = curTimeMillis64();
                cursor->init();

                LOG(5) << "   cursor type: " << cursor->type() << endl;
                shardedCursorTypes.hit( cursor->type() );

                if ( qSpec.isExplain() ) {
                    // fetch elapsed time for the query
                    long long elapsed_millis = curTimeMillis64() - start_millis;
                    BSONObjBuilder explain_builder;
                    cursor->explain( explain_builder );
                    explain_builder.appendNumber( "millis", elapsed_millis );
                    BSONObj b = explain_builder.obj();

                    replyToQuery( 0 , r.p() , r.m() , b );
                    delete( cursor );
                    return;
                }
            }
            catch(...) {
                delete cursor;
                throw;
            }

            if( cursor->isSharded() ){
                ShardedClientCursorPtr cc (new ShardedClientCursor( q , cursor ));

                if ( ! cc->sendNextBatch( r ) ) {
                    return;
                }

                LOG(6) << "storing cursor : " << cc->getId() << endl;
                cursorCache.store( cc );
            }
            else{
                // TODO:  Better merge this logic.  We potentially can now use the same cursor logic for everything.
                ShardPtr primary = cursor->getPrimary();
                DBClientCursorPtr shardCursor = cursor->getShardCursor( *primary );
                r.reply( *(shardCursor->getMessage()) , primary->getConnString() );
            }
        }

        virtual void getMore( Request& r ) {

            // TODO:  Handle stale config exceptions here from coll being dropped or sharded during op
            // for now has same semantics as legacy request
            ChunkManagerPtr info = r.getChunkManager();

            if( ! info ){
                SINGLE->getMore( r );
                return;
            }
            else {
                int ntoreturn = r.d().pullInt();
                long long id = r.d().pullInt64();

                LOG(6) << "want cursor : " << id << endl;

                ShardedClientCursorPtr cursor = cursorCache.get( id );
                if ( ! cursor ) {
                    LOG(6) << "\t invalid cursor :(" << endl;
                    replyToQuery( ResultFlag_CursorNotFound , r.p() , r.m() , 0 , 0 , 0 );
                    return;
                }

                if ( cursor->sendNextBatch( r , ntoreturn ) ) {
                    // still more data
                    cursor->accessed();
                    return;
                }

                // we've exhausted the cursor
                cursorCache.remove( id );
            }
        }

        void _insert( Request& r , DbMessage& d, ChunkManagerPtr manager ) {
            const int flags = d.reservedField() | InsertOption_ContinueOnError; // ContinueOnError is always on when using sharding.
            map<ChunkPtr, vector<BSONObj> > insertsForChunk; // Group bulk insert for appropriate shards
            try {
                while ( d.moreJSObjs() ) {
                    BSONObj o = d.nextJsObj();
                    if ( ! manager->hasShardKey( o ) ) {

                        bool bad = true;

                        if ( manager->getShardKey().partOfShardKey( "_id" ) ) {
                            BSONObjBuilder b;
                            b.appendOID( "_id" , 0 , true );
                            b.appendElements( o );
                            o = b.obj();
                            bad = ! manager->hasShardKey( o );
                        }

                        if ( bad ) {
                            log() << "tried to insert object with no valid shard key: " << r.getns() << "  " << o << endl;
                            uasserted( 8011 , "tried to insert object with no valid shard key" );
                        }

                    }

                    // Many operations benefit from having the shard key early in the object
                    o = manager->getShardKey().moveToFront(o);
                    insertsForChunk[manager->findChunk(o)].push_back(o);
                }
                for (map<ChunkPtr, vector<BSONObj> >::iterator it = insertsForChunk.begin(); it != insertsForChunk.end(); ++it) {
                    ChunkPtr c = it->first;
                    vector<BSONObj> objs = it->second;
                    const int maxTries = 30;

                    bool gotThrough = false;
                    for ( int i=0; i<maxTries; i++ ) {
                        try {
                            LOG(4) << "  server:" << c->getShard().toString() << " bulk insert " << objs.size() << " documents" << endl;
                            insert( c->getShard() , r.getns() , objs , flags);

                            int bytesWritten = 0;
                            for (vector<BSONObj>::iterator vecIt = objs.begin(); vecIt != objs.end(); ++vecIt) {
                                r.gotInsert(); // Record the correct number of individual inserts
                                bytesWritten += (*vecIt).objsize();
                            }
                            if ( r.getClientInfo()->autoSplitOk() )
                                c->splitIfShould( bytesWritten );
                            gotThrough = true;
                            break;
                        }
                        catch ( StaleConfigException& e ) {
                            int logLevel = i < ( maxTries / 2 );
                            LOG( logLevel ) << "retrying bulk insert of " << objs.size() << " documents because of StaleConfigException: " << e << endl;
                            r.reset();

                            manager = r.getChunkManager();
                            if( ! manager ) {
                                uasserted(14804, "collection no longer sharded");
                            }

                            unsigned long long old = manager->getSequenceNumber();
                            
                            LOG( logLevel ) << "  sequence number - old: " << old << " new: " << manager->getSequenceNumber() << endl;
                        }
                        sleepmillis( i * 20 );
                    }

                    assert( inShutdown() || gotThrough ); // not caught below
                }
            } catch (const UserException&){
                if (!d.moreJSObjs()){
                    throw;
                }
                // Ignore and keep going. ContinueOnError is implied with sharding.
            }
        }

        void _update( Request& r , DbMessage& d, ChunkManagerPtr manager ) {
            int flags = d.pullInt();

            BSONObj query = d.nextJsObj();
            uassert( 13506 ,  "$atomic not supported sharded" , query["$atomic"].eoo() );
            uassert( 10201 ,  "invalid update" , d.moreJSObjs() );
            BSONObj toupdate = d.nextJsObj();
            BSONObj chunkFinder = query;

            bool upsert = flags & UpdateOption_Upsert;
            bool multi = flags & UpdateOption_Multi;

            if (upsert) {
                uassert(8012, "can't upsert something without valid shard key",
                        (manager->hasShardKey(toupdate) ||
                         (toupdate.firstElementFieldName()[0] == '$' && manager->hasShardKey(query))));

                BSONObj key = manager->getShardKey().extractKey(query);
                BSONForEach(e, key) {
                    uassert(13465, "shard key in upsert query must be an exact match", getGtLtOp(e) == BSONObj::Equality);
                }
            }

            bool save = false;
            if ( ! manager->hasShardKey( query ) ) {
                if ( multi ) {
                }
                else if ( strcmp( query.firstElementFieldName() , "_id" ) || query.nFields() != 1 ) {
                    log() << "Query " << query << endl;
                    throw UserException( 8013 , "can't do non-multi update with query that doesn't have a valid shard key" );
                }
                else {
                    save = true;
                    chunkFinder = toupdate;
                }
            }


            if ( ! save ) {
                if ( toupdate.firstElementFieldName()[0] == '$' ) {
                    BSONObjIterator ops(toupdate);
                    while(ops.more()) {
                        BSONElement op(ops.next());
                        if (op.type() != Object)
                            continue;
                        BSONObjIterator fields(op.embeddedObject());
                        while(fields.more()) {
                            const string field = fields.next().fieldName();
                            uassert(13123,
                                    str::stream() << "Can't modify shard key's value field" << field
                                    << " for collection: " << manager->getns(),
                                    ! manager->getShardKey().partOfShardKey(field));
                        }
                    }
                }
                else if ( manager->hasShardKey( toupdate ) ) {
                    uassert( 8014,
                             str::stream() << "cannot modify shard key for collection: " << manager->getns(),
                             manager->getShardKey().compare( query , toupdate ) == 0 );
                }
                else {
                    uasserted(12376,
                              str::stream() << "valid shard key must be in update object for collection: " << manager->getns() );
                }
            }

            if ( multi ) {
                set<Shard> shards;
                manager->getShardsForQuery( shards , chunkFinder );
                int * x = (int*)(r.d().afterNS());
                x[0] |= UpdateOption_Broadcast;
                for ( set<Shard>::iterator i=shards.begin(); i!=shards.end(); i++) {
                    doWrite( dbUpdate , r , *i , false );
                }
            }
            else {
                int left = 5;
                while ( true ) {
                    try {
                        ChunkPtr c = manager->findChunk( chunkFinder );
                        doWrite( dbUpdate , r , c->getShard() );
                        if ( r.getClientInfo()->autoSplitOk() )
                            c->splitIfShould( d.msg().header()->dataLen() );
                        break;
                    }
                    catch ( StaleConfigException& e ) {
                        if ( left <= 0 )
                            throw e;
                        left--;
                        log() << "update will be retried b/c sharding config info is stale, "
                              << " left:" << left << " ns: " << r.getns() << " query: " << query << endl;
                        r.reset();
                        manager = r.getChunkManager();
                        uassert(14806, "collection no longer sharded", manager);
                    }
                }
            }
        }

        void _delete( Request& r , DbMessage& d, ChunkManagerPtr manager ) {

            int flags = d.pullInt();
            bool justOne = flags & 1;

            uassert( 10203 ,  "bad delete message" , d.moreJSObjs() );
            BSONObj pattern = d.nextJsObj();
            uassert( 13505 ,  "$atomic not supported sharded" , pattern["$atomic"].eoo() );

            set<Shard> shards;
            int left = 5;

            while ( true ) {
                try {
                    manager->getShardsForQuery( shards , pattern );
                    LOG(2) << "delete : " << pattern << " \t " << shards.size() << " justOne: " << justOne << endl;
                    if ( shards.size() == 1 ) {
                        doWrite( dbDelete , r , *shards.begin() );
                        return;
                    }
                    break;
                }
                catch ( StaleConfigException& e ) {
                    if ( left <= 0 )
                        throw e;
                    left--;
                    log() << "delete failed b/c of StaleConfigException, retrying "
                          << " left:" << left << " ns: " << r.getns() << " patt: " << pattern << endl;
                    r.reset();
                    shards.clear();
                    manager = r.getChunkManager();
                    uassert(14805, "collection no longer sharded", manager);
                }
            }

            if ( justOne && ! pattern.hasField( "_id" ) )
                throw UserException( 8015 , "can only delete with a non-shard key pattern if can delete as many as we find" );

            for ( set<Shard>::iterator i=shards.begin(); i!=shards.end(); i++) {
                int * x = (int*)(r.d().afterNS());
                x[0] |= RemoveOption_Broadcast;
                doWrite( dbDelete , r , *i , false );
            }
        }

        virtual void writeOp( int op , Request& r ) {

            // TODO:  Handle stale config exceptions here from coll being dropped or sharded during op
            // for now has same semantics as legacy request
            ChunkManagerPtr info = r.getChunkManager();

            if( ! info ){
                SINGLE->writeOp( op, r );
                return;
            }
            else{
                const char *ns = r.getns();
                LOG(3) << "write: " << ns << endl;

                DbMessage& d = r.d();

                if ( op == dbInsert ) {
                    _insert( r , d , info );
                }
                else if ( op == dbUpdate ) {
                    _update( r , d , info );
                }
                else if ( op == dbDelete ) {
                    _delete( r , d , info );
                }
                else {
                    log() << "sharding can't do write op: " << op << endl;
                    throw UserException( 8016 , "can't do this write op on sharded collection" );
                }
                return;
            }
        }

    };

    Strategy * SHARDED = new ShardStrategy();
}
