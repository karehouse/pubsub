/*
  Run all the aggregation tests
*/

/* load the test documents */
load('articles.js');

/* load the test utilities */
load('utils.js');

// make sure we're using the right db; this is the same as "use mydb;" in shell
db = db.getSisterDB("aggdb");


// just passing through fields
var p1 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	tags : 1,
	pageViews : 1
    }}
]});

var p1result = [
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066d"),
        "pageViews" : 5,
        "tags" : [
            "fun",
            "good",
            "fun"
        ]
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066e"),
        "pageViews" : 7,
        "tags" : [
            "fun",
            "nasty"
        ]
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066f"),
        "pageViews" : 6,
        "tags" : [
            "nasty",
            "filthy"
        ]
    }
];

assert(arrayEq(p1.result, p1result), 'p1 failed');


// unwinding an array
var p2 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	author : 1,
	tag : { $unwind : "$tags" },
	pageViews : 1
    }}
]});

var p2result = [
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066d"),
        "author" : "bob",
        "pageViews" : 5,
        "tag" : "fun"
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066d"),
        "author" : "bob",
        "pageViews" : 5,
        "tag" : "good"
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066d"),
        "author" : "bob",
        "pageViews" : 5,
        "tag" : "fun"
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066e"),
        "author" : "dave",
        "pageViews" : 7,
        "tag" : "fun"
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066e"),
        "author" : "dave",
        "pageViews" : 7,
        "tag" : "nasty"
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066f"),
        "author" : "jane",
        "pageViews" : 6,
        "tag" : "nasty"
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066f"),
        "author" : "jane",
        "pageViews" : 6,
        "tag" : "filthy"
    }
];

assert(arrayEq(p2.result, p2result), 'p2 failed');


// pulling values out of subdocuments
var p3 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	otherfoo : "$other.foo",
	otherbar : "$other.bar"
    }}
]});

var p3result = [
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066d"),
        "otherfoo" : 5
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066e"),
        "otherbar" : 14
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066f"),
        "otherbar" : 14
    }
];

assert(arrayEq(p3.result, p3result), 'p3 failed');


// projection includes a computed value
var p4 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	author : 1,
	daveWroteIt : { $eq:["$author", "dave"] }
    }}
]});

var p4result = [
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066d"),
        "author" : "bob",
        "daveWroteIt" : false
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066e"),
        "author" : "dave",
        "daveWroteIt" : true
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066f"),
        "author" : "jane",
        "daveWroteIt" : false
    }
];

assert(arrayEq(p4.result, p4result), 'p4 failed');


// projection includes a virtual (fabricated) document
var p5 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	author : 1,
	pageViews : 1,
	tag : { $unwind : "$tags" }
    }},
    { $project : {
	author : 1,
	subDocument : { foo : "$pageViews", bar : "$tag"  }
    }}
]});

var p5result = [
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066d"),
        "author" : "bob",
        "subDocument" : {
            "foo" : 5,
            "bar" : "fun"
        }
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066d"),
        "author" : "bob",
        "subDocument" : {
            "foo" : 5,
            "bar" : "good"
        }
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066d"),
        "author" : "bob",
        "subDocument" : {
            "foo" : 5,
            "bar" : "fun"
        }
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066e"),
        "author" : "dave",
        "subDocument" : {
            "foo" : 7,
            "bar" : "fun"
        }
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066e"),
        "author" : "dave",
        "subDocument" : {
            "foo" : 7,
            "bar" : "nasty"
        }
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066f"),
        "author" : "jane",
        "subDocument" : {
            "foo" : 6,
            "bar" : "nasty"
        }
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066f"),
        "author" : "jane",
        "subDocument" : {
            "foo" : 6,
            "bar" : "filthy"
        }
    }
];

assert(arrayEq(p5.result, p5result), 'p5 failed');


// multi-step aggregate
// nested expressions in computed fields
var p6 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	author : 1,
	tag : { $unwind : "$tags" },
	pageViews : 1
    }},
    { $project : {
	author : 1,
	tag : 1,
	pageViews : 1,
	daveWroteIt : { $eq:["$author", "dave"] },
	weLikeIt : { $or:[ { $eq:["$author", "dave"] },
			   { $eq:["$tag", "good"] } ] }
    }}
]});

