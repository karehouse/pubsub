/* jsobj.h

   BSONObj and its helpers

   "BSON" stands for "binary JSON" -- ie a binary way to represent objects that would be
   represented in JSON (plus a few extensions useful for databases & other languages).
*/

/**
*    Copyright (C) 2008 10gen Inc.
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

#include "../stdafx.h"
#include "../util/builder.h"
#include "boost/utility.hpp"

#include <set>

class BSONObj;
class Record;
class BSONObjBuilder;

#pragma pack(push,1)

/* BinData = binary data types. 
   EOO = end of object
*/
enum BSONType { EOO = 0, NumberDouble=1, String=2, Object=3, Array=4, BinData=5, 
              Undefined=6, jstOID=7, Bool=8, Date=9 , jstNULL=10, RegEx=11 ,
              DBRef=12, Code=13, Symbol=14, CodeWScope=15 , 
              NumberInt = 16, 
              JSTypeMax=16, 
              MaxKey=127

};

/* subtypes of BinData.
   bdtCustom and above are ones that the JS compiler understands, but are
   opaque to the database.
*/
enum BinDataType { Function=1, ByteArray=2, bdtCustom=128 };

/*	Object id's are optional for BSONObjects.  
	When present they should be the first object member added.
*/
struct OID { 
	long long a;
	unsigned b;
	bool operator==(const OID& r) { return a==r.a&&b==r.b; }
	void out(){ cout << hex << a << hex << b << endl; };
};

/* marshalled js object format:

   <unsigned totalSize> {<byte BSONType><cstring FieldName><Data>}* EOO
      totalSize includes itself.

   Data:
     Bool:      <byte>
     EOO:       nothing follows
     Undefined: nothing follows
     OID:       an OID object
     NumberDouble: <double>
     NumberInt: <int32>
     String:    <unsigned32 strsizewithnull><cstring>
     Date:      <8bytes>
     Regex:     <cstring regex><cstring options>
     Object:    a nested object, leading with its entire size, which terminates with EOO.
     Array:     same as object
     DBRef:     <strlen> <cstring ns> <oid>
                DBRef is a database reference: basically a collection name plus an Object ID
     BinData:   <int len> <byte subtype> <byte[len] data>
     Code:      a function (not a closure): same format as String.
     Symbol:    a language symbol (say a python symbol).  same format as String.
     Code With Scope: <total size><String><Object>
*/

#pragma pack(pop)

/* <type><fieldName    ><value> 
   -------- size() ------------
         -fieldNameSize-
                        value()
   type()
*/
class BSONElement {
	friend class BSONObjIterator;
	friend class BSONObj;
public:
	string toString();
    BSONType type() const { return (BSONType) *data; }
    bool isNumber() const { return type() == NumberDouble || type() == NumberInt; }
	bool eoo() const { return type() == EOO; }
	int size() const;

	// wrap this element up as a singleton object.
	BSONObj wrap();

	const char * fieldName() { 
		if( eoo() ) return ""; // no fieldname for it.
		return data + 1; 
	}

	// raw data be careful:
	const char * value() const { return (data + fieldNameSize + 1); }
	int valuesize() const { return size() - fieldNameSize - 1; } 

	bool boolean() { return *value() ? true : false; }

	unsigned long long date() const { return *((unsigned long long*) value()); }
  	//double& number() { return *((double *) value()); }

    void setNumber(double d) { 
        if( type() == NumberDouble ) *((double *) value()) = d;
        else if( type() == NumberInt ) *((int *) value()) = (int) d;
    }

	double number() const { 
        if( type() == NumberDouble ) return *((double *) value()); 
        if( type() == NumberInt ) return *((int *) value()); 
        return 0;
    }
	OID& oid() { return *((OID*) value()); }

	// for strings
	int valuestrsize() const { 
		return *((int *) value());
	}

	// for objects the size *includes* the size of the size field
	int objsize() const { 
		return *((int *) value());
	}

	// for strings.  also gives you start of the real data for an embedded object
	const char * valuestr() const { return value() + 4; }

    const char *valuestrsafe() const { 
        return type() == String ? valuestr() : ""; 
    }

	const char * codeWScopeCode() const { return value() + 8; }
	const char * codeWScopeScopeData() const { 
	  // TODO fix
	  return codeWScopeCode() + strlen( codeWScopeCode() ) + 1;
	}
	
	BSONObj embeddedObject();

    /* uassert if not an object */
	BSONObj embeddedObjectUserCheck();

	const char *regex() { assert(type() == RegEx); return value(); }
	const char *regexFlags() { 
		const char *p = regex();
		return p + strlen(p) + 1;
	}

