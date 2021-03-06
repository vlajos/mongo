// @file mutexdebugger.h

/**
*    Copyright (C) 2012 10gen Inc.
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

#pragma once

#include "mongo/client/undef_macros.h"

#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "boost/thread/mutex.hpp"

#include "mongo/client/redef_macros.h"

#include "mongo/util/assert_util.h"

namespace mongo {

    /** only used on _DEBUG builds.
        MutexDebugger checks that we always acquire locks for multiple mutexes in a consistent (acyclic) order.
        If we were inconsistent we could deadlock.
    */
    class MutexDebugger {
        typedef const char * mid; // mid = mutex ID
        typedef std::map<mid,int> Preceding;
        std::map< mid, int > maxNest;
        boost::thread_specific_ptr< Preceding > us;
        std::map< mid, std::set<mid> > followers;
        boost::mutex &x;
        unsigned magic;
        void aBreakPoint() { } // for debugging
    public:
        // set these to create an assert that
        //   b must never be locked before a
        //   so
        //     a.lock(); b.lock(); is fine
        //     b.lock(); alone is fine too
        //   only checked on _DEBUG builds.
        std::string a,b;

        /** outputs some diagnostic info on mutexes (on _DEBUG builds) */
        void programEnding();

        MutexDebugger();

        std::string currentlyLocked() const { 
            Preceding *_preceding = us.get();
            if( _preceding == 0 )
                return "";
            Preceding &preceding = *_preceding;
            std::stringstream q;
            for( Preceding::const_iterator i = preceding.begin(); i != preceding.end(); i++ ) {
                if( i->second > 0 )
                    q << "  " << i->first << ' ' << i->second << '\n';
            }
            return q.str();
        }

        void entering(mid m) {
            if( this == 0 || m == 0 ) return;
            verify( magic == 0x12345678 );

            Preceding *_preceding = us.get();
            if( _preceding == 0 )
                us.reset( _preceding = new Preceding() );
            Preceding &preceding = *_preceding;

            if( a == m ) {
                aBreakPoint();
                if( preceding[b.c_str()] ) {
                    std::cout << "****** MutexDebugger error! warning " << b << " was locked before " << a << std::endl;
                    verify(false);
                }
            }

            preceding[m]++;
            if( preceding[m] > 1 ) {
                // recursive re-locking.
                if( preceding[m] > maxNest[m] )
                    maxNest[m] = preceding[m];
                return;
            }

            bool failed = false;
            std::string err;
            {
                boost::mutex::scoped_lock lk(x);
                followers[m];
                for( Preceding::iterator i = preceding.begin(); i != preceding.end(); i++ ) {
                    if( m != i->first && i->second > 0 ) {
                        followers[i->first].insert(m);
                        if( followers[m].count(i->first) != 0 ) {
                            failed = true;
                            std::stringstream ss;
                            mid bad = i->first;
                            ss << "mutex problem" <<
                               "\n  when locking " << m <<
                               "\n  " << bad << " was already locked and should not be."
                               "\n  set a and b above to debug.\n";
                            std::stringstream q;
                            for( Preceding::iterator i = preceding.begin(); i != preceding.end(); i++ ) {
                                if( i->first != m && i->first != bad && i->second > 0 )
                                    q << "  " << i->first << '\n';
                            }
                            std::string also = q.str();
                            if( !also.empty() )
                                ss << "also locked before " << m << " in this thread (no particular order):\n" << also;
                            err = ss.str();
                            break;
                        }
                    }
                }
            }
            if( failed ) {
                std::cout << err << std::endl;
                verify( 0 );
            }
        }
        void leaving(mid m) {
            if( this == 0 || m == 0 ) return; // still in startup pre-main()
            Preceding& preceding = *us.get();
            preceding[m]--;
            if( preceding[m] < 0 ) {
                std::cout << "ERROR: lock count for " << m << " is " << preceding[m] << std::endl;
                verify( preceding[m] >= 0 );
            }
        }
    };
    extern MutexDebugger &mutexDebugger;

}