var p6result = [
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066d"),
        "author" : "bob",
        "pageViews" : 5,
        "tag" : "fun",
        "daveWroteIt" : false,
        "weLikeIt" : false
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066d"),
        "author" : "bob",
        "pageViews" : 5,
        "tag" : "good",
        "daveWroteIt" : false,
        "weLikeIt" : true
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066d"),
        "author" : "bob",
        "pageViews" : 5,
        "tag" : "fun",
        "daveWroteIt" : false,
        "weLikeIt" : false
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066e"),
        "author" : "dave",
        "pageViews" : 7,
        "tag" : "fun",
        "daveWroteIt" : true,
        "weLikeIt" : true
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066e"),
        "author" : "dave",
        "pageViews" : 7,
        "tag" : "nasty",
        "daveWroteIt" : true,
        "weLikeIt" : true
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066f"),
        "author" : "jane",
        "pageViews" : 6,
        "tag" : "nasty",
        "daveWroteIt" : false,
        "weLikeIt" : false
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066f"),
        "author" : "jane",
        "pageViews" : 6,
        "tag" : "filthy",
        "daveWroteIt" : false,
        "weLikeIt" : false
    }
];

assert(arrayEq(p6.result, p6result), 'p6 failed');


// slightly more complex computed expression; $ifNull
var p7 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	theSum : { $add:["$pageViews",
			 { $ifNull:["$other.foo",
				    "$other.bar"] } ] }
    }}
]});

var p7result = [
    {
        "_id" : ObjectId("4de54958bf1505139918fce6"),
        "theSum" : 10
    },
    {
        "_id" : ObjectId("4de54958bf1505139918fce7"),
        "theSum" : 21
    },
    {
        "_id" : ObjectId("4de54958bf1505139918fce8"),
        "theSum" : 20
    }
];

assert(arrayEq(p7.result, p7result), 'p7 failed');


// dotted path inclusion; _id exclusion
var p8 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	_id : 0,
	author : 1,
	tag : { $unwind : "$tags" },
	"comments.author" : 1
    }}
]});

var p8result = [
    {
        "author" : "bob",
        "comments" : [
            {
                "author" : "joe"
            },
            {
                "author" : "sam"
            }
        ],
        "tag" : "fun"
    },
    {
        "author" : "bob",
        "comments" : [
            {
                "author" : "joe"
            },
            {
                "author" : "sam"
            }
        ],
        "tag" : "good"
    },
    {
        "author" : "bob",
        "comments" : [
            {
                "author" : "joe"
            },
            {
                "author" : "sam"
            }
        ],
        "tag" : "fun"
    },
    {
        "author" : "dave",
        "comments" : [
            {
                "author" : "barbarella"
            },
            {
                "author" : "leia"
            }
        ],
        "tag" : "fun"
    },
    {
        "author" : "dave",
        "comments" : [
            {
                "author" : "barbarella"
            },
            {
                "author" : "leia"
            }
        ],
        "tag" : "nasty"
    },
    {
        "author" : "jane",
        "comments" : [
            {
                "author" : "r2"
            },
            {
                "author" : "leia"
            }
        ],
        "tag" : "nasty"
    },
    {
        "author" : "jane",
        "comments" : [
            {
                "author" : "r2"
            },
            {
                "author" : "leia"
            }
        ],
        "tag" : "filthy"
    }
];

assert(arrayEq(p8.result, p8result), 'p8 failed');


// collapse a dotted path with an intervening array
var p9 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	_id : 0,
	author : 1,
	commentsAuthor : "$comments.author"
    }}
]});

var p9result = [
    {
        "author" : "bob",
        "commentsAuthor" : [
            "joe",
            "sam"
        ]
    },
    {
        "author" : "dave",
        "commentsAuthor" : [
            "barbarella",
            "leia"
        ]
    },
    {
        "author" : "jane",
        "commentsAuthor" : [
            "r2",
            "leia"
        ]
    }
];