	/* like operator== but doesn't check the fieldname,
	   just the value.
	   */
    bool valuesEqual(BSONElement& r) {
        if( isNumber() )
            return number() == r.number() && r.isNumber();
		bool match= valuesize() == r.valuesize() && 
			memcmp(value(),r.value(),valuesize()) == 0;
		return match;
		// todo: make "0" == 0.0, undefined==null
	}

	bool operator==(BSONElement& r) { 
		if( strcmp(fieldName(), r.fieldName()) != 0 )
			return false;
		return valuesEqual(r);
/*
		int sz = size();
		return sz == r.size() && 
			memcmp(data, r.data, sz) == 0;
*/
	}

	const char * rawdata() { return data; }

	BSONElement();

private:
	BSONElement(const char *d) : data(d) {
		fieldNameSize = eoo() ? 0 : strlen(fieldName()) + 1;
		totalSize = -1;
	}
	const char *data;
	int fieldNameSize;
	int totalSize; /* caches the computed size */
};

/* l and r MUST have same type when called: check that first. */
int compareElementValues(const BSONElement& l, const BSONElement& r);

class BSONObj {
	friend class BSONObjIterator;
	class Details {
	public:
		~Details() {
			// note refCount means two different things (thus the assert here)
			assert(refCount <= 0);
			if (owned()) {
				free((void *)_objdata);
			}
			_objdata = 0;
		}
		const char *_objdata;
		int _objsize;
		int refCount; // -1 == don't free (we don't "own" the buffer)
		bool owned() { return refCount >= 0; }
	} *details;
	void init(const char *data, bool ifree) { 
		details = new Details();
		details->_objdata = data;
		details->_objsize = *((int*) data);
		assert( details->_objsize > 0 );
		assert( details->_objsize <= 1024 * 1024 * 16 );
		details->refCount = ifree ? 1 : -1;
	}
public:
	explicit BSONObj(const char *msgdata, bool ifree = false) {
		init(msgdata, ifree);
	}
	BSONObj(Record *r);
	BSONObj() : details(0) { }
	~BSONObj() { 
		if( details ) {
			if( --details->refCount <= 0 )
				delete details;
			details = 0;
		}
	}

	void appendSelfToBufBuilder(BufBuilder& b) { 
		assert( objsize() );
		b.append((void *) objdata(), objsize());
	}

	/* switch the buffer's ownership to us. */
	void iWillFree() { 
		assert( !details->owned() );
		details->refCount = 1;
	}

	string toString() const;
	/* note: addFields always adds _id even if not specified */
	int addFields(BSONObj& from, set<string>& fields); /* returns n added */

    /* adds the field names to the fields set.  does NOT clear it (appends). */
	int getFieldNames(set<string>& fields);

	/* return has eoo() true if no match 
	   supports "." notation to reach into embedded objects
	*/
	BSONElement getFieldDotted(const char *name); 

	BSONElement getField(const char *name); /* return has eoo() true if no match */

	// returns "" if DNE or wrong type
	const char * getStringField(const char *name);

	BSONObj getObjectField(const char *name);

    int getIntField(const char *name); // INT_MIN if not present

	bool getBoolField(const char *name);

	/* makes a new BSONObj with the fields specified in pattern.
       fields returned in the order they appear in pattern.
	   if any field missing, you get back an empty object overall.
	   */
	BSONObj extractFieldsDotted(BSONObj pattern, BSONObjBuilder& b); // this version, builder owns the returned obj buffer
    BSONObj extractFieldsUnDotted(BSONObj pattern, BSONObjBuilder& b);
	BSONObj extractFields(BSONObj &pattern);

	const char *objdata() const { return details->_objdata; }
	int objsize() const { return details ? details->_objsize : 0; } // includes the embedded size field
	bool isEmpty() const { return objsize() <= 5; }

    void dump() { 
        cout << hex;
        const char *p = objdata();
        for( int i = 0; i < objsize(); i++ ) { 
            cout << i << '\t' << (unsigned) *p;
            if( *p >= 'A' && *p <= 'z' )
                cout << '\t' << *p;
            cout << endl;
            p++;
        }
    }

	/* this is broken if elements aren't in the same order. */
	bool operator<(const BSONObj& r) const { return woCompare(r) < 0; }

	/* -1: l<r. 0:l==r. 1:l>r 
	   wo='well ordered'.  fields must be in same order in each object.
	*/
	int woCompare(const BSONObj& r) const;
	bool woEqual(const BSONObj& r) const { 
		int os = objsize();
		return os == r.objsize() &&
			(os == 0 || memcmp(objdata(),r.objdata(),os)==0);
	}
	bool operator==(const BSONObj& r) const { 
		return this->woEqual(r);
	}