assert(arrayEq(p9.result, p9result), 'p9 failed');


// simple sort
var p10 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $sort : {
	key: {
        title : 1
    }}}
]});

var p10result = [
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066d"),
        "title" : "this is my title",
        "author" : "bob",
        "posted" : ISODate("2011-05-03T22:21:33.251Z"),
        "pageViews" : 5,
        "tags" : [
            "fun",
            "good",
            "fun"
        ],
        "comments" : [
            {
                "author" : "joe",
                "text" : "this is cool"
            },
            {
                "author" : "sam",
                "text" : "this is bad"
            }
        ],
        "other" : {
            "foo" : 5
        }
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066f"),
        "title" : "this is some other title",
        "author" : "jane",
        "posted" : ISODate("2011-05-03T22:21:33.252Z"),
        "pageViews" : 6,
        "tags" : [
            "nasty",
            "filthy"
        ],
        "comments" : [
            {
                "author" : "r2",
                "text" : "beep boop"
            },
            {
                "author" : "leia",
                "text" : "this is too smutty"
            }
        ],
        "other" : {
            "bar" : 14
        }
    },
    {
        "_id" : ObjectId("4dc07fedd8420ab8d0d4066e"),
        "title" : "this is your title",
        "author" : "dave",
        "posted" : ISODate("2011-05-03T22:21:33.251Z"),
        "pageViews" : 7,
        "tags" : [
            "fun",
            "nasty"
        ],
        "comments" : [
            {
                "author" : "barbarella",
                "text" : "this is hot"
            },
            {
                "author" : "leia",
                "text" : "i prefer the brass bikini",
                "votes" : 10
            }
        ],
        "other" : {
            "bar" : 14
        }
    }
];

assert(orderedArrayEq(p10.result, p10result), 'p10 failed');


// unwind on nested array
db.p11.drop();
db.p11.save( {
    name : 'MongoDB',
    items : {
	authors : ['jay', 'vivek', 'bjornar'],
	dbg : [17, 42]
    },
    favorites : ['pickles', 'ice cream', 'kettle chips']
});

var p11 = db.runCommand(
{ aggregate : "p11", pipeline : [
    { $project : {
	name : 1,
	author : { $unwind : "$items.authors" },
    }}
]});

p11result = [
    {
        "_id" : ObjectId("4ded2e7d4a0eb8caae28044d"),
        "name" : "MongoDB",
        "author" : "jay"
    },
    {
        "_id" : ObjectId("4ded2e7d4a0eb8caae28044d"),
        "name" : "MongoDB",
        "author" : "vivek"
    },
    {
        "_id" : ObjectId("4ded2e7d4a0eb8caae28044d"),
        "name" : "MongoDB",
        "author" : "bjornar"
    }
];

assert(arrayEq(p11.result, p11result), 'p11 failed');


// multiply test
var p12 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	theProduct : { $multiply:["$pageViews",
			 { $ifNull:["$other.foo",
				    "$other.bar"] } ] }
    }}
]});

var p12result = [
    {
        "_id" : ObjectId("4de54958bf1505139918fce6"),
        "theProduct" : 25
    },
    {
        "_id" : ObjectId("4de54958bf1505139918fce7"),
        "theProduct" : 98
    },
    {
        "_id" : ObjectId("4de54958bf1505139918fce8"),
        "theProduct" : 84
    }
];

assert(arrayEq(p12.result, p12result), 'p12 failed');


// subtraction test
var p13 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	theDifference : { $subtract:["$pageViews",
			 { $ifNull:["$other.foo",
				    "$other.bar"] } ] }
    }}
]});

var p13result = [
    {
        "_id" : ObjectId("4de54958bf1505139918fce6"),
        "theDifference" : 0
    },
    {
        "_id" : ObjectId("4de54958bf1505139918fce7"),
        "theDifference" : -7
    },
    {
        "_id" : ObjectId("4de54958bf1505139918fce8"),
        "theDifference" : -8
    }
];

assert(arrayEq(p13.result, p13result), 'p13 failed');


// mod test
var p14 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	theRemainder : { $mod:[
	    { $ifNull:["$other.foo",
		    "$other.bar"] },
                "$pageViews", ] }
    }}
]});

var p14result = [
    {
        "_id" : ObjectId("4de54958bf1505139918fce6"),
        "theRemainder" : 0
    },
    {
        "_id" : ObjectId("4de54958bf1505139918fce7"),
        "theRemainder" : 0
    },
    {
        "_id" : ObjectId("4de54958bf1505139918fce8"),
        "theRemainder" : 2
    }
];

assert(arrayEq(p14.result, p14result), 'p14 failed');


// toUpper test
var p15 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	author : {$toUpper: "$author"},
	pageViews : 1
    }}
]});

var p15result = [
    {
        "_id" : ObjectId("4e09d403278071aa11bd1560"),
        "pageViews" : 5,
        "author" : "BOB"
    },
    {
        "_id" : ObjectId("4e09d656c705acb9967683c4"),
        "pageViews" : 7,
        "author" : "DAVE"
    },
    {
        "_id" : ObjectId("4e09d656c705acb9967683c5"),
        "pageViews" : 6,
        "author" : "JANE"
    }
];

assert(arrayEq(p15.result, p15result), 'p15 failed');


// toLower test
var p16 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	author : {$toUpper: "$author"},
	pageViews : 1
    }},
    { $project : {
    author : {$toLower: "$author"},
    pageViews : 1
    }}
]});

var p16result = [
    {
        "_id" : ObjectId("4e09d403278071aa11bd1560"),
        "pageViews" : 5,
        "author" : "bob"
    },
    {
        "_id" : ObjectId("4e09d656c705acb9967683c4"),
        "pageViews" : 7,
        "author" : "dave"
    },
    {
        "_id" : ObjectId("4e09d656c705acb9967683c5"),
        "pageViews" : 6,
        "author" : "jane"
    }
];

assert(arrayEq(p16.result, p16result), 'p16 failed');


// substr test
var p17 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	author : {$substr: ["$author", 1, 2]},
    }}
]});

var p17result = [
    {
        "_id" : ObjectId("4e09d403278071aa11bd1560"),
        "author" : "ob"
    },
    {
        "_id" : ObjectId("4e09d656c705acb9967683c4"),
        "author" : "av"
    },
    {
        "_id" : ObjectId("4e09d656c705acb9967683c5"),
        "author" : "an"
    }
];

assert(arrayEq(p17.result, p17result), 'p17 failed');


// strcasecmp test
var p18 = db.runCommand(
{aggregate : "article", pipeline : [
    { $project : {
        tags : 1,
        thisisalametest : {$strcasecmp: ["foo","bar"]},
        thisisalamepass : {$strcasecmp: ["foo","foo"]}
    }}
]});

var p18result = [
    {
        "_id" : ObjectId("4e09ee2d75f2a257194c996e"),
        "tags" : [
            "fun",
            "good",
            "fun"
        ],
        "thisisalametest" : 1,
        "thisisalamepass" : 0
    },
    {
        "_id" : ObjectId("4e09ee2d75f2a257194c996f"),
        "tags" : [
            "fun",
            "nasty"
        ],
        "thisisalametest" : 1,
        "thisisalamepass" : 0
    },
    {
        "_id" : ObjectId("4e09ee2d75f2a257194c9970"),
        "tags" : [
            "nasty",
            "filthy"
        ],
        "thisisalametest" : 1,
        "thisisalamepass" : 0
    }
];

assert(arrayEq(p18.result, p18result), 'p18 failed');


// date tests
var p19 = db.runCommand(
{aggregate : "article", pipeline : [
    { $project : {
        authors : 1,
        seconds: {$second: "$posted"},
        minutes: {$minute: "$posted"},
        hour: {$hour: "$posted"},
        dayOfYear: {$dayOfYear: "$posted"},
        dayOfMonth: {$dayOfMonth: "$posted"},
        dayOfWeek: {$dayOfWeek: "$posted"},
        month: {$month: "$posted"},
        week: {$week: "$posted"},
        year: {$year: "$posted"}
    }}
]});