	BSONElement firstElement() { 
		return BSONElement(objdata() + 4);
	}
	BSONElement findElement(const char *name);

	OID* getOID() {
		BSONElement e = firstElement();
		if( e.type() != jstOID )
			return 0;
		return &e.oid();
	}

	BSONObj(const BSONObj& r) { 
		if( r.details == 0 )
			details = 0;
		else if( r.details->owned() ) {
			details = r.details;
			details->refCount++;
		}
		else { 
			details = new Details(*r.details);
		}
	}
	BSONObj& operator=(const BSONObj& r) {
		if( details && details->owned() ) {
			if( --details->refCount == 0 )
				delete details;
		}

		if( r.details == 0 )
			details = 0;
		else if( r.details->owned() ) {
			details = r.details;
			details->refCount++;
		}
		else { 
			details = new Details(*r.details);
		}
		return *this;
	}

	/* makes a copy of the object.  Normally, a jsobj points to data "owned" 
	   by something else.  this is a useful way to get your own copy of the buffer 
	   data (which is freed when the new jsobj destructs).
	   */
	BSONObj copy();

	int hash() const {
		unsigned x = 0;
		const char *p = objdata();
		for( int i = 0; i < objsize(); i++ )
			x = x * 131 + p[i];
		return (x & 0x7fffffff) | 0x8000000; // must be > 0
	}

    // true unless corrupt
    bool valid() const;
};

class BSONObjBuilder { 
public:
	BSONObjBuilder(int initsize=512) : b(initsize) { b.skip(4); /*leave room for size field*/ }

	/* add all the fields from the object specified to this object */
	void appendElements(BSONObj x);

	void append(BSONElement& e) { 
        assert( !e.eoo() ); // do not append eoo, that would corrupt us. the builder auto appends when done() is called.
        b.append((void*) e.rawdata(), e.size()); 
    }

	/* append an element but with a new name */
	void appendAs(BSONElement& e, const char *as) { 
		b.append((char) e.type());
		b.append(as);
		b.append((void *) e.value(), e.valuesize());
	}

	/* add a subobject as a member */
	void append(const char *fieldName, BSONObj subObj) { 
		b.append((char) Object);
		b.append(fieldName);
		b.append((void *) subObj.objdata(), subObj.objsize());
	}

	void appendBool(const char *fieldName, int val) { 
		b.append((char) Bool);
		b.append(fieldName);
		b.append((char) (val?1:0));
	}
	void append(const char *fieldName, double n) { 
		b.append((char) NumberDouble);
		b.append(fieldName);
		b.append(n);
	}
	void appendOID(const char *fieldName, OID *oid = 0) { 
		b.append((char) jstOID);
		b.append(fieldName);
		b.append((long long) (oid ? oid->a : 0));
		b.append((unsigned) (oid ? oid->b : 0));
	}
	void appendDate(const char *fieldName, unsigned long long dt) { 
		b.append((char) Date);
		b.append(fieldName);
		b.append(dt);
	}
	void append(const char *fieldName, const char *str) {
		b.append((char) String);
		b.append(fieldName);
		b.append((int) strlen(str)+1);
		b.append(str);
	}
	void append(const char *fieldName, string str) {
		append(fieldName, str.c_str());
	}

	/* BSONObj will free the buffer when it is finished. */
	BSONObj doneAndDecouple() { 
		int l;
		return BSONObj(decouple(l), true);
	}

	/* this version, jsobjbuilder still frees the jsobj
	when the builder goes out of scope.  use it this way
	by default, that's simplest.
	*/
	BSONObj done() { 
		return BSONObj(_done());
	}

	/* assume ownership of the buffer - you must then free it (with free()) */
	char* decouple(int& l) {
		char *x = _done();
                assert( x );
		l = b.len();
		b.decouple();
		return x;
	}
	void decouple() { b.decouple(); } // post done() call version.  be sure jsobj frees...

private:
	char* _done() { 
		b.append((char) EOO);
		char *data = b.buf();
		*((int*)data) = b.len();
		return data;
	}

	BufBuilder b;
};

/* iterator for a BSONObj 

   Note each BSONObj ends with an EOO element: so you will get more() on an empty 
   object, although next().eoo() will be true.
*/
class BSONObjIterator {
public:
	BSONObjIterator(const BSONObj& jso) {
		pos = jso.objdata() + 4;
		theend = jso.objdata() + jso.objsize();
	}
	bool more() { return pos < theend; }
	BSONElement next() {
		BSONElement e(pos);
		pos += e.size();
		return e;
	}
private:
	const char *pos;
	const char *theend;
};