var p19result = [
    {
        "_id" : ObjectId("4e14a3f1ffc569a332159c69"),
        "seconds" : 54,
        "minutes" : 59,
        "hour" : 18,
        "dayOfYear" : 81,
        "dayOfMonth" : 21,
        "dayOfWeek" : 1,
        "month" : 3,
        "week" : 12,
        "year" : 2004
    },
    {
        "_id" : ObjectId("4e14a3f1ffc569a332159c6a"),
        "seconds" : 10,
        "minutes" : 11,
        "hour" : 4,
        "dayOfYear" : 220,
        "dayOfMonth" : 8,
        "dayOfWeek" : 1,
        "month" : 8,
        "week" : 32,
        "year" : 2100
    },
    {
        "_id" : ObjectId("4e14a3f1ffc569a332159c6b"),
        "seconds" : 14,
        "minutes" : 17,
        "hour" : 5,
        "dayOfYear" : 366,
        "dayOfMonth" : 31,
        "dayOfWeek" : 1,
        "month" : 12,
        "week" : 53,
        "year" : 2000
    }
];

assert(arrayEq(p19.result, p19result), 'p19 failed');






// simple matching
var m1 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $match : { author : "dave" } }
]});

var m1result = [
    {
        "_id" : ObjectId("4de54958bf1505139918fce7"),
        "title" : "this is your title",
        "author" : "dave",
        "posted" : ISODate("2011-05-31T20:02:32.256Z"),
        "pageViews" : 7,
        "tags" : [
            "fun",
            "nasty"
        ],
        "comments" : [
            {
                "author" : "barbarella",
                "text" : "this is hot"
            },
            {
                "author" : "leia",
                "text" : "i prefer the brass bikini",
                "votes" : 10
            }
        ],
        "other" : {
            "bar" : 14
        }
    }
];

assert(arrayEq(m1.result, m1result), 'm1 failed');


// combining matching with a projection
var m2 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	title : 1,
	author : 1,
	pageViews : 1,
	tag : { $unwind : "$tags" },
	comments : 1
    }},
    { $match : { tag : "nasty" } }
]});

var m2result = [
    {
        "_id" : ObjectId("4de54958bf1505139918fce7"),
        "title" : "this is your title",
        "author" : "dave",
        "pageViews" : 7,
        "comments" : [
            {
                "author" : "barbarella",
                "text" : "this is hot"
            },
            {
                "author" : "leia",
                "text" : "i prefer the brass bikini",
                "votes" : 10
            }
        ],
        "tag" : "nasty"
    },
    {
        "_id" : ObjectId("4de54958bf1505139918fce8"),
        "title" : "this is some other title",
        "author" : "jane",
        "pageViews" : 6,
        "comments" : [
            {
                "author" : "r2",
                "text" : "beep boop"
            },
            {
                "author" : "leia",
                "text" : "this is too smutty"
            }
        ],
        "tag" : "nasty"
    }
];

assert(arrayEq(m2.result, m2result), 'm2 failed');


// group by tag
var g1 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	author : 1,
	tag : { $unwind : "$tags" },
	pageViews : 1
    }},
    { $group : {
	_id: { tag : 1 },
	docsByTag : { $sum : 1 },
	viewsByTag : { $sum : "$pageViews" }
    }}
]});

var g1result = [
    {
        "_id" : {
            "tag" : "filthy"
        },
        "docsByTag" : 1,
        "viewsByTag" : 6
    },
    {
        "_id" : {
            "tag" : "fun"
        },
        "docsByTag" : 3,
        "viewsByTag" : 17
    },
    {
        "_id" : {
            "tag" : "good"
        },
        "docsByTag" : 1,
        "viewsByTag" : 5
    },
    {
        "_id" : {
            "tag" : "nasty"
        },
        "docsByTag" : 2,
        "viewsByTag" : 13
    }
];

assert(arrayEq(g1.result, g1result), 'g1 failed');