/* iterator a BSONObj which is an array, in array order. 
class JSArrayIter {
public:
	BSONObjIterator(const BSONObj& jso) {
...
	}
	bool more() { return ... } 
	BSONElement next() {
...
	}
};
*/

#include <pcrecpp.h> 

class RegexMatcher { 
public:
	const char *fieldName;
	pcrecpp::RE *re;
	RegexMatcher() { re = 0; }
	~RegexMatcher() { delete re; }
};

// SQL where clause equivalent
class Where; 

/* For when a js object is a query pattern. 

   e.g.
       db.foo.find( { a : 3 } );

   { a : 3 } is the pattern object.

   GT/LT:
   { a : { $gt : 3 } } 

   Not equal:
   { a : { $ne : 3 } } 

   TODO: we should rewrite the matcher to be more an AST style.
*/
class JSMatcher : boost::noncopyable { 
	int matchesDotted(
		const char *fieldName, 
		BSONElement& toMatch, BSONObj& obj, 
		int compareOp, bool *deep, bool isArr = false);

	struct element_lt
	{
		bool operator()(const BSONElement& l, const BSONElement& r) const
		{
			int x = (int) l.type() - (int) r.type();
			if( x < 0 ) return true;
			if( x > 0 ) return false;
			return compareElementValues(l,r) < 0;
		}
	};
public:
	enum { 
		Equality = 0,
		LT = 0x1,
		LTE = 0x3,
		GTE = 0x6,
		GT = 0x4, 
		opIN = 0x8, // { x : { $in : [1,2,3] } }
        NE = 0x9
	};

	static int opDirection(int op) { 
		return op <= LTE ? -1 : 1;
	}

	JSMatcher(BSONObj& pattern);

	~JSMatcher();

	/* deep means we looked into arrays for a match */
	bool matches(BSONObj& j, bool *deep = 0);

	int getN() { return n; }

private:
	int valuesMatch(BSONElement& l, BSONElement& r, int op);

	set<BSONElement,element_lt> *in;
	Where *where;
	BSONObj& jsobj;
	vector<BSONElement> toMatch;
	vector<int> compareOp;
	int n;

	RegexMatcher regexs[4];
	int nRegex;

	// so we delete the mem when we're done:
	BSONObjBuilder *builders[8];
	int nBuilders;
};

extern BSONObj maxKey;

/*- just for testing -- */

#pragma pack(push,1)
struct JSObj1 {
	JSObj1() {
		totsize=sizeof(JSObj1); 
		n = NumberDouble; strcpy_s(nname, 5, "abcd"); N = 3.1;
		s = String; strcpy_s(sname, 7, "abcdef"); slen = 10; 
		strcpy_s(sval, 10, "123456789"); eoo = EOO;
	}
	unsigned totsize;

	char n;
	char nname[5];
	double N;

	char s;
	char sname[7];
	unsigned slen;
	char sval[10];

	char eoo;
};
#pragma pack(pop)
extern JSObj1 js1;

inline BSONObj BSONElement::embeddedObjectUserCheck() { 
    uassert( "invalid parameter: expected an object", type()==Object || type()==Array ); 
	return BSONObj(value()); 
}

inline BSONObj BSONElement::embeddedObject() { 
	assert( type()==Object || type()==Array ); 
	return BSONObj(value()); 
}

inline BSONObj BSONObj::copy() { 
	if( isEmpty() )
		return *this;

	char *p = (char*) malloc(objsize());
	memcpy(p, objdata(), objsize());
	return BSONObj(p, true);
}

// wrap this element up as a singleton object.
inline BSONObj BSONElement::wrap() { 
	BSONObjBuilder b;
	b.append(*this);
	return b.doneAndDecouple();
}

inline BSONElement BSONObj::findElement(const char *name) { 
	if( !isEmpty() ) {
		BSONObjIterator it(*this);
		while( it.more() ) {
			BSONElement e = it.next();
			if( strcmp(name, e.fieldName()) == 0 ) 
				return e;
		}
	}
	return BSONElement();
}

/* add all the fields from the object specified to this object */
inline void BSONObjBuilder::appendElements(BSONObj x) { 
	BSONObjIterator it(x);
	while( it.more() ) {
		BSONElement e = it.next();
		if( e.eoo() ) break;
		append(e);
	}
}

extern BSONObj emptyObj;