// $max, and averaging in a final projection
var g2 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	author : 1,
	tag : { $unwind : "$tags" },
	pageViews : 1
    }},
    { $group : {
	_id: { tag : 1 },
	docsByTag : { $sum : 1 },
	viewsByTag : { $sum : "$pageViews" },
	mostViewsByTag : { $max : "$pageViews" },
    }},
    { $project : {
	_id: false,
	tag : "$_id.tag",
	mostViewsByTag : 1,
	docsByTag : 1,
	viewsByTag : 1,
	avgByTag : { $divide:["$viewsByTag", "$docsByTag"] }
    }}
]});

var g2result = [
    {
        "docsByTag" : 1,
        "viewsByTag" : 6,
        "mostViewsByTag" : 6,
        "tag" : "filthy",
        "avgByTag" : 6
    },
    {
        "docsByTag" : 3,
        "viewsByTag" : 17,
        "mostViewsByTag" : 7,
        "tag" : "fun",
        "avgByTag" : 5.666666666666667
    },
    {
        "docsByTag" : 1,
        "viewsByTag" : 5,
        "mostViewsByTag" : 5,
        "tag" : "good",
        "avgByTag" : 5
    },
    {
        "docsByTag" : 2,
        "viewsByTag" : 13,
        "mostViewsByTag" : 7,
        "tag" : "nasty",
        "avgByTag" : 6.5
    }
];

assert(arrayEq(g2.result, g2result), 'g2 failed');


// $push as an accumulator; can pivot data
var g3 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	author : 1,
	tag : { $unwind : "$tags" }
    }},
    { $group : {
	_id : { tag : 1 },
	authors : { $push : "$author" }
    }}
]});

var g3result = [
    {
        "_id" : {
            "tag" : "filthy"
        },
        "authors" : [
            "jane"
        ]
    },
    {
        "_id" : {
            "tag" : "fun"
        },
        "authors" : [
            "bob",
            "bob",
            "dave"
        ]
    },
    {
        "_id" : {
            "tag" : "good"
        },
        "authors" : [
            "bob"
        ]
    },
    {
        "_id" : {
            "tag" : "nasty"
        },
        "authors" : [
            "dave",
            "jane"
        ]
    }
];

assert(arrayEq(g3.result, g3result), 'g3 failed');


// $avg, and averaging in a final projection
var g4 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	author : 1,
	tag : { $unwind : "$tags" },
	pageViews : 1
    }},
    { $group : {
	_id: { tag : 1 },
	docsByTag : { $sum : 1 },
	viewsByTag : { $sum : "$pageViews" },
	avgByTag : { $avg : "$pageViews" },
    }}
]});

var g4result = [
    {
        "_id" : {
            "tag" : "filthy"
        },
        "docsByTag" : 1,
        "viewsByTag" : 6,
        "avgByTag" : 6
    },
    {
        "_id" : {
            "tag" : "fun"
        },
        "docsByTag" : 3,
        "viewsByTag" : 17,
        "avgByTag" : 5.666666666666667
    },
    {
        "_id" : {
            "tag" : "good"
        },
        "docsByTag" : 1,
        "viewsByTag" : 5,
        "avgByTag" : 5
    },
    {
        "_id" : {
            "tag" : "nasty"
        },
        "docsByTag" : 2,
        "viewsByTag" : 13,
        "avgByTag" : 6.5
    }
];

assert(arrayEq(g4.result, g4result), 'g4 failed');


// $addToSet as an accumulator; can pivot data
var g5 = db.runCommand(
{ aggregate : "article", pipeline : [
    { $project : {
	author : 1,
	tag : { $unwind : "$tags" }
    }},
    { $group : {
	_id : { tag : 1 },
	authors : { $addToSet : "$author" }
    }}
]});

var g5result = [
    {
        "_id" : {
            "tag" : "filthy"
        },
        "authors" : [
            "jane"
        ]
    },
    {
        "_id" : {
            "tag" : "fun"
        },
        "authors" : [
            "bob",
            "dave"
        ]
    },
    {
        "_id" : {
            "tag" : "good"
        },
        "authors" : [
            "bob"
        ]
    },
    {
        "_id" : {
            "tag" : "nasty"
        },
        "authors" : [
            "dave",
            "jane"
        ]
    }
];

assert(arrayEq(g5.result, g5result), 'g5 failed');
